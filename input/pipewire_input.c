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
 *      - If PipeWire buffer contains fewer frames than frame->sample_frames, zero-pads
 *        the remainder so consumers always see a fixed-size frame.
 *      - If PipeWire buffer contains more frames than the frame can hold, drops it.
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
};

static void on_process(void *userdata)
{
    audyn_pw_input_t *in = (audyn_pw_input_t*)userdata;
    if (!in || !in->stream) return;

    struct pw_buffer *pw_buf = pw_stream_dequeue_buffer(in->stream);
    if (!pw_buf) return;

    struct spa_buffer *buf = pw_buf->buffer;
    if (!buf || buf->n_datas < 1 || !buf->datas[0].data || !buf->datas[0].chunk) {
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    const uint32_t bytes = buf->datas[0].chunk->size;
    const uint32_t frame_bytes = (uint32_t)(sizeof(float) * in->channels);
    const uint32_t nframes_in = frame_bytes ? (bytes / frame_bytes) : 0;

    if (nframes_in == 0) {
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    audyn_audio_frame_t *f = audyn_frame_acquire(in->pool);
    if (!f) {
        /* Pool exhausted: drop this buffer. */
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    /* Validate channel agreement. */
    if ((uint32_t)f->channels != in->channels) {
        audyn_frame_release(f);
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    const uint32_t nframes_cap = f->sample_frames;

    if (nframes_in > nframes_cap) {
        /* Buffer is larger than our frame size: drop to keep fixed sizing simple. */
        audyn_frame_release(f);
        pw_stream_queue_buffer(in->stream, pw_buf);
        return;
    }

    const size_t copy_bytes = (size_t)nframes_in * (size_t)frame_bytes;
    memcpy(f->data, buf->datas[0].data, copy_bytes);

    /* Zero-pad remainder, if any, so consumers always see a fixed-size frame. */
    if (nframes_in < nframes_cap) {
        const size_t pad_bytes = (size_t)(nframes_cap - nframes_in) * (size_t)frame_bytes;
        memset((unsigned char*)f->data + copy_bytes, 0, pad_bytes);
    }

    if (!audyn_audio_queue_push(in->q, f)) {
        /* Queue full: release frame. */
        audyn_frame_release(f);
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
    if (!pool || !queue || sample_rate == 0 || channels == 0)
        return NULL;

    if (channels > 0xFFFFu) /* frame uses uint16_t channels */
        return NULL;

    pw_ref_init();

    audyn_pw_input_t *in = (audyn_pw_input_t*)calloc(1, sizeof(*in));
    if (!in) {
        pw_ref_deinit();
        return NULL;
    }

    in->pool = pool;
    in->q = queue;
    in->rate = sample_rate;
    in->channels = channels;

    in->loop = pw_main_loop_new(NULL);
    if (!in->loop) goto fail;

    in->ctx = pw_context_new(pw_main_loop_get_loop(in->loop), NULL, 0);
    if (!in->ctx) goto fail;

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
    if (!in->stream) goto fail;

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
        LOG_ERROR("pipewire_input: pw_stream_connect failed (%d)", rc);
        goto fail;
    }

    return in;

fail:
    audyn_pw_input_destroy(in);
    return NULL;
}

int audyn_pw_input_start(audyn_pw_input_t *in)
{
    if (!in || in->thread_started) return -1;

    if (pthread_create(&in->thread, NULL, pw_thread_main, in) != 0)
        return -1;

    in->thread_started = 1;
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
