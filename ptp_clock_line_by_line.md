# Audyn Reference: ptp_clock.c — Line-by-Line Explanation

**Project:** Audyn — Professional Audio Capture & Archival Engine
**Author:** B. Wynne
**Status:** Canonical / Reference Implementation
**Scope:** This document explains *every significant line* in `core/ptp_clock.c`.

This file is intended for:
- onboarding new and junior developers
- understanding *why* the implementation is correct
- auditing timing correctness, thread safety, and PTP/RTP integration

---

## Purpose

The PTP clock module provides time synchronization for AES67 audio. It abstracts three timing sources:

1. **NONE** — Raw RTP timestamps only (no external time reference)
2. **SOFTWARE** — System clock (CLOCK_REALTIME) assumed synced by linuxptp
3. **HARDWARE** — Direct access to PTP Hardware Clock (PHC) via /dev/ptp*

The critical function is RTP-to-PTP conversion: mapping RTP timestamps (sample counts) to absolute PTP time (nanoseconds).

---

## File Header and Includes

```c
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
#include <pthread.h>
#include <stdint.h>
#include <limits.h>
```

- `_GNU_SOURCE` — Enables GNU extensions needed for some Linux features
- `pthread.h` — Thread safety via mutex
- `limits.h` — For INT64_MAX used in overflow protection

### Linux-Specific Headers

```c
#ifdef __linux__
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>
#include <linux/ptp_clock.h>
#endif
```

Required for:
- PHC device access
- ethtool ioctl for PHC discovery
- Network interface queries

---

## PHC File Descriptor to Clock ID Conversion

```c
#ifndef CLOCKFD
#define CLOCKFD 3
#endif
#ifndef FD_TO_CLOCKID
#define FD_TO_CLOCKID(fd) ((clockid_t)((((unsigned int)~(fd)) << 3) | CLOCKFD))
#endif
```

This is the **standard Linux mechanism** for using PHC devices with `clock_gettime()`.

### How It Works

Linux allows `clock_gettime()` to read from PHC devices by encoding the file descriptor into a special `clockid_t`:

```
fd = 3 (example PHC fd)
~fd = 0xFFFFFFFC
~fd << 3 = 0xFFFFFFE0
| CLOCKFD = 0xFFFFFFE3
```

The kernel recognizes this encoding and reads from the PHC instead of a standard clock.

---

## Constants

```c
#define NS_PER_SEC 1000000000ULL
```

Nanoseconds per second — used throughout for time conversions.

```c
#define MAX_SAFE_SAMPLE_DELTA ((int64_t)(INT64_MAX / NS_PER_SEC))
```

Maximum sample delta before overflow risk in nanosecond calculation:
- INT64_MAX ≈ 9.2 × 10^18
- MAX_SAFE_SAMPLE_DELTA ≈ 9.2 × 10^9 samples
- At 48kHz: ~53 hours of audio

---

## Internal Structure

```c
struct audyn_ptp_clock {
    audyn_ptp_mode_t mode;
    int phc_fd;                     /* PHC device fd (hardware mode) */
    clockid_t clock_id;             /* Clock ID for clock_gettime() */

    /* Thread safety */
    pthread_mutex_t lock;           /* Protects epoch and wraparound state */

    /* RTP epoch tracking */
    int epoch_set;                  /* 1 if epoch has been established */
    uint32_t epoch_rtp_ts;          /* RTP timestamp at epoch */
    uint64_t epoch_ptp_ns;          /* PTP time at epoch */
    uint32_t epoch_sample_rate;     /* Sample rate for epoch */

    /* RTP timestamp wraparound handling */
    uint32_t last_rtp_ts;           /* Last seen RTP timestamp */
    uint64_t rtp_wraparound_count;  /* Number of 32-bit wraparounds */
};
```

### Mode and Clock Access

- `mode` — Which timing source is active
- `phc_fd` — File descriptor for /dev/ptp* (hardware mode only, -1 otherwise)
- `clock_id` — Clock ID for `clock_gettime()` (varies by mode)

### Thread Safety

- `lock` — Mutex protecting epoch and wraparound state during concurrent access

### RTP Epoch

