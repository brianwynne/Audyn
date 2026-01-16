/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      pipewire_input.h
 *
 *  Purpose:
 *      PipeWire capture input for Ubuntu 24.04+.
 *      Captures float32 interleaved buffers from PipeWire and enqueues
 *      audyn_audio_frame_t* objects into an SPSC audyn_audio_queue_t.
 *
 *  Real-time Constraints:
 *      - The PipeWire process callback MUST NOT allocate memory or block.
 *      - Frames MUST be acquired from audyn_frame_pool_t (lock-free) and
 *        pushed to audyn_audio_queue_t (lock-free).
 *
 *  Dependencies:
 *      - PipeWire: libpipewire-0.3, libspa-0.2
 *      - POSIX: pthread
 *      - Audyn: frame_pool, audio_queue
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

#ifndef AUDYN_PIPEWIRE_INPUT_H
#define AUDYN_PIPEWIRE_INPUT_H

#include <stdint.h>

#include "frame_pool.h"
#include "audio_queue.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audyn_pw_input audyn_pw_input_t;

/*
 * PipeWire input statistics.
 *
 * All counters are atomically updated and can be read while capture is running.
 */
typedef struct audyn_pw_stats {
    uint64_t frames_captured;   /* Total audio frames successfully captured */
    uint64_t callbacks;         /* Total process callbacks received */
    uint64_t drops_pool;        /* Drops due to frame pool exhaustion */
    uint64_t drops_queue;       /* Drops due to audio queue full */
    uint64_t drops_empty;       /* Drops due to empty/invalid PipeWire buffer */
    uint64_t truncations;       /* Times buffer was truncated to fit frame */
} audyn_pw_stats_t;

/*
 * Create a PipeWire input instance.
 *
 * Parameters:
 *   pool        - Frame pool for acquiring audio frames (lock-free)
 *   queue       - Audio queue for pushing captured frames (lock-free)
 *   sample_rate - Desired sample rate in Hz (1-384000)
 *   channels    - Desired channel count (1-32)
 *
 * Notes:
 *   - The frame pool defines the fixed frame size (audyn_audio_frame_t::sample_frames)
 *     used for capture. If PipeWire delivers more frames than capacity, data is truncated.
 *   - Downstream consumers (e.g., opus_sink FIFO) accumulate frames as needed.
 *   - Errors are logged via the project's logging system.
 *
 * Returns:
 *   pointer on success, NULL on failure.
 */
audyn_pw_input_t *audyn_pw_input_create(audyn_frame_pool_t *pool,
                                        audyn_audio_queue_t *queue,
                                        uint32_t sample_rate,
                                        uint32_t channels);

/* Start/stop capture (NOT real-time safe). */
int  audyn_pw_input_start(audyn_pw_input_t *in);
void audyn_pw_input_stop(audyn_pw_input_t *in);

/* Destroy input (NOT real-time safe). Calls stop() internally. */
void audyn_pw_input_destroy(audyn_pw_input_t *in);

/*
 * Check if capture is currently running.
 *
 * Returns 1 if running, 0 if stopped or NULL.
 */
int audyn_pw_input_is_running(const audyn_pw_input_t *in);

/*
 * Get capture statistics.
 *
 * Safe to call while capture is running (uses atomic reads).
 *
 * Parameters:
 *   in    - input instance (must not be NULL)
 *   stats - output statistics structure (must not be NULL)
 */
void audyn_pw_input_get_stats(const audyn_pw_input_t *in, audyn_pw_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_PIPEWIRE_INPUT_H */
