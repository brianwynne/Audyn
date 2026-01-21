#define _GNU_SOURCE

/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      aes_input.c
 *
 *  Purpose:
 *      AES67 / RTP audio input implementation with PTP timestamping support.
 *
 *      Receives AES67-compatible RTP packets carrying uncompressed linear PCM
 *      and converts them into Audyn audio frames (interleaved float PCM),
 *      pushing frames into an audyn_audio_queue_t for downstream processing.
 *
 *  Supported PCM payloads:
 *      - L16: 16-bit signed PCM, big-endian network order (2 bytes/sample)
 *      - L24: 24-bit signed PCM, big-endian network order (3 bytes/sample)
 *
 *      The input infers L16 vs L24 by comparing payload length to
 *      (channels * samples_per_packet * bytes_per_sample).
 *
 *  PTP Support:
 *      - Hardware timestamps via SO_TIMESTAMPING (requires network driver support)
 *      - Software timestamps via CLOCK_REALTIME (fallback)
 *      - Optional jitter buffer for packet reordering
 *      - RTP-to-PTP timestamp correlation
 *
 *  Design Guarantees:
 *      - No hard-coded multicast addresses/ports: configured at runtime via cfg.
 *      - Dedicated non-real-time receiver thread.
 *      - Bounded behavior: if queue is full or pool is exhausted, audio is dropped
 *        and counters are incremented.
 *      - Robust RTP header parsing (CSRC, extension, padding).
 *
 *  Dependencies:
 *      - POSIX sockets + pthread
 *      - Audyn core: frame_pool, audio_queue, ptp_clock, jitter_buffer
 *      - Audyn core logging: core/log.h
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

#include "aes_input.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <pthread.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#ifdef __linux__
#include <sys/types.h>
#include <linux/net_tstamp.h>
#include <linux/errqueue.h>
#endif

#include "log.h"
#include "ptp_clock.h"
#include "jitter_buffer.h"

/* -------- Limits -------- */

/* Sample rate limits (must match worker.h) */
#define AES_MAX_SAMPLE_RATE 384000

/* Maximum channel count */
#define AES_MAX_CHANNELS 32

/* Maximum samples per packet (AES67 allows up to 48 for 1ms at 48kHz) */
#define AES_MAX_SAMPLES_PER_PACKET 1024

/* -------- RTP parsing helpers -------- */

#define RTP_MIN_HEADER_BYTES 12U
#define RTP_VERSION_EXPECTED 2U

static inline uint16_t rd_be16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] << 8) | (uint16_t)p[1];
}

static inline int32_t rd_be24s(const uint8_t *p) {
    /* 24-bit big-endian signed -> sign-extended int32 */
    int32_t v = ((int32_t)p[0] << 16) | ((int32_t)p[1] << 8) | (int32_t)p[2];
    if (v & 0x00800000) v |= 0xFF000000;
    return v;
}

/* Convert PCM integers to float in [-1, 1) */
static inline float s16_to_f32(int16_t s) {
    return (float)s / 32768.0f;
}
static inline float s24_to_f32(int32_t s) {
    return (float)s / 8388608.0f;
}

/* Opaque instance */
struct audyn_aes_input {
    audyn_frame_pool_t   *pool;
    audyn_audio_queue_t  *queue;
    audyn_aes_input_cfg_t cfg;

    /* Owned copies of config strings */
    char *source_ip;
    char *bind_interface;

    int sock_fd;

    pthread_t thread;
    int thread_started;

    pthread_mutex_t err_mu;
    char last_error[256];

    pthread_mutex_t state_mu;
    int stop_requested;

    /* PTP timestamping */
    audyn_ptp_clock_t *ptp_clk;         /* Optional PTP clock (not owned) */
    int hw_timestamps_enabled;          /* 1 if SO_TIMESTAMPING succeeded */
    int ptp_epoch_set;                  /* 1 if RTP epoch has been set */

    /* Continuity tracking */
    int have_seq;
    uint16_t expected_seq;

