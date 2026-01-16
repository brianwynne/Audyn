# Worker Implementation Review

## Overview

The `worker` module implements the non-real-time consumer thread that bridges Audyn's lock-free audio pipeline to file I/O. It drains audio frames from an SPSC queue, writes them to a WAV sink, and returns frames to the pool.

**Files:**
- `core/worker.h` - Public API header
- `core/worker.c` - Implementation

## Key Design Decisions

### 1. SPSC Consumer Pattern

The worker is the single consumer of the audio queue:
- **Producer (RT thread):** Pushes `audyn_audio_frame_t*` to queue
- **Consumer (worker):** Pops frames, writes to disk, releases to pool

This separation ensures RT threads never block on I/O.

### 2. Cooperative Shutdown

The worker uses a cooperative stop mechanism:
1. Caller sets `running` flag to 0 via `audyn_worker_stop()`
2. Worker thread checks flag in main loop
3. Caller joins thread after signaling

### 3. C11 Atomics for Thread Safety

The worker uses C11 atomics for cross-thread communication:
- `_Atomic int running` - Stop signal
- `_Atomic int started` - Thread lifecycle tracking
- `_Atomic int status` - Error flag
- `_Atomic uint64_t frames_processed` - Statistics
- `_Atomic uint64_t write_errors` - Statistics

### 4. Mutex-Protected Error String

The last error message is protected by a pthread mutex since it's a variable-length string that can't be atomically updated.

### 5. Drain on Stop Option

If `drain_on_stop` is enabled, the worker processes all remaining queued frames before exiting. This ensures no audio data is lost on shutdown.

## Structure Definition

```c
struct audyn_worker {
    audyn_frame_pool_t   *pool;         /* Frame pool for releases */
    audyn_audio_queue_t  *q;            /* Input queue (SPSC consumer) */
    audyn_worker_cfg_t    cfg;          /* Configuration copy */
    char                 *output_path;   /* Owned copy of output path */

    audyn_wav_sink_t     *sink;         /* WAV file writer */

    pthread_t             thread;        /* Worker thread */
    _Atomic int           running;       /* 1 while should run */
    _Atomic int           started;       /* 1 if thread was created */
    _Atomic int           status;        /* 0=ok, nonzero=error */

    pthread_mutex_t       err_mu;        /* Protects last_err */
    char                  last_err[256]; /* Last error message */

    /* Statistics - atomic for thread-safe reads */
    _Atomic uint64_t      frames_processed;
    _Atomic uint64_t      write_errors;
};
```

## Function-by-Function Analysis

### Helper Functions

#### `set_err()` (lines 72-82)
Sets the error flag and message atomically:
1. Store status=1 (atomic)
2. Lock mutex
3. Copy error string
4. Unlock mutex

### Thread Function

#### `worker_main()` (lines 84-161)

The main worker loop.

**Initialization:**
1. Open WAV sink with configured path/rate/channels
2. Log start message

**Main Loop:**
```c
while (atomic_load_explicit(&w->running, memory_order_acquire)) {
    // Pop frame from queue
    // If no frame, sleep idle_sleep_us
    // Write frame to WAV sink
    // Track statistics
    // Release frame to pool
}
```

**Error Handling:**
- If sink open fails: set error, clear running, return
- If write fails: set error, increment write_errors, stop loop

**Drain Phase:**
If `drain_on_stop` is enabled:
1. Pop all remaining frames
2. Write each to sink (track errors but continue)
3. Release frames
4. Log drained count

**Cleanup:**
1. Close WAV sink
2. Log final statistics

### Public API

#### `audyn_worker_create()` (lines 164-236)

Creates a worker instance.

**Validation:**
- Pool and queue must be non-NULL
- Config must be non-NULL
- Output path must be non-empty
- Sample rate: 1 to 384000 Hz
- Channels: 1 to 32

**Initialization:**
1. Allocate structure
2. Copy config
3. Duplicate output path (owned copy)
4. Initialize all atomic variables
5. Initialize error mutex
6. Create WAV sink (not opened yet)
7. Log creation

**Improvement:** Output path is now copied so caller can free their copy.

#### `audyn_worker_start()` (lines 238-261)

Starts the worker thread.

**Process:**
1. Validate worker exists and not already running
2. Set running and started flags
3. Create pthread
4. Log start

#### `audyn_worker_stop()` (lines 263-284)

Stops the worker thread.

**Process:**
1. Check if never started (return immediately)
2. If already stopped but started flag set, join zombie thread
3. Otherwise: clear running flag, join thread, clear started
4. Log stop

**Improvement:** Now properly handles case where thread stopped itself on error.

#### `audyn_worker_destroy()` (lines 286-294)

Cleanup function.

**Process:**
1. Stop worker (joins thread)
2. Destroy WAV sink
3. Destroy mutex
4. Free output path
5. Free structure

#### `audyn_worker_status()` (lines 296-299)

