/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      worker.h
 *
 *  Purpose:
 *      Non-real-time worker thread that drains an SPSC audio_queue, writes
 *      audio frames to a wav_sink, and returns frames to the frame_pool.
 *
 *  Threading Model (SPSC):
 *      - Producer thread (typically RT): pushes audyn_audio_frame_t* to audio_queue
 *      - Consumer thread (this worker): pops frames, performs I/O, releases frames
 *
 *  Contract:
 *      - The queue payload MUST be audyn_audio_frame_t* originating from
 *        audyn_frame_acquire().
 *      - audyn_worker_stop() is cooperative: it signals the worker to stop,
 *        then joins the thread.
 *
 *  Dependencies:
 *      - POSIX pthread
 *      - Audyn: frame_pool, audio_queue, wav_sink
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

#ifndef AUDYN_WORKER_H
#define AUDYN_WORKER_H

#include <stdint.h>

#include "frame_pool.h"
#include "audio_queue.h"
#include "wav_sink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audyn_worker audyn_worker_t;

typedef struct audyn_worker_cfg {
    const char *output_path;
    uint32_t sample_rate;
    uint16_t channels;

    uint32_t idle_sleep_us;  /* sleep when no frames (non-RT) */
    int drain_on_stop;       /* drain queue on stop */

    audyn_wav_sink_cfg_t wav_cfg;
} audyn_worker_cfg_t;

/* Create/destroy (NOT real-time safe). */
audyn_worker_t *audyn_worker_create(audyn_frame_pool_t *pool,
                                    audyn_audio_queue_t *queue,
                                    const audyn_worker_cfg_t *cfg);

int  audyn_worker_start(audyn_worker_t *w);
int  audyn_worker_stop(audyn_worker_t *w);
void audyn_worker_destroy(audyn_worker_t *w);

/* 0 = ok, nonzero = error */
int  audyn_worker_status(const audyn_worker_t *w);

/* Returns "ok" if no error has been recorded. */
const char *audyn_worker_last_error(const audyn_worker_t *w);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_WORKER_H */
