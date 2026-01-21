/*
 *  Audyn - Professional Audio Capture & Archival Engine
 *
 *  File:
 *      sap_discovery.c
 *
 *  Purpose:
 *      SAP (Session Announcement Protocol) listener implementation.
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#include "sap_discovery.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <net/if.h>

/*
 * SAP packet header (RFC 2974)
 *
 *  0                   1                   2                   3
 *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | V=1 |A|R|T|E|C|   auth len    |         msg id hash           |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * :                  originating source (32/128 bits)             :
 * :                                                               :
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                    optional authentication data               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                      optional payload type                    |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * |                                                               |
 * :                          payload (SDP)                        :
 * |                                                               |
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * V = Version (3 bits) - must be 1
 * A = Address type (1 bit) - 0=IPv4, 1=IPv6
 * R = Reserved (1 bit)
 * T = Message type (1 bit) - 0=announcement, 1=deletion
 * E = Encryption (1 bit)
 * C = Compressed (1 bit)
 */

#define SAP_VERSION         1
#define SAP_FLAG_IPV6       0x10
#define SAP_FLAG_DELETE     0x04
#define SAP_FLAG_ENCRYPTED  0x02
#define SAP_FLAG_COMPRESSED 0x01

#define SAP_MIN_PACKET_SIZE 8   /* Minimum SAP header + IPv4 source */
#define SAP_MAX_PACKET_SIZE 8192

struct sap_discovery {
    /* Configuration */
    char            bind_interface[64];
    char            multicast_addr[64];
    uint16_t        port;
    int             timeout_sec;
    sap_callback_fn callback;
    void           *callback_userdata;

    /* Socket */
    int             sock;

    /* Thread */
    pthread_t       thread;
    volatile int    running;
    pthread_mutex_t lock;

    /* Stream database */
    sap_stream_entry_t streams[SAP_MAX_STREAMS];
    int             stream_count;

    /* Statistics */
    sap_stats_t     stats;

    /* Error */
    char            last_error[256];
};

static void set_error(sap_discovery_t *sap, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(sap->last_error, sizeof(sap->last_error), fmt, ap);
    va_end(ap);
}

/*
 * Find or create stream entry by origin IP and message ID hash.
 */
static sap_stream_entry_t *find_or_create_entry(sap_discovery_t *sap,
                                                 const char *origin_ip,
                                                 uint16_t msg_id_hash,
                                                 int *is_new)
{
    *is_new = 0;

    /* First, try to find existing entry */
    for (int i = 0; i < sap->stream_count; i++) {
        if (sap->streams[i].active &&
            sap->streams[i].msg_id_hash == msg_id_hash &&
            strcmp(sap->streams[i].origin_ip, origin_ip) == 0) {
            return &sap->streams[i];
        }
    }

    /* Not found - create new entry */
    if (sap->stream_count >= SAP_MAX_STREAMS) {
        /* Try to find an inactive slot */
        for (int i = 0; i < SAP_MAX_STREAMS; i++) {
            if (!sap->streams[i].active) {
                *is_new = 1;
                return &sap->streams[i];
            }
        }
        return NULL;  /* No room */
    }

    *is_new = 1;
    return &sap->streams[sap->stream_count++];
}

/*
 * Process a received SAP packet.
 */
