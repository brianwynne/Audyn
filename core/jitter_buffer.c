/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      jitter_buffer.c
 *
 *  Purpose:
 *      RTP jitter buffer implementation for AES67 packet reordering.
 *
 *      Uses a circular buffer indexed by sequence number modulo buffer size.
 *      Handles wraparound of 16-bit sequence numbers correctly.
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

#include "jitter_buffer.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* Nanoseconds per millisecond */
#define NS_PER_MS 1000000ULL

/* Maximum sequence number delta to consider as reordering vs new stream */
#define SEQ_MAX_DELTA 1000

/* Forward declaration for internal reset */
static void jb_reset_unlocked(audyn_jitter_buffer_t *jb);

/* Sequence number comparison - handles wraparound */
/* Returns: negative if a < b, 0 if a == b, positive if a > b */
static inline int16_t seq_compare(uint16_t a, uint16_t b)
{
    return (int16_t)(a - b);
}

/* Internal jitter buffer structure */
struct audyn_jitter_buffer {
    /* Configuration */
    audyn_jb_cfg_t cfg;
    uint32_t buffer_size;       /* Number of packet slots */
    uint32_t loss_threshold;    /* Packets ahead before declaring loss */

    /* Packet storage */
    audyn_jb_packet_t *packets; /* Circular buffer */

    /* Sequence tracking */
    int initialized;            /* 1 if we've received first packet */
    uint16_t next_seq;          /* Next expected sequence number for playout */
    uint16_t highest_seq;       /* Highest sequence number received */

    /* Timing */
    uint64_t playout_time_ns;   /* PTP time for next playout */
    uint64_t packet_duration_ns;/* Duration of one packet in ns */

    /* Thread safety */
    pthread_mutex_t lock;       /* Protects all mutable state */

    /* Statistics */
    audyn_jb_stats_t stats;
};

/*
 * Create a jitter buffer instance.
 */
audyn_jitter_buffer_t *audyn_jb_create(const audyn_jb_cfg_t *cfg)
{
    if (!cfg) {
        LOG_ERROR("JB: NULL configuration");
        return NULL;
    }

    if (cfg->sample_rate == 0 || cfg->channels == 0 ||
        cfg->samples_per_packet == 0 || cfg->depth_ms == 0) {
        LOG_ERROR("JB: Invalid configuration parameters");
        return NULL;
    }

    audyn_jitter_buffer_t *jb = calloc(1, sizeof(*jb));
    if (!jb) {
        LOG_ERROR("JB: Failed to allocate jitter buffer");
        return NULL;
    }

    jb->cfg = *cfg;

    /* Calculate buffer size: enough slots for depth_ms worth of packets */
    /* packets_per_ms = sample_rate / (samples_per_packet * 1000) */
    uint32_t packets_per_ms = cfg->sample_rate / (cfg->samples_per_packet * 1000);
    if (packets_per_ms == 0) packets_per_ms = 1;

    /* Add some headroom for reordering */
    jb->buffer_size = packets_per_ms * cfg->depth_ms * 2;
    if (jb->buffer_size < 16) jb->buffer_size = 16;
    if (jb->buffer_size > 1024) jb->buffer_size = 1024;

    jb->packets = calloc(jb->buffer_size, sizeof(audyn_jb_packet_t));
    if (!jb->packets) {
        LOG_ERROR("JB: Failed to allocate packet buffer");
        free(jb);
        return NULL;
    }

    /* Initialize mutex */
    if (pthread_mutex_init(&jb->lock, NULL) != 0) {
        LOG_ERROR("JB: Failed to initialize mutex");
        free(jb->packets);
        free(jb);
        return NULL;
    }

    /* Calculate packet duration in nanoseconds */
    /* duration_ns = samples_per_packet * 1e9 / sample_rate */
    jb->packet_duration_ns = (uint64_t)cfg->samples_per_packet * 1000000000ULL /
                             cfg->sample_rate;

    /* Calculate loss threshold in packets (2x depth_ms worth of packets) */
    /* This properly handles non-1ms packet configurations */
    jb->loss_threshold = packets_per_ms * cfg->depth_ms * 2;
    if (jb->loss_threshold < 4) jb->loss_threshold = 4;

    jb->initialized = 0;
    memset(&jb->stats, 0, sizeof(jb->stats));

    LOG_INFO("JB: Created jitter buffer - depth=%ums, slots=%u, packet_duration=%luns, loss_threshold=%u",
             cfg->depth_ms, jb->buffer_size, (unsigned long)jb->packet_duration_ns, jb->loss_threshold);

    return jb;
}

/*
 * Destroy a jitter buffer instance.
 */
void audyn_jb_destroy(audyn_jitter_buffer_t *jb)
{
    if (!jb) {
        return;
    }

    LOG_DEBUG("JB: Destroying - received=%lu played=%lu lost=%lu late=%lu reordered=%lu",
              (unsigned long)jb->stats.packets_received,
              (unsigned long)jb->stats.packets_played,
              (unsigned long)jb->stats.packets_lost,
              (unsigned long)jb->stats.packets_late,
              (unsigned long)jb->stats.packets_reordered);

    pthread_mutex_destroy(&jb->lock);
    free(jb->packets);
    free(jb);
}