The "epoch" is the reference point for RTP-to-PTP conversion:
- `epoch_rtp_ts` — RTP timestamp when epoch was established
- `epoch_ptp_ns` — Corresponding PTP time in nanoseconds
- `epoch_sample_rate` — Sample rate (validated on conversion)

### Wraparound Handling

RTP timestamps are 32-bit and wrap every ~24 hours at 48kHz:
- `last_rtp_ts` — Previous RTP timestamp for wraparound detection
- `rtp_wraparound_count` — Number of times RTP has wrapped

---

## Clock Creation

```c
audyn_ptp_clock_t *audyn_ptp_clock_create(const audyn_ptp_cfg_t *cfg)
```

### Mutex Initialization

```c
if (pthread_mutex_init(&clk->lock, NULL) != 0) {
    LOG_ERROR("PTP: Failed to initialize mutex");
    free(clk);
    return NULL;
}
```

Mutex must be initialized before any other operations. All error paths after this point must destroy the mutex.

### Mode: NONE

```c
case AUDYN_PTP_MODE_NONE:
    LOG_INFO("PTP: Mode NONE - using raw RTP timestamps only");
    clk->clock_id = CLOCK_MONOTONIC;
    break;
```

Uses `CLOCK_MONOTONIC` for basic timing. RTP-to-PTP conversion will still work but won't correlate with wall-clock time.

### Mode: SOFTWARE

```c
case AUDYN_PTP_MODE_SOFTWARE:
    LOG_INFO("PTP: Mode SOFTWARE - using CLOCK_REALTIME (assumed synced by linuxptp)");
    clk->clock_id = CLOCK_REALTIME;
    break;
```

Relies on `ptp4l` and `phc2sys` keeping CLOCK_REALTIME synchronized to PTP. No special permissions required.

### Mode: HARDWARE

```c
case AUDYN_PTP_MODE_HARDWARE:
```

#### PHC Discovery

```c
if (!phc_path && cfg->interface) {
    int phc_idx = audyn_ptp_get_phc_index(cfg->interface);
    if (phc_idx >= 0) {
        snprintf(path_buf, sizeof(path_buf), "/dev/ptp%d", phc_idx);
        phc_path = path_buf;
    }
}
```

If no explicit PHC device is specified, discovers it from the network interface using ethtool.

#### PHC Device Open

```c
clk->phc_fd = open(phc_path, O_RDONLY);
if (clk->phc_fd < 0) {
    LOG_ERROR("PTP: Failed to open PHC device %s: %s", phc_path, strerror(errno));
    pthread_mutex_destroy(&clk->lock);
    free(clk);
    return NULL;
}

clk->clock_id = FD_TO_CLOCKID(clk->phc_fd);
```

Opens the PHC device and converts the fd to a clock ID.

#### Verification

```c
struct timespec ts;
if (clock_gettime(clk->clock_id, &ts) != 0) {
    LOG_ERROR("PTP: Failed to read PHC clock: %s", strerror(errno));
    close(clk->phc_fd);
    pthread_mutex_destroy(&clk->lock);
    free(clk);
    return NULL;
}
```

Verifies the PHC is readable before returning.

---

## Clock Destruction

```c
void audyn_ptp_clock_destroy(audyn_ptp_clock_t *clk)
{
    if (!clk) return;

    if (clk->phc_fd >= 0) {
        close(clk->phc_fd);
    }

    pthread_mutex_destroy(&clk->lock);
    free(clk);
}
```

Cleanup order:
1. Close PHC device (if open)
2. Destroy mutex
3. Free structure

---

## Time Retrieval

### Nanoseconds

```c
uint64_t audyn_ptp_clock_now_ns(audyn_ptp_clock_t *clk)
{
    struct timespec ts;
    if (clock_gettime(clk->clock_id, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * NS_PER_SEC + (uint64_t)ts.tv_nsec;
}
```

Returns current PTP time as a single 64-bit nanosecond value.

### Seconds + Nanoseconds

```c
int audyn_ptp_clock_gettime(audyn_ptp_clock_t *clk, uint64_t *sec, uint32_t *nsec)
{
    struct timespec ts;
    if (clock_gettime(clk->clock_id, &ts) != 0) {
        return -1;
    }
    *sec = (uint64_t)ts.tv_sec;
    *nsec = (uint32_t)ts.tv_nsec;
    return 0;
}
```

