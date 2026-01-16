# Level Meter Implementation Review

## Overview

The `level_meter` module provides real-time audio level metering with RMS and peak detection. It outputs JSON to stdout for integration with web backends and monitoring tools.

**Files:**
- `core/level_meter.h` - Public API header
- `core/level_meter.c` - Implementation

## Key Features

- **RMS Level Calculation:** Root Mean Square for average loudness
- **Peak Detection:** Maximum absolute sample value
- **Peak Hold:** Peak decays over 1.5 seconds
- **Clipping Detection:** Flags samples >= 0.99
- **JSON Output:** Formatted for web backend consumption
- **Configurable Interval:** Default 33ms (~30fps)

## Constants

```c
/* Maximum supported channels */
#define AUDYN_METER_MAX_CHANNELS 2

/* Sample rate limits */
#define AUDYN_METER_MAX_SAMPLE_RATE 384000

/* Internal constants */
#define MIN_DB -60.0f           /* Silence threshold */
#define PEAK_HOLD_TIME 1.5f     /* Peak hold decay time (seconds) */
#define CLIP_THRESHOLD 0.99f    /* Clipping detection threshold */
```

## Structure Definitions

### Channel Level Data
```c
typedef struct audyn_channel_level {
    float rms_linear;       /* RMS level (0.0 to 1.0) */
    float rms_db;           /* RMS level in dB (typically -60 to 0) */
    float peak_linear;      /* Peak level (0.0 to 1.0) */
    float peak_db;          /* Peak level in dB */
    int clipping;           /* Non-zero if clipping detected */
} audyn_channel_level_t;
```

### Statistics Structure
```c
typedef struct audyn_meter_stats {
    uint64_t frames_processed;
    uint64_t outputs_sent;
} audyn_meter_stats_t;
```

### Main Structure
```c
typedef struct audyn_level_meter {
    uint32_t channels;
    uint32_t sample_rate;

    /* Per-channel accumulators */
    double sum_sq[AUDYN_METER_MAX_CHANNELS];    /* Sum of squares for RMS */
    float peak[AUDYN_METER_MAX_CHANNELS];       /* Peak value */
    uint64_t sample_count;                       /* Samples accumulated */

    /* Peak hold (decays over time) */
    float peak_hold[AUDYN_METER_MAX_CHANNELS];
    uint64_t peak_hold_samples;

    /* Output interval */
    uint32_t output_interval_ms;
    uint64_t last_output_samples;

    /* Computed levels (updated on output) */
    audyn_channel_level_t levels[AUDYN_METER_MAX_CHANNELS];

    /* Statistics */
    uint64_t frames_processed;
    uint64_t outputs_sent;

    /* Wall clock time for NULL frame handling */
    uint64_t last_output_time_ms;
} audyn_level_meter_t;
```

**Note:** Unlike other modules, this structure is fully exposed in the header (not opaque). This allows direct field access but breaks encapsulation.

## Function-by-Function Analysis

### Helper Functions

#### `linear_to_db()` (lines 36-43)
Converts linear amplitude to decibels with clamping.

```c
static float linear_to_db(float linear)
{
    if (linear <= 0.0f) {
        return MIN_DB;
    }
    float db = 20.0f * log10f(linear);
    return (db < MIN_DB) ? MIN_DB : db;
}
```

#### `compute_levels()` (lines 110-143)
Computes RMS and peak levels from accumulators.

**Process:**
1. Calculate RMS: `sqrt(sum_of_squares / count)`
2. Update peak hold if new peak is higher
3. Detect clipping (peak >= 0.99)
4. Apply peak hold decay

**Peak Hold Decay:**
```c
float decay = 1.0f - (float)meter->sample_count / (float)meter->peak_hold_samples;
if (decay < 0.0f) decay = 0.0f;
meter->peak_hold[ch] *= decay;
```

#### `output_json()` (lines 145-166)
Outputs level data as JSON to stdout.

**Mono Output:**
```json
{"type":"levels","channels":1,"left":{"rms_db":-12.5,"peak_db":-3.2,"clipping":false}}
```

**Stereo Output:**
```json
{"type":"levels","channels":2,"left":{"rms_db":-12.5,"peak_db":-3.2,"clipping":false},"right":{"rms_db":-14.1,"peak_db":-5.0,"clipping":false}}
```

### Public API

#### `audyn_level_meter_create()` (lines 45-85)

Creates a level meter instance.

**Validation:**
- Channels: 1-2 (AUDYN_METER_MAX_CHANNELS)
- Sample rate: 1-384000 Hz

**Initialization:**
1. Allocate structure (calloc zeros accumulators)
2. Set parameters
3. Calculate peak hold samples
4. Initialize levels to silence (-60 dB)
5. Log creation

#### `audyn_level_meter_destroy()` (lines 87-96)

Destroys a level meter instance.

**Process:**
1. Log statistics (frames processed, outputs sent)
2. Free memory

#### `audyn_level_meter_reset()` (lines 98-108)

Clears all accumulators.

**Resets:**
- Sum of squares (all channels)
- Peak values (all channels)
- Peak hold values (all channels)
- Sample count

#### `audyn_level_meter_process()` (lines 168-247)

Processes an audio frame and outputs levels when interval reached.

**NULL Frame Handling:**
When called with NULL frame (for silence/UI keepalive):
1. Uses wall clock time (CLOCK_MONOTONIC)
2. Checks if output interval elapsed
3. Outputs silence levels (-60 dB)
4. Tracks via `last_output_time_ms`

**Audio Frame Processing:**
1. Clamp channels to meter's configured channels
2. For each sample:
   - Accumulate square for RMS: `sum_sq += sample * sample`
   - Track peak: `peak = max(peak, abs(sample))`
