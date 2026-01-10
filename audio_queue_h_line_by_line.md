# 📘 Audyn Reference: audio_queue.h — Line-by-Line Explanation

**Project:** Audyn — Professional Audio Capture & Archival Engine  
**Author:** B. Wynne  
**Status:** Canonical / Reference Implementation  
**Scope:** Public contract and API for Audyn’s audio queue.

---

## Purpose of this header

`audio_queue.h` defines the public API for a **real-time safe audio frame queue**.

The queue sits *between*:
- the RT thread that produces audio frames, and
- a non-RT worker thread that consumes them.

It defines:
- an opaque queue type
- queue creation and destruction
- push (enqueue) and pop (dequeue) operations
- capacity and state queries

No implementation details are exposed here.

---

## Include guards

```c
#ifndef AUDYN_AUDIO_QUEUE_H
#define AUDYN_AUDIO_QUEUE_H
```

Prevents multiple inclusion of this header in a single translation unit.

---

## Required includes

```c
#include <stdint.h>
#include <stdbool.h>
```

- `stdint.h` provides fixed-width integer types (`uint32_t`)
- `stdbool.h` provides the `bool` type

These are used to keep the API explicit and portable.

---

## Forward declarations

```c
typedef struct audyn_audio_queue audyn_audio_queue_t;
```

Declares the queue type without revealing its internals.

This enforces encapsulation:
- callers can hold and pass queue handles
- callers cannot modify internal state

---

## Dependency on frame_pool

```c
#include "frame_pool.h"
```

The audio queue operates on `audyn_audio_frame_t` objects.

This makes the queue:
- part of the same memory contract as `frame_pool`
- responsible only for ordering, not allocation

---

## Queue creation

```c
audyn_audio_queue_t *
audyn_audio_queue_create(uint32_t capacity);
```

Creates a queue that can hold up to `capacity` audio frames.

Important properties:
- capacity is fixed at creation time
- no allocation occurs during enqueue/dequeue
- returns `NULL` on allocation failure

---

## Queue destruction

```c
void
audyn_audio_queue_destroy(audyn_audio_queue_t *q);
```

Destroys the queue and frees all owned memory.

Rules:
- must not be called while the queue is in use
- does not free audio frames themselves

---

## Enqueue operation

```c
bool
audyn_audio_queue_push(audyn_audio_queue_t *q,
                       audyn_audio_frame_t *frame);
```

Attempts to push a frame onto the queue.

Returns:
- `true` if the frame was queued
- `false` if the queue is full

Properties:
- constant time
- RT-safe
- no allocation

---

## Dequeue operation

```c
audyn_audio_frame_t *
audyn_audio_queue_pop(audyn_audio_queue_t *q);
```

Removes and returns the next frame from the queue.

Returns:
- a frame pointer if available
- `NULL` if the queue is empty

---

## Queue state queries

```c
uint32_t audyn_audio_queue_size(const audyn_audio_queue_t *q);
uint32_t audyn_audio_queue_capacity(const audyn_audio_queue_t *q);
```

Used for diagnostics and monitoring.

Not intended for RT decision-making.

---

## C++ compatibility

```c
#ifdef __cplusplus
extern "C" {
#endif
...
#ifdef __cplusplus
}
#endif
```

Ensures the API can be used from C++ code.

---

## End of include guard

```c
#endif /* AUDYN_AUDIO_QUEUE_H */
```

---

## Final notes

This header defines the **ordering contract** for audio frames.

Memory ownership remains with `frame_pool`.
The queue only manages *when* frames move between threads.
