/*
 * vox.c - Voice Activity Detection (VOX) implementation
 *
 * State machine with pre-roll ring buffer for threshold-based recording.
 *
 * Copyright: (c) 2026 B. Wynne
 * License: GPLv2 or later
 */

#include "vox.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Auto-hysteresis offset in dB (release = threshold - this value) */
#define AUTO_HYSTERESIS_DB 5.0f

/* Minimum silence floor */
#define MIN_DB -60.0f

/* Maximum ring buffer slots (5s at 48kHz with 1ms frames = 5000 slots) */
#define MAX_RING_SLOTS 8192

/*
 * Ring buffer for pre-roll frames.
 * Stores pointers to frames (frames are owned by caller's pool).
 */
typedef struct ring_buffer {
    const audyn_audio_frame_t **frames;
    uint32_t capacity;      /* Number of slots */
    uint32_t head;          /* Next write position */
    uint32_t count;         /* Current occupancy */
} ring_buffer_t;

/*
 * VOX detector internal state.
 */
struct audyn_vox {
    /* Configuration */
    audyn_vox_config_t cfg;
    float effective_release_db;     /* Actual release threshold (computed) */

    /* State machine */
    audyn_vox_state_t state;
    uint64_t state_enter_samples;   /* Sample count when entering current state */
    uint64_t samples_processed;     /* Total samples processed */

    /* Timer thresholds (in samples) */
    uint64_t detection_samples;     /* Samples needed in DETECTING before ACTIVE */
    uint64_t hangover_samples;      /* Samples needed in HANGOVER before IDLE */

    /* Pre-roll ring buffer */
    ring_buffer_t preroll;

    /* File boundary flags (cleared after reading) */
    int should_open;
    int should_close;

    /* Statistics */
    audyn_vox_stats_t stats;
    uint64_t active_start_samples;  /* For tracking active duration */
    uint64_t idle_start_samples;    /* For tracking idle duration */
};

/* State names for logging */
static const char *state_names[] = {
    "IDLE",
    "DETECTING",
    "ACTIVE",
    "HANGOVER"
};

/* Mode names for logging */
static const char *mode_names[] = {
    "rms",
    "peak",
    "any"
};

/*
 * Initialize ring buffer.
 */
static int ring_init(ring_buffer_t *ring, uint32_t capacity)
{
    if (capacity > MAX_RING_SLOTS) {
        capacity = MAX_RING_SLOTS;
    }

    ring->frames = calloc(capacity, sizeof(audyn_audio_frame_t *));
    if (!ring->frames) {
        return -1;
    }

    ring->capacity = capacity;
    ring->head = 0;
    ring->count = 0;
    return 0;
}

/*
 * Destroy ring buffer.
 */
static void ring_destroy(ring_buffer_t *ring)
{
    if (ring->frames) {
        free(ring->frames);
        ring->frames = NULL;
    }
    ring->capacity = 0;
    ring->head = 0;
    ring->count = 0;
}

/*
 * Clear ring buffer.
 */
static void ring_clear(ring_buffer_t *ring)
{
    ring->head = 0;
    ring->count = 0;
}

/*
 * Push a frame into ring buffer.
 * Overwrites oldest frame if full.
 */
static void ring_push(ring_buffer_t *ring, const audyn_audio_frame_t *frame)
{
    ring->frames[ring->head] = frame;
    ring->head = (ring->head + 1) % ring->capacity;

    if (ring->count < ring->capacity) {
        ring->count++;
    }
}

/*
 * Get all frames from ring buffer in order (oldest first).
 * Returns number of frames copied.
 */
static int ring_get_all(ring_buffer_t *ring,
                        const audyn_audio_frame_t **out,
                        int max_out)
{
    if (ring->count == 0) {
        return 0;
    }

    int to_copy = (int)ring->count;
    if (to_copy > max_out) {
        to_copy = max_out;
    }

    /* Calculate start position (oldest frame) */
    uint32_t start;
    if (ring->count == ring->capacity) {
        start = ring->head;  /* Buffer is full, head points to oldest */
    } else {
        start = 0;  /* Buffer not full, oldest is at 0 */
    }

    /* Copy frames in order */
    for (int i = 0; i < to_copy; i++) {
        uint32_t idx = (start + i) % ring->capacity;
        out[i] = ring->frames[idx];
    }

    return to_copy;
}

