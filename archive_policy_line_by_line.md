# Archive Policy Implementation Review

## Overview

The `archive_policy` module implements Rotter-compatible file rotation and naming for the Audyn audio capture engine. It handles time-based file chunking with wall-clock aligned boundaries and multiple naming layouts.

**Files:**
- `core/archive_policy.h` - Public API header
- `core/archive_policy.c` - Implementation

## Rotter Compatibility

This module replicates Rotter's exact behaviour for:
- Rotation period (default 3600 seconds = 1 hour)
- Wall-clock aligned boundaries (e.g., hourly = on the hour)
- All naming layouts: flat, hierarchy, combo, dailydir, accurate, custom
- Zero-padded, locale-independent formatting

## Constants

```c
/* Rotation period limits */
#define AUDYN_ARCHIVE_MIN_ROTATION_SEC 10      /* Minimum (prevent excessive files) */
#define AUDYN_ARCHIVE_MAX_ROTATION_SEC 31536000 /* Maximum (1 year) */

/* Internal constants */
#define NS_PER_SEC 1000000000ULL
#define DEFAULT_ROTATION_PERIOD_SEC 3600
#define MAX_PATH_LEN 4096
#define TAI_UTC_OFFSET_SEC 37
```

## Layout Types

| Layout | Path Format | Description |
|--------|-------------|-------------|
| FLAT | `/root/YYYY-MM-DD-HH.<suffix>` | Simple flat files |
| HIERARCHY | `/root/YYYY/MM/DD/HH/archive.<suffix>` | Deep directory tree |
| COMBO | `/root/YYYY/MM/DD/HH/YYYY-MM-DD-HH.<suffix>` | Hierarchy + dated filename |
| DAILYDIR | `/root/YYYY-MM-DD/YYYY-MM-DD-HH.<suffix>` | Daily directories |
| ACCURATE | `/root/YYYY-MM-DD/YYYY-MM-DD-HH-mm-ss-cc.<suffix>` | Centisecond precision |
| CUSTOM | User-supplied strftime format | Flexible custom naming |

## Clock Sources

| Clock | Description |
|-------|-------------|
| LOCALTIME | System local time (default, matches Rotter) |
| UTC | Coordinated Universal Time |
| PTP_TAI | PTP/TAI time from capture timestamps (for broadcast) |

## Structure Definitions

### Configuration Structure
```c
typedef struct audyn_archive_cfg {
    const char *root_dir;           /* Root directory for archives (required) */
    const char *suffix;             /* File extension without dot (required) */
    audyn_archive_layout_t layout;  /* Naming layout */
    const char *custom_format;      /* strftime format (for CUSTOM layout) */
    uint32_t rotation_period_sec;   /* Rotation period (0 = no rotation) */
    audyn_archive_clock_t clock_source;
    int create_directories;         /* Create dirs recursively */
} audyn_archive_cfg_t;
```

### Statistics Structure
```c
typedef struct audyn_archive_stats {
    uint64_t rotations;           /* Number of file rotations */
    uint64_t paths_generated;     /* Total paths generated */
    uint64_t directories_created; /* Directories created via mkdir */
} audyn_archive_stats_t;
```

### Internal Structure
```c
struct audyn_archive_policy {
    /* Configuration (owned copies) */
    char *root_dir;
    char *suffix;
    char *custom_format;
    audyn_archive_layout_t layout;
    audyn_archive_clock_t clock_source;
    uint32_t rotation_period_sec;
    int create_directories;

    /* Runtime state */
    int initialized;              /* 1 after first file opened */
    uint64_t current_period_ns;   /* Start of current period (ns since epoch) */
    uint64_t next_boundary_ns;    /* Next rotation boundary (ns since epoch) */
    struct tm current_tm;         /* Broken-down time for current period */
    uint32_t current_centisec;    /* Centiseconds for accurate layout */

    /* Statistics */
    uint64_t rotations;
    uint64_t paths_generated;
    uint64_t directories_created;
};
```

## Function-by-Function Analysis

### Helper Functions

#### `safe_strdup()` (lines 96-105)
Duplicates a string, handling NULL input.
```c
static char *safe_strdup(const char *s)
{
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}
```

#### `mkdir_recursive()` (lines 112-173)
Creates directories recursively (like `mkdir -p`).

**Process:**
1. Check if path already exists as directory
2. Make mutable copy of path
3. Iterate through path components
4. Create each directory if it doesn't exist
5. Handle EEXIST (concurrent creation)

**Error Handling:**
- Returns -1 if path exists but is not a directory (ENOTDIR)
- Returns -1 on mkdir failure (preserves errno)

#### `get_directory()` (lines 180-202)
Extracts directory portion from a path.
- Returns "." if no directory component
- Returns "/" for root paths
- Returns newly allocated string

#### `ns_to_tm()` (lines 215-256)
Converts nanoseconds since epoch to `struct tm`.

**Clock Handling:**
- LOCALTIME: Uses `localtime_r()` for local timezone
- UTC: Uses `gmtime_r()` for UTC
- PTP_TAI: Uses `gmtime_r()` (TAI treated as UTC for struct tm)

