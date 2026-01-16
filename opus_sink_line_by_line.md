# Opus Sink Implementation Review

## Overview

The `opus_sink` module implements a non-real-time audio sink that encodes interleaved float32 PCM audio into Ogg Opus (.opus) files using libopus and libogg. It is designed to be called from Audyn's worker thread, not from real-time audio callbacks.

**Files:**
- `sink/opus_sink.h` - Public API header
- `sink/opus_sink.c` - Implementation

## Key Design Decisions

### 1. Fixed 20ms Opus Frames

Opus accepts specific frame sizes (2.5/5/10/20/40/60ms). The sink always uses 20ms frames:
```c
frame_size = sample_rate / 50
```

This is valid for all Opus-supported sample rates (8/12/16/24/48 kHz) and provides a good balance between latency and compression efficiency.

### 2. FIFO Buffering for Arbitrary Input Sizes

Since Audyn audio frames may not align with 20ms boundaries, the sink uses a FIFO buffer:
- Input audio is appended to the FIFO
- When FIFO contains >= frame_size samples, encode one Opus frame
- Consume encoded samples from FIFO front

This allows the sink to accept any input block size while producing valid Opus frames.

### 3. Granulepos in 48kHz Units

Per Ogg Opus specification, granulepos is always expressed in 48kHz sample units regardless of the actual sample rate. The sink tracks this with:
```c
ogg_int64_t granulepos_48k;
```

The granulepos starts at `-preskip` (typically -312) and increments by `frames_to_48k(frame_size)` for each encoded packet.

### 4. Single-Threaded Design

Unlike jitter_buffer and ptp_clock, the opus_sink is explicitly **not thread-safe**. This is intentional:
- It's designed for a single worker thread
- Avoids mutex overhead for non-shared resource
- File I/O inherently serializes operations

## Structure Definition

```c
struct audyn_opus_sink
{
    FILE *fp;                   /* Output file handle */
    int fd;                     /* File descriptor for fsync */

    audyn_opus_cfg_t cfg;       /* Configuration copy */
    char *path;                 /* Output path (for error messages) */

    ogg_stream_state os;        /* libogg stream state */
    int ogg_inited;             /* 1 if ogg_stream_init succeeded */

    OpusEncoder *enc;           /* libopus encoder handle */

    unsigned char *pkt;         /* Opus packet buffer (reused) */
    int pkt_cap;                /* Packet buffer capacity */

    float *fifo;                /* Input FIFO (interleaved float32) */
    uint32_t fifo_cap_frames;   /* FIFO capacity in frames */
    uint32_t fifo_len_frames;   /* Current FIFO length in frames */
    uint32_t frame_size;        /* Opus frame size (samples per channel) */

    ogg_int64_t granulepos_48k; /* Granule position in 48kHz units */
    ogg_int64_t packetno;       /* Ogg packet sequence number */

    uint16_t preskip_48k;       /* OpusHead preskip (48kHz samples) */

    int wrote_audio;            /* 1 if any audio packets written */
    int eos_written;            /* 1 if EOS packet written */
    int closed;                 /* 1 if close() was called */

    audyn_opus_stats_t stats;   /* Encoding statistics */
};
```

## Function-by-Function Analysis

### Helper Functions

#### `le16()` / `le32()` (lines 105-117)
Little-endian encoding helpers for writing Ogg Opus headers. Required because Ogg Opus specifies little-endian header fields.

#### `choose_frame_size()` (lines 119-123)
Returns 20ms frame size for the given sample rate (sample_rate / 50).

#### `opus_frame_size_is_valid()` (lines 125-137)
Validates that a frame size is one of Opus's supported sizes (2.5/5/10/20/40/60ms).

#### `make_serial()` (lines 139-158)
Generates a unique serial number for the Ogg stream:
1. First tries `/dev/urandom` for proper randomness
2. Falls back to time^pid mixing if urandom unavailable

**Improvement:** Previous version used only time^pid which could collide for concurrent streams.

#### `write_page()` (lines 160-164)
Writes an Ogg page to the output file, optionally calling fsync if enabled.

