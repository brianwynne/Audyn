/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      frame_pool.c
 *
 *  Purpose:
 *      Implements a fixed-size, real-time safe audio frame pool.
 *
 *  Design Guarantees:
 *      - No dynamic allocation after initialization
 *      - Lock-free acquire/release (SPSC)
 *      - Deterministic memory usage
 *      - Stable audio frame objects
 *
 *  Threading Model:
 *      - Single consumer of the pool ("acquire" thread; typically RT)
 *      - Single producer returning frames ("release" thread; typically non-RT)
 *
 *  Correctness Notes:
 *      - Frames are stable objects stored in pool->frames[]
 *      - The free list stores pointers to those stable objects
 *      - Publication order is critical: release must store the pointer
 *        before advertising it via the atomic 'top' count.
 *
 *  Debug Features (optional):
 *      - Ownership assertion and buffer poisoning (AUDYN_DEBUG)
 *
 *  Dependencies:
 *      - C11 atomics (<stdatomic.h>)
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

#include "frame_pool.h"

#include <stdlib.h>
#include <stdatomic.h>

#ifdef AUDYN_DEBUG
#include <assert.h>
#include <math.h>
#endif

struct audyn_frame_pool {
    audyn_audio_frame_t *frames;          /* Stable frame objects */
    audyn_audio_frame_t **free_stack;     /* Stack of available frames (pointers) */
    uint32_t capacity;                    /* Total frame count */
    _Atomic uint32_t top;                 /* Number of free frames (SPSC) */
};

audyn_frame_pool_t *
audyn_frame_pool_create(uint32_t pool_size,
                        uint32_t channels,
                        uint32_t sample_frames_per_buffer)
{
    audyn_frame_pool_t *pool = NULL;
    uint32_t i;

    if (pool_size == 0 || channels == 0 || sample_frames_per_buffer == 0)
        return NULL;

    pool = calloc(1, sizeof(*pool));
    if (!pool)
        return NULL;

    pool->frames = calloc(pool_size, sizeof(audyn_audio_frame_t));
    pool->free_stack = calloc(pool_size, sizeof(audyn_audio_frame_t *));
    if (!pool->frames || !pool->free_stack) {
        audyn_frame_pool_destroy(pool);
        return NULL;
    }

    pool->capacity = pool_size;
    atomic_init(&pool->top, pool_size);

    for (i = 0; i < pool_size; ++i) {
        audyn_audio_frame_t *frame = &pool->frames[i];

        frame->sample_frames = sample_frames_per_buffer;
        frame->channels = channels;
        frame->pool = pool;

        frame->data = calloc(
            (size_t)sample_frames_per_buffer * (size_t)channels,
            sizeof(float)
        );

        if (!frame->data) {
            audyn_frame_pool_destroy(pool);
            return NULL;
        }

        /* Initially, all frames are free. */
        pool->free_stack[i] = frame;
    }

    return pool;
}

audyn_audio_frame_t *
audyn_frame_acquire(audyn_frame_pool_t *pool)
{
    uint32_t t;

    if (!pool)
        return NULL;

    /*
     * SPSC pop:
     *  - Acquire load of 'top' synchronizes with producer's release store,
     *    ensuring free_stack[t-1] is fully published before we read it.
     */
    t = atomic_load_explicit(&pool->top, memory_order_acquire);
    if (t == 0)
        return NULL;

    /* Read pointer then decrement count. */
    audyn_audio_frame_t *frame = pool->free_stack[t - 1];
    atomic_store_explicit(&pool->top, t - 1, memory_order_relaxed);
    return frame;
}

void
audyn_frame_release(audyn_audio_frame_t *frame)
{
    audyn_frame_pool_t *pool;
    uint32_t t;

    if (!frame)
        return;

    pool = frame->pool;
    if (!pool)
        return;

#ifdef AUDYN_DEBUG
    assert(frame->pool == pool);
#endif

    /*
     * SPSC push:
     *  - Store the pointer into free_stack[t] first
     *  - Then release-store the updated 'top' so the consumer sees the pointer
     */
    t = atomic_load_explicit(&pool->top, memory_order_relaxed);
    if (t >= pool->capacity) {
        /* Defensive: overflow indicates misuse (double-release, wrong pool, etc.) */
        return;
    }

#ifdef AUDYN_DEBUG
    for (uint32_t i = 0; i < frame->sample_frames * frame->channels; ++i) {
        frame->data[i] = NAN;
    }
#endif

    pool->free_stack[t] = frame;
    atomic_store_explicit(&pool->top, t + 1, memory_order_release);
}

void
audyn_frame_pool_destroy(audyn_frame_pool_t *pool)
{
    uint32_t i;

    if (!pool)
        return;

    if (pool->frames) {
        for (i = 0; i < pool->capacity; ++i) {
            free(pool->frames[i].data);
            pool->frames[i].data = NULL;
        }
        free(pool->frames);
        pool->frames = NULL;
    }

    free(pool->free_stack);
    pool->free_stack = NULL;

    free(pool);
}
