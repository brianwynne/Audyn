# WAV Sink Implementation Review

## Overview

The `wav_sink` module implements a minimal PCM16 WAV file writer (RIFF/WAVE format). It accepts interleaved float32 audio and writes 16-bit little-endian PCM samples with a standard 44-byte header.

**Files:**
- `sink/wav_sink.h` - Public API header
- `sink/wav_sink.c` - Implementation

## Key Design Decisions

### 1. Placeholder Header with Seek-Back Patching

The WAV format requires file and data chunk sizes in the header. Since these aren't known until all audio is written:
1. Write a 44-byte header with placeholder zeros for sizes
2. Append audio data, tracking bytes written
3. On close, seek back and patch the size fields

This is the standard approach for streaming WAV writers.

### 2. Float32 to PCM16 Conversion

Input audio is expected in [-1.0, +1.0] range and converted to 16-bit signed integers:
```c
int v = (int)(x * 32767.0f);
```

Values are clamped at both input (+/- 1.0) and output (+/- 32767/32768) to prevent overflow.

### 3. Chunked Writing

Audio is converted and written in 4096-sample chunks to avoid large stack allocations:
```c
enum { CHUNK = 4096 };
int16_t tmp[CHUNK];  /* ~8KB on stack */
```

### 4. RIFF/WAVE 4GB Limit

Classic RIFF/WAVE uses 32-bit size fields, limiting files to ~4GB. The sink:
- Tracks bytes written as `uint64_t`
- Returns error if write would exceed `UINT32_MAX`
- Future enhancement would be RF64 format support

### 5. Single-Threaded Design

Like opus_sink, this module is **not thread-safe**:
- Designed for worker thread use
- No mutex overhead
- File I/O naturally serializes

## Structure Definition

```c
struct audyn_wav_sink {
    audyn_wav_sink_cfg_t cfg;   /* Configuration copy */
    FILE    *fp;                /* Output file handle */
    int      fd;                /* File descriptor for fsync */
    char    *path;              /* Output path (for error messages) */
    uint32_t sample_rate;       /* Current file's sample rate */
    uint16_t channels;          /* Current file's channel count */
    uint64_t bytes_written;     /* Data chunk bytes written */

    audyn_wav_stats_t stats;    /* Writing statistics */
};
```

## Function-by-Function Analysis

### Helper Functions

#### `write_u16le()` / `write_u32le()` (lines 57-74)
Write 16-bit and 32-bit values in little-endian byte order. WAV format requires little-endian regardless of host architecture.

#### `write_header_placeholder()` (lines 76-103)
Writes the 44-byte RIFF/WAVE header structure:

| Offset | Size | Content |
|--------|------|---------|
| 0 | 4 | "RIFF" |
| 4 | 4 | File size - 8 (placeholder: 0) |
| 8 | 4 | "WAVE" |
| 12 | 4 | "fmt " |
| 16 | 4 | fmt chunk size: 16 |
| 20 | 2 | Audio format: 1 (PCM) |
| 22 | 2 | Channels |
| 24 | 4 | Sample rate |
| 28 | 4 | Byte rate |
| 32 | 2 | Block align |
| 34 | 2 | Bits per sample: 16 |
| 36 | 4 | "data" |
| 40 | 4 | Data size (placeholder: 0) |

#### `f32_to_i16()` (lines 216-227)
Converts float32 sample to int16:
1. Clamp input to [-1.0, +1.0]
2. Scale by 32767 (not 32768 to avoid overflow on +1.0)
3. Clamp result to [-32768, +32767]
4. Return as int16_t

### Public API

#### `audyn_wav_sink_create()` (lines 106-124)

Creates a WAV sink instance.

**Process:**
1. Allocate structure with calloc
2. Copy configuration or use defaults (PCM16, no fsync)
3. Initialize fd to -1
4. Zero statistics

**Improvement:** Added logging on allocation failure.

#### `audyn_wav_sink_destroy()` (lines 127-135)

Cleanup function:
1. Close file if open
2. Free path string
3. Free structure

Safe to call with NULL.

#### `audyn_wav_sink_open()` (lines 138-213)

Opens a WAV file for writing.

**Validation:**
- Sink must be non-NULL
- Path must be non-empty
- Sample rate: 1 to 384000 Hz
- Channels: 1 to 32

**Process:**
1. Close any previously-open file
2. Free old path string
3. Validate format is PCM16
4. Store path copy
5. Open file for writing
6. Get file descriptor (disable fsync if invalid)
7. Write header placeholder
8. Reset statistics
9. Log successful open

**Improvement:** Added comprehensive validation with descriptive error messages.

#### `audyn_wav_sink_write()` (lines 229-298)

Writes audio data to the WAV file.

**Validation:**
- Sink and file must be valid
- Audio data must be non-NULL
- Channel count must match open()
- Check for frames * channels overflow

**Process:**
1. Check 4GB size limit before writing
2. Convert and write in 4096-sample chunks
3. Update bytes_written counter
4. Update statistics
5. Optionally fsync