static void process_sap_packet(sap_discovery_t *sap, const uint8_t *data, size_t len)
{
    if (len < SAP_MIN_PACKET_SIZE) {
        sap->stats.packets_invalid++;
        return;
    }

    /* Parse SAP header */
    uint8_t flags = data[0];
    uint8_t version = (flags >> 5) & 0x07;
    int is_ipv6 = (flags & SAP_FLAG_IPV6) != 0;
    int is_delete = (flags & SAP_FLAG_DELETE) != 0;
    int is_encrypted = (flags & SAP_FLAG_ENCRYPTED) != 0;
    int is_compressed = (flags & SAP_FLAG_COMPRESSED) != 0;

    if (version != SAP_VERSION) {
        sap->stats.packets_invalid++;
        return;
    }

    if (is_encrypted || is_compressed) {
        /* Not supported */
        sap->stats.packets_invalid++;
        return;
    }

    uint8_t auth_len = data[1];
    uint16_t msg_id_hash = (data[2] << 8) | data[3];

    /* Calculate header size */
    size_t addr_size = is_ipv6 ? 16 : 4;
    size_t header_size = 4 + addr_size + (auth_len * 4);

    if (len < header_size) {
        sap->stats.packets_invalid++;
        return;
    }

    /* Extract originating source IP */
    char origin_ip[64];
    if (is_ipv6) {
        struct in6_addr addr;
        memcpy(&addr, data + 4, 16);
        inet_ntop(AF_INET6, &addr, origin_ip, sizeof(origin_ip));
    } else {
        struct in_addr addr;
        memcpy(&addr, data + 4, 4);
        inet_ntop(AF_INET, &addr, origin_ip, sizeof(origin_ip));
    }

    /* Skip optional payload type string (e.g., "application/sdp") */
    const uint8_t *payload = data + header_size;
    size_t payload_len = len - header_size;

    /* Check for payload type string */
    if (payload_len > 0 && payload[0] != 'v') {
        /* Skip payload type string */
        const uint8_t *p = memchr(payload, 0, payload_len);
        if (p) {
            size_t skip = (p - payload) + 1;
            payload += skip;
            payload_len -= skip;
        }
    }

    pthread_mutex_lock(&sap->lock);

    if (is_delete) {
        /* Handle deletion */
        sap->stats.deletions++;
        for (int i = 0; i < sap->stream_count; i++) {
            if (sap->streams[i].active &&
                sap->streams[i].msg_id_hash == msg_id_hash &&
                strcmp(sap->streams[i].origin_ip, origin_ip) == 0) {

                sap->streams[i].active = 0;
                sap->stats.active_streams--;

                if (sap->callback) {
                    pthread_mutex_unlock(&sap->lock);
                    sap->callback(SAP_EVENT_DELETE, &sap->streams[i], sap->callback_userdata);
                    pthread_mutex_lock(&sap->lock);
                }
                break;
            }
        }
    } else {
        /* Handle announcement */
        sap->stats.announcements++;

        int is_new = 0;
        sap_stream_entry_t *entry = find_or_create_entry(sap, origin_ip, msg_id_hash, &is_new);

        if (!entry) {
            pthread_mutex_unlock(&sap->lock);
            return;
        }

        /* Parse SDP */
        sdp_stream_t sdp;
        if (sdp_parse((const char *)payload, payload_len, &sdp) != 0) {
            sap->stats.sdp_parse_errors++;
            pthread_mutex_unlock(&sap->lock);
            return;
        }

        /* Update entry */
        time_t now = time(NULL);

        if (is_new) {
            memset(entry, 0, sizeof(*entry));
            entry->first_seen = now;
            entry->msg_id_hash = msg_id_hash;
            strncpy(entry->origin_ip, origin_ip, sizeof(entry->origin_ip) - 1);
            sap->stats.active_streams++;
        }

        entry->sdp = sdp;
        entry->last_seen = now;
        entry->active = 1;

        /* Store raw SDP */
        size_t sdp_copy_len = payload_len < sizeof(entry->raw_sdp) - 1 ?
                              payload_len : sizeof(entry->raw_sdp) - 1;
        memcpy(entry->raw_sdp, payload, sdp_copy_len);
        entry->raw_sdp[sdp_copy_len] = '\0';

        sap_event_t event = is_new ? SAP_EVENT_NEW : SAP_EVENT_UPDATE;

        if (sap->callback) {
            pthread_mutex_unlock(&sap->lock);
            sap->callback(event, entry, sap->callback_userdata);
            pthread_mutex_lock(&sap->lock);
        }
    }

    pthread_mutex_unlock(&sap->lock);
}

/*
 * Cleanup expired streams.
 */
static void cleanup_expired(sap_discovery_t *sap)
{
    time_t now = time(NULL);
    time_t cutoff = now - sap->timeout_sec;

    pthread_mutex_lock(&sap->lock);

    for (int i = 0; i < sap->stream_count; i++) {
        if (sap->streams[i].active && sap->streams[i].last_seen < cutoff) {
            sap->streams[i].active = 0;
            sap->stats.active_streams--;

            if (sap->callback) {
                pthread_mutex_unlock(&sap->lock);
                sap->callback(SAP_EVENT_DELETE, &sap->streams[i], sap->callback_userdata);
                pthread_mutex_lock(&sap->lock);
            }
        }
    }

    pthread_mutex_unlock(&sap->lock);
}

