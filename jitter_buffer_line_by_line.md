# Audyn Reference: jitter_buffer.c — Line-by-Line Explanation

**Project:** Audyn — Professional Audio Capture & Archival Engine
**Author:** B. Wynne
**Status:** Canonical / Reference Implementation
**Scope:** This document explains *every significant line* in `core/jitter_buffer.c`.

This file is intended for:
- onboarding new and junior developers
- understanding *why* the implementation is correct
- auditing timing correctness, thread safety, and packet handling

---

## Purpose

The jitter buffer solves three problems in network audio:

1. **Reordering** — Packets may arrive out of sequence due to network routing
2. **Timing** — Packets arrive with variable delay (jitter)
3. **Loss** — Some packets never arrive

For AES67 specifically:
- Packets are typically 1ms (48 samples @ 48kHz)
- Sequence numbers are 16-bit, wrapping at 65536
- Low latency is critical (1-4ms buffer depth)

---

## File Header and Includes

```c
#include "jitter_buffer.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
```

- `jitter_buffer.h` — Public API and type definitions
- `log.h` — Audyn logging macros
- `stdlib.h` — `calloc`, `free`
- `string.h` — `memcpy`, `memset`
- `pthread.h` — POSIX threads mutex for thread safety

---

## Constants

```c
#define NS_PER_MS 1000000ULL
```

Nanoseconds per millisecond. Used to convert `depth_ms` to nanoseconds for PTP timing.

```c
#define SEQ_MAX_DELTA 1000
```

Maximum sequence number difference to consider as reordering rather than a new stream. If a packet arrives with sequence number more than 1000 behind the current position, we assume it's a new stream and reset.

---

## Forward Declaration

```c
static void jb_reset_unlocked(audyn_jitter_buffer_t *jb);
```

Internal reset function that operates without acquiring the lock. This is necessary because:
- `audyn_jb_reset()` is a public API that acquires the lock
- `audyn_jb_insert()` calls reset internally while already holding the lock

Using separate locked/unlocked versions prevents deadlock.

---

## Sequence Number Comparison

```c
static inline int16_t seq_compare(uint16_t a, uint16_t b)
{
    return (int16_t)(a - b);
}
```

This is the **critical function** for correct RTP handling.

### Why This Works

RTP sequence numbers are 16-bit unsigned integers (0-65535) that wrap around. Simple comparison (`a < b`) fails at wraparound:

```
seq 65535 followed by seq 0
65535 < 0 is FALSE (wrong!)
```

By casting the subtraction to signed 16-bit:

```
(int16_t)(0 - 65535) = (int16_t)(-65535) = 1
```

This correctly indicates that 0 comes *after* 65535.

### Return Value Interpretation

| Result | Meaning |
|--------|---------|
| Negative | `a` is before `b` |
| Zero | `a` equals `b` |
| Positive | `a` is after `b` |

---

## Internal Structure

```c
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
```

### Configuration

- `cfg` — Copy of user-provided configuration
- `buffer_size` — Number of packet slots (calculated from `depth_ms`)
- `loss_threshold` — Number of packets ahead before declaring a missing packet as lost (properly calculated based on packets-per-ms, not assuming 1 packet/ms)

### Packet Storage

- `packets` — Circular buffer indexed by `seq % buffer_size`

### Sequence Tracking

- `initialized` — Set to 1 after first packet received
- `next_seq` — The sequence number we need for playout (consumer side)
- `highest_seq` — The highest sequence number received (producer side)

### Timing

- `playout_time_ns` — PTP timestamp when next packet should play
- `packet_duration_ns` — Duration of one packet in nanoseconds

### Thread Safety

- `lock` — Mutex protecting all mutable state, enabling safe concurrent access from network receive and audio playout threads

---

## Buffer Creation

```c
audyn_jitter_buffer_t *audyn_jb_create(const audyn_jb_cfg_t *cfg)
```

### Validation

```c
if (!cfg) {
    LOG_ERROR("JB: NULL configuration");
    return NULL;
}

if (cfg->sample_rate == 0 || cfg->channels == 0 ||
    cfg->samples_per_packet == 0 || cfg->depth_ms == 0) {
    LOG_ERROR("JB: Invalid configuration parameters");
    return NULL;
}
```

All parameters must be non-zero.

### Buffer Size Calculation