/*
 * Calculate effective level based on mode.
 */
static float get_effective_level(const audyn_vox_t *vox,
                                  float rms_left, float rms_right,
                                  float peak_left, float peak_right)
{
    float left, right;

    switch (vox->cfg.mode) {
    case AUDYN_VOX_LEVEL_PEAK:
        left = peak_left;
        right = peak_right;
        break;

    case AUDYN_VOX_LEVEL_ANY_CHANNEL:
        /* For "any" mode, we check if either channel exceeds threshold */
        /* Return the higher of the two RMS values */
        left = rms_left;
        right = rms_right;
        return (left > right) ? left : right;

    case AUDYN_VOX_LEVEL_RMS:
    default:
        left = rms_left;
        right = rms_right;
        break;
    }

    /* For mono, just use left channel */
    if (vox->cfg.channels == 1) {
        return left;
    }

    /* For stereo, average the channels */
    return (left + right) / 2.0f;
}

/*
 * Check if level exceeds threshold.
 */
static int level_exceeds_threshold(const audyn_vox_t *vox, float level_db)
{
    return level_db > vox->cfg.threshold_db;
}

/*
 * Check if level is below release threshold.
 */
static int level_below_release(const audyn_vox_t *vox, float level_db)
{
    return level_db < vox->effective_release_db;
}

/*
 * Transition to a new state.
 */
static void transition_to(audyn_vox_t *vox, audyn_vox_state_t new_state)
{
    audyn_vox_state_t old_state = vox->state;

    if (old_state == new_state) {
        return;
    }

    /* Update statistics for state duration */
    uint64_t now = vox->samples_processed;

    if (old_state == AUDYN_VOX_ACTIVE) {
        uint64_t duration = now - vox->active_start_samples;
        vox->stats.total_active_ms += (duration * 1000) / vox->cfg.sample_rate;
    } else if (old_state == AUDYN_VOX_IDLE) {
        uint64_t duration = now - vox->idle_start_samples;
        vox->stats.total_idle_ms += (duration * 1000) / vox->cfg.sample_rate;
    }

    /* Log transition */
    LOG_DEBUG("VOX: %s -> %s", state_names[old_state], state_names[new_state]);

    /* Set new state */
    vox->state = new_state;
    vox->state_enter_samples = now;

    /* Track start times for statistics */
    if (new_state == AUDYN_VOX_ACTIVE) {
        vox->active_start_samples = now;
        vox->stats.activations++;
        vox->should_open = 1;
    } else if (new_state == AUDYN_VOX_IDLE) {
        vox->idle_start_samples = now;
        if (old_state == AUDYN_VOX_HANGOVER) {
            vox->should_close = 1;
        }
    }
}

/*
 * Create VOX detector.
 */
