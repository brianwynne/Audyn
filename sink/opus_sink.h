\
/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      opus_sink.h
 *
 *  Purpose:
 *      Non-real-time audio sink that encodes PCM audio frames into an
 *      Ogg Opus file using libopus (encoder) and libogg (container).
 *
 *      This sink is designed to be driven by Audyn's worker thread
 *      (non-RT context). It MUST NOT be called from any real-time audio
 *      callback or time-critical thread.
 *
 *  Design Overview:
 *      - Input: fixed-size interleaved float32 PCM frames
 *      - Encode: libopus (Opus packets)
 *      - Mux:   libogg (Ogg pages with OpusHead/OpusTags + audio packets)
 *      - Output: standards-compliant ".opus" file
 *
 *  Threading Model:
 *      - Single-threaded; not thread-safe.
 *      - Expected to be used by exactly one consumer thread.
 *
 *  Dependencies:
 *      - Standard C: stdint.h
 *      - libopus:    encoder (linked in implementation)
 *      - libogg:     container (linked in implementation)
 *
 *      Note: This header intentionally does NOT include <opus/opus.h> or <ogg/ogg.h>
 *      to keep compile-time dependencies minimal. The public API exposes only
 *      plain C types.
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

#ifndef AUDYN_OPUS_SINK_H
#define AUDYN_OPUS_SINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque sink handle */
typedef struct audyn_opus_sink audyn_opus_sink_t;

/*
 * Opus application mode.
 *
 * Mirrors libopus application choices without requiring opus headers.
 */
typedef enum audyn_opus_application_e
{
    AUDYN_OPUS_APP_VOIP = 2048,               /* OPUS_APPLICATION_VOIP */
    AUDYN_OPUS_APP_AUDIO = 2049,              /* OPUS_APPLICATION_AUDIO */
    AUDYN_OPUS_APP_RESTRICTED_LOWDELAY = 2051 /* OPUS_APPLICATION_RESTRICTED_LOWDELAY */
} audyn_opus_application_t;

/*
 * Opus sink configuration.
 *
 * All fields must be initialized by the caller.
 * Suggested defaults:
 *   - sample_rate: 48000
 *   - channels: 1 or 2
 *   - bitrate: 64000 (mono speech) .. 128000 (stereo music)
 *   - vbr: 1
 *   - complexity: 5
 *   - application: AUDYN_OPUS_APP_AUDIO
 */
typedef struct audyn_opus_cfg
{
    /* Required */
    uint32_t sample_rate;              /* Opus supports 8000/12000/16000/24000/48000; 48000 recommended */
    uint16_t channels;                 /* 1 (mono) or 2 (stereo) */
    uint32_t bitrate;                  /* Target bitrate in bits/sec */

    /* Optional tuning */
    int      vbr;                      /* 1 = enable VBR (recommended), 0 = CBR */
    int      complexity;               /* 0..10 (default 5) */
    audyn_opus_application_t application;

    /* Durability */
    int      enable_fsync;             /* 1 = fsync after page flush, 0 = no fsync (default 0) */

} audyn_opus_cfg_t;


/*
 * Create an Opus sink and write container headers.
 *
 * Parameters:
 *      path  - output .opus path
 *      cfg   - fully initialized configuration (must not be NULL)
 *
 * Returns:
 *      sink pointer on success, NULL on failure.
 */
audyn_opus_sink_t *
audyn_opus_sink_create(const char *path, const audyn_opus_cfg_t *cfg);


/*
 * Encode and write one block of audio.
 *
 * Parameters:
 *      sink            - sink instance (must not be NULL)
 *      interleaved_f32 - interleaved float32 PCM (L0,R0,L1,R1,...)
 *      frames          - number of sample-frames per channel
 *
 * Returns:
 *      0 on success
 *     -1 on error (encoder or I/O failure)
 *
 * Notes:
 *      - The sink may buffer packets until an Ogg page is ready to flush.
 *      - The caller retains ownership of interleaved_f32.
 */
int
audyn_opus_sink_write(audyn_opus_sink_t *sink,
                      const float *interleaved_f32,
                      uint32_t frames);


/*
 * Flush any buffered Ogg pages to disk.
 *
 * Returns:
 *      0 on success
 *     -1 on failure
 */
int
audyn_opus_sink_flush(audyn_opus_sink_t *sink);


/*
 * Finalize the Ogg stream and close the file.
 *
 * Returns:
 *      0 on success
 *     -1 on failure
 */
int
audyn_opus_sink_close(audyn_opus_sink_t *sink);


/*
 * Destroy the sink (safe to call with NULL).
 * Performs a best-effort close if not already closed.
 */
void
audyn_opus_sink_destroy(audyn_opus_sink_t *sink);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_OPUS_SINK_H */