```c
uint32_t packets_per_ms = cfg->sample_rate / (cfg->samples_per_packet * 1000);
if (packets_per_ms == 0) packets_per_ms = 1;

jb->buffer_size = packets_per_ms * cfg->depth_ms * 2;
if (jb->buffer_size < 16) jb->buffer_size = 16;
if (jb->buffer_size > 1024) jb->buffer_size = 1024;
```

For AES67 at 48kHz with 48 samples/packet:
```
packets_per_ms = 48000 / (48 * 1000) = 1
```

With `depth_ms = 4`:
```
buffer_size = 1 * 4 * 2 = 8 → clamped to 16
```

The 2x multiplier provides headroom for reordering.

### Mutex Initialization

```c
if (pthread_mutex_init(&jb->lock, NULL) != 0) {
    LOG_ERROR("JB: Failed to initialize mutex");
    free(jb->packets);
    free(jb);
    return NULL;
}
```

Initializes the mutex with default attributes. Proper cleanup on failure prevents memory leaks.

### Packet Duration

```c
jb->packet_duration_ns = (uint64_t)cfg->samples_per_packet * 1000000000ULL /
                         cfg->sample_rate;
```

For 48 samples at 48kHz:
```
duration = 48 * 1,000,000,000 / 48000 = 1,000,000 ns = 1 ms
```

### Loss Threshold Calculation

```c
jb->loss_threshold = packets_per_ms * cfg->depth_ms * 2;
if (jb->loss_threshold < 4) jb->loss_threshold = 4;
```

This properly calculates the threshold based on actual packets-per-millisecond, rather than assuming 1 packet/ms. This ensures correct behavior for non-standard packet sizes.

---

## Buffer Destruction

```c
void audyn_jb_destroy(audyn_jitter_buffer_t *jb)
{
    if (!jb) {
        return;
    }

    LOG_DEBUG("JB: Destroying - received=%lu played=%lu lost=%lu late=%lu reordered=%lu",
              ...);

    pthread_mutex_destroy(&jb->lock);
    free(jb->packets);
    free(jb);
}
```

Cleanup order:
1. Log final statistics
2. Destroy mutex
3. Free packet buffer
4. Free structure

---

## Slot Indexing

```c
static inline uint32_t seq_to_index(audyn_jitter_buffer_t *jb, uint16_t seq)
{
    return seq % jb->buffer_size;
}
```

Maps any sequence number to a slot in the circular buffer.

### Collision Handling

This modulo indexing means sequence numbers that differ by `buffer_size` will map to the same slot. The implementation now detects and logs these collisions (see insert function).

---

## Packet Insertion

```c
int audyn_jb_insert(audyn_jitter_buffer_t *jb,
                    uint16_t seq,
                    uint32_t rtp_ts,
                    uint64_t arrival_ns,
                    const uint8_t *payload,
                    uint32_t payload_len)
```

### Thread Safety

```c
pthread_mutex_lock(&jb->lock);
// ... all operations ...
pthread_mutex_unlock(&jb->lock);
```

The entire insert operation is protected by the mutex. Early returns must unlock before returning.

### First Packet Initialization

```c
if (!jb->initialized) {
    jb->next_seq = seq;
    jb->highest_seq = seq;
    jb->playout_time_ns = arrival_ns + (jb->cfg.depth_ms * NS_PER_MS);
    jb->initialized = 1;
}
```

On first packet:
- Start sequence tracking from this packet
- Set playout time to `arrival + depth_ms` (the buffering delay)

### Late Packet Detection

```c
int16_t delta_from_next = seq_compare(seq, jb->next_seq);
if (delta_from_next < 0) {
    if (delta_from_next > -SEQ_MAX_DELTA) {
        jb->stats.packets_late++;
        pthread_mutex_unlock(&jb->lock);
        return -1;
    } else {
        /* Large backward jump - reset */
        jb_reset_unlocked(jb);
        /* ... reinitialize ... */
    }
}
```

If packet sequence is behind `next_seq`:
- Small gap (< 1000): Packet arrived too late, discard it
- Large gap (>= 1000): Probably a new stream, reset buffer

Note: Uses `jb_reset_unlocked()` since we already hold the lock.

### Reordering Detection

```c
int16_t delta_from_highest = seq_compare(seq, jb->highest_seq);
if (delta_from_highest < 0 && delta_from_highest > -SEQ_MAX_DELTA) {
    jb->stats.packets_reordered++;
} else if (delta_from_highest > 0) {
    jb->highest_seq = seq;
}
```

If packet is behind highest but within valid range, it's reordered.

### Sliding Window Recovery (Buffer Overflow)