/*
 * Get buffer slot index for a sequence number.
 */
static inline uint32_t seq_to_index(audyn_jitter_buffer_t *jb, uint16_t seq)
{
    return seq % jb->buffer_size;
}

/*
 * Insert a packet into the jitter buffer.
 */
int audyn_jb_insert(audyn_jitter_buffer_t *jb,
                    uint16_t seq,
                    uint32_t rtp_ts,
                    uint64_t arrival_ns,
                    const uint8_t *payload,
                    uint32_t payload_len)
{
    if (!jb || !payload) {
        return -1;
    }

    if (payload_len > AUDYN_JB_MAX_PAYLOAD) {
        LOG_ERROR("JB: Payload too large: %u > %u", payload_len, AUDYN_JB_MAX_PAYLOAD);
        return -1;
    }

    pthread_mutex_lock(&jb->lock);

    jb->stats.packets_received++;

    /* First packet - initialize */
    if (!jb->initialized) {
        jb->next_seq = seq;
        jb->highest_seq = seq;
        jb->playout_time_ns = arrival_ns + (jb->cfg.depth_ms * NS_PER_MS);
        jb->initialized = 1;
        LOG_DEBUG("JB: First packet - seq=%u, playout starts at +%ums",
                  seq, jb->cfg.depth_ms);
    }

    /* Check if packet is too old (already played or too far behind) */
    int16_t delta_from_next = seq_compare(seq, jb->next_seq);
    if (delta_from_next < 0) {
        /* Packet is behind our playout position */
        if (delta_from_next > -SEQ_MAX_DELTA) {
            /* Late packet - we've already passed this sequence */
            jb->stats.packets_late++;
            LOG_DEBUG("JB: Late packet seq=%u (next=%u)", seq, jb->next_seq);
            pthread_mutex_unlock(&jb->lock);
            return -1;
        } else {
            /* Large backward jump - probably new stream, reset */
            LOG_INFO("JB: Large sequence jump detected, resetting");
            jb_reset_unlocked(jb);
            jb->next_seq = seq;
            jb->highest_seq = seq;
            jb->playout_time_ns = arrival_ns + (jb->cfg.depth_ms * NS_PER_MS);
            jb->initialized = 1;
        }
    }

    /* Check if this packet is out of order */
    int16_t delta_from_highest = seq_compare(seq, jb->highest_seq);
    if (delta_from_highest < 0 && delta_from_highest > -SEQ_MAX_DELTA) {
        jb->stats.packets_reordered++;
        LOG_DEBUG("JB: Reordered packet seq=%u (highest=%u)", seq, jb->highest_seq);
    } else if (delta_from_highest > 0) {
        jb->highest_seq = seq;
    }

    /* Check if packet would be too far ahead (buffer overflow) */
    /* Instead of rejecting, advance next_seq to maintain sliding window */
    int16_t delta_ahead = seq_compare(seq, jb->next_seq);
    if (delta_ahead >= (int16_t)jb->buffer_size) {
        /* Advance next_seq to make room, marking skipped packets as lost */
        uint16_t advance_count = delta_ahead - (int16_t)jb->buffer_size + 1;
        LOG_INFO("JB: Buffer overflow - advancing next_seq by %u to accommodate seq=%u",
                 advance_count, seq);
        for (uint16_t i = 0; i < advance_count; i++) {
            uint32_t skip_index = seq_to_index(jb, jb->next_seq);
            if (!jb->packets[skip_index].valid ||
                jb->packets[skip_index].seq != jb->next_seq) {
                jb->stats.packets_lost++;
            }
            jb->packets[skip_index].valid = 0;
            jb->next_seq++;
            jb->playout_time_ns += jb->packet_duration_ns;
        }
        jb->stats.buffer_overflows++;
    }

    /* Find slot and insert */
    uint32_t index = seq_to_index(jb, seq);
    audyn_jb_packet_t *slot = &jb->packets[index];

    if (slot->valid && slot->seq == seq) {
        /* Duplicate packet */
        LOG_DEBUG("JB: Duplicate packet seq=%u", seq);
        pthread_mutex_unlock(&jb->lock);
        return 0;
    }

    /* Check for slot collision - different seq in same slot */
    if (slot->valid && slot->seq != seq) {
        /* Slot contains a different packet - this is a collision */
        /* The old packet will be lost, log it */
        jb->stats.packets_lost++;
        LOG_INFO("JB: Slot collision - seq=%u overwrites seq=%u at slot %u",
                 seq, slot->seq, index);
    }

    slot->valid = 1;
    slot->seq = seq;
    slot->rtp_ts = rtp_ts;
    slot->arrival_ptp_ns = arrival_ns;
    slot->payload_len = payload_len;
    memcpy(slot->payload, payload, payload_len);

    /* Update depth statistics */
    int32_t depth = seq_compare(jb->highest_seq, jb->next_seq) + 1;
    if (depth < 0) depth = 0;
    jb->stats.current_depth = depth;
    if (depth > jb->stats.max_depth) {
        jb->stats.max_depth = depth;
    }

    pthread_mutex_unlock(&jb->lock);
    return 0;
}

