/*
 * vox.h - Voice Activity Detection (VOX) module
 *
 * Provides threshold-based recording with configurable:
 * - Activation/release thresholds with hysteresis
 * - Detection delay to prevent false triggers
 * - Hangover time to prevent choppy recordings
 * - Pre-roll buffer to capture audio before trigger
 *
 * Copyright: (c) 2026 B. Wynne
 * License: GPLv2 or later
 */

#ifndef AUDYN_VOX_H
#define AUDYN_VOX_H

#include <stdint.h>
#include "frame_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum pre-roll buffer in milliseconds */
#define AUDYN_VOX_MAX_PREROLL_MS 5000

/* Default values */
#define AUDYN_VOX_DEFAULT_THRESHOLD_DB  -30.0f
#define AUDYN_VOX_DEFAULT_DETECTION_MS  100
#define AUDYN_VOX_DEFAULT_HANGOVER_MS   2000
#define AUDYN_VOX_DEFAULT_PREROLL_MS    500

/* Level detection mode */
typedef enum audyn_vox_level_mode {
    AUDYN_VOX_LEVEL_RMS = 0,        /* Use RMS level (average loudness) */
    AUDYN_VOX_LEVEL_PEAK,           /* Use peak level (transient response) */
    AUDYN_VOX_LEVEL_ANY_CHANNEL     /* Trigger if any channel exceeds threshold */
} audyn_vox_level_mode_t;

/* VOX state machine states */
typedef enum audyn_vox_state {
    AUDYN_VOX_IDLE = 0,     /* Waiting for audio activity */
    AUDYN_VOX_DETECTING,    /* Activity detected, confirming */
    AUDYN_VOX_ACTIVE,       /* Recording active */
    AUDYN_VOX_HANGOVER      /* Silence detected, waiting hangover period */
} audyn_vox_state_t;

/* VOX configuration */
typedef struct audyn_vox_config {
    float threshold_db;             /* Activation threshold in dB (-60 to 0) */
    float release_db;               /* Release threshold in dB (0 = auto: threshold - 5dB) */
    uint32_t detection_ms;          /* Time above threshold before activation */
    uint32_t hangover_ms;           /* Time to continue after silence detected */
    uint32_t preroll_ms;            /* Pre-roll buffer size (max 5000ms) */
    audyn_vox_level_mode_t mode;    /* Level detection mode */
    uint32_t sample_rate;           /* Sample rate for time calculations */
    uint16_t channels;              /* Number of audio channels */
} audyn_vox_config_t;

/* VOX statistics */
typedef struct audyn_vox_stats {
    uint64_t frames_processed;      /* Total frames processed */
    uint64_t frames_passed;         /* Frames passed to output */
    uint64_t frames_gated;          /* Frames blocked (silence) */
    uint64_t activations;           /* Number of IDLE→ACTIVE transitions */
    uint64_t total_active_ms;       /* Total time in ACTIVE state */
    uint64_t total_idle_ms;         /* Total time in IDLE state */
} audyn_vox_stats_t;

/* Opaque VOX detector type */
typedef struct audyn_vox audyn_vox_t;

/*
 * Create a VOX detector instance.
 *
 * @param cfg  Configuration parameters
 * @return     VOX detector instance, or NULL on error
 */
audyn_vox_t *audyn_vox_create(const audyn_vox_config_t *cfg);

/*
 * Destroy a VOX detector instance.
 *
 * @param vox  VOX detector to destroy
 */
void audyn_vox_destroy(audyn_vox_t *vox);

/*
 * Process an audio frame through the VOX detector.
 *
 * The detector uses the provided level measurements to determine voice activity.
 * When transitioning from IDLE to ACTIVE, pre-roll frames are returned first.
 *
 * @param vox           VOX detector instance
 * @param frame         Audio frame to process (stored in pre-roll buffer)
 * @param rms_db_left   RMS level in dB for left/mono channel
 * @param rms_db_right  RMS level in dB for right channel (ignored if mono)
 * @param peak_db_left  Peak level in dB for left/mono channel
 * @param peak_db_right Peak level in dB for right channel (ignored if mono)
 * @param out_frames    Array to receive frames to write
 * @param max_out       Maximum frames that can be returned
 * @return              Number of frames in out_frames (0 = silence, gate closed)
 */
int audyn_vox_process(audyn_vox_t *vox,
                      const audyn_audio_frame_t *frame,
                      float rms_db_left, float rms_db_right,
                      float peak_db_left, float peak_db_right,
                      const audyn_audio_frame_t **out_frames,
                      int max_out);

/*
 * Get current VOX state.
 *
 * @param vox  VOX detector instance
 * @return     Current state
 */
audyn_vox_state_t audyn_vox_get_state(const audyn_vox_t *vox);

/*
 * Check if a new file should be opened.
 *
 * Returns 1 once when transitioning from IDLE to ACTIVE (after detection period).
 * The flag is cleared after reading.
 *
 * @param vox  VOX detector instance
 * @return     1 if file should be opened, 0 otherwise
 */
int audyn_vox_should_open_file(audyn_vox_t *vox);

/*
 * Check if current file should be closed.
 *
 * Returns 1 once when transitioning from HANGOVER to IDLE (after hangover expires).
 * The flag is cleared after reading.
 *
 * @param vox  VOX detector instance
 * @return     1 if file should be closed, 0 otherwise
 */
int audyn_vox_should_close_file(audyn_vox_t *vox);

/*
 * Flush remaining frames from pre-roll buffer.
 *
 * Called on shutdown to retrieve any buffered frames.
 *
 * @param vox        VOX detector instance
 * @param out_frames Array to receive frames
 * @param max_out    Maximum frames that can be returned
 * @return           Number of frames returned
 */
int audyn_vox_flush(audyn_vox_t *vox,
                    const audyn_audio_frame_t **out_frames,
                    int max_out);

/*
 * Reset VOX detector state.
 *
 * Clears pre-roll buffer and resets to IDLE state.
 *
 * @param vox  VOX detector instance
 */
void audyn_vox_reset(audyn_vox_t *vox);

/*
 * Get VOX statistics.
 *
 * @param vox    VOX detector instance
 * @param stats  Output statistics structure
 */
void audyn_vox_get_stats(const audyn_vox_t *vox, audyn_vox_stats_t *stats);

/*
 * Get string name for VOX state (for logging).
 *
 * @param state  VOX state
 * @return       State name string
 */
const char *audyn_vox_state_name(audyn_vox_state_t state);

/*
 * Get string name for level mode (for logging).
 *
 * @param mode  Level mode
 * @return      Mode name string
 */
const char *audyn_vox_mode_name(audyn_vox_level_mode_t mode);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_VOX_H */
