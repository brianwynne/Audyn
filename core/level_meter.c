/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      level_meter.c
 *
 *  Purpose:
 *      Real-time audio level metering with RMS and peak detection.
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#include "level_meter.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

/* Minimum dB value (silence threshold) */
#define MIN_DB -60.0f

/* Peak hold decay time in seconds */
#define PEAK_HOLD_TIME 1.5f

/* Clipping threshold (slightly below 1.0 to catch near-clips) */
#define CLIP_THRESHOLD 0.99f

/* Convert linear to dB, clamped to MIN_DB */
static float linear_to_db(float linear)
{
    if (linear <= 0.0f) {
        return MIN_DB;
    }
    float db = 20.0f * log10f(linear);
    return (db < MIN_DB) ? MIN_DB : db;
}

audyn_level_meter_t *audyn_level_meter_create(
    uint32_t channels,
    uint32_t sample_rate,
    uint32_t output_interval_ms)
{
    if (channels == 0 || channels > AUDYN_METER_MAX_CHANNELS) {
        return NULL;
    }
    if (sample_rate == 0) {
        return NULL;
    }

    audyn_level_meter_t *meter = calloc(1, sizeof(audyn_level_meter_t));
    if (!meter) {
        return NULL;
    }

    meter->channels = channels;
    meter->sample_rate = sample_rate;
    meter->output_interval_ms = output_interval_ms > 0 ? output_interval_ms : 33;
    meter->peak_hold_samples = (uint64_t)(PEAK_HOLD_TIME * sample_rate);

    /* Initialize levels to silence */
    for (uint32_t i = 0; i < channels; i++) {
        meter->levels[i].rms_db = MIN_DB;
        meter->levels[i].peak_db = MIN_DB;
        meter->levels[i].rms_linear = 0.0f;
        meter->levels[i].peak_linear = 0.0f;
        meter->levels[i].clipping = 0;
    }

    return meter;
}

void audyn_level_meter_destroy(audyn_level_meter_t *meter)
{
    free(meter);
}

void audyn_level_meter_reset(audyn_level_meter_t *meter)
{
    if (!meter) return;

    for (uint32_t i = 0; i < meter->channels; i++) {
        meter->sum_sq[i] = 0.0;
        meter->peak[i] = 0.0f;
        meter->peak_hold[i] = 0.0f;
    }
    meter->sample_count = 0;
}

static void compute_levels(audyn_level_meter_t *meter)
{
    if (meter->sample_count == 0) {
        return;
    }

    for (uint32_t ch = 0; ch < meter->channels; ch++) {
        /* RMS = sqrt(sum_of_squares / count) */
        double rms = sqrt(meter->sum_sq[ch] / (double)meter->sample_count);
        meter->levels[ch].rms_linear = (float)rms;
        meter->levels[ch].rms_db = linear_to_db((float)rms);

        /* Peak with hold */
        if (meter->peak[ch] > meter->peak_hold[ch]) {
            meter->peak_hold[ch] = meter->peak[ch];
        }

        meter->levels[ch].peak_linear = meter->peak_hold[ch];
        meter->levels[ch].peak_db = linear_to_db(meter->peak_hold[ch]);

        /* Clipping detection */
        meter->levels[ch].clipping = (meter->peak[ch] >= CLIP_THRESHOLD) ? 1 : 0;
    }

    /* Decay peak hold */
    float decay = 1.0f - (float)meter->sample_count / (float)meter->peak_hold_samples;
    if (decay < 0.0f) decay = 0.0f;
    for (uint32_t ch = 0; ch < meter->channels; ch++) {
        meter->peak_hold[ch] *= decay;
        if (meter->peak_hold[ch] < 0.001f) {
            meter->peak_hold[ch] = 0.0f;
        }
    }
}