**Centiseconds:**
```c
*out_csec = (uint32_t)(nsec_part / 10000000ULL);  /* ns / 10^7 = centisec */
```

#### `tm_to_ns()` (lines 267-292)
Converts `struct tm` back to nanoseconds since epoch.

**Clock Handling:**
- LOCALTIME: Uses `mktime()` (interprets as local time)
- UTC/PTP_TAI: Uses `timegm()` (GNU extension, interprets as UTC)

#### `calculate_period_boundary()` (lines 316-387)
Calculates wall-clock aligned rotation boundaries.

**Wall-Clock Alignment:**
For a 3600-second (1 hour) period at 14:23:45:
- Period starts at 14:00:00
- Period ends at 15:00:00

**Algorithm:**
1. Convert time to struct tm
2. Calculate seconds since midnight
3. Find period index: `period_index = sec_since_midnight / period_sec`
4. Calculate period start: `period_start_sec = period_index * period_sec`
5. Build struct tm for period start
6. Convert back to nanoseconds

**Special Case:**
When `period_sec == 0` (no rotation), returns `UINT64_MAX` as next boundary.

### Path Generation Functions

#### `generate_path_flat()` (lines 392-408)
Generates: `/root/YYYY-MM-DD-HH.<suffix>`

#### `generate_path_hierarchy()` (lines 413-429)
Generates: `/root/YYYY/MM/DD/HH/archive.<suffix>`

#### `generate_path_combo()` (lines 434-454)
Generates: `/root/YYYY/MM/DD/HH/YYYY-MM-DD-HH.<suffix>`

#### `generate_path_dailydir()` (lines 459-478)
Generates: `/root/YYYY-MM-DD/YYYY-MM-DD-HH.<suffix>`

#### `generate_path_accurate()` (lines 483-506)
Generates: `/root/YYYY-MM-DD/YYYY-MM-DD-HH-mm-ss-cc.<suffix>`

Note: Uses actual time (not period boundary) for precise timestamps.

#### `generate_path_custom()` (lines 511-535)
Uses strftime with user-supplied format string.

### Public API

#### `audyn_archive_policy_create()` (lines 539-637)

Creates an archive policy instance.

**Validation:**
- NULL config check
- root_dir required (non-NULL, non-empty)
- suffix required (non-NULL, non-empty)
- custom_format required when layout == CUSTOM
- layout enum bounds check (FLAT to CUSTOM)
- clock_source enum bounds check (LOCALTIME to PTP_TAI)
- rotation_period_sec bounds (10s to 31536000s, or 0 for disabled)

**Initialization:**
1. Allocate structure (calloc zeros statistics)
2. Copy configuration strings via safe_strdup()
3. Set rotation period
4. Log creation with configuration

#### `audyn_archive_policy_destroy()` (lines 639-652)

Destroys an archive policy instance.

**Process:**
1. Log final statistics
2. Free owned strings (root_dir, suffix, custom_format)
3. Free structure

#### `audyn_archive_policy_should_rotate()` (lines 654-670)

Checks if file rotation should occur.

**Returns 1 if:**
- First call (not initialized) - always need initial file
- Current time >= next rotation boundary

**Returns 0 if:**
- NULL policy
- Rotation disabled (period == 0) after first file
- Not yet reached boundary

#### `audyn_archive_policy_next_path()` (lines 672-780)

Generates the next archive file path.

**Process:**
1. Calculate period boundary and get struct tm
2. For ACCURATE layout, use actual time (not boundary)
3. Generate path based on layout type
4. Create directories if configured:
   - Check if directory exists first
   - Call mkdir_recursive()
   - Increment directories_created if new
5. Increment paths_generated
6. Store period info for advance()
7. Log generated path

**Returns:** 0 on success, -1 on failure

#### `audyn_archive_policy_advance()` (lines 782-797)

Advances to the next rotation period.

**Must be called after:**
- Successfully opening a new file
- Updates initialized flag
- Increments rotations counter
- Logs advance with rotation number

#### `audyn_archive_policy_next_boundary_ns()` (lines 799-805)

Gets the next rotation boundary time in nanoseconds.

**Returns:** 0 if rotation disabled, otherwise next boundary timestamp.

#### `audyn_archive_policy_current_time()` (lines 807-821)

Gets the current period start time as struct tm.

**Returns:** -1 if not initialized, 0 on success.

#### `audyn_archive_layout_from_string()` (lines 823-842)

Parses layout name to enum value.

**Accepts:** "flat", "hierarchy", "combo", "dailydir", "accurate", "custom"
**Returns:** Enum value or -1 if invalid.

#### `audyn_archive_layout_to_string()` (lines 844-855)

Converts layout enum to string name.

**Returns:** Static string or "unknown".

#### `audyn_archive_clock_from_string()` (lines 857-871)

Parses clock source name to enum value.

**Accepts:** "localtime", "local", "utc", "ptp", "tai", "ptp_tai"
**Returns:** Enum value or -1 if invalid.

#### `audyn_archive_clock_to_string()` (lines 873-881)

