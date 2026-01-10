/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      audio_queue.c
 *
 *  Purpose:
 *      Implements a bounded SPSC ring queue of pointers using C11 atomics.
 *
 *  Correctness:
 *      - This is a classic SPSC ring with head/tail indices.
 *      - Producer writes slots[tail] then publishes tail with release semantics.
 *      - Consumer acquires tail then reads slots[head] and advances head.
 *      - Head is written only by the consumer; tail is written only by producer.
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

#include "audio_queue.h"

#include <stdlib.h>
#include <stdatomic.h>

struct audyn_audio_queue {
    uint32_t cap;        /* Total slots in ring; usable capacity is cap-1 */
    void   **slots;

    _Atomic uint32_t head; /* consumer-owned index */
    _Atomic uint32_t tail; /* producer-owned index */
};

static inline uint32_t next_idx(uint32_t cur, uint32_t cap)
{
    cur++;
    return (cur == cap) ? 0u : cur;
}

audyn_audio_queue_t *audyn_audio_queue_create(uint32_t capacity)
{
    if (capacity < 2)
        return NULL;

    audyn_audio_queue_t *q = calloc(1, sizeof(*q));
    if (!q)
        return NULL;

    q->cap = capacity;
    q->slots = calloc((size_t)capacity, sizeof(void *));
    if (!q->slots) {
        free(q);
        return NULL;
    }

    atomic_init(&q->head, 0u);
    atomic_init(&q->tail, 0u);

    return q;
}

void audyn_audio_queue_destroy(audyn_audio_queue_t *q)
{
    if (!q)
        return;

    free(q->slots);
    q->slots = NULL;
    free(q);
}

int audyn_audio_queue_push(audyn_audio_queue_t *q, void *ptr)
{
    if (!q || !ptr)
        return 0;

    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&q->head, memory_order_acquire);

    uint32_t nt = next_idx(tail, q->cap);
    if (nt == head)
        return 0; /* full */

    q->slots[tail] = ptr;

    /* Publish the new tail after writing the slot. */
    atomic_store_explicit(&q->tail, nt, memory_order_release);
    return 1;
}

void *audyn_audio_queue_pop(audyn_audio_queue_t *q)
{
    if (!q)
        return NULL;

    uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);

    if (head == tail)
        return NULL; /* empty */

    void *ptr = q->slots[head];

    /* Advance head after consuming the slot. */
    atomic_store_explicit(&q->head, next_idx(head, q->cap), memory_order_release);
    return ptr;
}

uint32_t audyn_audio_queue_capacity(const audyn_audio_queue_t *q)
{
    return q ? q->cap : 0u;
}