static void output_json(audyn_level_meter_t *meter)
{
    /* Output JSON to stdout for the backend to parse */
    if (meter->channels == 1) {
        printf("{\"type\":\"levels\",\"channels\":1,"
               "\"left\":{\"rms_db\":%.1f,\"peak_db\":%.1f,\"clipping\":%s}}\n",
               meter->levels[0].rms_db,
               meter->levels[0].peak_db,
               meter->levels[0].clipping ? "true" : "false");
    } else {
        printf("{\"type\":\"levels\",\"channels\":2,"
               "\"left\":{\"rms_db\":%.1f,\"peak_db\":%.1f,\"clipping\":%s},"
               "\"right\":{\"rms_db\":%.1f,\"peak_db\":%.1f,\"clipping\":%s}}\n",
               meter->levels[0].rms_db,
               meter->levels[0].peak_db,
               meter->levels[0].clipping ? "true" : "false",
               meter->levels[1].rms_db,
               meter->levels[1].peak_db,
               meter->levels[1].clipping ? "true" : "false");
    }
    fflush(stdout);
}

int audyn_level_meter_process(
    audyn_level_meter_t *meter,
    const audyn_audio_frame_t *frame)
{
    if (!meter) {
        return 0;
    }

    /* Handle NULL frame - just check if we need to output silence */
    if (!frame || !frame->data) {
        /* Still need to output periodically for UI responsiveness */
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t now_ms = (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

        if (now_ms - meter->last_output_samples >= meter->output_interval_ms) {
            /* Output silence levels */
            for (uint32_t ch = 0; ch < meter->channels; ch++) {
                meter->levels[ch].rms_db = MIN_DB;
                meter->levels[ch].peak_db = MIN_DB;
                meter->levels[ch].rms_linear = 0.0f;
                meter->levels[ch].peak_linear = 0.0f;
                meter->levels[ch].clipping = 0;
            }
            output_json(meter);
            meter->last_output_samples = now_ms;
            return 1;
        }
        return 0;
    }

    uint32_t channels = frame->channels;
    if (channels > meter->channels) {
        channels = meter->channels;
    }

    /* Process all samples in the frame */
    for (uint32_t i = 0; i < frame->sample_frames; i++) {
        for (uint32_t ch = 0; ch < channels; ch++) {
            float sample = frame->data[i * frame->channels + ch];

            /* Absolute value for peak detection */
            float abs_sample = fabsf(sample);

            /* Accumulate sum of squares for RMS */
            meter->sum_sq[ch] += (double)(sample * sample);

            /* Track peak */
            if (abs_sample > meter->peak[ch]) {
                meter->peak[ch] = abs_sample;
            }
        }
    }

    meter->sample_count += frame->sample_frames;

    /* Check if we should output levels */
    uint64_t output_samples = (uint64_t)meter->sample_rate *
                              meter->output_interval_ms / 1000;

    if (meter->sample_count - meter->last_output_samples >= output_samples) {
        compute_levels(meter);
        output_json(meter);

        /* Reset for next interval */
        for (uint32_t ch = 0; ch < meter->channels; ch++) {
            meter->sum_sq[ch] = 0.0;
            meter->peak[ch] = 0.0f;
        }
        meter->last_output_samples = meter->sample_count;
        meter->sample_count = 0;

        return 1;
    }

    return 0;
}

void audyn_level_meter_flush(audyn_level_meter_t *meter)
{
    if (!meter || meter->sample_count == 0) {
        return;
    }

    compute_levels(meter);
    output_json(meter);

    /* Reset */
    for (uint32_t ch = 0; ch < meter->channels; ch++) {
        meter->sum_sq[ch] = 0.0;
        meter->peak[ch] = 0.0f;
    }
    meter->sample_count = 0;
}

void audyn_level_meter_get_levels(
    audyn_level_meter_t *meter,
    audyn_channel_level_t *out_levels)
{
    if (!meter || !out_levels) {
        return;
    }

    compute_levels(meter);
    memcpy(out_levels, meter->levels,
           meter->channels * sizeof(audyn_channel_level_t));
}
