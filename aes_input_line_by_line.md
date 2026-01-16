# AES67 Input Implementation Review

## Overview

The `aes_input` module implements AES67/RTP audio reception for the Audyn audio capture engine. It receives uncompressed linear PCM audio over RTP multicast or unicast and converts it into Audyn audio frames for downstream processing.

**Files:**
- `input/aes_input.h` - Public API header
- `input/aes_input.c` - Implementation

## Standards Supported

- **AES67:** Professional audio-over-IP (RTP/PCM)
- **SMPTE ST 2110-30:** Subset for uncompressed audio

## PCM Formats Supported

| Format | Description | Bytes/Sample |
|--------|-------------|--------------|
| L16 | 16-bit signed PCM, big-endian | 2 |
| L24 | 24-bit signed PCM, big-endian | 3 |

Format is auto-detected from payload size: `payload_len == channels * samples_per_packet * bytes_per_sample`

## Limits

```c
#define AES_MAX_SAMPLE_RATE 384000
#define AES_MAX_CHANNELS 32
#define AES_MAX_SAMPLES_PER_PACKET 1024
```

## Structure Definitions

### Configuration Structure
```c
typedef struct audyn_aes_input_cfg {
    const char *source_ip;          /* Multicast/unicast IP */
    uint16_t    port;               /* UDP port (e.g., 5004) */
    uint8_t     payload_type;       /* RTP payload type (0-127) */

    uint32_t    sample_rate;        /* 1-384000 Hz */
    uint16_t    channels;           /* 1-32 */
    uint16_t    samples_per_packet; /* 1-1024 (AES67: typically 48 for 1ms) */

    uint32_t    socket_rcvbuf;      /* Socket receive buffer size */
    const char *bind_interface;     /* Network interface for multicast */
} audyn_aes_input_cfg_t;
```

### Statistics Structure
```c
typedef struct audyn_aes_stats {
    uint64_t packets_rx;          /* Total RTP packets received */
    uint64_t packets_dropped;     /* Invalid RTP, wrong PT, etc. */
    uint64_t discontinuities;     /* RTP sequence discontinuities */
    uint64_t frames_pushed;       /* Frames pushed to queue */
    uint64_t frames_dropped_pool; /* Pool exhaustion drops */
    uint64_t frames_dropped_queue;/* Queue full drops */
} audyn_aes_stats_t;
```

### Internal Structure
```c
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
    audyn_ptp_clock_t *ptp_clk;
    int hw_timestamps_enabled;
    int ptp_epoch_set;

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
```

## Function-by-Function Analysis

### RTP Parsing Helpers

#### `rd_be16()` (lines 94-96)
Reads 16-bit big-endian value from byte buffer.

#### `rd_be24s()` (lines 98-102)
Reads 24-bit big-endian signed value with sign extension to int32.

#### `s16_to_f32()` (lines 105-107)
Converts 16-bit signed PCM to float32 in range [-1, 1).
```c
return (float)s / 32768.0f;
```

#### `s24_to_f32()` (lines 108-110)
Converts 24-bit signed PCM to float32 in range [-1, 1).
```c
return (float)s / 8388608.0f;
```

### Error Handling

#### `set_error()` (lines 152-157)
Sets error message with mutex protection.

#### `set_error_errno()` (lines 159-164)
Sets error message including errno string.

### State Management

#### `stop_is_requested()` (lines 166-172)
Thread-safe check of stop flag.

#### `request_stop()` (lines 174-178)
Thread-safe request to stop.

### Network Setup

#### `is_ipv4_multicast()` (lines 180-187)
Checks if IP is in multicast range (224.0.0.0/4).
```c
uint32_t host = ntohl(a.s_addr);
return (host >= 0xE0000000u) && (host <= 0xEFFFFFFFu);
```

#### `open_socket()` (lines 189-293)

Opens and configures the UDP socket.

**Process:**
1. Create UDP socket
2. Set SO_REUSEADDR
3. Set SO_RCVBUF if configured
4. Set receive timeout (100ms for clean shutdown)
5. If PTP clock configured:
   - Hardware mode: enable SO_TIMESTAMPING with RX_HARDWARE
   - Software mode: enable SO_TIMESTAMPING with RX_SOFTWARE
6. Bind to INADDR_ANY with configured port
7. If multicast address:
   - Configure IP_ADD_MEMBERSHIP
   - Bind to specific interface if configured

### Packet Processing

#### `handle_packet()` (lines 295-455)

Processes a single RTP packet.