#### `flush_pages()` (lines 166-181)
Flushes pending Ogg pages from the stream. Can force flush or let libogg decide page boundaries.

#### `write_opus_headers()` (lines 183-269)
Writes the required Ogg Opus headers:

**OpusHead (19 bytes):**
- "OpusHead" magic (8 bytes)
- Version: 1 (1 byte)
- Channel count (1 byte)
- Pre-skip: 312 samples @ 48kHz (2 bytes LE)
- Original sample rate (4 bytes LE, informational)
- Output gain: 0 dB (2 bytes LE)
- Channel mapping family: 0 (1 byte, mono/stereo)

**OpusTags:**
- "OpusTags" magic (8 bytes)
- Vendor string length + string
- User comment count + comments ("ENCODER=Audyn")

After headers, initializes granulepos to `-preskip` so first audio packet has correct position.

#### `ensure_fifo_capacity()` (lines 271-289)
Grows the FIFO buffer if needed. Doubles capacity up to 1MB frames, then grows linearly.

#### `fifo_consume_frames()` (lines 291-302)
Removes frames from the front of the FIFO using memmove.

#### `frames_to_48k()` (lines 304-309)
Converts frame count from native sample rate to 48kHz units for granulepos calculation.

### Public API

#### `audyn_opus_sink_create()` (lines 323-477)

Creates and initializes an Opus sink.

**Validation:**
- Path and config must be non-NULL
- Channels must be 1 or 2
- Sample rate must be 8000/12000/16000/24000/48000
- Complexity clamped to 0-10
- Bitrate clamped to 6000-510000 bps (Opus limits)
- Invalid application mode defaults to AUDYN_OPUS_APP_AUDIO

**Initialization sequence:**
1. Allocate sink structure
2. Store path copy (for error messages)
3. Open output file
4. Validate fileno() result (disable fsync if -1)
5. Initialize libogg stream with random serial
6. Create libopus encoder
7. Configure encoder (bitrate, VBR, complexity)
8. Allocate packet buffer (4096 bytes)
9. Allocate FIFO buffer (2x frame_size)
10. Write Ogg Opus headers
11. Initialize statistics

**Error handling:** Each step has proper cleanup on failure, including logging.

#### `audyn_opus_sink_write()` (lines 480-568)

Encodes and writes audio data.

**Process:**
1. Validate sink is open and not closed
2. Check for integer overflow on FIFO length
3. Enforce maximum FIFO size (10 seconds @ 48kHz)
4. Expand FIFO capacity if needed
5. Copy input data to FIFO
6. Update statistics (frames_in)
7. While FIFO has >= frame_size samples:
   - Encode one Opus frame
   - Create Ogg packet with correct granulepos
   - Submit to Ogg stream
   - Update statistics (frames_encoded, packets_encoded, bytes_encoded)
   - Flush pages if libogg decides
   - Consume encoded frames from FIFO

**Protections:**
- Integer overflow check before adding to fifo_len_frames
- FIFO size limit prevents runaway memory usage
- Encoder errors are logged with opus_strerror()

#### `audyn_opus_sink_flush()` (lines 571-574)

Forces a flush of any buffered Ogg pages to disk.

#### `pad_and_encode_final()` (lines 598-642)

Internal function to encode any remaining partial audio at close time:
1. Zero-pad FIFO to full frame size
2. Encode the padded frame
3. Mark packet with EOS flag

#### `write_eos_marker()` (lines 576-596)

Writes an empty Ogg packet with EOS flag. Used when audio frames were perfectly aligned (no padding needed).

#### `audyn_opus_sink_close()` (lines 646-689)

Finalizes the Ogg stream and closes the file:
1. Encode any remaining partial audio (pad_and_encode_final)
2. Write EOS marker if needed
3. Clear libogg stream
4. Destroy Opus encoder
5. Flush and close file
6. Log final statistics

#### `audyn_opus_sink_destroy()` (lines 692-714)

Cleanup function:
1. Call close() if not already closed
2. Free packet buffer
3. Free FIFO buffer
4. Free path string
5. Free structure

Safe to call with NULL.

#### `audyn_opus_sink_get_stats()` (lines 717-721)

