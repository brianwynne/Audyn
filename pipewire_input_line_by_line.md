# PipeWire Input Implementation Review

## Overview

The `pipewire_input` module captures audio from PipeWire (the modern Linux audio server) and feeds it into Audyn's lock-free audio pipeline. This is a **real-time audio component** with strict constraints on what operations can be performed in the audio callback.

**Files:**
- `input/pipewire_input.h` - Public API header
- `input/pipewire_input.c` - Implementation

## Real-Time Constraints

The PipeWire process callback (`on_process`) runs in a real-time thread context. It **MUST NOT**:
- Allocate or free memory (no malloc/free)
- Block on locks, I/O, or syscalls
- Call non-RT-safe functions

Operations that **ARE** safe in the callback:
- Lock-free queue/pool operations (audyn_frame_acquire, audyn_audio_queue_push)
- Atomic operations (statistics counters)
- memcpy for audio data
- PipeWire buffer management (dequeue/queue)

## Key Design Decisions

### 1. Lock-Free Architecture

The entire audio path is lock-free:
- `audyn_frame_pool_t` provides lock-free frame acquisition
- `audyn_audio_queue_t` provides lock-free SPSC queue
- Statistics use atomic counters

### 2. Reference-Counted PipeWire Initialization

PipeWire's `pw_init()`/`pw_deinit()` are process-global. Multiple input instances share initialization via atomic reference counting:

```c
static _Atomic int g_pw_refcnt = 0;

static void pw_ref_init(void) {
    int prev = atomic_fetch_add_explicit(&g_pw_refcnt, 1, memory_order_acq_rel);
    if (prev == 0) pw_init(NULL, NULL);
}
```

### 3. Dedicated Thread for PipeWire Loop

PipeWire requires its main loop to run continuously. We spawn a dedicated thread:
```c
static void *pw_thread_main(void *arg) {
    audyn_pw_input_t *in = (audyn_pw_input_t*)arg;
    pw_main_loop_run(in->loop);  /* Blocks until quit */
    return NULL;
}
```

### 4. Variable Buffer Size Handling

PipeWire delivers variable-sized buffers. The sink:
- Copies exactly what PipeWire delivers
- Truncates if larger than frame capacity (with tracking)
- Sets `frame->sample_frames` to actual count
- Downstream FIFOs (opus_sink, etc.) accumulate as needed

### 5. Atomic Statistics for RT-Safe Monitoring

Statistics are tracked with atomic counters using relaxed memory ordering (sufficient for counters):
```c
atomic_fetch_add_explicit(&in->frames_captured, nframes, memory_order_relaxed);
```

## Structure Definition

```c
struct audyn_pw_input {
    audyn_frame_pool_t  *pool;      /* Lock-free frame pool */
    audyn_audio_queue_t *q;         /* Lock-free output queue */

    uint32_t rate;                  /* Requested sample rate */
    uint32_t channels;              /* Requested channel count */

    struct pw_main_loop *loop;      /* PipeWire main loop */
    struct pw_context   *ctx;       /* PipeWire context */
    struct pw_stream    *stream;    /* PipeWire capture stream */

    pthread_t thread;               /* Loop thread */
    int thread_started;             /* 1 if thread running */
    int running;                    /* 1 if capture active */

    /* Atomic statistics for RT-safe updates */
    _Atomic uint64_t frames_captured;   /* Frames successfully captured */
    _Atomic uint64_t callbacks;         /* Total process callbacks */
    _Atomic uint64_t drops_pool;        /* Drops: pool exhausted */
    _Atomic uint64_t drops_queue;       /* Drops: queue full */
    _Atomic uint64_t drops_empty;       /* Drops: empty/invalid buffer */
    _Atomic uint64_t truncations;       /* Times we truncated data */
};
```

## Function-by-Function Analysis

### Global Reference Counting

#### `pw_ref_init()` (lines 55-60)
Atomically increments global refcount. Calls `pw_init()` on first reference.
Uses `memory_order_acq_rel` for proper synchronization.

#### `pw_ref_deinit()` (lines 62-68)
Atomically decrements global refcount. Calls `pw_deinit()` on last reference.

### Process Callback (RT Context)

#### `on_process()` (lines 95-172)

**This is the real-time audio callback. No allocations or blocking allowed.**

**Flow:**
1. Increment callback counter (atomic)
2. Dequeue PipeWire buffer
3. Validate buffer has data (track drops_empty)
4. Calculate frame count from byte size
5. Acquire frame from lock-free pool (track drops_pool)
6. Validate channel agreement
7. Calculate copy size (truncate if needed, track truncations)
8. memcpy audio data
9. Set frame's actual sample count
10. Push to lock-free queue (track drops_queue)
11. Track frames_captured on success
12. Return PipeWire buffer

**Statistics tracked (all atomic):**
- `callbacks` - Every callback invocation
- `drops_empty` - Empty or invalid PipeWire buffers
- `drops_pool` - Frame pool exhausted
- `drops_queue` - Audio queue full
- `truncations` - Buffer larger than frame capacity
- `frames_captured` - Successfully queued frames

### Thread Function

#### `pw_thread_main()` (lines 179-185)
Simple thread entry point that runs PipeWire's main loop until `pw_main_loop_quit()` is called.

### Public API

#### `audyn_pw_input_create()` (lines 187-286)

Creates and initializes a PipeWire input instance.

**Validation:**
- Pool and queue must be non-NULL
- Sample rate: 1 to 384000 Hz
- Channels: 1 to 32