    /* Counters */
    uint64_t packets_rx;
    uint64_t packets_dropped;
    uint64_t discontinuities;
    uint64_t frames_pushed;
    uint64_t frames_dropped_pool_empty;
    uint64_t frames_dropped_queue_full;
};

static void set_error(audyn_aes_input_t *in, const char *msg) {
    if (!in || !msg) return;
    pthread_mutex_lock(&in->err_mu);
    (void)snprintf(in->last_error, sizeof(in->last_error), "%s", msg);
    pthread_mutex_unlock(&in->err_mu);
}

static void set_error_errno(audyn_aes_input_t *in, const char *prefix) {
    if (!in || !prefix) return;
    pthread_mutex_lock(&in->err_mu);
    (void)snprintf(in->last_error, sizeof(in->last_error), "%s: %s", prefix, strerror(errno));
    pthread_mutex_unlock(&in->err_mu);
}

static int stop_is_requested(audyn_aes_input_t *in) {
    int v = 0;
    pthread_mutex_lock(&in->state_mu);
    v = in->stop_requested;
    pthread_mutex_unlock(&in->state_mu);
    return v;
}

static void request_stop(audyn_aes_input_t *in) {
    pthread_mutex_lock(&in->state_mu);
    in->stop_requested = 1;
    pthread_mutex_unlock(&in->state_mu);
}

/* Detect IPv4 multicast address (224.0.0.0/4). */
static int is_ipv4_multicast(const char *ip_str) {
    struct in_addr a;
    if (!ip_str) return 0;
    if (inet_pton(AF_INET, ip_str, &a) != 1) return 0;
    uint32_t host = ntohl(a.s_addr);
    return (host >= 0xE0000000u) && (host <= 0xEFFFFFFFu);
}

static int open_socket(audyn_aes_input_t *in) {
    if (!in) return -1;

    const char *src_ip = in->source_ip;
    uint16_t port = in->cfg.port;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        set_error_errno(in, "socket()");
        return -1;
    }

    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (in->cfg.socket_rcvbuf > 0) {
        int rcv = (int)in->cfg.socket_rcvbuf;
        if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv)) != 0) {
            /* Non-fatal */
            LOG_ERROR("aes_input: failed to set SO_RCVBUF=%d: %s", rcv, strerror(errno));
        }
    }

    /* Set receive timeout for clean shutdown (100ms) */
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 100000;
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

#ifdef __linux__
    /* Enable hardware timestamping if PTP clock is configured */
    if (in->ptp_clk && audyn_ptp_clock_mode(in->ptp_clk) == AUDYN_PTP_MODE_HARDWARE) {
        int ts_flags = SOF_TIMESTAMPING_RX_HARDWARE |
                       SOF_TIMESTAMPING_RAW_HARDWARE |
                       SOF_TIMESTAMPING_SOFTWARE;
        if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &ts_flags, sizeof(ts_flags)) == 0) {
            in->hw_timestamps_enabled = 1;
            LOG_INFO("aes_input: Hardware timestamping enabled");
        } else {
            LOG_ERROR("aes_input: SO_TIMESTAMPING failed: %s (falling back to software)", strerror(errno));
            in->hw_timestamps_enabled = 0;
        }
    } else if (in->ptp_clk && audyn_ptp_clock_mode(in->ptp_clk) == AUDYN_PTP_MODE_SOFTWARE) {
        /* Software mode - use kernel software timestamps */
        int ts_flags = SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
        if (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMPING, &ts_flags, sizeof(ts_flags)) == 0) {
            LOG_INFO("aes_input: Software timestamping enabled");
        }
        in->hw_timestamps_enabled = 0;
    }