3. Increment sample count and frames_processed
4. Check if output interval reached (in samples)
5. If yes: compute levels, output JSON, reset accumulators

**Returns:** 1 if levels were output, 0 otherwise

#### `audyn_level_meter_flush()` (lines 249-265)

Forces output of current accumulated levels.

**Use case:** Called on shutdown to output final levels.

**Process:**
1. Compute levels from current accumulators
2. Output JSON
3. Increment outputs_sent
4. Reset accumulators

#### `audyn_level_meter_get_levels()` (lines 267-278)

Gets current levels without outputting JSON.

**Process:**
1. Compute levels from current accumulators
2. Copy to caller's array

#### `audyn_level_meter_get_stats()` (lines 280-292)

Gets meter statistics.

**Returns:**
- `frames_processed`: Total audio frames processed
- `outputs_sent`: Total JSON outputs sent

## Improvements Made

### 1. Logging Added
- Create logs configuration at DEBUG level
- Destroy logs final statistics
- Validation failures logged with descriptive errors

### 2. Input Validation
- Sample rate validated: 1-384000 Hz
- Channels validated: 1-2

### 3. Statistics Tracking
- Added `frames_processed` counter
- Added `outputs_sent` counter
- Added `audyn_meter_stats_t` structure
- Added `audyn_level_meter_get_stats()` function

### 4. Fixed NULL Frame Time Tracking
- Added separate `last_output_time_ms` field for wall clock
- Previously reused `last_output_samples` for both sample count and wall clock time

## Audio Level Calculations

### RMS (Root Mean Square)

RMS measures average loudness:

```
RMS = sqrt(sum(sample^2) / count)
```

- Accumulates sum of squared samples
- Divides by count
- Takes square root
- Result is in range [0.0, 1.0]

### Peak Detection

Peak tracks the maximum absolute sample value:

```c
if (abs(sample) > peak) {
    peak = abs(sample);
}
```

### Peak Hold

Peak hold provides visual persistence:
- New peak updates hold immediately
- Hold decays over `PEAK_HOLD_TIME` (1.5s)
- Decay is linear based on samples processed

### dB Conversion

Linear to decibels:

```
dB = 20 * log10(linear)
```

- 1.0 linear = 0 dB (full scale)
- 0.5 linear = -6 dB
- 0.1 linear = -20 dB
- Minimum clamped to -60 dB

### Clipping Detection

Sample is flagged as clipping if:

```c
abs(sample) >= 0.99
```

Threshold is slightly below 1.0 to catch near-clips.

## JSON Output Format

### Schema

```json
{
    "type": "levels",
    "channels": 1 or 2,
    "left": {
        "rms_db": float,
        "peak_db": float,
        "clipping": boolean
    },
    "right": {           // Only for stereo
        "rms_db": float,
        "peak_db": float,
        "clipping": boolean
    }
}
```

### Output Rate

- Default: 33ms interval (~30 fps)
- Configurable via `output_interval_ms` parameter
- Outputs even during silence (for UI responsiveness)

## Usage Example

```c
/* Create level meter for stereo, 48kHz, 30fps output */
audyn_level_meter_t *meter = audyn_level_meter_create(2, 48000, 33);
if (!meter) {
    // Error logged internally
    return -1;
}

/* Process audio frames from pipeline */
while (running) {
    audyn_audio_frame_t *frame = get_next_frame();

    if (audyn_level_meter_process(meter, frame)) {
        /* Levels were output to stdout */
    }

    /* If no frames available, still call with NULL for UI keepalive */
    if (!frame) {
        audyn_level_meter_process(meter, NULL);
    }
}

/* Get current levels programmatically */
audyn_channel_level_t levels[2];
audyn_level_meter_get_levels(meter, levels);
printf("Left RMS: %.1f dB, Peak: %.1f dB\n",
       levels[0].rms_db, levels[0].peak_db);

/* Get statistics */
audyn_meter_stats_t stats;
audyn_level_meter_get_stats(meter, &stats);
printf("Processed %lu frames, sent %lu outputs\n",
       stats.frames_processed, stats.outputs_sent);

/* Flush on shutdown */
audyn_level_meter_flush(meter);
audyn_level_meter_destroy(meter);
```

## Integration with Audyn

The level meter is optional and enabled via `--levels` flag:

```bash
audyn -o recording.wav --pipewire --levels
audyn -o recording.opus -m 239.69.1.1 --levels --levels-interval 50
```

**Processing Flow:**
1. Worker thread processes each audio frame
2. Passes frame to level meter
3. Level meter outputs JSON to stdout
4. Web backend parses JSON for visualization

## Typical Output

```
{"type":"levels","channels":2,"left":{"rms_db":-18.5,"peak_db":-6.2,"clipping":false},"right":{"rms_db":-19.1,"peak_db":-7.0,"clipping":false}}
{"type":"levels","channels":2,"left":{"rms_db":-15.2,"peak_db":-3.1,"clipping":false},"right":{"rms_db":-16.0,"peak_db":-4.5,"clipping":false}}
{"type":"levels","channels":2,"left":{"rms_db":-12.0,"peak_db":-0.5,"clipping":true},"right":{"rms_db":-13.5,"peak_db":-2.0,"clipping":false}}
```

## Limitations

1. **Max 2 Channels:** Unlike other modules that support up to 32 channels
2. **Non-Atomic Statistics:** Stats may have minor inconsistency if read while processing
3. **stdout Output:** May block briefly on fflush(); use in non-RT context only
4. **Structure Exposed:** Full structure in header breaks encapsulation

## Dependencies

- **C Standard Library:** stdio, stdlib, string, math, time
- **Audyn:** frame_pool (for audyn_audio_frame_t), log
