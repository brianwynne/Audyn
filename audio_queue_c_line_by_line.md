# 📘 Audyn Reference: audio_queue.c — Line-by-Line Explanation

**Project:** Audyn — Professional Audio Capture & Archival Engine  
**Author:** B. Wynne  
**Status:** Canonical / Reference Implementation  
**Scope:** Implementation of the SPSC audio frame queue.

---

## File purpose

`audio_queue.c` implements a **single-producer / single-consumer (SPSC)** queue
for audio frame objects.

It is designed to:
- be lock-free
- be real-time safe
- provide deterministic behaviour

---

## Includes

```c
#include "audio_queue.h"
```

Brings in the public API and type declarations.

```c
#include <stdlib.h>
#include <stdatomic.h>
#include <assert.h>
```

Used for:
- memory allocation (initialization only)
- atomic operations
- debug-time correctness checks

---

## Internal queue structure

```c
struct audyn_audio_queue {
    audyn_audio_frame_t **buf;
    uint32_t cap;
    _Atomic uint32_t head;
    _Atomic uint32_t tail;
};
```

### `buf`
Ring buffer storing frame pointers.

### `cap`
Fixed capacity of the queue.

### `head`
Index of the next element to read.

### `tail`
Index of the next element to write.

This layout implements a circular buffer.

---

## Queue creation

```c
audyn_audio_queue_create(uint32_t capacity)
```

Allocates:
- queue structure
- ring buffer storage

All allocation happens here.

---

## Push operation

```c
audyn_audio_queue_push(...)
```

- Loads head and tail atomically
- Checks for full queue
- Stores frame pointer
- Advances tail

Constant time.
RT-safe.

---

## Pop operation

```c
audyn_audio_queue_pop(...)
```

- Checks if queue is empty
- Retrieves frame pointer
- Advances head

Constant time.
Non-blocking.

---

## Queue size and capacity

```c
audyn_audio_queue_size(...)
audyn_audio_queue_capacity(...)
```

Utility functions for monitoring and diagnostics.

---

## Design guarantees proven by code

- No allocation in steady state
- Lock-free SPSC operation
- Stable frame pointer handling
- Deterministic behaviour

---

## Relationship to frame_pool

The queue:
- never allocates frames
- never frees frames
- only moves pointers

This preserves the Audyn memory contract.

---

## Summary

`audio_queue.c` provides safe, predictable transfer of audio frames between
threads without violating real-time constraints.
