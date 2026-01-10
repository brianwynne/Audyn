/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      frame_pool.h
 *
 *  Purpose:
 *      Public interface for Audyn's real-time safe audio frame pool.
 *
 *  Terminology (Canonical):
 *      - Sample frame: one time index across all channels (e.g. L0,R0).
 *      - Audio frame object: a fixed-size buffer containing multiple
 *        sample frames of interleaved floating-point PCM audio.
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

#ifndef AUDYN_FRAME_POOL_H
#define AUDYN_FRAME_POOL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque frame pool type */
typedef struct audyn_frame_pool audyn_frame_pool_t;

/*
 * Interleaved PCM indexing helper.
 *
 * data[(sample_frame_index * channels) + channel]
 */
#define AUDYN_PCM_IDX(sample_i, chan, channels)     (((sample_i) * (channels)) + (chan))

/*
 * Audio frame object.
 *
 * Represents a fixed-size buffer containing multiple sample frames
 * of interleaved floating-point PCM audio.
 *
 * Ownership:
 *      - Memory is owned by the frame pool
 *      - Must be returned via audyn_frame_release()
 */
typedef struct audyn_audio_frame {
    float *data;                    /* Interleaved PCM sample data */
    uint32_t sample_frames;         /* Number of sample frames */
    uint32_t channels;              /* Channel count */
    audyn_frame_pool_t *pool;       /* Owning pool (internal use) */
} audyn_audio_frame_t;

/*
 * Create a frame pool.
 *
 * Allocates all audio frame objects and buffers. No further memory
 * allocation occurs after this call.
 *
 * NOT real-time safe.
 */
audyn_frame_pool_t *audyn_frame_pool_create(
    uint32_t pool_size,
    uint32_t channels,
    uint32_t sample_frames_per_buffer
);

/*
 * Acquire an audio frame object from the pool.
 *
 * Real-time safe:
 *      - Lock-free (SPSC)
 *      - Constant time
 *      - Non-blocking
 *
 * Returns:
 *      - Pointer to audio frame object
 *      - NULL if pool is exhausted (backpressure signal)
 */
audyn_audio_frame_t *audyn_frame_acquire(
    audyn_frame_pool_t *pool
);

/*
 * Release an audio frame object back to its owning pool.
 *
 * Lock-free (SPSC). The frame must originate from audyn_frame_acquire().
 * The frame's owning pool is frame->pool.
 */
void audyn_frame_release(
    audyn_audio_frame_t *frame
);

/*
 * Destroy a frame pool and free all associated memory.
 *
 * All audio processing must have stopped and no frames may be
 * in flight when this function is called.
 *
 * NOT real-time safe.
 */
void audyn_frame_pool_destroy(
    audyn_frame_pool_t *pool
);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_FRAME_POOL_H */