Returns encoding statistics:
- frames_in: Total input frames received
- frames_encoded: Total frames encoded (includes padding)
- packets_encoded: Total Opus packets written
- bytes_encoded: Total compressed bytes
- fifo_overflows: FIFO overflow events (data dropped)

## Improvements Made

### 1. Added Logging Support
- Included project's log.h
- Added LOG_ERROR for all failure paths with descriptive messages
- Added LOG_INFO for configuration clamping and creation
- Added LOG_DEBUG for close statistics

### 2. Bitrate Validation
- Added AUDYN_OPUS_BITRATE_MIN (6000) and AUDYN_OPUS_BITRATE_MAX (510000)
- Named with AUDYN_ prefix to avoid conflict with libopus's OPUS_BITRATE_MAX macro
- Invalid bitrates are clamped with logging

### 3. FIFO Size Limit
- Added FIFO_MAX_FRAMES (10 seconds at 48kHz)
- Prevents runaway memory allocation from buggy callers
- Returns error and increments fifo_overflows stat

### 4. Integer Overflow Protection
- Check `frames > UINT32_MAX - fifo_len_frames` before addition
- Prevents wraparound that could corrupt memory

### 5. Improved Serial Number Generation
- Now tries /dev/urandom first for proper randomness
- Falls back to time^pid mixing
- Better uniqueness for concurrent streams

### 6. fileno() Error Handling
- Check for fileno() returning -1
- Disable fsync if file descriptor invalid

### 7. Statistics Tracking
- Added audyn_opus_stats_t structure
- Track frames_in, frames_encoded, packets_encoded, bytes_encoded
- Track fifo_overflows for diagnostics
- Added audyn_opus_sink_get_stats() getter

### 8. Path Storage for Error Messages
- Store output path copy in structure
- Use in error/debug log messages for context

## Ogg Opus Format Details

### Granulepos Calculation

Ogg Opus granulepos represents the PCM sample count at 48kHz after preskip:
```
Initial:    granulepos = -preskip = -312
After 1st:  granulepos = -312 + 960 = 648  (20ms @ 48kHz)
After 2nd:  granulepos = 648 + 960 = 1608
...
```

### Preskip

The preskip value (312 samples @ 48kHz, ~6.5ms) accounts for:
1. Opus encoder lookahead
2. Internal resampling delay

Decoders must discard preskip samples at the start of playback.

### EOS Handling

Two cases for end-of-stream:
1. **Partial final frame:** Pad with zeros, encode, set EOS flag on that packet
2. **Aligned final frame:** Write an empty packet with just the EOS flag

Both produce valid Ogg Opus files per specification.

## Thread Safety

This module is **not thread-safe** by design:
- Single-threaded use by worker thread
- No mutex overhead needed
- File I/O naturally serializes

If concurrent access is ever needed, add a mutex around all state modifications.

## Dependencies

- **libopus:** Opus encoder (opus_encoder_create, opus_encode_float, etc.)
- **libogg:** Ogg container (ogg_stream_*, ogg_page, ogg_packet)
- **POSIX:** open(), read(), close() for /dev/urandom
- **Standard C:** stdio, stdlib, string, time, unistd

## Usage Example

```c
audyn_opus_cfg_t cfg = {
    .sample_rate = 48000,
    .channels = 2,
    .bitrate = 128000,
    .vbr = 1,
    .complexity = 5,
    .application = AUDYN_OPUS_APP_AUDIO,
    .enable_fsync = 0
};

audyn_opus_sink_t *sink = audyn_opus_sink_create("/tmp/output.opus", &cfg);
if (!sink) {
    // Error logged internally
    return -1;
}

// Write audio blocks (any size)
while (have_audio) {
    float *pcm = get_audio_block(&frames);
    if (audyn_opus_sink_write(sink, pcm, frames) != 0) {
        // Error logged internally
        break;
    }
}

// Get final statistics
audyn_opus_stats_t stats;
audyn_opus_sink_get_stats(sink, &stats);
printf("Encoded %lu frames into %lu bytes\n",
       stats.frames_encoded, stats.bytes_encoded);

// Cleanup
audyn_opus_sink_destroy(sink);
```
