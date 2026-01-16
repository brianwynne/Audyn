/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      pipewire_input.c
 *
 *  Purpose:
 *      Implements PipeWire stream capture and pushes frames into Audyn audio_queue.
 *
 *  RT constraints (process callback):
 *      - No malloc/free
 *      - No blocking syscalls
 *
 *  Behavior:
 *      - Acquires a frame from the frame pool.
 *      - Copies interleaved float32 samples into frame->data.
 *      - Sets frame->sample_frames to the actual number of samples received.
 *      - If PipeWire delivers more samples than frame capacity, truncates to fit.
 *      - Downstream consumers (e.g., opus_sink FIFO) handle accumulation.
 *
 *  Dependencies:
 *      - PipeWire: libpipewire-0.3, libspa-0.2
 *      - POSIX: pthread
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  Author:
 *      B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#include "pipewire_input.h"
#include "log.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Maximum reasonable sample rate */
#define PW_MAX_SAMPLE_RATE 384000

/* Maximum reasonable channel count */
#define PW_MAX_CHANNELS 32

/* PipeWire init/deinit should be process-wide. We keep a simple refcount. */
static _Atomic int g_pw_refcnt = 0;

static void pw_ref_init(void)
{
    int prev = atomic_fetch_add_explicit(&g_pw_refcnt, 1, memory_order_acq_rel);
    if (prev == 0) {
        pw_init(NULL, NULL);
    }
}

static void pw_ref_deinit(void)
{
    int prev = atomic_fetch_sub_explicit(&g_pw_refcnt, 1, memory_order_acq_rel);
    if (prev == 1) {
        pw_deinit();
    }
}

struct audyn_pw_input {
    audyn_frame_pool_t  *pool;
    audyn_audio_queue_t *q;

    uint32_t rate;
    uint32_t channels;

    struct pw_main_loop *loop;
    struct pw_context   *ctx;
    struct pw_stream    *stream;

    pthread_t thread;
    int thread_started;
    int running;                /* 1 if loop is running */

    /* Statistics - atomic for RT-safe updates in process callback */
    _Atomic uint64_t frames_captured;   /* Total frames successfully captured */
    _Atomic uint64_t callbacks;         /* Total process callbacks */
    _Atomic uint64_t drops_pool;        /* Drops due to pool exhaustion */
    _Atomic uint64_t drops_queue;       /* Drops due to queue full */
    _Atomic uint64_t drops_empty;       /* Drops due to empty/invalid buffer */
    _Atomic uint64_t truncations;       /* Times we truncated oversized buffer */
};