Returns time split into seconds and nanoseconds.

---

## RTP Epoch Management

```c
void audyn_ptp_set_rtp_epoch(audyn_ptp_clock_t *clk,
                             uint32_t rtp_ts,
                             uint64_t ptp_ns,
                             uint32_t sample_rate)
```

### Thread Safety

```c
pthread_mutex_lock(&clk->lock);
// ... modify epoch state ...
pthread_mutex_unlock(&clk->lock);
```

The entire epoch update is atomic.

### State Update

```c
clk->epoch_rtp_ts = rtp_ts;
clk->epoch_ptp_ns = ptp_ns;
clk->epoch_sample_rate = sample_rate;
clk->last_rtp_ts = rtp_ts;
clk->rtp_wraparound_count = 0;
clk->epoch_set = 1;
```

Establishes the reference point for all future RTP-to-PTP conversions.

---

## RTP to PTP Conversion

```c
uint64_t audyn_ptp_rtp_to_ns(audyn_ptp_clock_t *clk,
                             uint32_t rtp_ts,
                             uint32_t sample_rate)
```

This is the **core function** of the module.

### Thread Safety

```c
pthread_mutex_lock(&clk->lock);
// ... all conversion logic ...
pthread_mutex_unlock(&clk->lock);
```

### Sample Rate Validation

```c
if (sample_rate != clk->epoch_sample_rate) {
    LOG_ERROR("PTP: Sample rate mismatch - epoch=%u, requested=%u",
              clk->epoch_sample_rate, sample_rate);
    pthread_mutex_unlock(&clk->lock);
    return 0;
}
```

Prevents incorrect conversions if sample rates don't match.

### RTP Wraparound Detection

```c
if (rtp_ts < clk->last_rtp_ts && (clk->last_rtp_ts - rtp_ts) > 0x80000000U) {
    clk->rtp_wraparound_count++;
}
clk->last_rtp_ts = rtp_ts;
```

Detects when the 32-bit RTP timestamp wraps from 0xFFFFFFFF to 0x00000000.

**Heuristic:** If the new timestamp is much smaller than the last one (by more than half the 32-bit range), assume wraparound occurred.

### Extended RTP Timestamp

```c
uint64_t extended_rtp = rtp_ts + (clk->rtp_wraparound_count << 32);
```

Creates a 64-bit RTP timestamp by combining the 32-bit value with the wraparound count.

### Sample Delta Calculation

```c
int64_t sample_delta;
uint64_t extended_epoch = clk->epoch_rtp_ts;

if (extended_rtp >= extended_epoch) {
    sample_delta = (int64_t)(extended_rtp - extended_epoch);
} else {
    sample_delta = -(int64_t)(extended_epoch - extended_rtp);
}
```

Calculates how many samples have elapsed since the epoch.

### Overflow Protection

```c
int64_t abs_sample_delta = sample_delta >= 0 ? sample_delta : -sample_delta;
if (abs_sample_delta > MAX_SAFE_SAMPLE_DELTA) {
    LOG_ERROR("PTP: Sample delta too large, would overflow - resetting epoch");
    clk->epoch_rtp_ts = rtp_ts;
    clk->epoch_ptp_ns = audyn_ptp_clock_now_ns(clk);
    clk->rtp_wraparound_count = 0;
    clk->last_rtp_ts = rtp_ts;
    pthread_mutex_unlock(&clk->lock);
    return clk->epoch_ptp_ns;
}
```

If the sample delta is too large (>53 hours at 48kHz), the multiplication would overflow. The epoch is automatically reset to recover.

### Nanosecond Conversion

```c
int64_t ns_delta = (sample_delta * (int64_t)NS_PER_SEC) / (int64_t)sample_rate;
```

Converts samples to nanoseconds:
```
ns = samples × 1,000,000,000 / sample_rate
```

For 48kHz:
```
1 sample = 1,000,000,000 / 48,000 = 20,833.33... ns
```

### Apply Delta to Epoch

