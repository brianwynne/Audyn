/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      wav_sink.h
 *
 *  Purpose:
 *      Minimal-dependency WAV writer sink (PCM16, RIFF/WAVE).
 *
 *  Design:
 *      - Writes a 44-byte RIFF/WAVE header with placeholder sizes on open()
 *      - Appends little-endian PCM16 sample data on write()
 *      - Seeks back and patches RIFF and data chunk sizes on close()
 *
 *  Format Limits:
 *      - Classic RIFF/WAVE uses 32-bit sizes; payload is limited to < 4 GiB.
 *      - This sink returns an error if the data chunk would exceed UINT32_MAX.
 *
 *  Threading:
 *      - NOT thread-safe. Intended to be used from a single consumer thread.
 *
 *  Dependencies:
 *      - Standard C: stdint.h
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  Author:
 *      B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#ifndef AUDYN_WAV_SINK_H
#define AUDYN_WAV_SINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum audyn_wav_format {
    AUDYN_WAV_PCM16 = 1
} audyn_wav_format_t;

typedef struct audyn_wav_sink_cfg {
    audyn_wav_format_t format;
    int enable_fsync; /* if non-zero, fsync() after each write() and close() */
} audyn_wav_sink_cfg_t;

/*
 * WAV sink statistics.
 */
typedef struct audyn_wav_stats {
    uint64_t frames_written;    /* Total frames written */
    uint64_t bytes_written;     /* Total data bytes written (excluding header) */
    int      size_limit_hit;    /* 1 if 4GB limit was hit */
} audyn_wav_stats_t;

typedef struct audyn_wav_sink audyn_wav_sink_t;

/* Create/destroy (NOT real-time safe). */
audyn_wav_sink_t *audyn_wav_sink_create(const audyn_wav_sink_cfg_t *cfg);
void audyn_wav_sink_destroy(audyn_wav_sink_t *s);

/*
 * Open a WAV file for writing.
 *
 * Parameters:
 *   s           - sink instance (must not be NULL)
 *   path        - output file path
 *   sample_rate - sample rate in Hz (1-384000)
 *   channels    - channel count (1-32)
 *
 * Returns 0 on success, -1 on error.
 * Errors are logged via the project's logging system.
 */
int  audyn_wav_sink_open(audyn_wav_sink_t *s,
                         const char *path,
                         uint32_t sample_rate,
                         uint16_t channels);

int  audyn_wav_sink_close(audyn_wav_sink_t *s);

/*
 * Write interleaved float32 samples to WAV as PCM16.
 *
 * Parameters:
 *   - interleaved_f32: [-1.0, +1.0] nominal (values are clamped)
 *   - frames: number of sample frames (time indices)
 *   - channels: must match the channels passed to open()
 *
 * Returns:
 *   0 on success
 *  -1 on error
 */
int  audyn_wav_sink_write(audyn_wav_sink_t *s,
                          const float *interleaved_f32,
                          uint32_t frames,
                          uint16_t channels);

/* Flush + optionally fsync to disk. */
int  audyn_wav_sink_sync(audyn_wav_sink_t *s);

/*
 * Get writing statistics.
 *
 * Parameters:
 *   s     - sink instance (must not be NULL)
 *   stats - output statistics structure (must not be NULL)
 */
void audyn_wav_sink_get_stats(const audyn_wav_sink_t *s, audyn_wav_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_WAV_SINK_H */