#endif

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        set_error_errno(in, "bind()");
        close(fd);
        return -1;
    }

    if (is_ipv4_multicast(src_ip)) {
        struct ip_mreq mreq;
        memset(&mreq, 0, sizeof(mreq));
        if (inet_pton(AF_INET, src_ip, &mreq.imr_multiaddr) != 1) {
            set_error(in, "inet_pton() failed for multicast address");
            close(fd);
            return -1;
        }

        /* Bind multicast to specific interface if configured */
        if (in->bind_interface && in->bind_interface[0] != '\0') {
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, in->bind_interface, IFNAMSIZ - 1);

            if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
                LOG_ERROR("aes_input: failed to get IP for interface '%s': %s",
                          in->bind_interface, strerror(errno));
                set_error_errno(in, "ioctl(SIOCGIFADDR)");
                close(fd);
                return -1;
            }

            struct sockaddr_in *ifaddr = (struct sockaddr_in *)&ifr.ifr_addr;
            mreq.imr_interface.s_addr = ifaddr->sin_addr.s_addr;
            LOG_INFO("aes_input: binding multicast to interface '%s' (%s)",
                     in->bind_interface, inet_ntoa(ifaddr->sin_addr));
        } else {
            mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        }

        if (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) != 0) {
            set_error_errno(in, "IP_ADD_MEMBERSHIP");
            close(fd);
            return -1;
        }
    }

    in->sock_fd = fd;
    return 0;
}

