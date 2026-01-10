/*
 *  Audyn — Professional Audio Capture & Archival Engine
 *
 *  File:
 *      ptp_clock.c
 *
 *  Purpose:
 *      PTP (Precision Time Protocol) clock implementation for AES67.
 *
 *      Hardware PTP mode:
 *        - Opens PHC device (/dev/ptp0, etc.)
 *        - Uses clock_gettime() with dynamic clock ID
 *        - Requires CAP_SYS_TIME or appropriate permissions
 *
 *      Software PTP mode:
 *        - Uses CLOCK_REALTIME
 *        - Assumes system clock is synced by linuxptp (ptp4l/phc2sys)
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

#define _GNU_SOURCE

#include "ptp_clock.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#ifdef __linux__
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/ptp_clock.h>
#endif

/* Convert PHC file descriptor to clock ID for clock_gettime() */
#ifndef CLOCKFD
#define CLOCKFD 3
#endif
#ifndef FD_TO_CLOCKID
#define FD_TO_CLOCKID(fd) ((clockid_t)((((unsigned int)~(fd)) << 3) | CLOCKFD))
#endif

/* Nanoseconds per second */
#define NS_PER_SEC 1000000000ULL

/* Internal PTP clock structure */
struct audyn_ptp_clock {
    audyn_ptp_mode_t mode;
    int phc_fd;                     /* PHC device fd (hardware mode) */
    clockid_t clock_id;             /* Clock ID for clock_gettime() */

    /* RTP epoch tracking */
    int epoch_set;                  /* 1 if epoch has been established */
    uint32_t epoch_rtp_ts;          /* RTP timestamp at epoch */
    uint64_t epoch_ptp_ns;          /* PTP time at epoch */
    uint32_t epoch_sample_rate;     /* Sample rate for epoch */

    /* RTP timestamp wraparound handling */
    uint32_t last_rtp_ts;           /* Last seen RTP timestamp */
    uint64_t rtp_wraparound_count;  /* Number of 32-bit wraparounds */
};

/*
 * Create a PTP clock instance.
 */
audyn_ptp_clock_t *audyn_ptp_clock_create(const audyn_ptp_cfg_t *cfg)
{
    if (!cfg) {
        LOG_ERROR("PTP: NULL configuration");
        return NULL;
    }

    audyn_ptp_clock_t *clk = calloc(1, sizeof(*clk));
    if (!clk) {
        LOG_ERROR("PTP: Failed to allocate clock structure");
        return NULL;
    }

    clk->mode = cfg->mode;
    clk->phc_fd = -1;
    clk->epoch_set = 0;

    switch (cfg->mode) {
        case AUDYN_PTP_MODE_NONE:
            LOG_INFO("PTP: Mode NONE - using raw RTP timestamps only");
            clk->clock_id = CLOCK_MONOTONIC;
            break;

        case AUDYN_PTP_MODE_SOFTWARE:
            LOG_INFO("PTP: Mode SOFTWARE - using CLOCK_REALTIME (assumed synced by linuxptp)");
            clk->clock_id = CLOCK_REALTIME;
            break;

        case AUDYN_PTP_MODE_HARDWARE:
#ifdef __linux__
        {
            const char *phc_path = cfg->phc_device;
            char path_buf[32];

            /* If no explicit PHC device, try to discover from interface */
            if (!phc_path && cfg->interface) {
                int phc_idx = audyn_ptp_get_phc_index(cfg->interface);
                if (phc_idx >= 0) {
                    snprintf(path_buf, sizeof(path_buf), "/dev/ptp%d", phc_idx);
                    phc_path = path_buf;
                    LOG_INFO("PTP: Discovered PHC device %s from interface %s",
                             phc_path, cfg->interface);
                } else {
                    LOG_ERROR("PTP: Failed to discover PHC from interface %s", cfg->interface);
                    free(clk);
                    return NULL;
                }
            }

            if (!phc_path) {
                LOG_ERROR("PTP: Hardware mode requires phc_device or interface");
                free(clk);
                return NULL;
            }

            clk->phc_fd = open(phc_path, O_RDONLY);
            if (clk->phc_fd < 0) {
                LOG_ERROR("PTP: Failed to open PHC device %s: %s",
                          phc_path, strerror(errno));
                free(clk);
                return NULL;
            }

            clk->clock_id = FD_TO_CLOCKID(clk->phc_fd);
            LOG_INFO("PTP: Mode HARDWARE - opened %s (fd=%d, clockid=%d)",
                     phc_path, clk->phc_fd, (int)clk->clock_id);

            /* Verify we can read the clock */
            struct timespec ts;
            if (clock_gettime(clk->clock_id, &ts) != 0) {
                LOG_ERROR("PTP: Failed to read PHC clock: %s", strerror(errno));
                close(clk->phc_fd);
                free(clk);
                return NULL;
            }
            LOG_DEBUG("PTP: PHC clock initial time: %ld.%09ld",
                      (long)ts.tv_sec, ts.tv_nsec);
        }
#else
            LOG_ERROR("PTP: Hardware mode only supported on Linux");
            free(clk);
            return NULL;
#endif
            break;

        default:
            LOG_ERROR("PTP: Unknown mode %d", cfg->mode);
            free(clk);
            return NULL;
    }

    return clk;
}

