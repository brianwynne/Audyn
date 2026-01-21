/*
 *  Audyn - Professional Audio Capture & Archival Engine
 *
 *  File:
 *      sdp_parser.c
 *
 *  Purpose:
 *      SDP (Session Description Protocol) parser implementation.
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#include "sdp_parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* Safe string copy */
static void safe_strcpy(char *dst, const char *src, size_t dstsize)
{
    if (dstsize == 0) return;
    strncpy(dst, src, dstsize - 1);
    dst[dstsize - 1] = '\0';
}

/* Trim whitespace from end of string */
static void trim_end(char *s)
{
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == ' ' || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
}

/* Parse o= line: o=<username> <sess-id> <sess-version> <nettype> <addrtype> <addr> */
static void parse_origin(const char *line, sdp_stream_t *stream)
{
    char username[SDP_MAX_NAME_LEN] = {0};
    char sess_id[SDP_MAX_NAME_LEN] = {0};
    uint64_t sess_version = 0;
    char nettype[16] = {0};
    char addrtype[16] = {0};
    char addr[SDP_MAX_ADDR_LEN] = {0};

    if (sscanf(line, "%127s %127s %lu %15s %15s %63s",
               username, sess_id, &sess_version, nettype, addrtype, addr) >= 6) {
        safe_strcpy(stream->origin_username, username, sizeof(stream->origin_username));
        safe_strcpy(stream->session_id, sess_id, sizeof(stream->session_id));
        stream->session_version = sess_version;
        safe_strcpy(stream->origin_address, addr, sizeof(stream->origin_address));
    }
}

/* Parse c= line: c=<nettype> <addrtype> <connection-address>[/<ttl>][/<num-addrs>] */
static void parse_connection(const char *line, sdp_stream_t *stream)
{
    char nettype[16] = {0};
    char addrtype[16] = {0};
    char addr[SDP_MAX_ADDR_LEN] = {0};
    int ttl = 0;

    /* Try with TTL first */
    if (sscanf(line, "%15s %15s %63[^/]/%d", nettype, addrtype, addr, &ttl) >= 3) {
        safe_strcpy(stream->multicast_addr, addr, sizeof(stream->multicast_addr));
        stream->ttl = ttl;
    }
}

/* Parse m= line: m=audio <port> RTP/AVP <fmt> */
static void parse_media(const char *line, sdp_stream_t *stream)
{
    char media[32] = {0};
    int port = 0;
    char proto[32] = {0};
    int fmt = 0;

    if (sscanf(line, "%31s %d %31s %d", media, &port, proto, &fmt) >= 4) {
        if (strcmp(media, "audio") == 0) {
            stream->port = (uint16_t)port;
            stream->payload_type = (uint8_t)fmt;
        }
    }
}

/* Parse a=rtpmap line: a=rtpmap:<pt> <encoding>/<clock>[/<channels>] */
static void parse_rtpmap(const char *line, sdp_stream_t *stream)
{
    int pt = 0;
    char encoding[32] = {0};
    int clock_rate = 0;
    int channels = 1;

    /* Try with channels */
    if (sscanf(line, "%d %31[^/]/%d/%d", &pt, encoding, &clock_rate, &channels) >= 3) {
        if (pt == stream->payload_type || stream->payload_type == 0) {
            stream->payload_type = (uint8_t)pt;
            stream->sample_rate = (uint32_t)clock_rate;
            stream->channels = (uint16_t)channels;

            /* Determine encoding type */
            if (strcasecmp(encoding, "L16") == 0) {
                stream->encoding = SDP_ENCODING_L16;
            } else if (strcasecmp(encoding, "L24") == 0) {
                stream->encoding = SDP_ENCODING_L24;
            } else if (strcasecmp(encoding, "L32") == 0) {
                stream->encoding = SDP_ENCODING_L32;
            } else if (strcasecmp(encoding, "AM824") == 0) {
                stream->encoding = SDP_ENCODING_AM824;
            }
        }
    }
}

/* Parse a=ptime line */
static void parse_ptime(const char *line, sdp_stream_t *stream)
{
    float ptime = 0;
    if (sscanf(line, "%f", &ptime) == 1) {
        stream->ptime = ptime;
        /* Calculate samples per packet */
        if (stream->sample_rate > 0 && ptime > 0) {
            stream->samples_per_packet = (uint16_t)(stream->sample_rate * ptime / 1000.0f);
        }
    }
}

