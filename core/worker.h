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
#include <stddef.h>  /* for size_t */

#include "frame_pool.h"
#include "audio_queue.h"
#include "wav_sink.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audyn_worker audyn_worker_t;

/*
 * Worker statistics.
 */
typedef struct audyn_worker_stats {
    uint64_t frames_processed;  /* Total audio frames written to sink */
    uint64_t write_errors;      /* Number of write errors encountered */
} audyn_worker_stats_t;

/*
 * Worker configuration.
 *
 * Notes:
 *   - sample_rate must be 1-384000 Hz
 *   - channels must be 1-32
 *   - output_path is copied internally, caller may free after create()
 */
typedef struct audyn_worker_cfg {
    const char *output_path;
    uint32_t sample_rate;
    uint16_t channels;

    uint32_t idle_sleep_us;  /* Sleep when no frames (default 1000us if 0) */
    int drain_on_stop;       /* If non-zero, drain remaining queue on stop */

    audyn_wav_sink_cfg_t wav_cfg;
} audyn_worker_cfg_t;

/*
 * Create a worker instance.
 *
 * Parameters:
 *   pool  - Frame pool for releasing processed frames
 *   queue - Audio queue to drain frames from (SPSC consumer)
 *   cfg   - Worker configuration
 *
 * Returns pointer on success, NULL on failure.
 * Errors are logged via the project's logging system.
 */
audyn_worker_t *audyn_worker_create(audyn_frame_pool_t *pool,
                                    audyn_audio_queue_t *queue,
                                    const audyn_worker_cfg_t *cfg);

/* Start/stop the worker thread (NOT real-time safe). */
int  audyn_worker_start(audyn_worker_t *w);
int  audyn_worker_stop(audyn_worker_t *w);

/* Destroy worker (NOT real-time safe). Calls stop() internally. */
void audyn_worker_destroy(audyn_worker_t *w);

/*
 * Get worker error status.
 *
 * Returns 0 if no error, nonzero if error occurred.
 */
int  audyn_worker_status(const audyn_worker_t *w);

/*
 * Check if worker is currently running.
 *
 * Returns 1 if running, 0 if stopped or NULL.
 */
int  audyn_worker_is_running(const audyn_worker_t *w);

/*
 * Get last error message (thread-safe version).
 *
 * Copies error message to caller's buffer.
 *
 * Parameters:
 *   w      - worker instance
 *   buf    - output buffer
 *   buflen - buffer size
 */
void audyn_worker_get_last_error(const audyn_worker_t *w, char *buf, size_t buflen);

/*
 * Get last error message.
 *
 * DEPRECATED: Use audyn_worker_get_last_error() for thread-safe access.
 * This function returns a pointer to internal storage that may change.
 *
 * Returns "ok" if no error has been recorded.
 */
const char *audyn_worker_last_error(const audyn_worker_t *w);

/*
 * Get worker statistics.
 *
 * Safe to call while worker is running (uses atomic reads).
 *
 * Parameters:
 *   w     - worker instance
 *   stats - output statistics structure
 */
void audyn_worker_get_stats(const audyn_worker_t *w, audyn_worker_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_WORKER_H */
