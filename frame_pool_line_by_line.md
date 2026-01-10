# 📘 Audyn Reference: frame_pool.c — Line‑by‑Line Explanation

**Project:** Audyn — Professional Audio Capture & Archival Engine  
**Author:** B. Wynne  
**Status:** Canonical / Reference Implementation  
**Scope:** This document explains *every significant line* in `src/core/frame_pool.c`.

This file is intended for:
- onboarding new and junior developers
- understanding *why* the implementation is correct
- auditing real‑time safety and correctness

---

## File header

The opening comment documents intent and guarantees.

Key points:
- All memory allocation happens during initialization
- Acquire/release are lock‑free
- Frame objects are stable for the lifetime of the pool
- The design is SPSC (single‑producer / single‑consumer)

These claims must be validated by the code below.

---

## Includes

```c
#include "frame_pool.h"
```

Includes the public contract:
- struct definitions
- function declarations
- ownership rules

This ensures the implementation matches the header exactly.

```c
#include <stdlib.h>
```

Provides:
- `calloc`
- `free`

All dynamic allocation in this file comes from here and occurs only during
initialization or destruction.

```c
#include <stdatomic.h>
```

Provides:
- C11 atomic types and operations

Used to make acquire/release safe between two threads without locks.

```c
#include <assert.h>
```

Used only for debug‑time correctness checks.
Assertions are not relied on for runtime safety.

```c
#ifdef AUDYN_DEBUG
#include <math.h>
#endif
```

Only included when debug features are enabled.
Used for buffer poisoning (`NaN`).

---

## Internal pool structure

```c
struct audyn_frame_pool {
    audyn_audio_frame_t *frames;
    audyn_audio_frame_t **free_stack;
    uint32_t capacity;
    _Atomic uint32_t top;
};
```

### `frames`
- Stable array of audio frame objects
- Each element exists for the lifetime of the pool
- Frame identity is defined by its address here

### `free_stack`
- Stack of *pointers* to available frames
- Stores addresses, not copies
- Order is not important

### `capacity`
- Total number of frames in the pool
- Fixed at creation time

### `top`
- Atomic counter
- Represents the number of free frames
- Also acts as the stack index

Invariant:
```
0 ≤ top ≤ capacity
```

---

## Pool creation

```c
audyn_frame_pool_t *
audyn_frame_pool_create(uint32_t pool_size,
                        uint32_t channels,
                        uint32_t frames_per_buffer)
```

Creates and fully initializes a frame pool.
After this function returns successfully, no more allocation is required.

---

### Parameter validation

```c
if (pool_size == 0 || channels == 0 || frames_per_buffer == 0)
    return NULL;
```

Prevents creation of an invalid pool.

---

### Allocate pool struct

```c
pool = calloc(1, sizeof(*pool));
```

Allocates and zero‑initializes the pool object.
This ensures known initial state.

---

### Allocate frame storage and free stack

```c
pool->frames = calloc(pool_size, sizeof(audyn_audio_frame_t));
pool->free_stack = calloc(pool_size, sizeof(audyn_audio_frame_t *));
```

Allocates:
- the stable frame objects
- the stack of frame pointers

If either fails, the pool is destroyed.

---

### Initialize atomic state

```c
atomic_init(&pool->top, pool_size);
```

All frames start free.
The free stack is initially full.

---

### Initialize each frame

```c
for (i = 0; i < pool_size; ++i) {
```

Every frame is configured identically.

```c
frame->frames   = frames_per_buffer;
frame->channels = channels;
frame->pool     = pool;
```

These values define the immutable shape of the frame.

```c
frame->data = calloc(frames_per_buffer * channels, sizeof(float));
```

Allocates the audio sample buffer.

This line guarantees:
- 32‑bit floating‑point PCM
- interleaved layout
- fixed buffer size

```c
pool->free_stack[i] = frame;
```

Places the frame on the free stack.
All frames start available.

---

## Frame acquisition (RT‑safe)

```c
audyn_audio_frame_t *
audyn_frame_acquire(audyn_frame_pool_t *pool)
```

Called from the real‑time thread.

```c
idx = atomic_load_explicit(&pool->top, memory_order_acquire);
```

Reads the number of free frames.

```c
if (idx == 0)
    return NULL;
```

No free frames → backpressure.

```c
idx = atomic_fetch_sub_explicit(&pool->top, 1, memory_order_acquire) - 1;
```

Atomically reserves one frame.

```c
return pool->free_stack[idx];
```

Returns a stable frame pointer.
No allocation, no locks.

---

## Frame release (non‑RT)

```c
audyn_frame_release(audyn_frame_pool_t *pool,
                    audyn_audio_frame_t *frame)
```

Returns a frame to the pool.

```c
idx = atomic_fetch_add_explicit(&pool->top, 1, memory_order_release);
```

Reserves a slot on the free stack.

```c
if (idx >= pool->capacity) {
    atomic_fetch_sub_explicit(&pool->top, 1, memory_order_relaxed);
    return;
}
```

Defensive check against misuse.

```c
pool->free_stack[idx] = frame;
```

Pushes the pointer back onto the stack.
Frame identity is preserved.

---

## Pool destruction

```c
audyn_frame_pool_destroy(audyn_frame_pool_t *pool)
```

Frees all memory owned by the pool.

```c
free(pool->frames[i].data);
```

Frees each audio buffer.

```c
free(pool->frames);
free(pool->free_stack);
free(pool);
```

Complete teardown.
No memory leaks.

---

## Final guarantees (proven by code)

- No allocation in RT paths
- Constant‑time acquire/release
- Stable frame identity
- Fixed memory footprint
- SPSC‑safe without locks

Any change violating these properties is a defect.