static int handle_packet(audyn_aes_input_t *in, const uint8_t *pkt, size_t len, uint64_t arrival_ns) {
    if (!in || !pkt) return -1;

    if (len < RTP_MIN_HEADER_BYTES) {
        in->packets_dropped++;
        return 0;
    }

    /* RTP fixed header */
    const uint8_t b0 = pkt[0];
    const uint8_t b1 = pkt[1];

    const uint8_t version = (uint8_t)(b0 >> 6);
    const uint8_t padding = (uint8_t)((b0 >> 5) & 0x1);
    const uint8_t extension = (uint8_t)((b0 >> 4) & 0x1);
    const uint8_t csrc_count = (uint8_t)(b0 & 0x0F);

    const uint8_t payload_type = (uint8_t)(b1 & 0x7F);

    if (version != RTP_VERSION_EXPECTED) {
        in->packets_dropped++;
        return 0;
    }

    if (payload_type != in->cfg.payload_type) {
        in->packets_dropped++;
        return 0;
    }

    size_t off = RTP_MIN_HEADER_BYTES;

    /* CSRC list */
    size_t csrc_bytes = (size_t)csrc_count * 4U;
    if (len < off + csrc_bytes) {
        in->packets_dropped++;
        return 0;
    }
    off += csrc_bytes;

    /* Header extension */
    if (extension) {
        if (len < off + 4U) {
            in->packets_dropped++;
            return 0;
        }
        uint16_t ext_len_words = rd_be16(pkt + off + 2);
        off += 4U;
        size_t ext_bytes = (size_t)ext_len_words * 4U;
        if (len < off + ext_bytes) {
            in->packets_dropped++;
            return 0;
        }
        off += ext_bytes;
    }

    /* Padding: last byte indicates padding count */
    size_t payload_len = len - off;
    if (padding) {
        if (payload_len == 0) {
            in->packets_dropped++;
            return 0;
        }
        uint8_t pad_count = pkt[len - 1];
        if (pad_count == 0 || pad_count > payload_len) {
            in->packets_dropped++;
            return 0;
        }
        payload_len -= pad_count;
    }

    /* Continuity checks */
    uint16_t seq = rd_be16(pkt + 2);
    uint32_t rtp_ts = ((uint32_t)pkt[4] << 24) | ((uint32_t)pkt[5] << 16) |
                      ((uint32_t)pkt[6] << 8) | (uint32_t)pkt[7];

    /* Set RTP epoch on first packet with valid PTP time */
    if (in->ptp_clk && !in->ptp_epoch_set && arrival_ns > 0) {
        audyn_ptp_set_rtp_epoch(in->ptp_clk, rtp_ts, arrival_ns, in->cfg.sample_rate);
        in->ptp_epoch_set = 1;
        LOG_DEBUG("aes_input: Set RTP epoch - rtp_ts=%u arrival_ns=%lu", rtp_ts, (unsigned long)arrival_ns);
    }

    if (!in->have_seq) {
        in->have_seq = 1;
        in->expected_seq = (uint16_t)(seq + 1);
    } else {
        if (seq != in->expected_seq) {
            in->discontinuities++;
            in->expected_seq = (uint16_t)(seq + 1);
        } else {
            in->expected_seq = (uint16_t)(in->expected_seq + 1);
        }
    }

    const uint16_t out_ch = in->cfg.channels;           /* Output channels */
    const uint16_t spp = in->cfg.samples_per_packet;

    if (out_ch == 0 || spp == 0) {
        in->packets_dropped++;
        return 0;
    }

    /* Stream channels: number of channels in the incoming RTP stream.
     * If stream_channels is 0, assume same as output channels. */
    const uint16_t stream_ch = (in->cfg.stream_channels > 0)
                                 ? in->cfg.stream_channels
                                 : out_ch;
    const uint16_t ch_offset = in->cfg.channel_offset;

    /* Validate channel selection */
    if (ch_offset + out_ch > stream_ch) {
        /* Channel selection out of range - only log once */
        static int logged = 0;
        if (!logged) {
            LOG_ERROR("aes_input: channel selection out of range: offset=%u + channels=%u > stream_channels=%u",
                      ch_offset, out_ch, stream_ch);
            logged = 1;
        }
        in->packets_dropped++;
        return 0;
    }

    /* Infer L16 vs L24 from payload length using stream_channels */
    enum { FMT_L16 = 16, FMT_L24 = 24 } fmt = 0;
    size_t bytes_per_sample = 0;
    size_t exp_l16 = (size_t)stream_ch * (size_t)spp * 2U;
    size_t exp_l24 = (size_t)stream_ch * (size_t)spp * 3U;

    if (payload_len == exp_l16) {
        fmt = FMT_L16;
        bytes_per_sample = 2U;
    } else if (payload_len == exp_l24) {
        fmt = FMT_L24;
        bytes_per_sample = 3U;
    } else {
        in->packets_dropped++;
        return 0;
    }

    audyn_audio_frame_t *frame = audyn_frame_acquire(in->pool);
    if (!frame) {
        in->frames_dropped_pool_empty++;
        return 0;
    }

    /* Validate the frame shape matches output config. */
    if (frame->channels != out_ch || frame->data == NULL || frame->sample_frames < (uint32_t)spp) {
        audyn_frame_release(frame);
        set_error(in, "frame_pool returned incompatible frame shape");
        return -1;
    }

    frame->sample_frames = (uint32_t)spp;

    const uint8_t *p = pkt + off;

    /* Payload is interleaved by channel per AES67 PCM conventions.
     * Extract only selected channels from the stream. */
    for (uint32_t i = 0; i < (uint32_t)spp; i++) {
        for (uint32_t c = 0; c < (uint32_t)out_ch; c++) {
            /* Index into the incoming stream at (sample i, channel ch_offset + c) */
            size_t stream_idx = ((size_t)i * (size_t)stream_ch + (size_t)(ch_offset + c)) * bytes_per_sample;
            float outv;
            if (fmt == FMT_L16) {
                int16_t s = (int16_t)rd_be16(p + stream_idx);
                outv = s16_to_f32(s);
            } else {
                int32_t s = rd_be24s(p + stream_idx);
                outv = s24_to_f32(s);
            }
            frame->data[AUDYN_PCM_IDX(i, c, out_ch)] = outv;
        }
    }

    if (!audyn_audio_queue_push(in->queue, frame)) {
        in->frames_dropped_queue_full++;
        audyn_frame_release(frame);
        return 0;
    }

    in->frames_pushed++;
    return 0;
}

/* Extract timestamp from control message */
static uint64_t extract_timestamp(struct msghdr *msg, audyn_aes_input_t *in) {
    uint64_t ts_ns = 0;

#ifdef __linux__
    struct cmsghdr *cmsg;
    for (cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL; cmsg = CMSG_NXTHDR(msg, cmsg)) {
        if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SO_TIMESTAMPING) {
            struct timespec *ts = (struct timespec *)CMSG_DATA(cmsg);
            /* ts[0] = software, ts[1] = deprecated, ts[2] = hardware */
            if (in->hw_timestamps_enabled && (ts[2].tv_sec != 0 || ts[2].tv_nsec != 0)) {
                /* Use hardware timestamp */
                ts_ns = (uint64_t)ts[2].tv_sec * 1000000000ULL + (uint64_t)ts[2].tv_nsec;
            } else if (ts[0].tv_sec != 0 || ts[0].tv_nsec != 0) {
                /* Use software timestamp */
                ts_ns = (uint64_t)ts[0].tv_sec * 1000000000ULL + (uint64_t)ts[0].tv_nsec;
            }
            break;
        }
    }
