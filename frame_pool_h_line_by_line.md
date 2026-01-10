# 📘 Audyn Reference: frame_pool.h — Line-by-Line Explanation

**Project:** Audyn — Professional Audio Capture & Archival Engine  
**Author:** B. Wynne  
**Status:** Canonical / Reference Implementation  
**Scope:** This document explains *every significant line* in `src/core/frame_pool.h`.

This file defines the **public contract** for Audyn’s real-time safe audio
memory model. It describes what is allowed, what is guaranteed, and what is
forbidden. The implementation in `frame_pool.c` must obey this contract.

---

## File purpose

`frame_pool.h` defines:

- the public data structures used to represent audio frame objects
- the opaque frame pool type
- the functions used to create, acquire, release, and destroy frame pools

This header contains **no implementation logic**.
It exists to define **rules and guarantees**.

---

## Include guards

```c
#ifndef AUDYN_FRAME_POOL_H
#define AUDYN_FRAME_POOL_H
```

These lines prevent the header from being included more than once in the same
translation unit.

Without include guards:
- duplicate type definitions would occur
- compilation would fail

---

## Required includes

```c
#include <stdint.h>
```

Provides fixed-width integer types such as `uint32_t`.

Fixed-width types are used to:
- make memory layout explicit
- ensure consistent behaviour across platforms

---

## Forward declaration

```c
typedef struct audyn_frame_pool audyn_frame_pool_t;
```

This declares the existence of a type named `audyn_frame_pool_t` without
revealing its contents.

Why this matters:

- callers can hold a pointer to a frame pool
- callers cannot see or modify internal fields
- the pool implementation remains private

This enforces encapsulation.

---

## Audio frame object structure

```c
typedef struct audyn_audio_frame {
    float *data;
    uint32_t frames;
    uint32_t channels;
    struct audyn_frame_pool *pool;
} audyn_audio_frame_t;
```

This structure represents **one audio frame object**.

Each field is explained below.

---

### `float *data;`

Points to the start of the audio sample memory for this frame.

Key properties:

- samples are 32-bit floating-point PCM
- samples are stored in a single contiguous buffer
- channels are interleaved

This pointer:
- is allocated by the frame pool
- must never be freed or reallocated by user code

---

### `uint32_t frames;`

The number of **sample frames** stored in this buffer.

A sample frame is:
- one point in time across all channels

This value:
- is fixed at pool creation
- never changes during runtime

---

### `uint32_t channels;`

The number of audio channels stored in this buffer.

Examples:
- 1 = mono
- 2 = stereo

This value:
- is fixed at pool creation
- must be read by code instead of assuming a channel count

---

### `struct audyn_frame_pool *pool;`

A back-reference to the owning frame pool.

This allows:
- safe return of the frame to the correct pool
- validation of correct ownership in debug builds

The frame does not own its memory.
Ownership always belongs to the pool.

---

## Public API functions

### Pool creation

```c
audyn_frame_pool_t *
audyn_frame_pool_create(uint32_t pool_size,
                        uint32_t channels,
                        uint32_t frames_per_buffer);
```

Creates and initializes a frame pool.

Parameters:

- `pool_size`  
  The total number of audio frame objects to allocate.

- `channels`  
  The number of audio channels per frame.

- `frames_per_buffer`  
  The number of sample frames per audio frame object.

All memory allocation happens inside this function.

If allocation fails, `NULL` is returned.

---

### Frame acquisition

```c
audyn_audio_frame_t *
audyn_frame_acquire(audyn_frame_pool_t *pool);
```

Acquires one free audio frame from the pool.

Important properties:

- safe to call from a real-time audio callback
- constant-time operation
- does not allocate memory

If no frames are available:
- `NULL` is returned
- this signals backpressure to the caller

---

### Frame release

```c
void
audyn_frame_release(audyn_frame_pool_t *pool,
                    audyn_audio_frame_t *frame);
```

Returns a previously acquired frame back to the pool.

Rules:

- every acquired frame must eventually be released
- frames must not be released more than once
- releasing to the wrong pool is a bug

This function does not free memory.
It only makes the frame available for reuse.

---

### Pool destruction

```c
void
audyn_frame_pool_destroy(audyn_frame_pool_t *pool);
```

Destroys a frame pool and frees all owned memory.

This function:
- frees all audio buffers
- frees all frame objects
- frees the pool itself

It must not be called while frames are still in use.

---

## End of include guard

```c
#endif /* AUDYN_FRAME_POOL_H */
```

Marks the end of the header file.

---

## Final notes for developers

This header defines **Audyn’s audio memory contract**.

If you follow it:
- audio is real-time safe
- memory usage is predictable
- behaviour is deterministic

If you violate it:
- correctness is lost
- audio glitches occur
- bugs become hard to reproduce
