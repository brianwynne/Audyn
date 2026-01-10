/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      audio_queue.h
 *
 *  Purpose:
 *      Single-producer / single-consumer (SPSC) lock-free ring queue for pointers.
 *
 *  Intended Use:
 *      - Producer: real-time capture callback (e.g., PipeWire process callback)
 *      - Consumer: worker thread performing encoding / file I/O
 *
 *  Contract:
 *      - SPSC only: exactly one thread may call push(), and exactly one thread
 *        may call pop(), for the lifetime of the queue.
 *      - NULL pointers are reserved: push(NULL) is rejected because pop()
 *        uses NULL to signal an empty queue.
 *      - The queue is bounded. With capacity N, usable slots are N-1.
 *
 *  Dependencies:
 *      - Standard C: stdint.h
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

#ifndef AUDYN_AUDIO_QUEUE_H
#define AUDYN_AUDIO_QUEUE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audyn_audio_queue audyn_audio_queue_t;

/* Create/destroy (NOT real-time safe). */
audyn_audio_queue_t *audyn_audio_queue_create(uint32_t capacity);
void audyn_audio_queue_destroy(audyn_audio_queue_t *q);

/*
 * Push a pointer into the queue.
 *
 * Real-time safe (SPSC, lock-free, constant time).
 *
 * Returns:
 *   1 on success
 *   0 if the queue is full OR ptr is NULL OR q is NULL
 */
int   audyn_audio_queue_push(audyn_audio_queue_t *q, void *ptr);

/*
 * Pop a pointer from the queue.
 *
 * Real-time safe (SPSC, lock-free, constant time).
 *
 * Returns:
 *   pointer on success
 *   NULL if the queue is empty OR q is NULL
 */
void *audyn_audio_queue_pop(audyn_audio_queue_t *q);

/* Returns the configured capacity (0 if q is NULL). */
uint32_t audyn_audio_queue_capacity(const audyn_audio_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_AUDIO_QUEUE_H */