**Initialization sequence:**
1. Validate parameters
2. Call `pw_ref_init()` for global PipeWire init
3. Allocate structure
4. Initialize atomic counters
5. Create PipeWire main loop
6. Create PipeWire context
7. Create PipeWire stream with properties:
   - Media type: Audio
   - Category: Capture
   - Role: Production
8. Build SPA format pod (F32 interleaved)
9. Connect stream with flags:
   - `AUTOCONNECT` - Auto-route to default source
   - `MAP_BUFFERS` - Memory-map buffers
   - `RT_PROCESS` - Real-time callback

**Error handling:** Each step has logging and proper cleanup via `goto fail`.

#### `audyn_pw_input_start()` (lines 289-308)

Starts the capture thread.

**Process:**
1. Validate input exists and not already started
2. Create pthread for main loop
3. Set running flag
4. Log start

#### `audyn_pw_input_stop()` (lines 311-332)

Stops capture and logs final statistics.

**Process:**
1. Signal main loop to quit
2. Join thread
3. Clear running flags
4. Log final statistics (DEBUG level)

#### `audyn_pw_input_destroy()` (lines 334-346)

Cleanup function.

**Process:**
1. Stop if running
2. Destroy PipeWire stream
3. Destroy PipeWire context
4. Destroy PipeWire main loop
5. Free structure
6. Decrement global PipeWire refcount

#### `audyn_pw_input_is_running()` (lines 348-352)

Returns 1 if capture is active, 0 otherwise.

#### `audyn_pw_input_get_stats()` (lines 354-364)

Retrieves statistics with atomic reads. Safe to call while capture is running.

## Improvements Made

### 1. Comprehensive Logging
- All creation steps now log on failure
- Start/stop transitions logged
- Final statistics logged on stop

### 2. Statistics Tracking
- Added `audyn_pw_stats_t` structure
- Six atomic counters covering all drop scenarios
- `audyn_pw_input_get_stats()` for runtime monitoring
- Statistics logged on stop

### 3. Input Validation
- Sample rate limited to 1-384000 Hz
- Channels limited to 1-32
- Descriptive error messages

### 4. State Tracking
- Added `running` flag
- `audyn_pw_input_is_running()` query function

### 5. Truncation Tracking
- Count when PipeWire delivers more data than frame capacity
- Helps diagnose frame pool sizing issues

## PipeWire Configuration

### Stream Properties

```c
pw_properties_new(
    PW_KEY_MEDIA_TYPE, "Audio",
    PW_KEY_MEDIA_CATEGORY, "Capture",
    PW_KEY_MEDIA_ROLE, "Production",
    NULL
)
```

- **Media Type:** Audio (not video, MIDI, etc.)
- **Category:** Capture (input, not playback)
- **Role:** Production (professional audio, gets higher priority)

### Stream Flags

```c
PW_STREAM_FLAG_AUTOCONNECT |    /* Connect to default source */
PW_STREAM_FLAG_MAP_BUFFERS |    /* Memory-map audio buffers */
PW_STREAM_FLAG_RT_PROCESS       /* RT callback context */
```

### Audio Format

```c
struct spa_audio_info_raw info = {
    .format = SPA_AUDIO_FORMAT_F32,  /* 32-bit float */
    .rate = sample_rate,              /* e.g., 48000 */
    .channels = channels              /* e.g., 2 */
};
```

## Thread Safety

- **Process callback:** Runs in PipeWire's RT thread
- **Main loop thread:** Dedicated pthread
- **Statistics:** Atomic counters safe for concurrent read/write
- **Create/Start/Stop/Destroy:** NOT thread-safe, call from single thread

## Dependencies

- **PipeWire:** libpipewire-0.3
- **SPA:** libspa-0.2 (Simple Plugin API)
- **POSIX:** pthread
- **Audyn:** frame_pool, audio_queue, log

## Usage Example

```c
/* Create frame pool and queue (must be lock-free) */
audyn_frame_pool_t *pool = audyn_frame_pool_create(1024, 2, 64);
audyn_audio_queue_t *queue = audyn_audio_queue_create(256);

/* Create PipeWire input */
audyn_pw_input_t *pw = audyn_pw_input_create(pool, queue, 48000, 2);
if (!pw) {
    // Error logged internally
    return -1;
}

/* Start capture */
if (audyn_pw_input_start(pw) != 0) {
    audyn_pw_input_destroy(pw);
    return -1;
}

/* Monitor while running */
while (running) {
    audyn_pw_stats_t stats;
    audyn_pw_input_get_stats(pw, &stats);

    if (stats.drops_pool > 0) {
        printf("Warning: %lu pool drops\n", stats.drops_pool);
    }

    /* Process frames from queue... */
    sleep(1);
}

/* Cleanup */
audyn_pw_input_stop(pw);
audyn_pw_input_destroy(pw);
```

## Diagnostics

### Common Issues

| Symptom | Likely Cause | Solution |
|---------|--------------|----------|
| High `drops_pool` | Frame pool too small | Increase pool size |
| High `drops_queue` | Consumer too slow | Optimize worker, increase queue |
| High `truncations` | Frame size too small | Increase frame capacity |
| No `callbacks` | PipeWire not running | Check `systemctl --user status pipewire` |

### Monitoring

```c
audyn_pw_stats_t stats;
audyn_pw_input_get_stats(pw, &stats);

printf("Captured: %lu frames in %lu callbacks\n",
       stats.frames_captured, stats.callbacks);
printf("Drops - pool:%lu queue:%lu empty:%lu\n",
       stats.drops_pool, stats.drops_queue, stats.drops_empty);
printf("Truncations: %lu\n", stats.truncations);
```
