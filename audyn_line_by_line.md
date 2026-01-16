# Audyn Main Executable Review

## Overview

The `audyn.c` file is the main orchestration layer of the Audyn Professional Audio Capture & Archival Engine. It coordinates all components to provide a complete audio capture solution.

**Key Responsibilities:**
- Parse command-line options
- Create and configure core resources (pool, queue, archive policy)
- Manage input sources (AES67 or PipeWire)
- Manage output sinks (WAV or Opus)
- Handle archive file rotation
- Handle graceful shutdown on SIGINT/SIGTERM

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                         audyn (main)                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────┐     ┌──────────────┐     ┌─────────────────┐  │
│  │  AES67 or   │────►│  SPSC Queue  │────►│  Worker Thread  │  │
│  │  PipeWire   │     │  (lock-free) │     │  (file I/O)     │  │
│  │   Input     │     └──────────────┘     └────────┬────────┘  │
│  └─────────────┘                                   │            │
│         │                                          ▼            │
│         │         ┌──────────────┐         ┌─────────────────┐  │
│         └────────►│  Frame Pool  │         │  WAV or Opus    │  │
│                   │  (lock-free) │◄────────│     Sink        │  │
│                   └──────────────┘         └─────────────────┘  │
│                                                    │            │
│                                                    ▼            │
│                                            ┌─────────────────┐  │
│                                            │ Archive Policy  │  │
│                                            │ (file rotation) │  │
│                                            └─────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

## Limits and Constants

```c
/* Sample rate limits (must match worker.h) */
#define AUDYN_MAX_SAMPLE_RATE 384000

/* Opus bitrate limits (must match opus_sink.c) */
#define AUDYN_CLI_BITRATE_MIN 6000
#define AUDYN_CLI_BITRATE_MAX 510000

/* Levels interval limits (ms) */
#define AUDYN_LEVELS_INTERVAL_MIN 10
#define AUDYN_LEVELS_INTERVAL_MAX 5000
```

## Type Definitions

### Input Source Enum (lines 74-77)
```c
typedef enum input_source {
    INPUT_AES67,
    INPUT_PIPEWIRE
} input_source_t;
```

### Output Format Enum (lines 79-82)
```c
typedef enum output_format {
    OUTPUT_WAV,
    OUTPUT_OPUS
} output_format_t;
```

### Worker Context Structure (lines 237-285)

The worker context holds all state for the file writing worker thread:

```c
typedef struct worker_ctx {
    /* Core resources (not owned) */
    audyn_frame_pool_t  *pool;
    audyn_audio_queue_t *queue;

    /* Archive policy (may be NULL for single-file mode) */
    audyn_archive_policy_t *archive;

    /* Archive clock source (valid when archive != NULL) */
    audyn_archive_clock_t archive_clock;

    /* Single-file mode path (NULL if using archive) */
    const char *single_file_path;

    /* Output format */
    output_format_t format;

    /* Audio parameters */
    uint32_t sample_rate;
    uint16_t channels;

    /* Opus parameters */
    uint32_t opus_bitrate;
    int opus_vbr;
    int opus_complexity;

    /* PTP clock for TAI timestamps (may be NULL) */
    audyn_ptp_clock_t *ptp_clk;

    /* Stop flag */
    volatile int *stop_flag;

    /* Worker status */
    int status;
    char error[256];

    /* Current sinks (owned by worker) */
    audyn_wav_sink_t  *wav_sink;
    audyn_opus_sink_t *opus_sink;

    /* Statistics */
    uint64_t files_written;
    uint64_t frames_written;
    uint64_t rotations;

    /* Level metering (optional) */
    audyn_level_meter_t *level_meter;
} worker_ctx_t;
```

## Function-by-Function Analysis

### Signal Handling

#### `on_signal()` (lines 88-92)
Simple async-signal-safe handler that sets the global stop flag.

#### `install_signal_handlers()` (lines 94-104)
Installs SIGINT and SIGTERM handlers using `sigaction()`.

### Command-Line Interface

#### `usage()` (lines 108-179)
Prints comprehensive help text with all options and examples.

### Parse Helpers

#### `parse_u32()` (lines 183-192)
Safely parses a string to uint32_t with overflow checking.

#### `parse_u16()` (lines 194-203)
Safely parses a string to uint16_t with overflow checking.

#### `parse_u8()` (lines 205-214)
Safely parses a string to uint8_t with overflow checking.

#### `detect_output_format()` (lines 216-223)
Returns OUTPUT_OPUS if suffix is "opus", OUTPUT_WAV otherwise.

#### `get_suffix_from_path()` (lines 225-233)
Extracts file extension from a path, defaults to "wav".