Returns error status: 0=ok, nonzero=error.

#### `audyn_worker_is_running()` (lines 302-306)

Returns 1 if worker thread is currently running, 0 otherwise.

#### `audyn_worker_get_last_error()` (lines 308-324)

**Thread-safe** error message retrieval. Copies message to caller's buffer.

**Process:**
1. Lock mutex
2. Copy error string (or "ok" if none)
3. Unlock mutex
4. Null-terminate

#### `audyn_worker_last_error()` (lines 326-333)

**Deprecated.** Returns pointer to internal storage.

**Warning:** This is not thread-safe if error can change. Use `get_last_error()` instead.

#### `audyn_worker_get_stats()` (lines 335-346)

Retrieves statistics with atomic reads. Safe to call while running.

## Improvements Made

### 1. Comprehensive Logging
- All creation steps log on failure
- Start/stop transitions logged
- Final statistics logged on stop

### 2. Output Path Ownership
- Path is now copied via `strdup()`
- Caller can free their copy after `create()`
- No more dangling pointer risk

### 3. Input Validation
- Sample rate limited to 1-384000 Hz
- Channels limited to 1-32
- Descriptive error messages

### 4. Statistics Tracking
- Added `audyn_worker_stats_t` structure
- Track `frames_processed` and `write_errors`
- `audyn_worker_get_stats()` for runtime monitoring

### 5. Thread-Safe Error Access
- Added `audyn_worker_get_last_error()` that copies to caller buffer
- Deprecated raw pointer version with warning

### 6. Running State Query
- Added `audyn_worker_is_running()` function
- Uses atomic load for thread safety

### 7. Improved Stop Logic
- Track `started` separately from `running`
- Handle case where thread stopped itself on error
- Always join thread if it was started

### 8. Drain Error Tracking
- Drain loop now tracks write errors
- Errors don't stop drain (best effort)
- Drained frame count logged

## Threading Model

```
┌─────────────────┐         ┌─────────────────┐
│   RT Thread     │         │  Worker Thread  │
│  (Producer)     │         │   (Consumer)    │
├─────────────────┤         ├─────────────────┤
│                 │         │                 │
│ frame_acquire() │         │ queue_pop()     │
│      │          │         │      │          │
│      ▼          │         │      ▼          │
│ fill frame      │         │ wav_sink_write()│
│      │          │         │      │          │
│      ▼          │         │      ▼          │
│ queue_push()────┼────────►│ frame_release() │
│                 │         │                 │
└─────────────────┘         └─────────────────┘
```

**Key Properties:**
- SPSC queue: single producer, single consumer
- No shared mutable state except queue and atomics
- RT thread never blocks on I/O
- Worker handles all file operations

## Memory Ordering

The code uses appropriate memory orderings:

- `memory_order_release` - Publishing running flag changes
- `memory_order_acquire` - Reading running flag in loop
- `memory_order_relaxed` - Statistics counters (no ordering needed)

## Dependencies

- **POSIX:** pthread
- **C11:** stdatomic
- **Audyn:** frame_pool, audio_queue, wav_sink, log

## Usage Example

```c
/* Create pool and queue */
audyn_frame_pool_t *pool = audyn_frame_pool_create(1024, 2, 64);
audyn_audio_queue_t *queue = audyn_audio_queue_create(256);

/* Configure worker */
audyn_worker_cfg_t cfg = {
    .output_path = "/tmp/recording.wav",
    .sample_rate = 48000,
    .channels = 2,
    .idle_sleep_us = 1000,
    .drain_on_stop = 1,
    .wav_cfg = {
        .format = AUDYN_WAV_PCM16,
        .enable_fsync = 0
    }
};

/* Create and start worker */
audyn_worker_t *worker = audyn_worker_create(pool, queue, &cfg);
if (!worker) {
    // Error logged internally
    return -1;
}

if (audyn_worker_start(worker) != 0) {
    audyn_worker_destroy(worker);
    return -1;
}

/* Monitor while running */
while (audyn_worker_is_running(worker)) {
    audyn_worker_stats_t stats;
    audyn_worker_get_stats(worker, &stats);
    printf("Frames: %lu, Errors: %lu\n",
           stats.frames_processed, stats.write_errors);
    sleep(1);
}

/* Check for errors */
if (audyn_worker_status(worker) != 0) {
    char err[256];
    audyn_worker_get_last_error(worker, err, sizeof(err));
    printf("Worker error: %s\n", err);
}

/* Cleanup */
audyn_worker_stop(worker);
audyn_worker_destroy(worker);
```

## Error Handling

| Error | Behavior |
|-------|----------|
| Sink open fails | Set error, stop immediately |
| Sink write fails | Set error, stop loop, drain if enabled |
| Thread create fails | Set error, return -1 |

All errors are logged and can be queried via `audyn_worker_status()` and `audyn_worker_get_last_error()`.