/*
 * Listener thread.
 */
static void *listener_thread(void *arg)
{
    sap_discovery_t *sap = (sap_discovery_t *)arg;
    uint8_t buf[SAP_MAX_PACKET_SIZE];

    LOG_INFO("SAP discovery started on %s:%u", sap->multicast_addr, sap->port);

    time_t last_cleanup = time(NULL);

    while (sap->running) {
        /* Use select for timeout */
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sap->sock, &fds);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };

        int ret = select(sap->sock + 1, &fds, NULL, NULL, &tv);

        if (ret < 0) {
            if (errno == EINTR) continue;
            LOG_ERROR("SAP select error: %s", strerror(errno));
            break;
        }

        if (ret > 0 && FD_ISSET(sap->sock, &fds)) {
            ssize_t n = recv(sap->sock, buf, sizeof(buf), 0);
            if (n > 0) {
                sap->stats.packets_received++;
                process_sap_packet(sap, buf, (size_t)n);
            }
        }

        /* Periodic cleanup */
        time_t now = time(NULL);
        if (now - last_cleanup >= 30) {
            cleanup_expired(sap);
            last_cleanup = now;
        }
    }

    LOG_INFO("SAP discovery stopped");
    return NULL;
}

sap_discovery_t *sap_discovery_create(const sap_discovery_cfg_t *cfg)
{
    sap_discovery_t *sap = calloc(1, sizeof(sap_discovery_t));
    if (!sap) return NULL;

    sap->sock = -1;

    /* Apply configuration */
    if (cfg) {
        if (cfg->bind_interface) {
            strncpy(sap->bind_interface, cfg->bind_interface, sizeof(sap->bind_interface) - 1);
        }
        if (cfg->multicast_addr) {
            strncpy(sap->multicast_addr, cfg->multicast_addr, sizeof(sap->multicast_addr) - 1);
        } else {
            strncpy(sap->multicast_addr, SAP_ADDR_GLOBAL, sizeof(sap->multicast_addr) - 1);
        }
        sap->port = cfg->port > 0 ? cfg->port : SAP_PORT;
        sap->timeout_sec = cfg->timeout_sec > 0 ? cfg->timeout_sec : SAP_STREAM_TIMEOUT;
        sap->callback = cfg->callback;
        sap->callback_userdata = cfg->callback_userdata;
    } else {
        strncpy(sap->multicast_addr, SAP_ADDR_GLOBAL, sizeof(sap->multicast_addr) - 1);
        sap->port = SAP_PORT;
        sap->timeout_sec = SAP_STREAM_TIMEOUT;
    }

    pthread_mutex_init(&sap->lock, NULL);

    return sap;
}

