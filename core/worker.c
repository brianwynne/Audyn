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

struct audyn_worker {
    audyn_frame_pool_t   *pool;
    audyn_audio_queue_t  *q;
    audyn_worker_cfg_t    cfg;

    audyn_wav_sink_t     *sink;

    pthread_t             thread;
    _Atomic int           running;     /* 1 while worker should run */
    _Atomic int           status;      /* 0 ok, nonzero error */

    pthread_mutex_t       err_mu;
    char                  last_err[256];
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

    if (audyn_wav_sink_open(w->sink,
                           w->cfg.output_path,
                           w->cfg.sample_rate,
                           w->cfg.channels) != 0) {
        LOG_ERROR("worker: failed to open wav sink: %s", w->cfg.output_path);
        set_err(w, "failed to open wav sink");
        atomic_store_explicit(&w->running, 0, memory_order_release);
        return NULL;
    }

    while (atomic_load_explicit(&w->running, memory_order_acquire)) {
        audyn_audio_frame_t *f =
            (audyn_audio_frame_t*)audyn_audio_queue_pop(w->q);

        if (!f) {
            usleep(w->cfg.idle_sleep_us ? w->cfg.idle_sleep_us : 1000);
            continue;
        }

        if (audyn_wav_sink_write(w->sink,
                                f->data,
                                f->sample_frames,
                                f->channels) != 0) {
            LOG_ERROR("worker: wav sink write failed");
            set_err(w, "wav sink write failed");
            audyn_frame_release(f);
            atomic_store_explicit(&w->running, 0, memory_order_release);
            break;
        }

        audyn_frame_release(f);
    }

    if (w->cfg.drain_on_stop) {
        while (1) {
            audyn_audio_frame_t *f =
                (audyn_audio_frame_t*)audyn_audio_queue_pop(w->q);
            if (!f) break;
            (void)audyn_wav_sink_write(w->sink,
                                      f->data,
                                      f->sample_frames,
                                      f->channels);
            audyn_frame_release(f);
        }
    }

    audyn_wav_sink_close(w->sink);
    return NULL;
}

audyn_worker_t *audyn_worker_create(audyn_frame_pool_t *pool,
                                    audyn_audio_queue_t *queue,
                                    const audyn_worker_cfg_t *cfg)
{
    if (!pool || !queue || !cfg || !cfg->output_path)
        return NULL;

    audyn_worker_t *w = calloc(1, sizeof(*w));
    if (!w) return NULL;

    w->pool = pool;
    w->q = queue;
    w->cfg = *cfg;

    atomic_init(&w->running, 0);
    atomic_init(&w->status, 0);
    w->last_err[0] = '\0';

    if (pthread_mutex_init(&w->err_mu, NULL) != 0) {
        free(w);
        return NULL;
    }

    w->sink = audyn_wav_sink_create(&w->cfg.wav_cfg);
    if (!w->sink) {
        pthread_mutex_destroy(&w->err_mu);
        free(w);
        return NULL;
    }

    return w;
}

int audyn_worker_start(audyn_worker_t *w)
{
    if (!w) return -1;
    if (atomic_load_explicit(&w->running, memory_order_relaxed))
        return -1;

    atomic_store_explicit(&w->running, 1, memory_order_release);

    if (pthread_create(&w->thread, NULL, worker_main, w) != 0) {
        atomic_store_explicit(&w->running, 0, memory_order_release);
        set_err(w, "pthread_create failed");
        return -1;
    }

    return 0;
}

int audyn_worker_stop(audyn_worker_t *w)
{
    if (!w) return -1;

    if (!atomic_load_explicit(&w->running, memory_order_relaxed))
        return 0;

    atomic_store_explicit(&w->running, 0, memory_order_release);
    pthread_join(w->thread, NULL);
    return 0;
}

void audyn_worker_destroy(audyn_worker_t *w)
{
    if (!w) return;

    (void)audyn_worker_stop(w);
    audyn_wav_sink_destroy(w->sink);
    pthread_mutex_destroy(&w->err_mu);
    free(w);
}

int audyn_worker_status(const audyn_worker_t *w)
{
    return w ? atomic_load_explicit(&w->status, memory_order_relaxed) : 1;
}

const char *audyn_worker_last_error(const audyn_worker_t *w)
{
    if (!w) return "no worker";

    /* If caller queries while running, protect the string. */
    pthread_mutex_lock((pthread_mutex_t *)&w->err_mu);
    const char *ret = (w->last_err[0] ? w->last_err : "ok");
    pthread_mutex_unlock((pthread_mutex_t *)&w->err_mu);
    return ret;
}