### Sink Management

#### `open_wav_sink()` (lines 289-311)
Creates and opens a WAV sink with PCM16 format.

**Process:**
1. Initialize wav_sink_cfg with PCM16 format
2. Create sink
3. Open with path, sample rate, channels
4. Log on success

#### `open_opus_sink()` (lines 313-333)
Creates an Opus sink with configured parameters.

**Process:**
1. Initialize opus_cfg with sample rate, channels, bitrate, VBR, complexity
2. Set application to AUDYN_OPUS_APP_AUDIO
3. Create sink with path and config
4. Log on success

#### `close_current_sink()` (lines 335-351)
Closes and destroys the current sink (WAV or Opus).

**Process:**
1. If WAV sink: close, destroy, increment files_written
2. If Opus sink: flush, close, destroy, increment files_written

#### `open_sink()` (lines 353-360)
Dispatcher that calls `open_wav_sink()` or `open_opus_sink()` based on format.

#### `write_to_sink()` (lines 362-380)
Writes a frame to the appropriate sink and tracks frames_written.

### Time and Rotation

#### `get_current_time_ns()` (lines 384-399)
Gets current time in nanoseconds for archive rotation.

**Clock Sources:**
- If archive mode: uses configured clock (localtime, UTC, or PTP)
- Otherwise: uses CLOCK_REALTIME

#### `maybe_rotate()` (lines 401-436)
Checks if file rotation is needed and performs it.

**Process:**
1. Skip if not in archive mode
2. Get current time
3. Check if rotation is due (via archive policy)
4. If yes: close current sink, generate new path, open new file
5. Advance archive policy state

### Worker Thread

#### `worker_main()` (lines 438-545)
Main worker thread function that processes audio frames.

**Initialization (lines 442-461):**
1. Archive mode: call `maybe_rotate()` to open first file
2. Single-file mode: open specified file
3. Neither: error

**Main Loop (lines 463-526):**
```c
while (!*ctx->stop_flag) {
    /* Check for rotation */
    if (ctx->archive && maybe_rotate(ctx) != 0) break;

    /* Pop frame from queue */
    frame = audyn_audio_queue_pop(ctx->queue);

    if (!frame) {
        usleep(1000);
        silence_counter++;

        /* Generate silence after 50ms of no data */
        if (silence_counter >= 50) {
            /* Acquire frame, fill with zeros, write */
        }
        continue;
    }

    /* Process through level meter */
    if (ctx->level_meter) {
        audyn_level_meter_process(ctx->level_meter, frame);
    }

    /* Write to sink */
    write_to_sink(ctx, frame);
    audyn_frame_release(frame);
}
```

**Drain Phase (lines 528-534):**
Processes remaining frames in queue before exiting.

**Cleanup (lines 536-543):**
Closes final file and logs statistics.

### Main Function

#### `main()` (lines 549-1016)

**Default Values (lines 551-595):**
- Sample rate: 48000 Hz
- Channels: 2
- AES67 port: 5004
- Opus bitrate: 128000 bps
- Queue capacity: 1024
- Pool frames: 256
- Frame size: 1024 samples

**Argument Parsing (lines 597-673):**
Handles all CLI options with proper error checking.

**Validation (lines 675-757):**
1. Must have `-o` or `--archive-root` (but not both)
2. AES67 requires source IP
3. Buffer sizes validated
4. **Sample rate: 1-384000 Hz**
5. **Opus bitrate: 6000-510000 bps**
6. **Levels interval: 10-5000 ms**
7. PTP options only with AES67
8. Archive layout and clock validated

**Initialization (lines 767-810):**
1. Init logging
2. Install signal handlers
3. Log startup configuration
4. Create frame pool
5. Create audio queue
6. Declare all pointers before goto targets (fixes compiler warnings)

**Resource Creation (lines 812-895):**
1. Create archive policy (if archive mode)
2. Create PTP clock (if configured)
3. Create level meter (if enabled)
4. Initialize worker context

**Thread Creation (lines 896-901):**
Create and start worker thread.

**Input Creation (lines 903-942):**
Create and start AES67 or PipeWire input.

**Main Loop (lines 944-960):**
```c
while (!g_stop) {
    usleep(50000);  /* 50ms */

    /* Process level meter */
    if (level_meter) {
        audyn_level_meter_process(level_meter, NULL);
    }

    /* Check worker status */
    if (worker_ctx.status != 0) {
        LOG_ERROR("Worker error: %s", worker_ctx.error);
        break;
    }
}
```

