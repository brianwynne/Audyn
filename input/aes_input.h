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

#include "frame_pool.h"
#include "audio_queue.h"
#include "ptp_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audyn_aes_input_cfg {
    const char *source_ip;
    uint16_t    port;
    uint8_t     payload_type;

    uint32_t    sample_rate;
    uint16_t    channels;
    uint16_t    samples_per_packet;

    uint32_t    socket_rcvbuf;

    const char *bind_interface;     /* Network interface for multicast (e.g., "eth0") */
} audyn_aes_input_cfg_t;

typedef struct audyn_aes_input audyn_aes_input_t;

audyn_aes_input_t *
audyn_aes_input_create(audyn_frame_pool_t *pool,
                       audyn_audio_queue_t *queue,
                       const audyn_aes_input_cfg_t *cfg);

int audyn_aes_input_start(audyn_aes_input_t *in);
void audyn_aes_input_stop(audyn_aes_input_t *in);
void audyn_aes_input_destroy(audyn_aes_input_t *in);
const char *audyn_aes_input_last_error(audyn_aes_input_t *in);

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