#else
    (void)msg;
    (void)in;
#endif

    /* Fallback to PTP clock if no timestamp from socket */
    if (ts_ns == 0 && in->ptp_clk) {
        ts_ns = audyn_ptp_clock_now_ns(in->ptp_clk);
    }

    return ts_ns;
}

static void *rx_thread_main(void *arg) {
    audyn_aes_input_t *in = (audyn_aes_input_t *)arg;
    if (!in) return NULL;

#ifdef __linux__
    (void)pthread_setname_np(pthread_self(), "audyn-aes-rx");
#endif

    uint8_t buf[4096];
    uint8_t ctrl_buf[256];
    struct iovec iov;
    struct msghdr msg;

    iov.iov_base = buf;
    iov.iov_len = sizeof(buf);

    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl_buf;
    msg.msg_controllen = sizeof(ctrl_buf);

    while (!stop_is_requested(in)) {
        /* Reset control message buffer for each receive */
        msg.msg_controllen = sizeof(ctrl_buf);
        msg.msg_flags = 0;

        ssize_t n = recvmsg(in->sock_fd, &msg, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (stop_is_requested(in)) break;
            if (errno == EAGAIN || errno == EWOULDBLOCK) continue;

            set_error_errno(in, "recvmsg()");
            LOG_ERROR("aes_input: recvmsg error: %s", strerror(errno));
            usleep(10 * 1000);
            continue;
        }
        if (n == 0) continue;

        /* Extract timestamp from control messages */
        uint64_t arrival_ns = extract_timestamp(&msg, in);

        in->packets_rx++;
        if (handle_packet(in, buf, (size_t)n, arrival_ns) != 0) {
            LOG_ERROR("aes_input: fatal packet handling error: %s",
                      audyn_aes_input_last_error(in) ? audyn_aes_input_last_error(in) : "unknown");
            request_stop(in);
            break;
        }
    }

    return NULL;
}

/* -------- Public API -------- */