**Shutdown (lines 962-1015):**
1. Log "Stopping..."
2. Stop and destroy input (log PipeWire stats)
3. Set g_stop = 1
4. Join worker thread
5. Destroy PTP clock
6. Destroy level meter (flush first)
7. Destroy archive policy
8. Destroy queue and pool
9. Shutdown logging
10. Return 0 if clean exit, 1 otherwise

## Improvements Made

### 1. Input Validation
- Added sample rate validation (1-384000 Hz)
- Added Opus bitrate validation (6000-510000 bps)
- Added levels interval validation (10-5000 ms)

### 2. Bug Fix: Archive Clock Source
- Fixed `get_current_time_ns()` to use configured clock source
- Previously hardcoded to AUDYN_ARCHIVE_CLOCK_LOCALTIME
- Now properly respects --archive-clock option

### 3. Debug Logging
- Added buffer configuration logging at DEBUG level
- Added Opus configuration logging at DEBUG level

### 4. Statistics on Shutdown
- PipeWire statistics logged on shutdown
- Shows: frames captured, callbacks, drops (pool/queue/empty), truncations

### 5. Compiler Warning Fixes
- Moved variable declarations before first goto target
- Fixes "may be used uninitialized" warnings

## Operation Modes

### Single-File Mode
```bash
audyn -o recording.wav -m 239.69.1.1 -p 5004
audyn -o recording.opus --pipewire --bitrate 96000
```
- One output file, no rotation
- Runs until Ctrl+C

### Archive Mode
```bash
audyn --archive-root /var/lib/audyn --archive-layout flat \
      --archive-suffix opus --archive-period 3600 -m 239.69.1.1
```
- Automatic file rotation
- Time-based naming
- Directory creation

## Silence Generation

When no audio data arrives for 50ms, the worker generates silence frames:
1. Prevents gaps in recording
2. Keeps level meter updated
3. Ensures Opus encoder stays primed

This is important for maintaining continuous recording even during input dropouts.

## Error Handling

| Condition | Behavior |
|-----------|----------|
| Pool create fails | Exit with error |
| Queue create fails | Cleanup pool, exit |
| Archive policy fails | Goto cleanup |
| PTP clock fails | Goto cleanup |
| Level meter fails | Goto cleanup |
| Worker thread fails | Goto cleanup |
| Input create fails | Goto cleanup |
| Input start fails | Goto cleanup |
| Worker runtime error | Break main loop, cleanup |

All errors are logged and the cleanup section ensures proper resource deallocation.

## Threading Model

```
┌────────────────────┐
│    Main Thread     │
│  (CLI, monitoring) │
└─────────┬──────────┘
          │
    ┌─────┴─────┐
    │           │
    ▼           ▼
┌────────┐  ┌────────────┐
│ Worker │  │   Input    │
│ Thread │  │ (AES/PW)   │
└────────┘  └────────────┘
```

- **Main thread:** Parses args, creates resources, monitors worker
- **Worker thread:** Drains queue, writes to files
- **Input threads:** PipeWire loop or AES67 receive (managed by input modules)

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Clean exit (SIGINT/SIGTERM, no errors) |
| 1 | Error during operation |
| 2 | Invalid arguments |

## Dependencies

- **POSIX:** pthread, signal, unistd
- **Audyn Core:** frame_pool, audio_queue, ptp_clock, archive_policy, log
- **Audyn Input:** aes_input, pipewire_input
- **Audyn Sink:** wav_sink, opus_sink, level_meter

## Usage Examples

### Basic AES67 Recording
```bash
# Record to WAV
audyn -o recording.wav -m 239.69.1.1 -p 5004

# Record to Opus
audyn -o recording.opus -m 239.69.1.1 --bitrate 128000
```

### PipeWire Recording
```bash
# Record local audio
audyn -o recording.wav --pipewire

# With level metering
audyn -o recording.opus --pipewire --levels
```

### Archive Mode
```bash
# Hourly rotation, flat layout
audyn --archive-root /var/lib/audyn --archive-layout flat \
      --archive-period 3600 -m 239.69.1.1

# Daily directories, UTC time
audyn --archive-root /mnt/archive --archive-layout dailydir \
      --archive-clock utc --archive-period 3600 -m 239.69.1.1

# Opus archive with 96kbps
audyn --archive-root /archive --archive-suffix opus \
      --bitrate 96000 -m 239.69.1.1
```

### Debug Mode
```bash
# Verbose logging
audyn -o recording.wav -m 239.69.1.1 -v

# Quiet (errors only)
audyn -o recording.wav -m 239.69.1.1 -q

# Syslog output
audyn -o recording.wav -m 239.69.1.1 --syslog
```

### Tuning
```bash
# Larger buffers for high-load systems
audyn -o recording.wav -m 239.69.1.1 -Q 2048 -P 512 -F 2048
```