audyn_vox_t *audyn_vox_create(const audyn_vox_config_t *cfg)
{
    if (!cfg) {
        LOG_ERROR("VOX: NULL config");
        return NULL;
    }

    if (cfg->sample_rate == 0 || cfg->sample_rate > 384000) {
        LOG_ERROR("VOX: invalid sample rate %u", cfg->sample_rate);
        return NULL;
    }

    if (cfg->channels == 0 || cfg->channels > 2) {
        LOG_ERROR("VOX: invalid channels %u (must be 1-2)", cfg->channels);
        return NULL;
    }

    if (cfg->preroll_ms > AUDYN_VOX_MAX_PREROLL_MS) {
        LOG_ERROR("VOX: pre-roll %u exceeds max %u ms",
                  cfg->preroll_ms, AUDYN_VOX_MAX_PREROLL_MS);
        return NULL;
    }

    audyn_vox_t *vox = calloc(1, sizeof(audyn_vox_t));
    if (!vox) {
        LOG_ERROR("VOX: allocation failed");
        return NULL;
    }

    /* Copy configuration */
    vox->cfg = *cfg;

    /* Calculate effective release threshold */
    if (cfg->release_db == 0.0f) {
        /* Auto-hysteresis: release = threshold - 5dB */
        vox->effective_release_db = cfg->threshold_db - AUTO_HYSTERESIS_DB;
    } else {
        vox->effective_release_db = cfg->release_db;
    }

    /* Clamp release to minimum */
    if (vox->effective_release_db < MIN_DB) {
        vox->effective_release_db = MIN_DB;
    }

    /* Calculate timer thresholds in samples */
    vox->detection_samples = ((uint64_t)cfg->detection_ms * cfg->sample_rate) / 1000;
    vox->hangover_samples = ((uint64_t)cfg->hangover_ms * cfg->sample_rate) / 1000;

    /* Initialize pre-roll ring buffer */
    /* Calculate slots needed: preroll_ms worth of frames */
    /* Assuming ~1ms per frame at 48kHz with 48 samples/frame */
    uint32_t samples_per_frame = 48;  /* Typical */
    uint32_t preroll_samples = (cfg->preroll_ms * cfg->sample_rate) / 1000;
    uint32_t ring_slots = (preroll_samples / samples_per_frame) + 1;

    if (ring_init(&vox->preroll, ring_slots) != 0) {
        LOG_ERROR("VOX: ring buffer allocation failed");
        free(vox);
        return NULL;
    }

    /* Initialize state */
    vox->state = AUDYN_VOX_IDLE;
    vox->idle_start_samples = 0;

    LOG_INFO("VOX: created (threshold=%.1fdB release=%.1fdB detection=%ums "
             "hangover=%ums preroll=%ums mode=%s)",
             cfg->threshold_db, vox->effective_release_db,
             cfg->detection_ms, cfg->hangover_ms, cfg->preroll_ms,
             mode_names[cfg->mode]);

    return vox;
}

/*
 * Destroy VOX detector.
 */
void audyn_vox_destroy(audyn_vox_t *vox)
{
    if (!vox) {
        return;
    }

    LOG_INFO("VOX: destroyed (processed=%lu passed=%lu gated=%lu activations=%lu)",
             vox->stats.frames_processed, vox->stats.frames_passed,
             vox->stats.frames_gated, vox->stats.activations);

    ring_destroy(&vox->preroll);
    free(vox);
}

/*
 * Process an audio frame.
 */
int audyn_vox_process(audyn_vox_t *vox,
                      const audyn_audio_frame_t *frame,
                      float rms_db_left, float rms_db_right,
                      float peak_db_left, float peak_db_right,
                      const audyn_audio_frame_t **out_frames,
                      int max_out)
{
    if (!vox || !frame || !out_frames || max_out <= 0) {
        return 0;
    }

    vox->stats.frames_processed++;

    /* Calculate effective level for threshold comparison */
    float level_db = get_effective_level(vox,
                                         rms_db_left, rms_db_right,
                                         peak_db_left, peak_db_right);

    /* Update sample counter */
    vox->samples_processed += frame->sample_frames;

    int out_count = 0;
    uint64_t samples_in_state = vox->samples_processed - vox->state_enter_samples;

    switch (vox->state) {
    case AUDYN_VOX_IDLE:
        /* Store frame in pre-roll buffer */
        ring_push(&vox->preroll, frame);

        if (level_exceeds_threshold(vox, level_db)) {
            /* Activity detected, start detection period */
            transition_to(vox, AUDYN_VOX_DETECTING);
        }
        vox->stats.frames_gated++;
        break;

    case AUDYN_VOX_DETECTING:
        /* Store frame in pre-roll buffer */
        ring_push(&vox->preroll, frame);

        if (!level_exceeds_threshold(vox, level_db)) {
            /* False trigger, back to idle */
            transition_to(vox, AUDYN_VOX_IDLE);
            vox->stats.frames_gated++;
        } else if (samples_in_state >= vox->detection_samples) {
            /* Detection period complete, activate */
            transition_to(vox, AUDYN_VOX_ACTIVE);

            /* Output all pre-roll frames first */
            out_count = ring_get_all(&vox->preroll, out_frames, max_out);
            ring_clear(&vox->preroll);

            vox->stats.frames_passed += out_count;
        } else {
            vox->stats.frames_gated++;
        }
        break;

    case AUDYN_VOX_ACTIVE:
        /* Pass frame directly to output */
        out_frames[0] = frame;
        out_count = 1;
        vox->stats.frames_passed++;

        if (level_below_release(vox, level_db)) {
            /* Silence detected, start hangover */
            transition_to(vox, AUDYN_VOX_HANGOVER);
        }
        break;

    case AUDYN_VOX_HANGOVER:
        /* Still pass frames during hangover */
        out_frames[0] = frame;
        out_count = 1;
        vox->stats.frames_passed++;

        if (level_exceeds_threshold(vox, level_db)) {
            /* Activity resumed, back to active */
            transition_to(vox, AUDYN_VOX_ACTIVE);
        } else if (samples_in_state >= vox->hangover_samples) {
            /* Hangover expired, return to idle */
            transition_to(vox, AUDYN_VOX_IDLE);
        }
        break;
    }

    return out_count;
}