/*
 * Destroy a PTP clock instance.
 */
void audyn_ptp_clock_destroy(audyn_ptp_clock_t *clk)
{
    if (!clk) {
        return;
    }

    if (clk->phc_fd >= 0) {
        close(clk->phc_fd);
        LOG_DEBUG("PTP: Closed PHC device");
    }

    free(clk);
}

/*
 * Get current PTP time in nanoseconds.
 */
uint64_t audyn_ptp_clock_now_ns(audyn_ptp_clock_t *clk)
{
    if (!clk) {
        return 0;
    }

    struct timespec ts;
    if (clock_gettime(clk->clock_id, &ts) != 0) {
        LOG_ERROR("PTP: clock_gettime failed: %s", strerror(errno));
        return 0;
    }

    return (uint64_t)ts.tv_sec * NS_PER_SEC + (uint64_t)ts.tv_nsec;
}

/*
 * Get current PTP time as seconds and nanoseconds.
 */
int audyn_ptp_clock_gettime(audyn_ptp_clock_t *clk, uint64_t *sec, uint32_t *nsec)
{
    if (!clk || !sec || !nsec) {
        return -1;
    }

    struct timespec ts;
    if (clock_gettime(clk->clock_id, &ts) != 0) {
        LOG_ERROR("PTP: clock_gettime failed: %s", strerror(errno));
        return -1;
    }

    *sec = (uint64_t)ts.tv_sec;
    *nsec = (uint32_t)ts.tv_nsec;
    return 0;
}

/*
 * Set RTP epoch reference.
 */
void audyn_ptp_set_rtp_epoch(audyn_ptp_clock_t *clk,
                             uint32_t rtp_ts,
                             uint64_t ptp_ns,
                             uint32_t sample_rate)
{
    if (!clk || sample_rate == 0) {
        return;
    }

    clk->epoch_rtp_ts = rtp_ts;
    clk->epoch_ptp_ns = ptp_ns;
    clk->epoch_sample_rate = sample_rate;
    clk->last_rtp_ts = rtp_ts;
    clk->rtp_wraparound_count = 0;
    clk->epoch_set = 1;

    LOG_DEBUG("PTP: Set RTP epoch - rtp_ts=%u ptp_ns=%lu sample_rate=%u",
              rtp_ts, (unsigned long)ptp_ns, sample_rate);
}

/*
 * Convert RTP timestamp to PTP nanoseconds.
 *
 * Handles 32-bit RTP timestamp wraparound by tracking wraparounds.
 */
