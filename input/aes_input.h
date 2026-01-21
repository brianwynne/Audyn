/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      aes_input.h
 *
 *  Purpose:
 *      AES67 / RTP audio input interface.
 *
 *      This module receives uncompressed PCM audio over RTP
 *      (AES67-compatible) and converts it into Audyn audio frames
 *      for downstream processing.
 *
 *      Network parameters (multicast/unicast address, port, payload type)
 *      are NOT hard-coded and must be provided at runtime via configuration.
 *
 *  Design:
 *      - RTP receive socket (IPv4 multicast or unicast)
 *      - Fixed sample rate & channel count per instance
 *      - Non-realtime network thread feeding Audyn frame pool
 *      - Explicit packet-to-frame conversion
 *
 *  Dependencies:
 *      - POSIX sockets (implementation)
 *      - pthreads (implementation)
 *      - Audyn core:
 *          frame_pool
 *          audio_queue
 *
 *  Standards:
 *      - AES67 (RTP/PCM)
 *      - SMPTE ST 2110-30 (subset)
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

#ifndef AUDYN_AES_INPUT_H
#define AUDYN_AES_INPUT_H

#include <stdint.h>

#include <stddef.h>  /* for size_t */

#include "frame_pool.h"
#include "audio_queue.h"
#include "ptp_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AES67 input statistics.
 *
 * All counters can be read while capture is running.
 */
typedef struct audyn_aes_stats {
    uint64_t packets_rx;              /* Total RTP packets received */
    uint64_t packets_dropped;         /* Packets dropped (invalid RTP, wrong PT, etc.) */
    uint64_t discontinuities;         /* RTP sequence discontinuities detected */
    uint64_t frames_pushed;           /* Frames successfully pushed to queue */
    uint64_t frames_dropped_pool;     /* Drops due to frame pool exhaustion */
    uint64_t frames_dropped_queue;    /* Drops due to audio queue full */
} audyn_aes_stats_t;

typedef struct audyn_aes_input_cfg {
    const char *source_ip;
    uint16_t    port;
    uint8_t     payload_type;

    uint32_t    sample_rate;
    uint16_t    channels;           /* Output channels (1 or 2) */
    uint16_t    samples_per_packet;

    uint32_t    socket_rcvbuf;

    const char *bind_interface;     /* Network interface for multicast (e.g., "eth0") */

    /* Channel selection for multi-channel streams */
    uint16_t    stream_channels;    /* Total channels in stream (0 = same as channels) */
    uint16_t    channel_offset;     /* First channel to extract (0-based, default 0) */
                                    /* e.g., offset=4, channels=2 extracts channels 5-6 */
} audyn_aes_input_cfg_t;

typedef struct audyn_aes_input audyn_aes_input_t;

audyn_aes_input_t *
audyn_aes_input_create(audyn_frame_pool_t *pool,
                       audyn_audio_queue_t *queue,
                       const audyn_aes_input_cfg_t *cfg);

int audyn_aes_input_start(audyn_aes_input_t *in);
void audyn_aes_input_stop(audyn_aes_input_t *in);
void audyn_aes_input_destroy(audyn_aes_input_t *in);

/*
 * Check if input is currently running.
 *
 * Returns 1 if running, 0 if stopped or NULL.
 */
int audyn_aes_input_is_running(const audyn_aes_input_t *in);

/*
 * Get last error message (thread-safe version).
 *
 * Copies error message to caller's buffer.
 *
 * Parameters:
 *   in     - input instance
 *   buf    - output buffer
 *   buflen - buffer size
 */
void audyn_aes_input_get_last_error(const audyn_aes_input_t *in, char *buf, size_t buflen);

/*
 * Get last error message.
 *
 * DEPRECATED: Use audyn_aes_input_get_last_error() for thread-safe access.
 * This function returns a pointer to internal storage that may change.
 */
const char *audyn_aes_input_last_error(const audyn_aes_input_t *in);

/*
 * Get input statistics.
 *
 * Safe to call while input is running.
 *
 * Parameters:
 *   in    - input instance (must not be NULL)
 *   stats - output statistics structure (must not be NULL)
 */
void audyn_aes_input_get_stats(const audyn_aes_input_t *in, audyn_aes_stats_t *stats);

/*
 * Set the PTP clock for packet timestamping.
 *
 * Must be called before audyn_aes_input_start().
 * The PTP clock is NOT owned by the aes_input - caller manages its lifetime.
 */
void audyn_aes_input_set_ptp_clock(audyn_aes_input_t *in, audyn_ptp_clock_t *clk);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_AES_INPUT_H */