```c
uint64_t ptp_ns;
if (ns_delta >= 0) {
    ptp_ns = clk->epoch_ptp_ns + (uint64_t)ns_delta;
} else {
    if ((uint64_t)(-ns_delta) > clk->epoch_ptp_ns) {
        LOG_ERROR("PTP: rtp_to_ns resulted in negative time");
        pthread_mutex_unlock(&clk->lock);
        return 0;
    }
    ptp_ns = clk->epoch_ptp_ns - (uint64_t)(-ns_delta);
}
```

Adds (or subtracts) the delta from the epoch PTP time.

---

## Health Check

```c
int audyn_ptp_clock_is_healthy(audyn_ptp_clock_t *clk)
```

### Mode: NONE

```c
case AUDYN_PTP_MODE_NONE:
    return 1;  /* Always "healthy" */
```

### Mode: SOFTWARE

```c
case AUDYN_PTP_MODE_SOFTWARE:
    {
        struct timespec ts;
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
            return 0;
        }

        /* Check if time is reasonable (after Jan 1, 2020) */
        if (ts.tv_sec < 1577836800) {
            LOG_DEBUG("PTP: System clock appears unsynced (time too old)");
            return 0;
        }

        /* Check for ptp4l socket */
        if (access("/var/run/ptp4l", F_OK) == 0 ||
            access("/run/ptp4l", F_OK) == 0) {
            return 1;
        }

        /* Still return healthy if ptp4l not found */
        LOG_DEBUG("PTP: ptp4l socket not found - assuming synced by other means");
        return 1;
    }
```

Checks:
1. CLOCK_REALTIME is readable
2. Time is after Jan 1, 2020 (detects unsynced systems)
3. ptp4l socket exists (optional indicator)

### Mode: HARDWARE

```c
case AUDYN_PTP_MODE_HARDWARE:
    if (clk->phc_fd < 0) {
        return 0;
    }
    struct timespec ts;
    return (clock_gettime(clk->clock_id, &ts) == 0) ? 1 : 0;
```

Verifies PHC device is still readable.

---

## PHC Discovery

```c
int audyn_ptp_get_phc_index(const char *interface)
```

Discovers the PHC device index associated with a network interface.

### Socket Creation

```c
int fd = socket(AF_INET, SOCK_DGRAM, 0);
```

Creates a socket for ioctl operations.

### Ethtool Query

```c
struct ethtool_ts_info ts_info;
memset(&ts_info, 0, sizeof(ts_info));
ts_info.cmd = ETHTOOL_GET_TS_INFO;

struct ifreq ifr;
memset(&ifr, 0, sizeof(ifr));
strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
ifr.ifr_data = (void *)&ts_info;

int ret = ioctl(fd, SIOCETHTOOL, &ifr);
```

Uses `ETHTOOL_GET_TS_INFO` to query timestamping capabilities and PHC index.

### Result

```c
if (ts_info.phc_index < 0) {
    LOG_ERROR("PTP: No PHC associated with interface %s", interface);
    return -1;
}

return ts_info.phc_index;
```

Returns the PHC index (e.g., 0 for /dev/ptp0) or -1 if none.

---

## RTP-to-PTP Conversion Diagram

```
First Packet Received:
┌─────────────────────────────────────────────────────────────────┐
│  RTP timestamp = 1000                                           │
│  PTP arrival   = 5,000,000,000 ns                               │
│  Sample rate   = 48000                                          │
│                                                                 │
│  → Set epoch: epoch_rtp_ts=1000, epoch_ptp_ns=5000000000        │
└─────────────────────────────────────────────────────────────────┘

Later Packet:
┌─────────────────────────────────────────────────────────────────┐
│  RTP timestamp = 1480                                           │
│                                                                 │
│  sample_delta = 1480 - 1000 = 480 samples                       │
│  ns_delta = 480 × 1,000,000,000 / 48000 = 10,000,000 ns         │
│  ptp_ns = 5,000,000,000 + 10,000,000 = 5,010,000,000 ns         │
└─────────────────────────────────────────────────────────────────┘
```

---

## RTP Wraparound Handling

