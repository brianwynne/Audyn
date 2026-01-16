/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      ptp_clock.h
 *
 *  Purpose:
 *      PTP (Precision Time Protocol) clock abstraction for AES67.
 *
 *      Supports three modes:
 *        1. None: Raw RTP timestamps only (no PTP)
 *        2. Software PTP: System CLOCK_REALTIME assumed synced by linuxptp (ptp4l/phc2sys)
 *        3. Hardware PTP: Direct access to PHC (PTP Hardware Clock) via /dev/ptp*
 *
 *      Thread Safety:
 *        - audyn_ptp_set_rtp_epoch() and audyn_ptp_rtp_to_ns() are thread-safe
 *        - Can be called from different threads (e.g., network receive and audio playout)
 *
 *  AES67 Timing:
 *      AES67 uses PTP (IEEE 1588) for synchronization. RTP timestamps in AES67 packets
 *      are derived from the PTP clock. To correctly process audio:
 *        - Capture packet arrival time (via SO_TIMESTAMPING or software timestamp)
 *        - Correlate RTP timestamp with PTP time
 *        - Use jitter buffer to reorder and playout at correct time
 *
 *  Dependencies:
 *      - Linux: /dev/ptp*, clock_gettime(), SO_TIMESTAMPING
 *      - linuxptp: ptp4l, phc2sys (for software mode)
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

#ifndef AUDYN_PTP_CLOCK_H
#define AUDYN_PTP_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PTP clock mode */
typedef enum audyn_ptp_mode {
    AUDYN_PTP_MODE_NONE,        /* No PTP - use raw RTP timestamps only */
    AUDYN_PTP_MODE_SOFTWARE,    /* Software PTP via system clock (linuxptp) */
    AUDYN_PTP_MODE_HARDWARE     /* Hardware PTP via PHC device */
} audyn_ptp_mode_t;

/* PTP clock configuration */
typedef struct audyn_ptp_cfg {
    audyn_ptp_mode_t mode;
    const char *phc_device;     /* PHC device path, e.g., "/dev/ptp0" (hardware mode) */
    const char *interface;      /* Network interface for PHC discovery, e.g., "eth0" */
} audyn_ptp_cfg_t;

/* Opaque PTP clock handle */
typedef struct audyn_ptp_clock audyn_ptp_clock_t;

/*
 * Create a PTP clock instance.
 *
 * For hardware mode:
 *   - If phc_device is set, use that directly
 *   - If interface is set, discover PHC from interface
 *   - Requires CAP_SYS_TIME or root for some operations
 *
 * For software mode:
 *   - Uses CLOCK_REALTIME, assumed synced by ptp4l/phc2sys
 *   - No special permissions required
 *
 * Returns NULL on failure.
 */
audyn_ptp_clock_t *audyn_ptp_clock_create(const audyn_ptp_cfg_t *cfg);

/*
 * Destroy a PTP clock instance.
 */
void audyn_ptp_clock_destroy(audyn_ptp_clock_t *clk);

/*
 * Get current PTP time in nanoseconds.
 *
 * Returns 0 on failure.
 */
uint64_t audyn_ptp_clock_now_ns(audyn_ptp_clock_t *clk);

/*
 * Get current PTP time as seconds and nanoseconds.
 *
 * Returns 0 on success, -1 on failure.
 */
int audyn_ptp_clock_gettime(audyn_ptp_clock_t *clk, uint64_t *sec, uint32_t *nsec);

/*
 * Convert RTP timestamp to PTP nanoseconds.
 *
 * AES67 RTP timestamps are in units of 1/sample_rate seconds.
 * This function converts to PTP nanoseconds using the clock's
 * current understanding of the RTP epoch.
 *
 * Parameters:
 *   clk         - PTP clock instance
 *   rtp_ts      - RTP timestamp from packet
 *   sample_rate - Audio sample rate (e.g., 48000)
 *
 * Returns PTP time in nanoseconds, or 0 on failure.
 */
uint64_t audyn_ptp_rtp_to_ns(audyn_ptp_clock_t *clk,
                             uint32_t rtp_ts,
                             uint32_t sample_rate);

/*
 * Set RTP epoch reference.
 *
 * Called when first packet is received to establish the mapping
 * between RTP timestamp and PTP time.
 *
 * Parameters:
 *   clk           - PTP clock instance
 *   rtp_ts        - RTP timestamp from first packet
 *   ptp_ns        - PTP arrival time of first packet
 *   sample_rate   - Audio sample rate
 */
void audyn_ptp_set_rtp_epoch(audyn_ptp_clock_t *clk,
                             uint32_t rtp_ts,
                             uint64_t ptp_ns,
                             uint32_t sample_rate);

/*
 * Get the active PTP mode.
 */
audyn_ptp_mode_t audyn_ptp_clock_mode(const audyn_ptp_clock_t *clk);

/*
 * Check if PTP clock is healthy/synchronized.
 *
 * For hardware mode: checks if PHC is accessible
 * For software mode: always returns 1 (assumes ptp4l is running)
 *
 * Returns 1 if healthy, 0 if not.
 */
int audyn_ptp_clock_is_healthy(audyn_ptp_clock_t *clk);

/*
 * Get PHC index from network interface name.
 *
 * Uses ethtool ioctl to discover PHC device.
 *
 * Returns PHC index (>=0) on success, -1 on failure.
 */
int audyn_ptp_get_phc_index(const char *interface);

#ifdef __cplusplus
}
#endif

#endif /* AUDYN_PTP_CLOCK_H */