/* Parse a=source-filter line for SSM */
static void parse_source_filter(const char *line, sdp_stream_t *stream)
{
    /* Format: incl IN IP4 <dest> <source> */
    char mode[16] = {0};
    char nettype[8] = {0};
    char addrtype[8] = {0};
    char dest[SDP_MAX_ADDR_LEN] = {0};
    char source[SDP_MAX_ADDR_LEN] = {0};

    if (sscanf(line, "%15s %7s %7s %63s %63s", mode, nettype, addrtype, dest, source) >= 5) {
        if (strcmp(mode, "incl") == 0) {
            safe_strcpy(stream->source_addr, source, sizeof(stream->source_addr));
            stream->is_ssm = 1;
        }
    }
}

/* Parse a=mediaclk line */
static void parse_mediaclk(const char *line, sdp_stream_t *stream)
{
    safe_strcpy(stream->mediaclk, line, sizeof(stream->mediaclk));
}

/* Parse a=ts-refclk line */
static void parse_ts_refclk(const char *line, sdp_stream_t *stream)
{
    safe_strcpy(stream->ts_refclk, line, sizeof(stream->ts_refclk));
}

/*
 * Parse channel labels from various formats.
 *
 * Calrec and other manufacturers may use different attribute formats:
 * - a=fmtp:<pt> channel-order=SMPTE2110.(ST)
 * - a=extmap with channel info
 * - Custom attributes
 */
static void parse_channel_labels(const char *line, sdp_stream_t *stream)
{
    /*
     * Look for channel-order in fmtp
     * Example: a=fmtp:96 channel-order=SMPTE2110.(ST,M,M,M,M,M,M,M)
     *
     * ST = Stereo (L,R)
     * M = Mono
     * LFE = Low frequency
     * etc.
     */
    const char *order = strstr(line, "channel-order=");
    if (order) {
        /* Parse the channel order - for now just note we have labels */
        stream->has_channel_labels = 1;

        /* Extract channel configuration */
        const char *start = strchr(order, '(');
        const char *end = start ? strchr(start, ')') : NULL;

        if (start && end) {
            int ch_idx = 0;
            const char *p = start + 1;
            char token[32];
            int token_len = 0;

            while (p < end && ch_idx < SDP_MAX_CHANNELS) {
                if (*p == ',' || p + 1 == end) {
                    if (p + 1 == end && *p != ',') {
                        token[token_len++] = *p;
                    }
                    token[token_len] = '\0';

                    /* Map token to channel label */
                    if (strcmp(token, "ST") == 0) {
                        /* Stereo - creates 2 channels */
                        stream->channel_info[ch_idx].index = ch_idx;
                        snprintf(stream->channel_info[ch_idx].label, SDP_MAX_LABEL_LEN, "L");
                        ch_idx++;
                        if (ch_idx < SDP_MAX_CHANNELS) {
                            stream->channel_info[ch_idx].index = ch_idx;
                            snprintf(stream->channel_info[ch_idx].label, SDP_MAX_LABEL_LEN, "R");
                            ch_idx++;
                        }
                    } else if (strcmp(token, "M") == 0) {
                        stream->channel_info[ch_idx].index = ch_idx;
                        snprintf(stream->channel_info[ch_idx].label, SDP_MAX_LABEL_LEN, "Ch %d", ch_idx + 1);
                        ch_idx++;
                    } else {
                        stream->channel_info[ch_idx].index = ch_idx;
                        safe_strcpy(stream->channel_info[ch_idx].label, token, SDP_MAX_LABEL_LEN);
                        ch_idx++;
                    }

                    token_len = 0;
                } else {
                    if (token_len < 31) {
                        token[token_len++] = *p;
                    }
                }
                p++;
            }
        }
    }
}