/*
 * Get current state.
 */
audyn_vox_state_t audyn_vox_get_state(const audyn_vox_t *vox)
{
    return vox ? vox->state : AUDYN_VOX_IDLE;
}

/*
 * Check if file should be opened.
 */
int audyn_vox_should_open_file(audyn_vox_t *vox)
{
    if (!vox) {
        return 0;
    }

    int result = vox->should_open;
    vox->should_open = 0;
    return result;
}

/*
 * Check if file should be closed.
 */
int audyn_vox_should_close_file(audyn_vox_t *vox)
{
    if (!vox) {
        return 0;
    }

    int result = vox->should_close;
    vox->should_close = 0;
    return result;
}

/*
 * Flush pre-roll buffer.
 */
int audyn_vox_flush(audyn_vox_t *vox,
                    const audyn_audio_frame_t **out_frames,
                    int max_out)
{
    if (!vox || !out_frames || max_out <= 0) {
        return 0;
    }

    /* Only flush if we're in an active recording state */
    if (vox->state == AUDYN_VOX_ACTIVE || vox->state == AUDYN_VOX_HANGOVER) {
        /* Nothing in pre-roll during active states */
        return 0;
    }

    /* Get any remaining pre-roll frames */
    int count = ring_get_all(&vox->preroll, out_frames, max_out);
    ring_clear(&vox->preroll);

    return count;
}

/*
 * Reset VOX detector.
 */
void audyn_vox_reset(audyn_vox_t *vox)
{
    if (!vox) {
        return;
    }

    ring_clear(&vox->preroll);
    vox->state = AUDYN_VOX_IDLE;
    vox->state_enter_samples = 0;
    vox->samples_processed = 0;
    vox->should_open = 0;
    vox->should_close = 0;
    vox->idle_start_samples = 0;

    LOG_DEBUG("VOX: reset");
}

/*
 * Get statistics.
 */
void audyn_vox_get_stats(const audyn_vox_t *vox, audyn_vox_stats_t *stats)
{
    if (!vox || !stats) {
        return;
    }

    *stats = vox->stats;

    /* Add current state duration */
    uint64_t now = vox->samples_processed;
    if (vox->state == AUDYN_VOX_ACTIVE) {
        uint64_t duration = now - vox->active_start_samples;
        stats->total_active_ms += (duration * 1000) / vox->cfg.sample_rate;
    } else if (vox->state == AUDYN_VOX_IDLE) {
        uint64_t duration = now - vox->idle_start_samples;
        stats->total_idle_ms += (duration * 1000) / vox->cfg.sample_rate;
    }
}

/*
 * Get state name.
 */
const char *audyn_vox_state_name(audyn_vox_state_t state)
{
    if (state > AUDYN_VOX_HANGOVER) {
        return "UNKNOWN";
    }
    return state_names[state];
}

/*
 * Get mode name.
 */
const char *audyn_vox_mode_name(audyn_vox_level_mode_t mode)
{
    if (mode > AUDYN_VOX_LEVEL_ANY_CHANNEL) {
        return "unknown";
    }
    return mode_names[mode];
}
