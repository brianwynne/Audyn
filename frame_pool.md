# 📘 Audyn Reference: frame_pool

**Project:** Audyn — Professional Audio Capture & Archival Engine  
**Author:** B. Wynne  
**Applies to:** Ubuntu 24.04  
**Status:** Canonical / Reference Implementation  

---

## Overview

The `frame_pool` module defines **Audyn’s real-time safe audio memory model**.

It provides a fixed-size pool of reusable audio frame objects that can be safely
used in real-time (RT) audio callback contexts without allocating or freeing
memory during runtime.

This module is part of Audyn’s **core infrastructure**.  
All audio capture, processing, and recording paths depend on it.

---

## The Problem Being Solved

Real-time audio code must execute predictably.

In particular, code running in an **audio callback** must **never**:

- allocate memory
- free memory
- block on locks
- perform unbounded or unpredictable work

Dynamic memory operations can:
- block internally
- cause page faults
- introduce unbounded latency

Any of these can cause audible glitches or dropouts.

---

## Audyn’s Solution

Audyn solves this by:

- allocating **all audio memory once**, at startup
- reusing that memory for the lifetime of the system
- acquiring and releasing audio buffers in **constant time**

The `frame_pool` module provides this guarantee.

---

## Key Concepts

### Audio callback

An **audio callback** is a function that the audio system (e.g. PipeWire)
calls repeatedly to deliver small blocks of audio data.

In Audyn:
- it runs on a high-priority real-time thread
- it is called many times per second
- it must always finish on time

Because of this, audio callbacks must not allocate memory.

---

### Sample frame (canonical term)

A **sample frame** is:

> One point in time across all channels.

Example (stereo):
- sample frame 0 → L₀, R₀
- sample frame 1 → L₁, R₁

---

### Audio frame object (canonical term)

An **audio frame object** represents:

> A fixed-size buffer containing multiple sample frames of interleaved
> 32-bit floating-point PCM audio for a fixed number of channels.

Example (2 channels, interleaved):

```
L0, R0, L1, R1, L2, R2, ...
```

Each audio frame object contains:
- a pointer to interleaved PCM sample data
- metadata:
  - number of sample frames
  - number of channels
- a back-reference to the owning frame pool

Audio frame objects **do not own memory**.  
Ownership always remains with the pool.

---

## Internal Audio Format (Canonical)

Audyn uses **32-bit floating-point PCM** as its **internal audio format**.

All audio stored in `frame_pool`:
- uses `float` samples
- is interleaved by channel
- has fixed size and layout

External audio formats (e.g. AES67 24-bit PCM) are:
- converted at the input boundary
- never stored directly in the frame pool

This keeps internal audio handling simple, safe, and consistent.

---

## How the Frame Pool Works

Conceptually:

```
[ frame_pool ]
     |
     |-- free_stack (LIFO)
           |
           +--> frame pointer
           +--> frame pointer
           +--> frame pointer
```

At startup:
- all frame objects are created
- all sample buffers are allocated
- all frames are placed on the free stack

At runtime:
- the RT thread acquires frames
- fills them with audio
- passes them to a queue
- a non-RT worker consumes the audio
- the frame is released back to the pool

No allocation or freeing occurs during steady-state operation.

---

## Design Guarantees

The `frame_pool` module guarantees:

| Property                         | Guaranteed |
|----------------------------------|------------|
| RT-safe operation                | ✅ |
| No allocation in RT paths        | ✅ |
| Fixed memory footprint           | ✅ |
| Constant-time acquire/release    | ✅ |
| Deterministic behaviour          | ✅ |
| Stable frame identity            | ✅ |
| Clear ownership semantics        | ✅ |

---

## Design Invariants (Authoritative)

### 1️⃣ Frame identity is stable

- All audio frame objects live in `pool->frames[]`
- A frame’s identity is its **address** in that array
- That address never changes for the lifetime of the pool

Frames are **never copied**, moved, or recreated.

---

### 2️⃣ The free list stores pointers, not frame structs

The free list is implemented as:

```c
audyn_audio_frame_t **free_stack;
```

It stores **pointers** to stable frame objects.

Copying frame structs would:
- break pointer identity
- corrupt the pool
- cause non-deterministic bugs

The implementation explicitly avoids this.

---

### 3️⃣ Frame size and format are immutable

For every frame:
- number of sample frames
- number of channels
- sample format (32-bit float)
- interleaved layout

are set **once**, during pool creation.

They must **never** be modified at runtime.

Changing them is undefined behaviour.

---

## Threading Model

The frame pool is designed for **Single Producer / Single Consumer (SPSC)** use:

- **RT thread** → acquires frames
- **Non-RT thread** → releases frames

This is enforced using C11 atomics.

---

### Important limitation

This implementation is **not safe** for:
- multiple acquiring threads
- multiple releasing threads
- general MPMC usage

---

## Developer Rules (Mandatory)

✔ Never allocate audio buffers outside `frame_pool`  
✔ Never free `frame->data` manually  
✔ Treat `NULL` from acquire as valid backpressure  
✔ Always release frames after use  
✔ Never change frame size or channel count at runtime  
✔ Never copy audio frame structs  

---

## Reference Implementation

`src/core/frame_pool.c` is the **reference implementation** for:

- real-time safe audio memory management
- stable audio frame object handling
- SPSC lock-free design using C11 atomics

---

## Summary

The `frame_pool` module is Audyn’s **memory contract**.

If you respect it:
- the system is real-time safe
- latency is bounded
- behaviour is predictable

If you violate it:
- audio glitches occur
- bugs become non-reproducible
- correctness is lost
