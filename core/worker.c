/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      worker.c
 *
 *  Purpose:
 *      Implements a non-RT consumer thread that drains an SPSC pointer queue of
 *      audyn_audio_frame_t*, writes frames to a WAV sink, and releases frames back
 *      to the pool.
 *
 *  Correctness & Production Notes:
 *      - Uses C11 atomics for the run flag and status to avoid data races.
 *      - Uses a pthread mutex to protect the last error string.
 *      - Cooperative stop: stop() flips run flag then joins.
 *      - If sink write fails, worker records error and stops.
 *
 *  Dependencies:
 *      - POSIX pthread
 *      - C11 atomics
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

#include "worker.h"

#include "log.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Default idle sleep if not specified (1ms) */
#define DEFAULT_IDLE_SLEEP_US 1000

/* Maximum reasonable sample rate */
#define WORKER_MAX_SAMPLE_RATE 384000

/* Maximum reasonable channel count */
#define WORKER_MAX_CHANNELS 32

struct audyn_worker {
    audyn_frame_pool_t   *pool;
    audyn_audio_queue_t  *q;
    audyn_worker_cfg_t    cfg;
    char                 *output_path;  /* Owned copy of output path */

    audyn_wav_sink_t     *sink;

    pthread_t             thread;
    _Atomic int           running;      /* 1 while worker should run */
    _Atomic int           started;      /* 1 if thread was ever started */
    _Atomic int           status;       /* 0 ok, nonzero error */

    pthread_mutex_t       err_mu;
    char                  last_err[256];

    /* Statistics - atomic for thread-safe reads */
    _Atomic uint64_t      frames_processed;
    _Atomic uint64_t      write_errors;
};

static void set_err(audyn_worker_t *w, const char *msg)
{
    if (!w) return;

    atomic_store_explicit(&w->status, 1, memory_order_relaxed);

    pthread_mutex_lock(&w->err_mu);
    strncpy(w->last_err, msg ? msg : "error", sizeof(w->last_err) - 1);
    w->last_err[sizeof(w->last_err) - 1] = '\0';
    pthread_mutex_unlock(&w->err_mu);
}

static void *worker_main(void *arg)
{
    audyn_worker_t *w = (audyn_worker_t*)arg;
    if (!w) return NULL;

    const char *path = w->output_path ? w->output_path : "(unknown)";

    if (audyn_wav_sink_open(w->sink,
                           w->output_path,
                           w->cfg.sample_rate,
                           w->cfg.channels) != 0) {
        LOG_ERROR("WORKER: Failed to open WAV sink: %s", path);
        set_err(w, "failed to open wav sink");
        atomic_store_explicit(&w->running, 0, memory_order_release);
        return NULL;
    }

    LOG_INFO("WORKER: Started writing to '%s'", path);

    const uint32_t idle_us = w->cfg.idle_sleep_us ? w->cfg.idle_sleep_us : DEFAULT_IDLE_SLEEP_US;

    while (atomic_load_explicit(&w->running, memory_order_acquire)) {
        audyn_audio_frame_t *f =
            (audyn_audio_frame_t*)audyn_audio_queue_pop(w->q);

        if (!f) {
            usleep(idle_us);
            continue;
        }

        if (audyn_wav_sink_write(w->sink,
                                f->data,
                                f->sample_frames,
                                f->channels) != 0) {
            LOG_ERROR("WORKER: WAV sink write failed for '%s'", path);
            set_err(w, "wav sink write failed");
            atomic_fetch_add_explicit(&w->write_errors, 1, memory_order_relaxed);
            audyn_frame_release(f);
            atomic_store_explicit(&w->running, 0, memory_order_release);
            break;
        }

        /* Track frames processed */
        atomic_fetch_add_explicit(&w->frames_processed, f->sample_frames, memory_order_relaxed);
        audyn_frame_release(f);
    }

    /* Drain remaining frames if requested */
    if (w->cfg.drain_on_stop) {
        uint64_t drained = 0;
        while (1) {
            audyn_audio_frame_t *f =
                (audyn_audio_frame_t*)audyn_audio_queue_pop(w->q);
            if (!f) break;

            if (audyn_wav_sink_write(w->sink,
                                    f->data,
                                    f->sample_frames,
                                    f->channels) != 0) {
                atomic_fetch_add_explicit(&w->write_errors, 1, memory_order_relaxed);
            } else {
                drained += f->sample_frames;
            }
            audyn_frame_release(f);
        }
        if (drained > 0) {
            atomic_fetch_add_explicit(&w->frames_processed, drained, memory_order_relaxed);
            LOG_DEBUG("WORKER: Drained %lu additional frames", (unsigned long)drained);
        }
    }

    audyn_wav_sink_close(w->sink);

    LOG_DEBUG("WORKER: Stopped - frames=%lu errors=%lu",
              (unsigned long)atomic_load(&w->frames_processed),
              (unsigned long)atomic_load(&w->write_errors));

    return NULL;
}

