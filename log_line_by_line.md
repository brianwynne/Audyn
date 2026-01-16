# Log Implementation Review

## Overview

The `log` module provides a simple logging interface with optional syslog support for the Audyn audio capture engine. It supports multiple log levels, timestamped stderr output, and statistics tracking.

**Files:**
- `core/log.h` - Public API header
- `core/log.c` - Implementation

## Key Features

- **Multiple Log Levels:** DEBUG, INFO, WARN, ERROR
- **Optional Syslog Support:** Linux syslog integration
- **Timestamped Output:** ISO-8601 formatted timestamps
- **Statistics Tracking:** Message counts per level
- **Runtime Level Control:** Change log level without restart
- **Convenience Macros:** LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR

## Log Levels

| Level | Value | Syslog Priority | Description |
|-------|-------|-----------------|-------------|
| DEBUG | 0 | LOG_DEBUG | Verbose debugging information |
| INFO | 1 | LOG_INFO | General operational messages |
| WARN | 2 | LOG_WARNING | Warning conditions |
| ERROR | 3 | LOG_ERR | Error conditions |

Messages are only output if their level >= the configured minimum level.

## Structure Definitions

### Statistics Structure
```c
typedef struct audyn_log_stats {
    uint64_t debug_count;   /* DEBUG messages logged */
    uint64_t info_count;    /* INFO messages logged */
    uint64_t warn_count;    /* WARN messages logged */
    uint64_t error_count;   /* ERROR messages logged */
    uint64_t total_count;   /* Total messages logged */
} audyn_log_stats_t;
```

### Global State
```c
static audyn_log_level_t g_level = AUDYN_LOG_INFO;  /* Current minimum level */
static int g_use_syslog = 0;                         /* Syslog enabled flag */
static int g_initialized = 0;                        /* Initialization flag */

/* Statistics counters */
static uint64_t g_debug_count = 0;
static uint64_t g_info_count = 0;
static uint64_t g_warn_count = 0;
static uint64_t g_error_count = 0;
```

## Function-by-Function Analysis

### `audyn_log_init()` (lines 43-60)

Initializes the logging subsystem.

```c
void audyn_log_init(audyn_log_level_t level, int use_syslog)
{
    g_level = level;
    g_use_syslog = use_syslog;
    g_initialized = 1;

    /* Reset statistics */
    g_debug_count = 0;
    g_info_count = 0;
    g_warn_count = 0;
    g_error_count = 0;

#ifdef __linux__
    if (use_syslog) {
        openlog("audyn", LOG_PID | LOG_NDELAY, LOG_USER);
    }
#endif
}
```

**Parameters:**
- `level` - Minimum log level to output
- `use_syslog` - Non-zero to enable syslog (Linux only)

**Syslog Flags:**
- `LOG_PID` - Include PID in messages
- `LOG_NDELAY` - Open connection immediately
- `LOG_USER` - User-level facility

### `audyn_log_shutdown()` (lines 62-70)

Shuts down the logging subsystem.

```c
void audyn_log_shutdown(void)
{
#ifdef __linux__
    if (g_use_syslog) {
        closelog();
    }
#endif
    g_initialized = 0;
}
```

### `audyn_log_is_initialized()` (lines 72-75)

Checks if logging is initialized.

```c
int audyn_log_is_initialized(void)
{
    return g_initialized;
}
```

**Returns:** Non-zero if initialized, 0 otherwise.

### `audyn_log_get_level()` (lines 77-80)

Gets the current minimum log level.

```c
audyn_log_level_t audyn_log_get_level(void)
{
    return g_level;
}
```

### `audyn_log_set_level()` (lines 82-85)

Sets the log level at runtime.

```c
void audyn_log_set_level(audyn_log_level_t level)
{
    g_level = level;
}
```

**Use Case:** Increase verbosity for debugging without restart.

### `audyn_log_write()` (lines 87-145)

Core logging function.

**Process:**
1. Check if level meets minimum threshold
2. Update statistics counter for this level
3. Format and output message

**Syslog Path (Linux):**
```c
if (g_use_syslog) {
    int priority;
    switch (level) {
        case AUDYN_LOG_DEBUG: priority = LOG_DEBUG;   break;
        case AUDYN_LOG_INFO:  priority = LOG_INFO;    break;
        case AUDYN_LOG_WARN:  priority = LOG_WARNING; break;
        case AUDYN_LOG_ERROR: priority = LOG_ERR;     break;
        default:              priority = LOG_INFO;    break;
    }
    vsyslog(priority, fmt, args);
}
```

**Stderr Path:**
```c
{
    const char *level_str;
    switch (level) {
        case AUDYN_LOG_DEBUG: level_str = "DEBUG"; break;
        case AUDYN_LOG_INFO:  level_str = "INFO";  break;
        case AUDYN_LOG_WARN:  level_str = "WARN";  break;
        case AUDYN_LOG_ERROR: level_str = "ERROR"; break;
        default:              level_str = "???";   break;
    }

    time_t now = time(NULL);
    struct tm tm_buf;
    struct tm *tm_info = localtime_r(&now, &tm_buf);
    char time_buf[24];
    strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(stderr, "[%s] [%-5s] ", time_buf, level_str);
    vfprintf(stderr, fmt, args);
    fprintf(stderr, "\n");
    fflush(stderr);
}
```

**Output Format:**
```
[2026-01-16 14:23:45] [INFO ] Message text here
[2026-01-16 14:23:46] [ERROR] Something went wrong
[2026-01-16 14:23:47] [DEBUG] Verbose debug info
```

### `audyn_log_get_stats()` (lines 147-156)

Gets logging statistics.