/*
 * Get the next packet ready for playout.
 */
audyn_jb_packet_t *audyn_jb_get(audyn_jitter_buffer_t *jb)
{
    if (!jb) {
        return NULL;
    }

    pthread_mutex_lock(&jb->lock);

    if (!jb->initialized) {
        pthread_mutex_unlock(&jb->lock);
        return NULL;
    }

    uint32_t index = seq_to_index(jb, jb->next_seq);
    audyn_jb_packet_t *slot = &jb->packets[index];

    if (slot->valid && slot->seq == jb->next_seq) {
        /* Got the expected packet */
        slot->valid = 0;  /* Mark as consumed */
        jb->next_seq++;
        jb->playout_time_ns += jb->packet_duration_ns;
        jb->stats.packets_played++;

        /* Update depth */
        int32_t depth = seq_compare(jb->highest_seq, jb->next_seq) + 1;
        if (depth < 0) depth = 0;
        jb->stats.current_depth = depth;

        pthread_mutex_unlock(&jb->lock);
        return slot;
    }

    /* Packet is missing */
    /* Check if we should skip it (give up waiting) */
    int16_t gap = seq_compare(jb->highest_seq, jb->next_seq);
    if (gap > (int16_t)jb->loss_threshold) {
        /* We've received packets well beyond this one - it's lost */
        jb->stats.packets_lost++;
        LOG_DEBUG("JB: Lost packet seq=%u (highest=%u)", jb->next_seq, jb->highest_seq);
        jb->next_seq++;
        jb->playout_time_ns += jb->packet_duration_ns;
        /* Return NULL - caller should insert silence */
    }

    pthread_mutex_unlock(&jb->lock);
    return NULL;
}

/*
 * Check if a packet is ready for playout.
 */
int audyn_jb_ready(audyn_jitter_buffer_t *jb, uint64_t current_ns)
{
    if (!jb) {
        return 0;
    }

    pthread_mutex_lock(&jb->lock);

    if (!jb->initialized) {
        pthread_mutex_unlock(&jb->lock);
        return 0;
    }

    /* Check if it's time to playout */
    if (current_ns < jb->playout_time_ns) {
        pthread_mutex_unlock(&jb->lock);
        return 0;
    }

    /* Check if we have the next packet or should report loss */
    uint32_t index = seq_to_index(jb, jb->next_seq);
    audyn_jb_packet_t *slot = &jb->packets[index];

    if (slot->valid && slot->seq == jb->next_seq) {
        pthread_mutex_unlock(&jb->lock);
        return 1;  /* Packet ready */
    }

    /* Packet missing - check if we've waited long enough */
    int16_t gap = seq_compare(jb->highest_seq, jb->next_seq);
    if (gap > (int16_t)jb->loss_threshold) {
        pthread_mutex_unlock(&jb->lock);
        return 1;  /* Time to report loss */
    }

    pthread_mutex_unlock(&jb->lock);
    return 0;  /* Still waiting */
}

/*
 * Internal reset - called with lock already held.
 */
static void jb_reset_unlocked(audyn_jitter_buffer_t *jb)
{
    /* Clear all packet slots */
    for (uint32_t i = 0; i < jb->buffer_size; i++) {
        jb->packets[i].valid = 0;
    }

    jb->initialized = 0;
    jb->next_seq = 0;
    jb->highest_seq = 0;
    jb->playout_time_ns = 0;

    /* Keep cumulative stats but reset current depth */
    jb->stats.current_depth = 0;

    LOG_DEBUG("JB: Reset");
}

/*
 * Reset the jitter buffer.
 */
void audyn_jb_reset(audyn_jitter_buffer_t *jb)
{
    if (!jb) {
        return;
    }

    pthread_mutex_lock(&jb->lock);
    jb_reset_unlocked(jb);
    pthread_mutex_unlock(&jb->lock);
}

/*
 * Get jitter buffer statistics.
 */
void audyn_jb_get_stats(audyn_jitter_buffer_t *jb, audyn_jb_stats_t *stats)
{
    if (!jb || !stats) {
        return;
    }
    pthread_mutex_lock(&jb->lock);
    *stats = jb->stats;
    pthread_mutex_unlock(&jb->lock);
}

/*
 * Get current buffer depth in packets.
 */
int audyn_jb_depth(audyn_jitter_buffer_t *jb)
{
    if (!jb) {
        return 0;
    }
    pthread_mutex_lock(&jb->lock);
    int depth = jb->stats.current_depth;
    pthread_mutex_unlock(&jb->lock);
    return depth;
}