audyn_aes_input_t *
audyn_aes_input_create(audyn_frame_pool_t *pool,
                       audyn_audio_queue_t *queue,
                       const audyn_aes_input_cfg_t *cfg)
{
    if (!pool || !queue || !cfg) {
        LOG_ERROR("aes_input: NULL pool, queue, or config");
        return NULL;
    }
    if (!cfg->source_ip || cfg->source_ip[0] == '\0') {
        LOG_ERROR("aes_input: NULL or empty source_ip");
        return NULL;
    }
    if (cfg->port == 0) {
        LOG_ERROR("aes_input: port must be non-zero");
        return NULL;
    }
    if (cfg->sample_rate == 0 || cfg->sample_rate > AES_MAX_SAMPLE_RATE) {
        LOG_ERROR("aes_input: invalid sample_rate %u (must be 1-%u)",
                  cfg->sample_rate, AES_MAX_SAMPLE_RATE);
        return NULL;
    }
    if (cfg->channels == 0 || cfg->channels > AES_MAX_CHANNELS) {
        LOG_ERROR("aes_input: invalid channels %u (must be 1-%u)",
                  cfg->channels, AES_MAX_CHANNELS);
        return NULL;
    }

    /* Validate channel selection configuration */
    uint16_t stream_ch = (cfg->stream_channels > 0) ? cfg->stream_channels : cfg->channels;
    if (stream_ch > AES_MAX_CHANNELS) {
        LOG_ERROR("aes_input: invalid stream_channels %u (must be 1-%u)",
                  cfg->stream_channels, AES_MAX_CHANNELS);
        return NULL;
    }
    if (cfg->channel_offset + cfg->channels > stream_ch) {
        LOG_ERROR("aes_input: channel selection out of range: offset=%u + channels=%u > stream_channels=%u",
                  cfg->channel_offset, cfg->channels, stream_ch);
        return NULL;
    }
    if (cfg->samples_per_packet == 0 || cfg->samples_per_packet > AES_MAX_SAMPLES_PER_PACKET) {
        LOG_ERROR("aes_input: invalid samples_per_packet %u (must be 1-%u)",
                  cfg->samples_per_packet, AES_MAX_SAMPLES_PER_PACKET);
        return NULL;
    }
    if (cfg->payload_type > 127) {
        LOG_ERROR("aes_input: invalid payload_type %u (must be 0-127)",
                  cfg->payload_type);
        return NULL;
    }

    audyn_aes_input_t *in = (audyn_aes_input_t *)calloc(1, sizeof(*in));
    if (!in) {
        LOG_ERROR("aes_input: failed to allocate structure");
        return NULL;
    }

    in->pool = pool;
    in->queue = queue;
    in->cfg = *cfg;

    /* Make owned copies of config strings */
    in->source_ip = strdup(cfg->source_ip);
    if (!in->source_ip) {
        LOG_ERROR("aes_input: failed to allocate source_ip");
        free(in);
        return NULL;
    }

    if (cfg->bind_interface && cfg->bind_interface[0] != '\0') {
        in->bind_interface = strdup(cfg->bind_interface);
        if (!in->bind_interface) {
            LOG_ERROR("aes_input: failed to allocate bind_interface");
            free(in->source_ip);
            free(in);
            return NULL;
        }
    } else {
        in->bind_interface = NULL;
    }

    in->sock_fd = -1;
    in->thread_started = 0;

    if (pthread_mutex_init(&in->err_mu, NULL) != 0) {
        LOG_ERROR("aes_input: failed to initialize error mutex");
        free(in->bind_interface);
        free(in->source_ip);
        free(in);
        return NULL;
    }

    if (pthread_mutex_init(&in->state_mu, NULL) != 0) {
        LOG_ERROR("aes_input: failed to initialize state mutex");
        pthread_mutex_destroy(&in->err_mu);
        free(in->bind_interface);
        free(in->source_ip);
        free(in);
        return NULL;
    }

    in->stop_requested = 0;
    in->last_error[0] = '\0';
    in->have_seq = 0;

    if (in->cfg.stream_channels > 0 && in->cfg.stream_channels != in->cfg.channels) {
        LOG_INFO("aes_input: created (%s:%u PT=%u rate=%u ch=%u spp=%u stream_ch=%u offset=%u)",
                 in->source_ip, (unsigned)in->cfg.port, (unsigned)in->cfg.payload_type,
                 (unsigned)in->cfg.sample_rate, (unsigned)in->cfg.channels,
                 (unsigned)in->cfg.samples_per_packet,
                 (unsigned)in->cfg.stream_channels, (unsigned)in->cfg.channel_offset);
    } else {
        LOG_INFO("aes_input: created (%s:%u PT=%u rate=%u ch=%u spp=%u)",
                 in->source_ip, (unsigned)in->cfg.port, (unsigned)in->cfg.payload_type,
                 (unsigned)in->cfg.sample_rate, (unsigned)in->cfg.channels,
                 (unsigned)in->cfg.samples_per_packet);
    }

    return in;
}

int audyn_aes_input_start(audyn_aes_input_t *in) {
    if (!in) return -1;
    if (in->thread_started) return 0;

    pthread_mutex_lock(&in->state_mu);
    in->stop_requested = 0;
    pthread_mutex_unlock(&in->state_mu);

    if (open_socket(in) != 0) return -1;

    int rc = pthread_create(&in->thread, NULL, rx_thread_main, in);
    if (rc != 0) {
        errno = rc;
        set_error_errno(in, "pthread_create()");
        close(in->sock_fd);
        in->sock_fd = -1;
        return -1;
    }

    in->thread_started = 1;

    LOG_INFO("aes_input: started (%s:%u PT=%u rate=%u ch=%u spp=%u)",
             in->source_ip, (unsigned)in->cfg.port, (unsigned)in->cfg.payload_type,
             (unsigned)in->cfg.sample_rate, (unsigned)in->cfg.channels, (unsigned)in->cfg.samples_per_packet);

    return 0;
}

