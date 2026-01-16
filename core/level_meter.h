/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      level_meter.h
 *
 *  Purpose:
 *      Real-time audio level metering with RMS and peak detection.
 *      Outputs JSON to stdout for integration with web backend.
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#ifndef AUDYN_LEVEL_METER_H
#define AUDYN_LEVEL_METER_H

#include <stdint.h>
#include "frame_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum supported channels */
#define AUDYN_METER_MAX_CHANNELS 2

/* Sample rate limits */
#define AUDYN_METER_MAX_SAMPLE_RATE 384000

/* Level data for a single channel */
typedef struct audyn_channel_level {
    float rms_linear;       /* RMS level (0.0 to 1.0) */
    float rms_db;           /* RMS level in dB (typically -60 to 0) */
    float peak_linear;      /* Peak level (0.0 to 1.0) */
    float peak_db;          /* Peak level in dB */
    int clipping;           /* Non-zero if clipping detected */
} audyn_channel_level_t;

/* Level meter state */
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
    uint64_t frames_processed;          /* Total audio frames processed */
    uint64_t outputs_sent;              /* Total JSON outputs sent */

    /* Wall clock time for NULL frame handling */
    uint64_t last_output_time_ms;

} audyn_level_meter_t;

/*
 * Level meter statistics.
 */
typedef struct audyn_meter_stats {
    uint64_t frames_processed;
    uint64_t outputs_sent;
} audyn_meter_stats_t;

/*
 * Create a level meter.
 *
 * channels: Number of audio channels (1 or 2)
 * sample_rate: Audio sample rate in Hz
 * output_interval_ms: How often to output levels (default 33ms = ~30fps)
 *
 * Returns: Pointer to level meter, or NULL on error
 */
audyn_level_meter_t *audyn_level_meter_create(
    uint32_t channels,
    uint32_t sample_rate,
    uint32_t output_interval_ms
);

/*
 * Destroy a level meter.
 */
void audyn_level_meter_destroy(audyn_level_meter_t *meter);

/*
 * Process an audio frame and accumulate levels.
 *
 * This should be called for every audio frame in the processing pipeline.
 * When the output interval is reached, it prints JSON to stdout.
 *
 * meter: Level meter instance
 * frame: Audio frame to process
 *
 * Returns: 1 if levels were output, 0 otherwise
 */
int audyn_level_meter_process(
    audyn_level_meter_t *meter,
    const audyn_audio_frame_t *frame
);

/*
 * Force output of current levels (e.g., on shutdown).
 */
void audyn_level_meter_flush(audyn_level_meter_t *meter);

/*
 * Get current levels without outputting.
 *
 * out_levels: Array of at least 'channels' channel_level structures
 */
void audyn_level_meter_get_levels(
    audyn_level_meter_t *meter,
    audyn_channel_level_t *out_levels
);

/*
 * Reset the meter (clear accumulators).
 */
void audyn_level_meter_reset(audyn_level_meter_t *meter);

/*
 * Get meter statistics.
 */
void audyn_level_meter_get_stats(const audyn_level_meter_t *meter,
                                  audyn_meter_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_LEVEL_METER_H */