```c
int16_t delta_ahead = seq_compare(seq, jb->next_seq);
if (delta_ahead >= (int16_t)jb->buffer_size) {
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
```

Instead of rejecting packets that are too far ahead (which would cause permanent rejection), we advance the sliding window:
1. Calculate how many positions to advance
2. Mark skipped packets as lost (if they weren't received)
3. Clear the slots
4. Advance `next_seq` and `playout_time_ns`

This maintains stream continuity even during network bursts.

### Duplicate Detection

```c
if (slot->valid && slot->seq == seq) {
    LOG_DEBUG("JB: Duplicate packet seq=%u", seq);
    pthread_mutex_unlock(&jb->lock);
    return 0;
}
```

Duplicate packets are silently accepted (return 0, not error).

### Slot Collision Detection

```c
if (slot->valid && slot->seq != seq) {
    jb->stats.packets_lost++;
    LOG_INFO("JB: Slot collision - seq=%u overwrites seq=%u at slot %u",
             seq, slot->seq, index);
}
```

If a slot contains a valid packet with a *different* sequence number, we have a collision. This can happen if:
- Network jitter causes packets to spread wider than `buffer_size`
- The buffer is undersized for the actual network conditions

The old packet is counted as lost and overwritten.

### Slot Storage

```c
slot->valid = 1;
slot->seq = seq;
slot->rtp_ts = rtp_ts;
slot->arrival_ptp_ns = arrival_ns;
slot->payload_len = payload_len;
memcpy(slot->payload, payload, payload_len);
```

Store packet data in the slot.

---

## Packet Retrieval

```c
audyn_jb_packet_t *audyn_jb_get(audyn_jitter_buffer_t *jb)
```

### Thread Safety

```c
pthread_mutex_lock(&jb->lock);
// ... operations ...
pthread_mutex_unlock(&jb->lock);
```

### Normal Case

```c
if (slot->valid && slot->seq == jb->next_seq) {
    slot->valid = 0;  /* Mark as consumed */
    jb->next_seq++;
    jb->playout_time_ns += jb->packet_duration_ns;
    jb->stats.packets_played++;
    pthread_mutex_unlock(&jb->lock);
    return slot;
}
```

If the expected packet is present:
1. Mark slot as consumed
2. Advance sequence counter
3. Advance playout time
4. Return packet pointer

### Missing Packet (Loss Detection)

```c
int16_t gap = seq_compare(jb->highest_seq, jb->next_seq);
if (gap > (int16_t)jb->loss_threshold) {
    jb->stats.packets_lost++;
    jb->next_seq++;
    jb->playout_time_ns += jb->packet_duration_ns;
}
pthread_mutex_unlock(&jb->lock);
return NULL;
```

If we've received packets far beyond the missing one (more than `loss_threshold`), declare it lost:
- Increment loss counter
- Advance past the missing packet
- Return NULL (caller should insert silence)

---

## Readiness Check

```c
int audyn_jb_ready(audyn_jitter_buffer_t *jb, uint64_t current_ns)
```

### Time Check

```c
if (current_ns < jb->playout_time_ns) {
    pthread_mutex_unlock(&jb->lock);
    return 0;  /* Not time yet */
}
```

### Packet or Loss Check

```c
if (slot->valid && slot->seq == jb->next_seq) {
    pthread_mutex_unlock(&jb->lock);
    return 1;  /* Packet ready */
}

int16_t gap = seq_compare(jb->highest_seq, jb->next_seq);
if (gap > (int16_t)jb->loss_threshold) {
    pthread_mutex_unlock(&jb->lock);
    return 1;  /* Time to report loss */
}

pthread_mutex_unlock(&jb->lock);
return 0;  /* Still waiting */
```

Returns 1 if either:
- The next packet is available
- The next packet should be declared lost

---

## Buffer Reset

### Internal (Unlocked) Version

```c
static void jb_reset_unlocked(audyn_jitter_buffer_t *jb)
{
    for (uint32_t i = 0; i < jb->buffer_size; i++) {
        jb->packets[i].valid = 0;
    }

    jb->initialized = 0;
    jb->next_seq = 0;
    jb->highest_seq = 0;
    jb->playout_time_ns = 0;
    jb->stats.current_depth = 0;

    LOG_DEBUG("JB: Reset");
}
```

Called internally when lock is already held.

### Public Version

```c
void audyn_jb_reset(audyn_jitter_buffer_t *jb)
{
    if (!jb) {
        return;
    }

    pthread_mutex_lock(&jb->lock);
    jb_reset_unlocked(jb);
    pthread_mutex_unlock(&jb->lock);
}
```

Acquires lock, calls internal version, releases lock.

---

## Statistics Access

```c
void audyn_jb_get_stats(audyn_jitter_buffer_t *jb, audyn_jb_stats_t *stats)
{
    if (!jb || !stats) {
        return;
    }
    pthread_mutex_lock(&jb->lock);
    *stats = jb->stats;
    pthread_mutex_unlock(&jb->lock);
}

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
```

Both functions acquire the lock to ensure consistent reads.

---

## Data Flow Diagram

```
Network Thread                     Audio Thread
     │                                  │
     ▼                                  ▼
┌─────────┐                      ┌──────────┐
│ insert()│                      │   get()  │
│  +lock  │                      │  +lock   │
└────┬────┘                      └────┬─────┘
     │                                │
     ▼                                ▼
┌─────────────────────────────────────────────┐
│           Circular Buffer                    │
│  ┌───┬───┬───┬───┬───┬───┬───┬───┐         │
│  │ 0 │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ ...     │
│  └───┴───┴───┴───┴───┴───┴───┴───┘         │
│       ▲                   ▲                 │
│       │                   │                 │
│   next_seq            highest_seq           │
│   (playout)           (received)            │
└─────────────────────────────────────────────┘
```

---

## Timing Model

```
Time ──────────────────────────────────────────────────►

Packet arrives:     ├── depth_ms ──┤
                    │              │
              arrival_ns     playout_time_ns

Each get() advances playout_time_ns by packet_duration_ns
```

---

## Thread Safety Model

```
┌────────────────────┐          ┌────────────────────┐
│   Network Thread   │          │   Audio Thread     │
│                    │          │                    │
│  audyn_jb_insert() │          │  audyn_jb_ready()  │
│         │          │          │         │          │
│         ▼          │          │         ▼          │
│  ┌─────────────┐   │          │  ┌─────────────┐   │
│  │ mutex_lock  │   │          │  │ mutex_lock  │   │
│  └──────┬──────┘   │          │  └──────┬──────┘   │
│         │          │          │         │          │
│         ▼          │          │         ▼          │
│  ┌─────────────┐   │          │  ┌─────────────┐   │
│  │  modify     │◄──┼── mutex ─┼──►  read       │   │
│  │  buffer     │   │          │  │  buffer     │   │
│  └──────┬──────┘   │          │  └──────┬──────┘   │
│         │          │          │         │          │
│         ▼          │          │         ▼          │
│  ┌─────────────┐   │          │  ┌─────────────┐   │
│  │mutex_unlock │   │          │  │mutex_unlock │   │
│  └─────────────┘   │          │  └─────────────┘   │
└────────────────────┘          └────────────────────┘
```

The mutex ensures that:
- `insert()` and `get()` never corrupt shared state
- Statistics are read consistently
- Reset operations are atomic

---

## Guarantees

- **Correct 16-bit sequence wraparound handling** via signed subtraction
- **Thread-safe** for concurrent insert/get from different threads
- **Bounded memory usage** with fixed buffer size
- **Graceful handling** of late, lost, reordered, and duplicate packets
- **Slot collision detection** with logging and loss accounting
- **Sliding window recovery** maintains stream continuity during overflows
- **Stream discontinuity detection** with automatic reset
- **Proper loss threshold** calculation for any packet configuration

---

## Usage Pattern

```c
/* Create */
audyn_jb_cfg_t cfg = {
    .sample_rate = 48000,
    .channels = 2,
    .bits_per_sample = 24,
    .samples_per_packet = 48,
    .depth_ms = 4
};
audyn_jitter_buffer_t *jb = audyn_jb_create(&cfg);

/* Network receive thread */
void *network_thread(void *arg) {
    while (running) {
        /* Receive RTP packet... */
        audyn_jb_insert(jb, seq, rtp_ts, arrival_ns, payload, len);
    }
}

/* Audio playout thread */
void *audio_thread(void *arg) {
    while (running) {
        if (audyn_jb_ready(jb, current_ptp_ns)) {
            audyn_jb_packet_t *pkt = audyn_jb_get(jb);
            if (pkt) {
                play_audio(pkt->payload, pkt->payload_len);
            } else {
                play_silence();  /* Packet loss concealment */
            }
        }
    }
}

/* Cleanup */
audyn_jb_destroy(jb);
```