**RTP Header Parsing:**
1. Validate minimum header size (12 bytes)
2. Extract version, padding, extension, CSRC count
3. Validate version == 2
4. Validate payload type matches config
5. Skip CSRC list (4 bytes each)
6. Skip header extension if present
7. Handle padding (last byte indicates count)

**Sequence Tracking:**
1. Extract sequence number
2. Track discontinuities (missing or reordered packets)
3. Set RTP epoch on first packet with PTP clock

**Payload Processing:**
1. Calculate expected payload size for L16 and L24
2. Auto-detect format from actual payload size
3. Acquire frame from pool
4. Validate frame shape matches config
5. Convert PCM samples to float32 (interleaved)
6. Push frame to queue

**Statistics tracked:**
- `packets_dropped` - Invalid RTP, wrong PT, bad payload size
- `discontinuities` - Sequence number gaps
- `frames_dropped_pool_empty` - Pool exhausted
- `frames_dropped_queue_full` - Queue full
- `frames_pushed` - Success count

#### `extract_timestamp()` (lines 457-488)

Extracts packet timestamp from socket control messages.

**Process:**
1. Iterate control messages looking for SO_TIMESTAMPING
2. Prefer hardware timestamp (ts[2]) if enabled and valid
3. Fall back to software timestamp (ts[0])
4. Fall back to PTP clock if no socket timestamp

### Receive Thread

#### `rx_thread_main()` (lines 490-543)

Main receive loop running in dedicated thread.

**Setup:**
1. Set thread name to "audyn-aes-rx"
2. Prepare iovec and msghdr for recvmsg()

**Main Loop:**
```c
while (!stop_is_requested(in)) {
    n = recvmsg(in->sock_fd, &msg, 0);
    if (n < 0) {
        // Handle errors, timeouts
        continue;
    }

    // Extract hardware/software timestamp
    arrival_ns = extract_timestamp(&msg, in);

    in->packets_rx++;
    handle_packet(in, buf, n, arrival_ns);
}
```

**Error Handling:**
- EINTR: continue
- EAGAIN/EWOULDBLOCK: continue (timeout, allows stop check)
- Other errors: log, sleep 10ms, continue

### Public API

#### `audyn_aes_input_create()` (lines 547-645)

Creates an AES67 input instance.

**Validation:**
- Pool and queue must be non-NULL
- Source IP must be non-empty
- Port must be non-zero
- Sample rate: 1-384000 Hz
- Channels: 1-32
- Samples per packet: 1-1024
- Payload type: 0-127

**Initialization:**
1. Allocate structure (calloc zeros counters)
2. Copy config strings (source_ip, bind_interface)
3. Initialize mutexes
4. Log creation

#### `audyn_aes_input_start()` (lines 647-673)

Starts the receive thread.

**Process:**
1. Clear stop flag
2. Open socket (with PTP timestamping if configured)
3. Create receive thread
4. Log start

#### `audyn_aes_input_stop()` (lines 675-703)

Stops the receive thread.

**Process:**
1. Request stop
2. Close socket (unblocks recvmsg)
3. Join thread
4. Log statistics

#### `audyn_aes_input_destroy()` (lines 705-713)

Cleanup function.

**Process:**
1. Stop if running
2. Destroy mutexes
3. Free owned strings
4. Free structure

#### `audyn_aes_input_is_running()` (lines 740-743)

Returns 1 if thread is running, 0 otherwise.

#### `audyn_aes_input_get_last_error()` (lines 745-760)

Thread-safe error message retrieval. Copies to caller's buffer.

#### `audyn_aes_input_last_error()` (lines 715-723)

**Deprecated.** Returns pointer to internal storage.

#### `audyn_aes_input_get_stats()` (lines 762-777)

Retrieves statistics. Safe to call while running.

#### `audyn_aes_input_set_ptp_clock()` (lines 725-738)

Sets PTP clock for timestamping. Must be called before start().

## Improvements Made

### 1. Input Validation
- Added limits for sample rate, channels, samples_per_packet
- Added logging for all validation failures
- Descriptive error messages

### 2. String Ownership
- source_ip and bind_interface are now copied via strdup()
- Caller can free their copies after create()
- No more dangling pointer risk

### 3. Statistics Query
- Added `audyn_aes_stats_t` structure
- Added `audyn_aes_input_get_stats()` for runtime monitoring
- Statistics logged on stop

### 4. Thread-Safe Error Access
- Added `audyn_aes_input_get_last_error()` that copies to caller buffer
- Deprecated raw pointer version with warning

### 5. Running State Query
- Added `audyn_aes_input_is_running()` function

