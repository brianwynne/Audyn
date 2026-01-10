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
 * Create a PipeWire input instance.
 *
 * Notes:
 *   - sample_rate and channels describe the desired stream format.
 *   - The frame pool defines the fixed frame size (audyn_audio_frame_t::sample_frames)
 *     used for capture. If PipeWire delivers fewer frames, the remainder is zero-padded.
 *     If PipeWire delivers more frames than the frame can hold, that buffer is dropped.
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

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_PIPEWIRE_INPUT_H */