int sap_discovery_start(sap_discovery_t *sap)
{
    if (!sap) return -1;
    if (sap->running) return 0;

    /* Create socket */
    sap->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sap->sock < 0) {
        set_error(sap, "socket: %s", strerror(errno));
        return -1;
    }

    /* Allow address reuse */
    int reuse = 1;
    setsockopt(sap->sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#ifdef SO_REUSEPORT
    setsockopt(sap->sock, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

    /* Bind to port */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(sap->port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sap->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        set_error(sap, "bind: %s", strerror(errno));
        close(sap->sock);
        sap->sock = -1;
        return -1;
    }

    /* Join multicast group */
    struct ip_mreqn mreq;
    memset(&mreq, 0, sizeof(mreq));
    inet_pton(AF_INET, sap->multicast_addr, &mreq.imr_multiaddr);
    mreq.imr_address.s_addr = INADDR_ANY;

    if (sap->bind_interface[0]) {
        mreq.imr_ifindex = if_nametoindex(sap->bind_interface);
        if (mreq.imr_ifindex == 0) {
            LOG_WARN("Interface %s not found, using default", sap->bind_interface);
        }
    }

    if (setsockopt(sap->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        set_error(sap, "IP_ADD_MEMBERSHIP: %s", strerror(errno));
        close(sap->sock);
        sap->sock = -1;
        return -1;
    }

    /* Also try admin-scoped SAP address */
    if (strcmp(sap->multicast_addr, SAP_ADDR_ADMIN) != 0) {
        struct ip_mreqn mreq2;
        memset(&mreq2, 0, sizeof(mreq2));
        inet_pton(AF_INET, SAP_ADDR_ADMIN, &mreq2.imr_multiaddr);
        mreq2.imr_address.s_addr = INADDR_ANY;
        mreq2.imr_ifindex = mreq.imr_ifindex;
        setsockopt(sap->sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq2, sizeof(mreq2));
    }

    /* Start listener thread */
    sap->running = 1;
    if (pthread_create(&sap->thread, NULL, listener_thread, sap) != 0) {
        set_error(sap, "pthread_create: %s", strerror(errno));
        sap->running = 0;
        close(sap->sock);
        sap->sock = -1;
        return -1;
    }

    return 0;
}

void sap_discovery_stop(sap_discovery_t *sap)
{
    if (!sap || !sap->running) return;

    sap->running = 0;
    pthread_join(sap->thread, NULL);

    if (sap->sock >= 0) {
        close(sap->sock);
        sap->sock = -1;
    }
}

void sap_discovery_destroy(sap_discovery_t *sap)
{
    if (!sap) return;

    sap_discovery_stop(sap);
    pthread_mutex_destroy(&sap->lock);
    free(sap);
}

int sap_discovery_is_running(const sap_discovery_t *sap)
{
    return sap ? sap->running : 0;
}

int sap_discovery_count(const sap_discovery_t *sap)
{
    if (!sap) return 0;
    return sap->stats.active_streams;
}

int sap_discovery_get_streams(const sap_discovery_t *sap,
                               sap_stream_entry_t *streams,
                               int max)
{
    if (!sap || !streams || max <= 0) return 0;

    int count = 0;
    pthread_mutex_lock((pthread_mutex_t *)&sap->lock);

    for (int i = 0; i < sap->stream_count && count < max; i++) {
        if (sap->streams[i].active) {
            streams[count++] = sap->streams[i];
        }
    }

    pthread_mutex_unlock((pthread_mutex_t *)&sap->lock);
    return count;
}

int sap_discovery_find_stream(const sap_discovery_t *sap,
                               const char *addr,
                               uint16_t port,
                               sap_stream_entry_t *stream)
{
    if (!sap || !addr) return 0;

    pthread_mutex_lock((pthread_mutex_t *)&sap->lock);

    for (int i = 0; i < sap->stream_count; i++) {
        if (sap->streams[i].active &&
            strcmp(sap->streams[i].sdp.multicast_addr, addr) == 0 &&
            (port == 0 || sap->streams[i].sdp.port == port)) {
            if (stream) {
                *stream = sap->streams[i];
            }
            pthread_mutex_unlock((pthread_mutex_t *)&sap->lock);
            return 1;
        }
    }

    pthread_mutex_unlock((pthread_mutex_t *)&sap->lock);
    return 0;
}

int sap_discovery_find_by_name(const sap_discovery_t *sap,
                                const char *name,
                                sap_stream_entry_t *stream)
{
    if (!sap || !name) return 0;

    pthread_mutex_lock((pthread_mutex_t *)&sap->lock);

    for (int i = 0; i < sap->stream_count; i++) {
        if (sap->streams[i].active &&
            strcasecmp(sap->streams[i].sdp.session_name, name) == 0) {
            if (stream) {
                *stream = sap->streams[i];
            }
            pthread_mutex_unlock((pthread_mutex_t *)&sap->lock);
            return 1;
        }
    }

    pthread_mutex_unlock((pthread_mutex_t *)&sap->lock);
    return 0;
}

void sap_discovery_get_stats(const sap_discovery_t *sap, sap_stats_t *stats)
{
    if (!sap || !stats) return;

    pthread_mutex_lock((pthread_mutex_t *)&sap->lock);
    *stats = sap->stats;
    pthread_mutex_unlock((pthread_mutex_t *)&sap->lock);
}

void sap_discovery_cleanup(sap_discovery_t *sap)
{
    if (sap) cleanup_expired(sap);
}

const char *sap_discovery_last_error(const sap_discovery_t *sap)
{
    return sap ? sap->last_error : "NULL instance";
}
