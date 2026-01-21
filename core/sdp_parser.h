/*
 *  Audyn - Professional Audio Capture & Archival Engine
 *
 *  File:
 *      sdp_parser.h
 *
 *  Purpose:
 *      SDP (Session Description Protocol) parser for AES67 streams.
 *
 *      Parses SDP payloads from SAP announcements to extract
 *      stream parameters: multicast address, port, channels,
 *      sample rate, and channel labels.
 *
 *  Standards:
 *      - RFC 4566 (SDP)
 *      - RFC 3190 (RTP Payload for L16/L24 Audio)
 *      - AES67-2018
 *      - SMPTE ST 2110-30
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#ifndef AUDYN_SDP_PARSER_H
#define AUDYN_SDP_PARSER_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum values */
#define SDP_MAX_CHANNELS        64
#define SDP_MAX_NAME_LEN        128
#define SDP_MAX_LABEL_LEN       32
#define SDP_MAX_ADDR_LEN        64

/*
 * Audio encoding format
 */
typedef enum {
    SDP_ENCODING_UNKNOWN = 0,
    SDP_ENCODING_L16,       /* 16-bit linear PCM */
    SDP_ENCODING_L24,       /* 24-bit linear PCM */
    SDP_ENCODING_L32,       /* 32-bit linear PCM */
    SDP_ENCODING_AM824      /* AES3 transparent (SMPTE ST 2110-31) */
} sdp_encoding_t;

/*
 * Channel information
 */
typedef struct {
    int     index;                          /* Channel index (0-based) */
    char    label[SDP_MAX_LABEL_LEN];       /* Channel label (e.g., "Prog L") */
} sdp_channel_t;

/*
 * Parsed SDP stream information
 */
typedef struct {
    /* Session info */
    char        session_name[SDP_MAX_NAME_LEN];     /* s= field */
    char        session_id[SDP_MAX_NAME_LEN];       /* From o= field */
    uint64_t    session_version;                     /* From o= field */

    /* Origin info */
    char        origin_username[SDP_MAX_NAME_LEN];
    char        origin_address[SDP_MAX_ADDR_LEN];

    /* Connection info */
    char        multicast_addr[SDP_MAX_ADDR_LEN];   /* c= field */
    int         ttl;                                 /* TTL from c= field */

    /* Source filter (SSM) */
    char        source_addr[SDP_MAX_ADDR_LEN];      /* Source IP for SSM */
    int         is_ssm;                              /* Source-specific multicast */

    /* Media info */
    uint16_t    port;                               /* m= field */
    uint8_t     payload_type;                       /* RTP payload type */
    sdp_encoding_t encoding;                        /* L16, L24, etc. */
    uint32_t    sample_rate;                        /* e.g., 48000 */
    uint16_t    channels;                           /* Number of channels */
    float       ptime;                              /* Packet time in ms */
    uint16_t    samples_per_packet;                 /* Calculated from ptime */

    /* Channel labels (from a=extmap or a=mid) */
    sdp_channel_t channel_info[SDP_MAX_CHANNELS];
    int         has_channel_labels;

    /* PTP/Media clock */
    char        mediaclk[SDP_MAX_NAME_LEN];         /* a=mediaclk */
    char        ts_refclk[SDP_MAX_NAME_LEN];        /* a=ts-refclk */

    /* Timing */
    time_t      last_seen;                          /* When this SDP was last received */
    int         valid;                              /* Parsing succeeded */
} sdp_stream_t;

/*
 * Parse an SDP string into a stream structure.
 *
 * Parameters:
 *   sdp     - SDP text (null-terminated)
 *   len     - Length of SDP text (or 0 to use strlen)
 *   stream  - Output stream structure
 *
 * Returns:
 *   0 on success, -1 on parse error
 */
int sdp_parse(const char *sdp, size_t len, sdp_stream_t *stream);

/*
 * Get encoding name as string.
 */
const char *sdp_encoding_name(sdp_encoding_t enc);

/*
 * Get bit depth for encoding.
 */
int sdp_encoding_bits(sdp_encoding_t enc);

/*
 * Print stream info to buffer (for debugging/logging).
 *
 * Returns number of characters written (excluding null terminator).
 */
int sdp_stream_to_string(const sdp_stream_t *stream, char *buf, size_t buflen);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_SDP_PARSER_H */