```
32-bit RTP Timestamp Timeline:
───────────────────────────────────────────────────────────────────►
    │                              │                              │
    0                        0xFFFFFFFF                           0
                                   │                              │
                              wraparound                     wraparound
                              count = 1                      count = 2

Extended 64-bit Timestamp:
    rtp_ts = 0x00000100, wraparound_count = 1
    extended_rtp = 0x00000100 + (1 << 32) = 0x100000100
```

---

## Thread Safety Model

```
┌────────────────────┐          ┌────────────────────┐
│  Network Thread    │          │   Audio Thread     │
│                    │          │                    │
│ set_rtp_epoch()    │          │  rtp_to_ns()       │
│        │           │          │        │           │
│        ▼           │          │        ▼           │
│  ┌───────────┐     │          │  ┌───────────┐     │
│  │mutex_lock │     │          │  │mutex_lock │     │
│  └─────┬─────┘     │          │  └─────┬─────┘     │
│        │           │          │        │           │
│        ▼           │          │        ▼           │
│  ┌───────────┐     │          │  ┌───────────┐     │
│  │  write    │◄────┼── mutex ─┼──►  read     │     │
│  │  epoch    │     │          │  │  epoch    │     │
│  └─────┬─────┘     │          │  └─────┬─────┘     │
│        │           │          │        │           │
│        ▼           │          │        ▼           │
│  ┌───────────┐     │          │  ┌───────────┐     │
│  │  unlock   │     │          │  │  unlock   │     │
│  └───────────┘     │          │  └───────────┘     │
└────────────────────┘          └────────────────────┘
```

---

## Mode Comparison

| Feature | NONE | SOFTWARE | HARDWARE |
|---------|------|----------|----------|
| Clock source | CLOCK_MONOTONIC | CLOCK_REALTIME | PHC device |
| Permissions | None | None | Read /dev/ptp* |
| External sync | None | ptp4l/phc2sys | Direct |
| Accuracy | N/A | ~μs (with phc2sys) | ~ns |
| Use case | Testing | Most deployments | Low-latency |

---

## Guarantees

- **Thread-safe** epoch and wraparound handling
- **Overflow protection** with automatic epoch reset
- **Sample rate validation** prevents miscalculation
- **32-bit RTP wraparound** correctly handled
- **PHC auto-discovery** from network interface
- **Improved health checks** for software mode

---

## Usage Pattern

```c
/* Create clock */
audyn_ptp_cfg_t cfg = {
    .mode = AUDYN_PTP_MODE_SOFTWARE
};
audyn_ptp_clock_t *clk = audyn_ptp_clock_create(&cfg);

/* On first AES67 packet */
uint64_t arrival_ns = audyn_ptp_clock_now_ns(clk);
audyn_ptp_set_rtp_epoch(clk, packet_rtp_ts, arrival_ns, 48000);

/* On subsequent packets */
uint64_t packet_ptp_time = audyn_ptp_rtp_to_ns(clk, packet_rtp_ts, 48000);

/* Use in jitter buffer */
if (audyn_jb_ready(jb, audyn_ptp_clock_now_ns(clk))) {
    audyn_jb_packet_t *pkt = audyn_jb_get(jb);
    // ...
}

/* Cleanup */
audyn_ptp_clock_destroy(clk);
```

---

## Integration with Jitter Buffer

```
┌─────────────────────────────────────────────────────────────────┐
│                        AES67 Packet                             │
│  ┌─────────────────┬──────────────────┬───────────────────┐     │
│  │  RTP Header     │  RTP Timestamp   │  Audio Payload    │     │
│  │  (seq, etc.)    │  (32-bit)        │  (24-bit samples) │     │
│  └─────────────────┴────────┬─────────┴───────────────────┘     │
└─────────────────────────────┼───────────────────────────────────┘
                              │
                              ▼
                 ┌────────────────────────┐
                 │  audyn_ptp_rtp_to_ns() │
                 │  Convert to PTP time   │
                 └────────────┬───────────┘
                              │
                              ▼
                 ┌────────────────────────┐
                 │    Jitter Buffer       │
                 │  audyn_jb_insert()     │
                 │  audyn_jb_ready()      │◄── audyn_ptp_clock_now_ns()
                 │  audyn_jb_get()        │
                 └────────────────────────┘
```