audyn_worker_t *audyn_worker_create(audyn_frame_pool_t *pool,
                                    audyn_audio_queue_t *queue,
                                    const audyn_worker_cfg_t *cfg)
{
    if (!pool || !queue) {
        LOG_ERROR("WORKER: NULL pool or queue");
        return NULL;
    }
    if (!cfg) {
        LOG_ERROR("WORKER: NULL config");
        return NULL;
    }
    if (!cfg->output_path || !cfg->output_path[0]) {
        LOG_ERROR("WORKER: NULL or empty output path");
        return NULL;
    }
    if (cfg->sample_rate == 0 || cfg->sample_rate > WORKER_MAX_SAMPLE_RATE) {
        LOG_ERROR("WORKER: Invalid sample rate %u (must be 1-%d)",
                  cfg->sample_rate, WORKER_MAX_SAMPLE_RATE);
        return NULL;
    }
    if (cfg->channels == 0 || cfg->channels > WORKER_MAX_CHANNELS) {
        LOG_ERROR("WORKER: Invalid channel count %u (must be 1-%d)",
                  cfg->channels, WORKER_MAX_CHANNELS);
        return NULL;
    }

    audyn_worker_t *w = calloc(1, sizeof(*w));
    if (!w) {
        LOG_ERROR("WORKER: Failed to allocate worker structure");
        return NULL;
    }

    w->pool = pool;
    w->q = queue;
    w->cfg = *cfg;

    /* Make owned copy of output path */
    w->output_path = strdup(cfg->output_path);
    if (!w->output_path) {
        LOG_ERROR("WORKER: Failed to allocate output path");
        free(w);
        return NULL;
    }

    atomic_init(&w->running, 0);
    atomic_init(&w->started, 0);
    atomic_init(&w->status, 0);
    atomic_init(&w->frames_processed, 0);
    atomic_init(&w->write_errors, 0);
    w->last_err[0] = '\0';

    if (pthread_mutex_init(&w->err_mu, NULL) != 0) {
        LOG_ERROR("WORKER: Failed to initialize mutex");
        free(w->output_path);
        free(w);
        return NULL;
    }

    w->sink = audyn_wav_sink_create(&w->cfg.wav_cfg);
    if (!w->sink) {
        LOG_ERROR("WORKER: Failed to create WAV sink");
        pthread_mutex_destroy(&w->err_mu);
        free(w->output_path);
        free(w);
        return NULL;
    }

    LOG_INFO("WORKER: Created - output='%s' %uHz %uch",
             cfg->output_path, cfg->sample_rate, cfg->channels);

    return w;
}

int audyn_worker_start(audyn_worker_t *w)
{
    if (!w) {
        LOG_ERROR("WORKER: NULL worker");
        return -1;
    }
    if (atomic_load_explicit(&w->running, memory_order_relaxed)) {
        LOG_ERROR("WORKER: Already running");
        return -1;
    }

    atomic_store_explicit(&w->running, 1, memory_order_release);
    atomic_store_explicit(&w->started, 1, memory_order_release);

    if (pthread_create(&w->thread, NULL, worker_main, w) != 0) {
        LOG_ERROR("WORKER: Failed to create thread");
        atomic_store_explicit(&w->running, 0, memory_order_release);
        set_err(w, "pthread_create failed");
        return -1;
    }

    LOG_INFO("WORKER: Thread started");
    return 0;
}

int audyn_worker_stop(audyn_worker_t *w)
{
    if (!w) return -1;

    if (!atomic_load_explicit(&w->started, memory_order_relaxed))
        return 0;

    if (!atomic_load_explicit(&w->running, memory_order_relaxed)) {
        /* Thread may have stopped itself on error, still need to join */
        if (atomic_load_explicit(&w->started, memory_order_relaxed)) {
            pthread_join(w->thread, NULL);
            atomic_store_explicit(&w->started, 0, memory_order_release);
        }
        return 0;
    }

    atomic_store_explicit(&w->running, 0, memory_order_release);
    pthread_join(w->thread, NULL);
    atomic_store_explicit(&w->started, 0, memory_order_release);
    LOG_INFO("WORKER: Stopped");
    return 0;
}

void audyn_worker_destroy(audyn_worker_t *w)
{
    if (!w) return;

    (void)audyn_worker_stop(w);
    audyn_wav_sink_destroy(w->sink);
    pthread_mutex_destroy(&w->err_mu);
    free(w->output_path);
    free(w);
}

int audyn_worker_status(const audyn_worker_t *w)
{
    return w ? atomic_load_explicit(&w->status, memory_order_relaxed) : 1;
}

int audyn_worker_is_running(const audyn_worker_t *w)
{
    if (!w) return 0;
    return atomic_load_explicit(&w->running, memory_order_relaxed);
}

void audyn_worker_get_last_error(const audyn_worker_t *w, char *buf, size_t buflen)
{
    if (!buf || buflen == 0) return;
    buf[0] = '\0';

    if (!w) {
        strncpy(buf, "no worker", buflen - 1);
        buf[buflen - 1] = '\0';
        return;
    }

    /* Cast is safe because we're only reading and the mutex protects access */
    pthread_mutex_lock((pthread_mutex_t *)&w->err_mu);
    strncpy(buf, w->last_err[0] ? w->last_err : "ok", buflen - 1);
    buf[buflen - 1] = '\0';
    pthread_mutex_unlock((pthread_mutex_t *)&w->err_mu);
}

/* Deprecated: use audyn_worker_get_last_error instead */
const char *audyn_worker_last_error(const audyn_worker_t *w)
{
    /* Note: This returns a pointer that may be invalidated if error changes.
     * Use audyn_worker_get_last_error() for thread-safe access. */
    if (!w) return "no worker";
    return w->last_err[0] ? w->last_err : "ok";
}

void audyn_worker_get_stats(const audyn_worker_t *w, audyn_worker_stats_t *stats)
{
    if (!stats) return;

    if (!w) {
        memset(stats, 0, sizeof(*stats));
        return;
    }

    stats->frames_processed = atomic_load_explicit(&w->frames_processed, memory_order_relaxed);
    stats->write_errors = atomic_load_explicit(&w->write_errors, memory_order_relaxed);
}