```c
void audyn_log_get_stats(audyn_log_stats_t *stats)
{
    if (!stats) return;

    stats->debug_count = g_debug_count;
    stats->info_count = g_info_count;
    stats->warn_count = g_warn_count;
    stats->error_count = g_error_count;
    stats->total_count = g_debug_count + g_info_count + g_warn_count + g_error_count;
}
```

### `audyn_log_level_from_string()` (lines 158-173)

Parses a log level name to enum value.

**Accepts:**
- "debug"
- "info"
- "warn", "warning"
- "error", "err"

**Returns:** Enum value or -1 if invalid.

### `audyn_log_level_to_string()` (lines 175-184)

Converts log level enum to string name.

**Returns:** Static string ("debug", "info", "warn", "error") or "unknown".

## Convenience Macros

```c
#define LOG_DEBUG(...) audyn_log_write(AUDYN_LOG_DEBUG, __VA_ARGS__)
#define LOG_INFO(...)  audyn_log_write(AUDYN_LOG_INFO,  __VA_ARGS__)
#define LOG_WARN(...)  audyn_log_write(AUDYN_LOG_WARN,  __VA_ARGS__)
#define LOG_ERROR(...) audyn_log_write(AUDYN_LOG_ERROR, __VA_ARGS__)
```

**Usage:**
```c
LOG_DEBUG("Processing frame %d with %u samples", frame_num, sample_count);
LOG_INFO("Capture started on %s", device_name);
LOG_WARN("Buffer underrun detected");
LOG_ERROR("Failed to open file: %s", strerror(errno));
```

## Improvements Made

### 1. Added WARN Level
- New `AUDYN_LOG_WARN` level between INFO and ERROR
- Maps to `LOG_WARNING` in syslog
- Added `LOG_WARN()` macro

### 2. Statistics Tracking
- Added `audyn_log_stats_t` structure
- Track counts per level (debug, info, warn, error)
- Added `audyn_log_get_stats()` function
- Statistics reset on init()

### 3. Runtime Level Control
- Added `audyn_log_get_level()` to query current level
- Added `audyn_log_set_level()` to change level at runtime

### 4. Initialization Check
- Added `g_initialized` flag
- Added `audyn_log_is_initialized()` function

### 5. String Conversion Functions
- Added `audyn_log_level_from_string()` for parsing command-line args
- Added `audyn_log_level_to_string()` for display/serialization

### 6. Output Formatting
- Fixed-width level field (`[%-5s]`) for aligned output
- Increased time buffer size for safety

## Usage Example

```c
/* Initialize logging */
audyn_log_init(AUDYN_LOG_INFO, 0);  /* INFO level, stderr only */

/* Or with syslog */
audyn_log_init(AUDYN_LOG_DEBUG, 1);  /* DEBUG level, syslog enabled */

/* Log messages */
LOG_INFO("Audyn version %s starting", VERSION);
LOG_DEBUG("Pool created with %u frames", frame_count);
LOG_WARN("High CPU usage detected: %.1f%%", cpu_percent);
LOG_ERROR("Failed to bind socket: %s", strerror(errno));

/* Check current level */
if (audyn_log_get_level() == AUDYN_LOG_DEBUG) {
    /* Extra verbose output */
}

/* Change level at runtime */
audyn_log_set_level(AUDYN_LOG_ERROR);  /* Quiet mode */

/* Get statistics */
audyn_log_stats_t stats;
audyn_log_get_stats(&stats);
printf("Logged: %lu debug, %lu info, %lu warn, %lu error (total: %lu)\n",
       stats.debug_count, stats.info_count, stats.warn_count,
       stats.error_count, stats.total_count);

/* Parse level from command line */
const char *level_arg = "--log-level=debug";
int level = audyn_log_level_from_string("debug");
if (level >= 0) {
    audyn_log_set_level((audyn_log_level_t)level);
}

/* Shutdown */
audyn_log_shutdown();
```

## Integration with Audyn

Logging is used throughout Audyn for operational visibility:

```bash
# Run with debug logging
audyn -o output.wav --pipewire --log-level debug

# Run with errors only (quiet)
audyn -o output.wav --pipewire --log-level error

# Log to syslog for daemon mode
audyn -o output.wav --pipewire --syslog
```

## Output Examples

### Stderr Output
```
[2026-01-16 14:00:00] [INFO ] audyn: starting capture
[2026-01-16 14:00:00] [DEBUG] frame_pool: created (frames=256 channels=2 samples=1024)
[2026-01-16 14:00:00] [DEBUG] audio_queue: created (capacity=1024)
[2026-01-16 14:00:00] [INFO ] pipewire: connected to stream
[2026-01-16 14:00:01] [WARN ] worker: buffer underrun, inserted silence
[2026-01-16 14:00:05] [INFO ] wav_sink: rotated to 2026-01-16-14.wav
[2026-01-16 14:01:00] [ERROR] aes_input: socket bind failed: Address already in use
```

### Syslog Output (via journalctl)
```
Jan 16 14:00:00 hostname audyn[12345]: audyn: starting capture
Jan 16 14:00:00 hostname audyn[12345]: pipewire: connected to stream
Jan 16 14:00:01 hostname audyn[12345]: worker: buffer underrun, inserted silence
```

## Threading Considerations

The log module uses global state without mutex protection. This is acceptable because:

1. **Init/shutdown** - Called once from main thread before/after worker threads
2. **Level changes** - Single atomic write, race is benign
3. **Statistics** - Counters may have minor inconsistency during reads, acceptable for monitoring
4. **Write operations** - `fprintf` and `vsyslog` are thread-safe

For strict thread safety, external synchronization would be needed, but the current design prioritizes simplicity and performance for this use case.

## Dependencies

- **C Standard Library:** stdio, stdlib, stdarg, string, time
- **POSIX:** strings.h (strcasecmp)
- **Linux:** syslog.h (optional, for syslog support)