int sdp_parse(const char *sdp, size_t len, sdp_stream_t *stream)
{
    if (!sdp || !stream) return -1;

    /* Initialize stream structure */
    memset(stream, 0, sizeof(*stream));
    stream->encoding = SDP_ENCODING_UNKNOWN;
    stream->last_seen = time(NULL);

    /* Use strlen if len not provided */
    if (len == 0) len = strlen(sdp);

    /* Parse line by line */
    const char *p = sdp;
    const char *end = sdp + len;
    char line[1024];
    int in_audio_media = 0;

    while (p < end) {
        /* Find end of line */
        const char *eol = p;
        while (eol < end && *eol != '\n' && *eol != '\r') eol++;

        /* Copy line */
        size_t line_len = eol - p;
        if (line_len >= sizeof(line)) line_len = sizeof(line) - 1;
        memcpy(line, p, line_len);
        line[line_len] = '\0';
        trim_end(line);

        /* Parse based on line type */
        if (line_len >= 2 && line[1] == '=') {
            char type = line[0];
            const char *value = line + 2;

            switch (type) {
                case 'v':
                    /* Version - just validate it's 0 */
                    break;

                case 'o':
                    parse_origin(value, stream);
                    break;

                case 's':
                    safe_strcpy(stream->session_name, value, sizeof(stream->session_name));
                    break;

                case 'c':
                    parse_connection(value, stream);
                    break;

                case 'm':
                    parse_media(value, stream);
                    in_audio_media = (strncmp(value, "audio", 5) == 0);
                    break;

                case 'a':
                    if (in_audio_media || stream->port > 0) {
                        if (strncmp(value, "rtpmap:", 7) == 0) {
                            parse_rtpmap(value + 7, stream);
                        } else if (strncmp(value, "ptime:", 6) == 0) {
                            parse_ptime(value + 6, stream);
                        } else if (strncmp(value, "source-filter:", 14) == 0) {
                            parse_source_filter(value + 14, stream);
                        } else if (strncmp(value, "mediaclk:", 9) == 0) {
                            parse_mediaclk(value + 9, stream);
                        } else if (strncmp(value, "ts-refclk:", 10) == 0) {
                            parse_ts_refclk(value + 10, stream);
                        } else if (strncmp(value, "fmtp:", 5) == 0) {
                            parse_channel_labels(value + 5, stream);
                        }
                    }
                    break;
            }
        }

        /* Move to next line */
        p = eol;
        while (p < end && (*p == '\n' || *p == '\r')) p++;
    }

    /* Validate we got minimum required info */
    if (stream->multicast_addr[0] && stream->port > 0) {
        stream->valid = 1;

        /* Set defaults if not specified */
        if (stream->sample_rate == 0) stream->sample_rate = 48000;
        if (stream->channels == 0) stream->channels = 2;
        if (stream->ptime == 0) stream->ptime = 1.0f;
        if (stream->samples_per_packet == 0) {
            stream->samples_per_packet = (uint16_t)(stream->sample_rate * stream->ptime / 1000.0f);
        }
        if (stream->encoding == SDP_ENCODING_UNKNOWN) {
            stream->encoding = SDP_ENCODING_L24;  /* AES67 default */
        }

        /* Generate default channel labels if none provided */
        if (!stream->has_channel_labels) {
            for (int i = 0; i < stream->channels && i < SDP_MAX_CHANNELS; i++) {
                stream->channel_info[i].index = i;
                snprintf(stream->channel_info[i].label, SDP_MAX_LABEL_LEN, "Ch %d", i + 1);
            }
        }

        return 0;
    }

    return -1;
}

const char *sdp_encoding_name(sdp_encoding_t enc)
{
    switch (enc) {
        case SDP_ENCODING_L16:   return "L16";
        case SDP_ENCODING_L24:   return "L24";
        case SDP_ENCODING_L32:   return "L32";
        case SDP_ENCODING_AM824: return "AM824";
        default:                 return "Unknown";
    }
}

int sdp_encoding_bits(sdp_encoding_t enc)
{
    switch (enc) {
        case SDP_ENCODING_L16:   return 16;
        case SDP_ENCODING_L24:   return 24;
        case SDP_ENCODING_L32:   return 32;
        case SDP_ENCODING_AM824: return 32;
        default:                 return 0;
    }
}

int sdp_stream_to_string(const sdp_stream_t *stream, char *buf, size_t buflen)
{
    if (!stream || !buf || buflen == 0) return 0;

    int n = snprintf(buf, buflen,
        "Stream: %s\n"
        "  Address: %s:%u\n"
        "  Encoding: %s @ %u Hz\n"
        "  Channels: %u\n"
        "  Packet time: %.2f ms (%u samples)\n"
        "  Payload type: %u\n",
        stream->session_name[0] ? stream->session_name : "(unnamed)",
        stream->multicast_addr,
        stream->port,
        sdp_encoding_name(stream->encoding),
        stream->sample_rate,
        stream->channels,
        stream->ptime,
        stream->samples_per_packet,
        stream->payload_type);

    if (stream->is_ssm && n < (int)buflen) {
        n += snprintf(buf + n, buflen - n, "  Source (SSM): %s\n", stream->source_addr);
    }

    if (stream->has_channel_labels && n < (int)buflen) {
        n += snprintf(buf + n, buflen - n, "  Channels:\n");
        for (int i = 0; i < stream->channels && i < SDP_MAX_CHANNELS && n < (int)buflen; i++) {
            n += snprintf(buf + n, buflen - n, "    [%d] %s\n",
                         stream->channel_info[i].index + 1,
                         stream->channel_info[i].label);
        }
    }

    return n;
}
