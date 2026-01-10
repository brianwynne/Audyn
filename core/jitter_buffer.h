/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      jitter_buffer.h
 *
 *  Purpose:
 *      RTP jitter buffer for AES67 packet reordering and timing.
 *
 *      The jitter buffer:
 *        1. Reorders packets that arrive out-of-sequence
 *        2. Handles packet loss gracefully (with silence insertion)
 *        3. Provides timing-correct playout based on RTP timestamps
 *
 *      For AES67:
 *        - Packets are 1ms nominal (48 samples @ 48kHz)
 *        - Jitter buffer depth typically 1-4ms for low latency
 *        - Sequence numbers wrap at 65536
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

#ifndef AUDYN_JITTER_BUFFER_H
#define AUDYN_JITTER_BUFFER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum packet payload size (enough for 48 samples * 8 channels * 3 bytes) */
#define AUDYN_JB_MAX_PAYLOAD 1152

/* Jitter buffer configuration */
typedef struct audyn_jb_cfg {
    uint32_t sample_rate;       /* Audio sample rate (e.g., 48000) */
    uint32_t channels;          /* Number of audio channels */
    uint32_t bits_per_sample;   /* Bits per sample (16 or 24) */
    uint32_t samples_per_packet;/* Samples per RTP packet (e.g., 48) */
    uint32_t depth_ms;          /* Buffer depth in milliseconds (e.g., 4) */
} audyn_jb_cfg_t;

/* Packet entry in jitter buffer */
typedef struct audyn_jb_packet {
    int valid;                  /* 1 if slot contains valid packet */
    uint16_t seq;               /* RTP sequence number */
    uint32_t rtp_ts;            /* RTP timestamp */
    uint64_t arrival_ptp_ns;    /* PTP arrival time (nanoseconds) */
    uint32_t payload_len;       /* Payload length in bytes */
    uint8_t payload[AUDYN_JB_MAX_PAYLOAD];
} audyn_jb_packet_t;

/* Jitter buffer statistics */
typedef struct audyn_jb_stats {
    uint64_t packets_received;  /* Total packets received */
    uint64_t packets_played;    /* Total packets played out */
    uint64_t packets_late;      /* Packets arrived too late */
    uint64_t packets_lost;      /* Packets never received (gaps) */
    uint64_t packets_reordered; /* Packets received out of order */
    uint64_t buffer_overflows;  /* Buffer full when packet arrived */
    int32_t current_depth;      /* Current buffer depth in packets */
    int32_t max_depth;          /* Maximum observed depth */
} audyn_jb_stats_t;

/* Opaque jitter buffer handle */
typedef struct audyn_jitter_buffer audyn_jitter_buffer_t;

/*
 * Create a jitter buffer instance.
 *
 * Returns NULL on failure.
 */
audyn_jitter_buffer_t *audyn_jb_create(const audyn_jb_cfg_t *cfg);

/*
 * Destroy a jitter buffer instance.
 */
void audyn_jb_destroy(audyn_jitter_buffer_t *jb);

/*
 * Insert a packet into the jitter buffer.
 *
 * Parameters:
 *   jb            - Jitter buffer instance
 *   seq           - RTP sequence number
 *   rtp_ts        - RTP timestamp
 *   arrival_ns    - Packet arrival PTP time (nanoseconds)
 *   payload       - Packet audio payload
 *   payload_len   - Payload length in bytes
 *
 * Returns 0 on success, -1 on failure (e.g., buffer full, packet too late).
 */
int audyn_jb_insert(audyn_jitter_buffer_t *jb,
                    uint16_t seq,
                    uint32_t rtp_ts,
                    uint64_t arrival_ns,
                    const uint8_t *payload,
                    uint32_t payload_len);

/*
 * Get the next packet ready for playout.
 *
 * This function returns packets in sequence order. If the next expected
 * packet is missing, it returns NULL and marks the packet as lost.
 *
 * The returned pointer is valid until the next call to audyn_jb_get()
 * or audyn_jb_destroy().
 *
 * Returns packet pointer, or NULL if no packet is ready.
 */
audyn_jb_packet_t *audyn_jb_get(audyn_jitter_buffer_t *jb);

/*
 * Check if a packet is ready for playout.
 *
 * Parameters:
 *   jb            - Jitter buffer instance
 *   current_ns    - Current PTP time in nanoseconds
 *
 * Returns 1 if a packet is ready, 0 otherwise.
 */
int audyn_jb_ready(audyn_jitter_buffer_t *jb, uint64_t current_ns);

/*
 * Reset the jitter buffer.
 *
 * Clears all buffered packets and resets sequence tracking.
 * Call this when starting a new stream or after a major discontinuity.
 */
void audyn_jb_reset(audyn_jitter_buffer_t *jb);

/*
 * Get jitter buffer statistics.
 */
void audyn_jb_get_stats(const audyn_jitter_buffer_t *jb, audyn_jb_stats_t *stats);

/*
 * Get current buffer depth in packets.
 */
int audyn_jb_depth(const audyn_jitter_buffer_t *jb);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_JITTER_BUFFER_H */