uint64_t audyn_ptp_rtp_to_ns(audyn_ptp_clock_t *clk,
                             uint32_t rtp_ts,
                             uint32_t sample_rate)
{
    if (!clk || sample_rate == 0) {
        return 0;
    }

    if (!clk->epoch_set) {
        /* No epoch set - can't convert */
        LOG_DEBUG("PTP: rtp_to_ns called but no epoch set");
        return 0;
    }

    /* Detect RTP timestamp wraparound */
    /* If the new timestamp is much smaller than the last one, assume wraparound */
    if (rtp_ts < clk->last_rtp_ts && (clk->last_rtp_ts - rtp_ts) > 0x80000000U) {
        clk->rtp_wraparound_count++;
        LOG_DEBUG("PTP: RTP timestamp wraparound detected (count=%lu)",
                  (unsigned long)clk->rtp_wraparound_count);
    }
    clk->last_rtp_ts = rtp_ts;

    /* Calculate extended 64-bit RTP timestamp */
    uint64_t extended_rtp = rtp_ts + (clk->rtp_wraparound_count << 32);

    /* Calculate delta from epoch in samples */
    int64_t sample_delta;
    uint64_t extended_epoch = clk->epoch_rtp_ts;  /* Epoch is always in first period */

    if (extended_rtp >= extended_epoch) {
        sample_delta = (int64_t)(extended_rtp - extended_epoch);
    } else {
        /* This shouldn't happen if epoch is first packet, but handle it */
        sample_delta = -(int64_t)(extended_epoch - extended_rtp);
    }

    /* Convert sample delta to nanoseconds */
    /* ns = samples * 1e9 / sample_rate */
    /* To avoid overflow, use: ns = samples * (1e9 / sample_rate) */
    /* For 48000 Hz: 1e9/48000 = 20833.333... */
    /* More precise: ns = samples * 1000000000 / sample_rate */
    int64_t ns_delta = (sample_delta * (int64_t)NS_PER_SEC) / (int64_t)sample_rate;

    /* Apply delta to epoch PTP time */
    uint64_t ptp_ns;
    if (ns_delta >= 0) {
        ptp_ns = clk->epoch_ptp_ns + (uint64_t)ns_delta;
    } else {
        if ((uint64_t)(-ns_delta) > clk->epoch_ptp_ns) {
            /* Would go negative - shouldn't happen */
            LOG_ERROR("PTP: rtp_to_ns resulted in negative time");
            return 0;
        }
        ptp_ns = clk->epoch_ptp_ns - (uint64_t)(-ns_delta);
    }

    return ptp_ns;
}

/*
 * Get the active PTP mode.
 */
audyn_ptp_mode_t audyn_ptp_clock_mode(const audyn_ptp_clock_t *clk)
{
    if (!clk) {
        return AUDYN_PTP_MODE_NONE;
    }
    return clk->mode;
}

/*
 * Check if PTP clock is healthy/synchronized.
 */
int audyn_ptp_clock_is_healthy(audyn_ptp_clock_t *clk)
{
    if (!clk) {
        return 0;
    }

    switch (clk->mode) {
        case AUDYN_PTP_MODE_NONE:
            return 1;  /* Always "healthy" in none mode */

        case AUDYN_PTP_MODE_SOFTWARE:
            /* Assume healthy if we can read the clock */
            {
                struct timespec ts;
                return (clock_gettime(CLOCK_REALTIME, &ts) == 0) ? 1 : 0;
            }

        case AUDYN_PTP_MODE_HARDWARE:
            /* Check if we can read from PHC */
            if (clk->phc_fd < 0) {
                return 0;
            }
            {
                struct timespec ts;
                return (clock_gettime(clk->clock_id, &ts) == 0) ? 1 : 0;
            }

        default:
            return 0;
    }
}

/*
 * Get PHC index from network interface name.
 */
int audyn_ptp_get_phc_index(const char *interface)
{
#ifdef __linux__
    if (!interface) {
        return -1;
    }

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        LOG_ERROR("PTP: Failed to create socket for PHC discovery: %s", strerror(errno));
        return -1;
    }

    struct ethtool_ts_info ts_info;
    memset(&ts_info, 0, sizeof(ts_info));
    ts_info.cmd = ETHTOOL_GET_TS_INFO;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    ifr.ifr_data = (void *)&ts_info;

    int ret = ioctl(fd, SIOCETHTOOL, &ifr);
    close(fd);

    if (ret < 0) {
        LOG_ERROR("PTP: ETHTOOL_GET_TS_INFO failed for %s: %s",
                  interface, strerror(errno));
        return -1;
    }

    if (ts_info.phc_index < 0) {
        LOG_ERROR("PTP: No PHC associated with interface %s", interface);
        return -1;
    }

    LOG_DEBUG("PTP: Interface %s has PHC index %d", interface, ts_info.phc_index);
    return ts_info.phc_index;
#else
    (void)interface;
    LOG_ERROR("PTP: PHC discovery only supported on Linux");
    return -1;
#endif
}