### 6. Comprehensive Logging
- Creation logs configuration
- Start/stop transitions logged
- Final statistics logged on stop
- All errors logged

### 7. Removed Unused Fields
- Removed `jitter_buf` (never used)
- Removed `hw_timestamp_failures` counter (never incremented)

## PTP Timestamping

The module supports three timestamping modes:

| Mode | Socket Options | Description |
|------|----------------|-------------|
| Hardware | SOF_TIMESTAMPING_RX_HARDWARE | NIC timestamps packets (requires driver support) |
| Software | SOF_TIMESTAMPING_RX_SOFTWARE | Kernel timestamps on receive |
| Fallback | None | Uses PTP clock at packet processing time |

Hardware timestamps provide the most accurate timing for:
- RTP-to-PTP epoch correlation
- Jitter measurement
- Lip-sync correction

## Threading Model

```
┌─────────────────┐
│  Main Thread    │
│  (lifecycle)    │
└────────┬────────┘
         │
         │ pthread_create
         ▼
┌─────────────────┐
│  RX Thread      │
│  (recvmsg loop) │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Frame Pool     │◄─────┐
│  (lock-free)    │      │
└────────┬────────┘      │
         │               │
         ▼               │
┌─────────────────┐      │
│  Audio Queue    │──────┘
│  (lock-free)    │ (worker releases)
└─────────────────┘
```

## Usage Example

```c
/* Create pool and queue */
audyn_frame_pool_t *pool = audyn_frame_pool_create(256, 2, 1024);
audyn_audio_queue_t *queue = audyn_audio_queue_create(1024);

/* Configure AES67 input */
audyn_aes_input_cfg_t cfg = {
    .source_ip = "239.69.1.1",
    .port = 5004,
    .payload_type = 96,
    .sample_rate = 48000,
    .channels = 2,
    .samples_per_packet = 48,
    .socket_rcvbuf = 2097152,
    .bind_interface = "eth0"
};

/* Create input */
audyn_aes_input_t *aes = audyn_aes_input_create(pool, queue, &cfg);
if (!aes) {
    // Error logged internally
    return -1;
}

/* Optionally set PTP clock */
audyn_ptp_clock_t *ptp = audyn_ptp_clock_create(&ptp_cfg);
audyn_aes_input_set_ptp_clock(aes, ptp);

/* Start capture */
if (audyn_aes_input_start(aes) != 0) {
    char err[256];
    audyn_aes_input_get_last_error(aes, err, sizeof(err));
    printf("Start failed: %s\n", err);
    audyn_aes_input_destroy(aes);
    return -1;
}

/* Monitor while running */
while (running) {
    audyn_aes_stats_t stats;
    audyn_aes_input_get_stats(aes, &stats);

    printf("RX: %lu packets, %lu frames, %lu disc\n",
           stats.packets_rx, stats.frames_pushed, stats.discontinuities);

    if (stats.frames_dropped_pool > 0 || stats.frames_dropped_queue > 0) {
        printf("Warning: drops - pool=%lu queue=%lu\n",
               stats.frames_dropped_pool, stats.frames_dropped_queue);
    }

    sleep(1);
}

/* Cleanup */
audyn_aes_input_stop(aes);
audyn_aes_input_destroy(aes);
audyn_ptp_clock_destroy(ptp);
```

## Diagnostics

### Common Issues

| Symptom | Likely Cause | Solution |
|---------|--------------|----------|
| No packets_rx | Wrong IP/port | Verify multicast address and port |
| High packets_dropped | Wrong PT or SPP | Match payload type and samples_per_packet |
| High discontinuities | Network jitter | Increase socket buffer, check network |
| High frames_dropped_pool | Pool too small | Increase pool frame count |
| High frames_dropped_queue | Consumer slow | Optimize worker, increase queue |

### Monitoring

```c
audyn_aes_stats_t stats;
audyn_aes_input_get_stats(aes, &stats);

double drop_rate = 0;
if (stats.packets_rx > 0) {
    drop_rate = (double)stats.packets_dropped / stats.packets_rx * 100.0;
}

printf("AES67: rx=%lu dropped=%.2f%% disc=%lu pool_drop=%lu q_drop=%lu\n",
       stats.packets_rx, drop_rate, stats.discontinuities,
       stats.frames_dropped_pool, stats.frames_dropped_queue);
```

## Dependencies

- **POSIX:** sockets, pthread
- **Linux:** SO_TIMESTAMPING (optional, for PTP)
- **Audyn:** frame_pool, audio_queue, ptp_clock, log