void audyn_aes_input_stop(audyn_aes_input_t *in) {
    if (!in) return;

    if (!in->thread_started) {
        if (in->sock_fd >= 0) {
            close(in->sock_fd);
            in->sock_fd = -1;
        }
        return;
    }

    request_stop(in);

    if (in->sock_fd >= 0) {
        close(in->sock_fd);
        in->sock_fd = -1;
    }

    (void)pthread_join(in->thread, NULL);
    in->thread_started = 0;

    LOG_INFO("aes_input: stopped (rx=%llu dropped=%llu disc=%llu pool_drop=%llu q_drop=%llu pushed=%llu)",
             (unsigned long long)in->packets_rx,
             (unsigned long long)in->packets_dropped,
             (unsigned long long)in->discontinuities,
             (unsigned long long)in->frames_dropped_pool_empty,
             (unsigned long long)in->frames_dropped_queue_full,
             (unsigned long long)in->frames_pushed);
}

void audyn_aes_input_destroy(audyn_aes_input_t *in) {
    if (!in) return;
    audyn_aes_input_stop(in);
    pthread_mutex_destroy(&in->err_mu);
    pthread_mutex_destroy(&in->state_mu);
    free(in->bind_interface);
    free(in->source_ip);
    free(in);
}

const char *audyn_aes_input_last_error(const audyn_aes_input_t *in) {
    if (!in) return NULL;
    /* Note: This returns a pointer that may be invalidated if error changes.
     * Use audyn_aes_input_get_last_error() for thread-safe access. */
    pthread_mutex_lock((pthread_mutex_t *)&in->err_mu);
    const char *msg = in->last_error[0] ? in->last_error : NULL;
    pthread_mutex_unlock((pthread_mutex_t *)&in->err_mu);
    return msg;
}

void audyn_aes_input_set_ptp_clock(audyn_aes_input_t *in, audyn_ptp_clock_t *clk) {
    if (!in) return;
    if (in->thread_started) {
        LOG_ERROR("aes_input: Cannot set PTP clock after start");
        return;
    }
    in->ptp_clk = clk;
    in->ptp_epoch_set = 0;

    if (clk) {
        audyn_ptp_mode_t mode = audyn_ptp_clock_mode(clk);
        const char *mode_str = (mode == AUDYN_PTP_MODE_HARDWARE) ? "HARDWARE" :
                               (mode == AUDYN_PTP_MODE_SOFTWARE) ? "SOFTWARE" : "NONE";
        LOG_INFO("aes_input: PTP clock set (mode=%s)", mode_str);
    }
}

int audyn_aes_input_is_running(const audyn_aes_input_t *in) {
    if (!in) return 0;
    return in->thread_started;
}

void audyn_aes_input_get_last_error(const audyn_aes_input_t *in, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return;
    buf[0] = '\0';

    if (!in) {
        strncpy(buf, "no input", buflen - 1);
        buf[buflen - 1] = '\0';
        return;
    }

    /* Cast is safe because we're only reading and the mutex protects access */
    pthread_mutex_lock((pthread_mutex_t *)&in->err_mu);
    strncpy(buf, in->last_error[0] ? in->last_error : "ok", buflen - 1);
    buf[buflen - 1] = '\0';
    pthread_mutex_unlock((pthread_mutex_t *)&in->err_mu);
}

void audyn_aes_input_get_stats(const audyn_aes_input_t *in, audyn_aes_stats_t *stats) {
    if (!stats) return;

    if (!in) {
        memset(stats, 0, sizeof(*stats));
        return;
    }

    stats->packets_rx = in->packets_rx;
    stats->packets_dropped = in->packets_dropped;
    stats->discontinuities = in->discontinuities;
    stats->frames_pushed = in->frames_pushed;
    stats->frames_dropped_pool = in->frames_dropped_pool_empty;
    stats->frames_dropped_queue = in->frames_dropped_queue_full;
}
