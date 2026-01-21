/*
 *  Audyn - Professional Audio Capture & Archival Engine
 *
 *  File:
 *      sap_discovery.h
 *
 *  Purpose:
 *      SAP (Session Announcement Protocol) listener for AES67 stream discovery.
 *
 *      Listens for SAP announcements on the network and maintains
 *      a list of discovered AES67/ST2110 audio streams.
 *
 *  Standards:
 *      - RFC 2974 (SAP)
 *      - RFC 4566 (SDP)
 *      - AES67-2018
 *
 *  Copyright:
 *      (c) 2026 B. Wynne
 *
 *  License:
 *      GPLv2 or later
 */

#ifndef AUDYN_SAP_DISCOVERY_H
#define AUDYN_SAP_DISCOVERY_H

#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include "sdp_parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SAP multicast addresses */
#define SAP_ADDR_GLOBAL     "224.2.127.254"     /* Global scope */
#define SAP_ADDR_ADMIN      "239.255.255.255"   /* Admin scope (commonly used) */
#define SAP_PORT            9875

/* Maximum streams to track */
#define SAP_MAX_STREAMS     256

/* Stream timeout (seconds) - streams not re-announced are removed */
#define SAP_STREAM_TIMEOUT  300

/*
 * Discovered stream entry
 */
typedef struct {
    sdp_stream_t    sdp;            /* Parsed SDP info */
    uint16_t        msg_id_hash;    /* SAP message ID hash */
    char            origin_ip[64];  /* SAP origin IP */
    time_t          first_seen;     /* When first discovered */
    time_t          last_seen;      /* Last announcement time */
    int             active;         /* Stream is active (not deleted) */
    char            raw_sdp[4096];  /* Raw SDP text */
} sap_stream_entry_t;

/*
 * SAP discovery statistics
 */
typedef struct {
    uint64_t    packets_received;
    uint64_t    packets_invalid;
    uint64_t    announcements;
    uint64_t    deletions;
    uint64_t    sdp_parse_errors;
    int         active_streams;
} sap_stats_t;

/*
 * Callback for stream events
 */
typedef enum {
    SAP_EVENT_NEW,      /* New stream discovered */
    SAP_EVENT_UPDATE,   /* Stream info updated */
    SAP_EVENT_DELETE,   /* Stream deleted (SAP deletion or timeout) */
} sap_event_t;

typedef void (*sap_callback_fn)(sap_event_t event, const sap_stream_entry_t *stream, void *userdata);

/*
 * SAP discovery configuration
 */
typedef struct {
    const char *bind_interface;     /* Network interface to bind to (NULL for any) */
    const char *multicast_addr;     /* SAP multicast address (NULL for default) */
    uint16_t    port;               /* SAP port (0 for default 9875) */
    int         timeout_sec;        /* Stream timeout in seconds (0 for default 300) */
    sap_callback_fn callback;       /* Optional callback for stream events */
    void       *callback_userdata;  /* User data for callback */
} sap_discovery_cfg_t;

typedef struct sap_discovery sap_discovery_t;

/*
 * Create SAP discovery instance.
 *
 * Returns NULL on error.
 */
sap_discovery_t *sap_discovery_create(const sap_discovery_cfg_t *cfg);

/*
 * Start listening for SAP announcements.
 *
 * Spawns a background thread that listens for SAP packets.
 *
 * Returns 0 on success, -1 on error.
 */
int sap_discovery_start(sap_discovery_t *sap);

/*
 * Stop listening.
 */
void sap_discovery_stop(sap_discovery_t *sap);

/*
 * Destroy SAP discovery instance.
 */
void sap_discovery_destroy(sap_discovery_t *sap);

/*
 * Check if discovery is running.
 */
int sap_discovery_is_running(const sap_discovery_t *sap);

/*
 * Get number of discovered streams.
 */
int sap_discovery_count(const sap_discovery_t *sap);

/*
 * Get list of discovered streams.
 *
 * Parameters:
 *   sap      - discovery instance
 *   streams  - array to fill with stream entries
 *   max      - maximum entries to return
 *
 * Returns number of streams copied.
 */
int sap_discovery_get_streams(const sap_discovery_t *sap,
                               sap_stream_entry_t *streams,
                               int max);

/*
 * Find stream by multicast address.
 *
 * Parameters:
 *   sap    - discovery instance
 *   addr   - multicast address to find
 *   port   - port (0 to match any port)
 *   stream - output stream entry (can be NULL to just check existence)
 *
 * Returns 1 if found, 0 if not found.
 */
int sap_discovery_find_stream(const sap_discovery_t *sap,
                               const char *addr,
                               uint16_t port,
                               sap_stream_entry_t *stream);

/*
 * Find stream by session name.
 *
 * Returns 1 if found, 0 if not found.
 */
int sap_discovery_find_by_name(const sap_discovery_t *sap,
                                const char *name,
                                sap_stream_entry_t *stream);

/*
 * Get discovery statistics.
 */
void sap_discovery_get_stats(const sap_discovery_t *sap, sap_stats_t *stats);

/*
 * Force cleanup of expired streams.
 *
 * Normally done automatically, but can be called manually.
 */
void sap_discovery_cleanup(sap_discovery_t *sap);

/*
 * Get last error message.
 */
const char *sap_discovery_last_error(const sap_discovery_t *sap);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_SAP_DISCOVERY_H */