static void on_process(void *userdata)
{
    audyn_pw_input_t *in = (audyn_pw_input_t*)userdata;
    if (!in || !in->stream) return;

    /* Count all callbacks for diagnostics */
    atomic_fetch_add_explicit(&in->callbacks, 1, memory_order_relaxed);

    struct pw_buffer *pw_buf = pw_stream_dequeue_buffer(in->stream);
    if (!pw_buf) return;

    struct spa_buffer *buf = pw_buf->buffer;
    if (!buf || buf->n_datas < 1 || !buf->datas[0].data || !buf->datas[0].chunk) {
        atomic_fetch_add_explicit(&in->drops_empty, 1, memory_order_relaxed);
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    const uint32_t bytes = buf->datas[0].chunk->size;
    const uint32_t frame_bytes = (uint32_t)(sizeof(float) * in->channels);
    const uint32_t nframes_in = frame_bytes ? (bytes / frame_bytes) : 0;

    if (nframes_in == 0) {
        atomic_fetch_add_explicit(&in->drops_empty, 1, memory_order_relaxed);
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    audyn_audio_frame_t *f = audyn_frame_acquire(in->pool);
    if (!f) {
        /* Pool exhausted: drop this buffer. */
        atomic_fetch_add_explicit(&in->drops_pool, 1, memory_order_relaxed);
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    /* Validate channel agreement. */
    if ((uint32_t)f->channels != in->channels) {
        audyn_frame_release(f);
        atomic_fetch_add_explicit(&in->drops_empty, 1, memory_order_relaxed);
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    const uint32_t nframes_cap = f->sample_frames;

    /*
     * Copy exactly the samples we received from PipeWire.
     * If PipeWire delivers more than our frame capacity, truncate.
     * The downstream FIFO (e.g., in opus_sink) will accumulate frames properly.
     *
     * This approach follows PipeWire's recommendation: process whatever
     * buffer size PipeWire delivers, don't enforce fixed sizes here.
     */
    const uint32_t nframes_to_copy = (nframes_in > nframes_cap) ? nframes_cap : nframes_in;

    /* Track truncations */
    if (nframes_in > nframes_cap) {
        atomic_fetch_add_explicit(&in->truncations, 1, memory_order_relaxed);
    }

    const size_t copy_bytes = (size_t)nframes_to_copy * (size_t)frame_bytes;
    memcpy(f->data, buf->datas[0].data, copy_bytes);

    /* Set actual sample count - downstream consumers use this value */
    f->sample_frames = nframes_to_copy;

    if (!audyn_audio_queue_push(in->q, f)) {
        /* Queue full: release frame. */
        atomic_fetch_add_explicit(&in->drops_queue, 1, memory_order_relaxed);
        audyn_frame_release(f);
    } else {
        /* Successfully captured */
        atomic_fetch_add_explicit(&in->frames_captured, nframes_to_copy, memory_order_relaxed);
    }

    pw_stream_queue_buffer(in->stream, pw_buf);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

static void *pw_thread_main(void *arg)
{
    audyn_pw_input_t *in = (audyn_pw_input_t*)arg;
    if (!in || !in->loop) return NULL;
    pw_main_loop_run(in->loop);
    return NULL;
}

audyn_pw_input_t *audyn_pw_input_create(audyn_frame_pool_t *pool,
                                        audyn_audio_queue_t *queue,
                                        uint32_t sample_rate,
                                        uint32_t channels)
{
    if (!pool || !queue) {
        LOG_ERROR("PW: NULL pool or queue");
        return NULL;
    }
    if (sample_rate == 0 || sample_rate > PW_MAX_SAMPLE_RATE) {
        LOG_ERROR("PW: Invalid sample rate %u (must be 1-%d)", sample_rate, PW_MAX_SAMPLE_RATE);
        return NULL;
    }
    if (channels == 0 || channels > PW_MAX_CHANNELS) {
        LOG_ERROR("PW: Invalid channel count %u (must be 1-%d)", channels, PW_MAX_CHANNELS);
        return NULL;
    }

    pw_ref_init();

    audyn_pw_input_t *in = (audyn_pw_input_t*)calloc(1, sizeof(*in));
    if (!in) {
        LOG_ERROR("PW: Failed to allocate input structure");
        pw_ref_deinit();
        return NULL;
    }

    in->pool = pool;
    in->q = queue;
    in->rate = sample_rate;
    in->channels = channels;

    /* Initialize atomic counters */
    atomic_init(&in->frames_captured, 0);
    atomic_init(&in->callbacks, 0);
    atomic_init(&in->drops_pool, 0);
    atomic_init(&in->drops_queue, 0);
    atomic_init(&in->drops_empty, 0);
    atomic_init(&in->truncations, 0);

    in->loop = pw_main_loop_new(NULL);
    if (!in->loop) {
        LOG_ERROR("PW: Failed to create main loop");
        goto fail;
    }

    in->ctx = pw_context_new(pw_main_loop_get_loop(in->loop), NULL, 0);
    if (!in->ctx) {
        LOG_ERROR("PW: Failed to create context");
        goto fail;
    }

    in->stream = pw_stream_new_simple(
        pw_main_loop_get_loop(in->loop),
        "audyn-input",
        pw_properties_new(
            PW_KEY_MEDIA_TYPE, "Audio",
            PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_MEDIA_ROLE, "Production",
            NULL
        ),
        &stream_events,
        in
    );
    if (!in->stream) {
        LOG_ERROR("PW: Failed to create stream");
        goto fail;
    }

    struct spa_audio_info_raw info;
    memset(&info, 0, sizeof(info));
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = (uint32_t)sample_rate;
    info.channels = (uint32_t)channels;

    uint8_t b[1024];
    struct spa_pod_builder pb = SPA_POD_BUILDER_INIT(b, sizeof(b));
    const struct spa_pod *params[1];
    params[0] = spa_format_audio_raw_build(&pb, SPA_PARAM_EnumFormat, &info);

    int rc = pw_stream_connect(
        in->stream,
        PW_DIRECTION_INPUT,
        PW_ID_ANY,
        PW_STREAM_FLAG_AUTOCONNECT |
        PW_STREAM_FLAG_MAP_BUFFERS |
        PW_STREAM_FLAG_RT_PROCESS,
        params, 1
    );
    if (rc < 0) {
        LOG_ERROR("PW: pw_stream_connect failed (%d)", rc);
        goto fail;
    }

    LOG_INFO("PW: Created input - %uHz %uch F32", sample_rate, channels);
    return in;

fail:
    audyn_pw_input_destroy(in);
    return NULL;
}

int audyn_pw_input_start(audyn_pw_input_t *in)
{
    if (!in) {
        LOG_ERROR("PW: NULL input");
        return -1;
    }
    if (in->thread_started) {
        LOG_ERROR("PW: Already started");
        return -1;
    }

    if (pthread_create(&in->thread, NULL, pw_thread_main, in) != 0) {
        LOG_ERROR("PW: Failed to create thread");
        return -1;
    }

    in->thread_started = 1;
    in->running = 1;
    LOG_INFO("PW: Started capture");
    return 0;
}

void audyn_pw_input_stop(audyn_pw_input_t *in)
{
    if (!in) return;

    if (in->loop)
        pw_main_loop_quit(in->loop);

    if (in->thread_started) {
        pthread_join(in->thread, NULL);
        in->thread_started = 0;
        in->running = 0;

        /* Log final statistics */
        LOG_DEBUG("PW: Stopped - captured=%lu callbacks=%lu drops_pool=%lu drops_queue=%lu drops_empty=%lu truncations=%lu",
                  (unsigned long)atomic_load(&in->frames_captured),
                  (unsigned long)atomic_load(&in->callbacks),
                  (unsigned long)atomic_load(&in->drops_pool),
                  (unsigned long)atomic_load(&in->drops_queue),
                  (unsigned long)atomic_load(&in->drops_empty),
                  (unsigned long)atomic_load(&in->truncations));
    }
}

void audyn_pw_input_destroy(audyn_pw_input_t *in)
{
    if (!in) return;

    audyn_pw_input_stop(in);

    if (in->stream) pw_stream_destroy(in->stream);
    if (in->ctx) pw_context_destroy(in->ctx);
    if (in->loop) pw_main_loop_destroy(in->loop);

    free(in);
    pw_ref_deinit();
}

int audyn_pw_input_is_running(const audyn_pw_input_t *in)
{
    if (!in) return 0;
    return in->running;
}

void audyn_pw_input_get_stats(const audyn_pw_input_t *in, audyn_pw_stats_t *stats)
{
    if (!in || !stats) return;

    stats->frames_captured = atomic_load_explicit(&in->frames_captured, memory_order_relaxed);
    stats->callbacks = atomic_load_explicit(&in->callbacks, memory_order_relaxed);
    stats->drops_pool = atomic_load_explicit(&in->drops_pool, memory_order_relaxed);
    stats->drops_queue = atomic_load_explicit(&in->drops_queue, memory_order_relaxed);
    stats->drops_empty = atomic_load_explicit(&in->drops_empty, memory_order_relaxed);
    stats->truncations = atomic_load_explicit(&in->truncations, memory_order_relaxed);
}