**Improvements:**
- Added overflow check for frames * channels multiplication
- Added statistics tracking
- Better error messages with file path

#### `audyn_wav_sink_sync()` (lines 301-320)

Flushes buffered data and optionally fsyncs.

**Process:**
1. fflush() the FILE stream
2. fsync() the file descriptor if valid

**Improvement:** Added error logging with file path.

#### `audyn_wav_sink_close()` (lines 323-395)

Finalizes the WAV file.

**Process:**
1. Flush any buffered data
2. Verify size doesn't exceed 32-bit limit
3. Calculate final RIFF and data chunk sizes
4. Seek to offset 4, patch RIFF size
5. Seek to offset 40, patch data size
6. Flush patched header
7. fsync if enabled (new)
8. Close file
9. Log final statistics

**Improvements:**
- Added fsync after header patching if enabled
- Added statistics logging on close
- Better error messages with file path

#### `audyn_wav_sink_get_stats()` (lines 397-401)

Returns writing statistics:
- frames_written: Total audio frames written
- bytes_written: Total data bytes (excluding header)
- size_limit_hit: 1 if 4GB limit was encountered

## Improvements Made

### 1. Path Storage for Error Messages
- Store output path in structure
- Include path in all error/debug log messages
- Freed on destroy

### 2. Statistics Tracking
- Added `audyn_wav_stats_t` structure
- Track frames_written, bytes_written
- Track size_limit_hit flag
- Added `audyn_wav_sink_get_stats()` getter

### 3. Input Validation
- Sample rate limited to 1-384000 Hz (WAV_MAX_SAMPLE_RATE)
- Channels limited to 1-32 (WAV_MAX_CHANNELS)
- Descriptive error messages for validation failures

### 4. Overflow Protection
- Check frames * channels multiplication for overflow
- Prevents potential memory corruption on 32-bit systems

### 5. fsync on Close
- If enable_fsync is set, also fsync after patching header
- Ensures file is durable before returning from close()

### 6. Improved Error Logging
- All error paths now have LOG_ERROR with descriptive messages
- Include file path in messages for debugging
- Include strerror(errno) for system errors

### 7. File Descriptor Handling
- Store fd from fileno() on open
- Validate fd before using for fsync
- Reset fd to -1 on close

## WAV Format Details

### Header Structure (44 bytes)

```
Offset  Length  Contents
0       4       "RIFF"
4       4       FileSize - 8 (little-endian)
8       4       "WAVE"
12      4       "fmt "
16      4       16 (fmt chunk size for PCM)
20      2       1 (audio format = PCM)
22      2       NumChannels
24      4       SampleRate
28      4       ByteRate = SampleRate * NumChannels * BitsPerSample/8
32      2       BlockAlign = NumChannels * BitsPerSample/8
34      2       BitsPerSample = 16
36      4       "data"
40      4       DataSize (little-endian)
44      ...     Audio data (PCM samples)
```

### Size Calculations

On close, two sizes are patched:
- **RIFF size** (offset 4): `4 + (8 + 16) + (8 + data_size)` = `36 + data_size`
- **Data size** (offset 40): Raw PCM byte count

### PCM16 Sample Format

- Signed 16-bit integers
- Little-endian byte order
- Interleaved channels (L0, R0, L1, R1, ...)
- Range: -32768 to +32767

## Thread Safety

This module is **not thread-safe** by design:
- Single-threaded use by worker thread
- No mutex overhead needed
- File I/O naturally serializes

## Dependencies

- **POSIX:** fileno(), fsync()
- **Standard C:** stdio, stdlib, string, errno, unistd

## Usage Example

```c
/* Create sink */
audyn_wav_sink_cfg_t cfg = {
    .format = AUDYN_WAV_PCM16,
    .enable_fsync = 0
};
audyn_wav_sink_t *wav = audyn_wav_sink_create(&cfg);

/* Open file */
if (audyn_wav_sink_open(wav, "/tmp/output.wav", 48000, 2) != 0) {
    // Error logged internally
    audyn_wav_sink_destroy(wav);
    return -1;
}

/* Write audio blocks */
while (have_audio) {
    float *pcm = get_audio_block(&frames);
    if (audyn_wav_sink_write(wav, pcm, frames, 2) != 0) {
        // Error logged internally
        break;
    }
}

/* Get statistics */
audyn_wav_stats_t stats;
audyn_wav_sink_get_stats(wav, &stats);
printf("Wrote %lu frames (%lu bytes)\n",
       stats.frames_written, stats.bytes_written);

/* Close and destroy */
audyn_wav_sink_close(wav);
audyn_wav_sink_destroy(wav);
```

## Limitations

1. **4GB file size limit** - RIFF/WAVE format constraint
2. **PCM16 only** - No 24-bit or 32-bit PCM support
3. **No RF64 support** - Would allow >4GB files
4. **No metadata** - No LIST/INFO chunk support
5. **No BWF support** - No Broadcast Wave Format extensions