Converts clock enum to string name.

**Returns:** Static string or "unknown".

#### `audyn_archive_policy_get_stats()` (lines 883-897)

Gets policy statistics.

**Returns:**
- `rotations`: Number of file rotations completed
- `paths_generated`: Total paths generated
- `directories_created`: New directories created

#### `audyn_archive_get_time_ns()` (lines 899-920)

Utility to get current time in nanoseconds for a clock source.

**For LOCALTIME/UTC:** Uses CLOCK_REALTIME
**For PTP_TAI:** Returns caller-provided timestamp directly

## Improvements Made

### 1. Statistics Tracking
- Added `audyn_archive_stats_t` structure
- Track rotations, paths_generated, directories_created
- Added `audyn_archive_policy_get_stats()` function

### 2. Input Validation
- Added `AUDYN_ARCHIVE_MIN_ROTATION_SEC` (10 seconds minimum)
- Added `AUDYN_ARCHIVE_MAX_ROTATION_SEC` (1 year maximum)
- Validate layout enum bounds
- Validate clock_source enum bounds
- All validation failures logged with descriptive errors

### 3. Destroy Logging
- Added statistics logging on destroy (DEBUG level)
- Shows rotations, paths generated, directories created

### 4. Enhanced Advance Logging
- Includes rotation count in log message

## Wall-Clock Alignment

Wall-clock alignment ensures files are named for predictable times:

| Period | Current Time | Period Start | Period End |
|--------|--------------|--------------|------------|
| 3600s (1h) | 14:23:45 | 14:00:00 | 15:00:00 |
| 1800s (30m) | 14:23:45 | 14:00:00 | 14:30:00 |
| 900s (15m) | 14:23:45 | 14:15:00 | 14:30:00 |
| 600s (10m) | 14:23:45 | 14:20:00 | 14:30:00 |

Files are named for the period **start** time, not the current time (except ACCURATE layout).

## Usage Example

```c
/* Configure archive policy */
audyn_archive_cfg_t cfg = {
    .root_dir = "/var/archive/radio",
    .suffix = "opus",
    .layout = AUDYN_ARCHIVE_LAYOUT_DAILYDIR,
    .rotation_period_sec = 3600,  /* 1 hour */
    .clock_source = AUDYN_ARCHIVE_CLOCK_LOCALTIME,
    .create_directories = 1
};

/* Create policy */
audyn_archive_policy_t *policy = audyn_archive_policy_create(&cfg);
if (!policy) {
    return -1;
}

/* Main recording loop */
while (running) {
    uint64_t now_ns = audyn_archive_get_time_ns(
        AUDYN_ARCHIVE_CLOCK_LOCALTIME, 0);

    if (audyn_archive_policy_should_rotate(policy, now_ns)) {
        /* Close current file if open */
        if (current_file) {
            close_file(current_file);
        }

        /* Generate new path */
        char path[512];
        if (audyn_archive_policy_next_path(policy, now_ns, path, sizeof(path)) == 0) {
            current_file = open_file(path);
            audyn_archive_policy_advance(policy);
            printf("Rotated to: %s\n", path);
        }
    }

    /* Write audio data... */
}

/* Get statistics */
audyn_archive_stats_t stats;
audyn_archive_policy_get_stats(policy, &stats);
printf("Archive stats: %lu rotations, %lu paths, %lu dirs created\n",
       stats.rotations, stats.paths_generated, stats.directories_created);

/* Cleanup */
audyn_archive_policy_destroy(policy);
```

## Integration with Audyn

The archive policy is used by sinks (wav_sink, opus_sink) for file rotation:

```bash
# Hourly WAV files with daily directories
audyn -o /archive/wav --pipewire --rotate 3600 --layout dailydir

# 15-minute Opus files with hierarchy layout
audyn -o /archive/opus --aes -m 239.69.1.1 --rotate 900 --layout hierarchy

# Accurate timestamps for forensic archival
audyn -o /archive/accurate --pipewire --rotate 3600 --layout accurate
```

## Custom Layout Examples

Using strftime format specifiers:

```c
/* ISO week-based: /root/2024-W23/2024-W23-Mon */
cfg.layout = AUDYN_ARCHIVE_LAYOUT_CUSTOM;
cfg.custom_format = "%G-W%V/%G-W%V-%a";

/* Station-style: /root/2024/Jun/15/1400 */
cfg.custom_format = "%Y/%b/%d/%H%M";
```

## Threading Model

```
┌─────────────────┐
│  Worker Thread  │
│  (owns policy)  │
└────────┬────────┘
         │
         │ should_rotate()
         │ next_path()
         │ advance()
         ▼
┌─────────────────┐
│ Archive Policy  │
│  (NOT thread-   │
│   safe)         │
└─────────────────┘
```

Single-threaded use only. The worker thread owns the archive policy instance.

## Dependencies

- **C Standard Library:** stdio, stdlib, string, time, errno
- **POSIX:** mkdir, stat, sys/stat.h
- **GNU Extension:** timegm (for UTC/TAI conversion)
- **Audyn:** log
