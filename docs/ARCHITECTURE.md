# Audyn Architecture

This document provides a comprehensive overview of Audyn's system architecture, explaining how the various components work together to provide reliable, professional-grade audio capture and archival.

## Table of Contents

1. [System Overview](#system-overview)
2. [Core Engine Architecture](#core-engine-architecture)
3. [Audio Pipeline](#audio-pipeline)
4. [Input Subsystem](#input-subsystem)
5. [Output Subsystem](#output-subsystem)
6. [Archive System](#archive-system)
7. [PTP Clock System](#ptp-clock-system)
8. [Web Application Architecture](#web-application-architecture)
9. [Multi-Recorder System](#multi-recorder-system)
10. [Data Flow](#data-flow)
11. [Threading Model](#threading-model)
12. [Memory Management](#memory-management)

---

## System Overview

Audyn is architected as a modular, high-performance audio capture system with two main components:

1. **Core Engine** (`audyn`): A C-based command-line application optimized for real-time audio capture
2. **Web Application**: A Python/Vue.js application for management and monitoring

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           AUDYN SYSTEM                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                      CORE ENGINE (C)                             │    │
│  │                                                                  │    │
│  │  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐     │    │
│  │  │  Input   │──>│  Frame   │──>│  Audio   │──>│  Output  │     │    │
│  │  │  Layer   │   │  Pool    │   │  Queue   │   │  Sinks   │     │    │
│  │  └──────────┘   └──────────┘   └──────────┘   └──────────┘     │    │
│  │       │                                             │           │    │
│  │       v                                             v           │    │
│  │  ┌──────────┐                               ┌──────────┐       │    │
│  │  │   PTP    │                               │ Archive  │       │    │
│  │  │  Clock   │                               │  Policy  │       │    │
│  │  └──────────┘                               └──────────┘       │    │
│  │                                                                  │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │                   WEB APPLICATION                                │    │
│  │                                                                  │    │
│  │  ┌──────────────────┐        ┌──────────────────┐               │    │
│  │  │    Frontend      │<──────>│     Backend      │               │    │
│  │  │    (Vue.js)      │  REST  │    (FastAPI)     │               │    │
│  │  │                  │   WS   │                  │               │    │
│  │  ├──────────────────┤        ├──────────────────┤               │    │
│  │  │ - Overview       │        │ - Auth (Entra)   │               │    │
│  │  │ - Recorders      │        │ - Recorders API  │               │    │
│  │  │ - Studios        │        │ - Studios API    │               │    │
│  │  │ - Files          │        │ - Assets API     │               │    │
│  │  │ - Settings       │        │ - WebSocket      │               │    │
│  │  └──────────────────┘        └──────────────────┘               │    │
│  │                                                                  │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Core Engine Architecture

The core engine is written in C for maximum performance and minimal latency. It follows a producer-consumer pattern with lock-free data structures for real-time safety.

### Design Principles

1. **Real-Time Safety**: Critical paths use lock-free algorithms
2. **Zero-Copy Where Possible**: Frame pool minimizes memory allocations
3. **Modular Design**: Input sources and output sinks are pluggable
4. **Graceful Degradation**: Handles buffer overruns without crashing

### Component Hierarchy

```
audyn.c (Main Orchestrator)
├── core/
│   ├── frame_pool.c      - Pre-allocated audio frame management
│   ├── audio_queue.c     - Lock-free SPSC queue
│   ├── ptp_clock.c       - PTP/PHC clock abstraction
│   ├── jitter_buffer.c   - RTP packet reordering
│   ├── archive_policy.c  - File rotation logic
│   └── log.c             - Logging subsystem
├── input/
│   ├── aes_input.c       - AES67/RTP capture
│   └── pipewire_input.c  - PipeWire local capture
└── sink/
    ├── wav_sink.c        - WAV file output
    └── opus_sink.c       - Opus/Ogg output
```

---

## Audio Pipeline

The audio pipeline is the heart of Audyn, moving audio data from network to disk with minimal latency and maximum reliability.

### Pipeline Stages

```
┌─────────────────────────────────────────────────────────────────────┐
│                        AUDIO PIPELINE                                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  1. CAPTURE                    2. BUFFERING                         │
│  ┌──────────────────┐         ┌──────────────────┐                  │
│  │   RTP Packets    │         │   Jitter Buffer  │                  │
│  │   ────────────>  │────────>│   (reordering)   │                  │
│  │   UDP Socket     │         │   ────────────>  │                  │
│  └──────────────────┘         └──────────────────┘                  │
│           │                            │                             │
│           v                            v                             │
│  3. FRAMING                    4. QUEUING                           │
│  ┌──────────────────┐         ┌──────────────────┐                  │
│  │   Frame Pool     │         │   Audio Queue    │                  │
│  │   (pre-alloc)    │────────>│   (lock-free)    │                  │
│  │   ────────────>  │         │   ────────────>  │                  │
│  └──────────────────┘         └──────────────────┘                  │
│                                        │                             │
│                                        v                             │
│                               5. ENCODING/WRITING                    │
│                               ┌──────────────────┐                  │
│                               │   Output Sink    │                  │
│                               │   (WAV/Opus)     │                  │
│                               │   ────────────>  │                  │
│                               └──────────────────┘                  │
│                                        │                             │
│                                        v                             │
│                               6. ARCHIVAL                            │
│                               ┌──────────────────┐                  │
│                               │  Archive Policy  │                  │
│                               │  (rotation)      │                  │
│                               └──────────────────┘                  │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

### Frame Pool (`core/frame_pool.c`)

The frame pool provides pre-allocated audio frames to avoid memory allocation in the real-time capture thread.

```c
// Frame structure
typedef struct audyn_audio_frame {
    int16_t *data;           // Interleaved PCM samples
    uint32_t sample_frames;  // Number of sample frames
    uint16_t channels;       // Channel count
    uint64_t timestamp_ns;   // PTP timestamp (TAI nanoseconds)
    uint32_t _refcount;      // Reference counting
    struct audyn_frame_pool *_pool;  // Parent pool
} audyn_audio_frame_t;
```

**Key Features:**
- Pre-allocated frame buffer at startup
- Reference counting for safe sharing
- Automatic return to pool when released
- Configurable frame size and count

### Audio Queue (`core/audio_queue.c`)

A single-producer, single-consumer (SPSC) lock-free ring buffer designed for real-time safety.

```
Producer Thread                    Consumer Thread
(Capture Callback)                 (Worker Thread)
      │                                  │
      v                                  v
  ┌───────────────────────────────────────────┐
  │  [F1] [F2] [F3] [F4] [  ] [  ] [  ] [  ]  │
  │   ^                   ^                    │
  │   │                   │                    │
  │  head               tail                   │
  └───────────────────────────────────────────┘
```

**Properties:**
- Lock-free push/pop operations
- Cache-line aligned indices to prevent false sharing
- Bounded capacity prevents memory exhaustion
- Returns NULL on empty (non-blocking)

---

## Input Subsystem

### AES67 Input (`input/aes_input.c`)

The primary input method, receiving audio over IP via RTP/UDP.

```
┌─────────────────────────────────────────────────────────────────┐
│                      AES67 INPUT                                 │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Network                        Processing                       │
│  ┌──────────────┐              ┌──────────────┐                 │
│  │  UDP Socket  │─────────────>│  RTP Parser  │                 │
│  │  (multicast) │              │  - Validate  │                 │
│  └──────────────┘              │  - Sequence  │                 │
│         │                      └──────────────┘                 │
│         v                             │                          │
│  ┌──────────────┐                     v                          │
│  │  SO_RCVBUF   │              ┌──────────────┐                 │
│  │  (2MB)       │              │   Jitter     │                 │
│  └──────────────┘              │   Buffer     │                 │
│                                └──────────────┘                 │
│                                       │                          │
│                                       v                          │
│                                ┌──────────────┐                 │
│                                │  Frame Pool  │                 │
│                                │   Acquire    │                 │
│                                └──────────────┘                 │
│                                       │                          │
│                                       v                          │
│                                ┌──────────────┐                 │
│                                │  Audio Queue │                 │
│                                │    Push      │                 │
│                                └──────────────┘                 │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Configuration Options:**
| Parameter | Default | Description |
|-----------|---------|-------------|
| `source_ip` | Required | Multicast group or unicast source |
| `port` | 5004 | UDP port number |
| `payload_type` | 96 | RTP payload type |
| `sample_rate` | 48000 | Audio sample rate (Hz) |
| `channels` | 2 | Number of audio channels |
| `samples_per_packet` | 48 | Samples per RTP packet |
| `socket_rcvbuf` | 2097152 | Socket receive buffer (bytes) |

### PipeWire Input (`input/pipewire_input.c`)

Fallback input for local audio capture when AES67 is unavailable.

**Use Cases:**
- Testing without network audio
- Recording from local sound card
- Capturing from other PipeWire applications

---

## Output Subsystem

### WAV Sink (`sink/wav_sink.c`)

Produces standard RIFF/WAVE files with PCM16 audio.

```
WAV File Structure:
┌────────────────────────────────────┐
│  RIFF Header (12 bytes)            │
│  - "RIFF" magic                    │
│  - File size                       │
│  - "WAVE" format                   │
├────────────────────────────────────┤
│  fmt Chunk (24 bytes)              │
│  - Audio format (PCM)              │
│  - Channels, sample rate           │
│  - Byte rate, block align          │
│  - Bits per sample                 │
├────────────────────────────────────┤
│  data Chunk (variable)             │
│  - Interleaved PCM samples         │
│  - Little-endian 16-bit            │
└────────────────────────────────────┘
```

**Features:**
- Streaming write (header updated on close)
- Optional fsync for durability
- Correct handling of large files (>2GB via RF64)

### Opus Sink (`sink/opus_sink.c`)

Produces Ogg Opus compressed audio files.

**Compression Options:**
| Parameter | Default | Description |
|-----------|---------|-------------|
| `bitrate` | 128000 | Target bitrate (bps) |
| `vbr` | true | Variable bitrate mode |
| `complexity` | 5 | Encoder complexity (0-10) |
| `application` | AUDIO | Opus application type |

**Advantages:**
- ~10x smaller files than WAV
- Excellent audio quality at 128 kbps
- Open, royalty-free format

---

## Archive System

The archive system manages automatic file rotation based on time, supporting multiple naming layouts compatible with the Rotter broadcast logger.

### Archive Policy (`core/archive_policy.c`)

```
┌─────────────────────────────────────────────────────────────────┐
│                     ARCHIVE POLICY                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Configuration                 Runtime State                     │
│  ┌──────────────────┐         ┌──────────────────┐              │
│  │ - root_dir       │         │ - current_period │              │
│  │ - layout         │         │ - last_rotation  │              │
│  │ - suffix         │         │ - file_count     │              │
│  │ - period_sec     │         └──────────────────┘              │
│  │ - clock_source   │                │                          │
│  └──────────────────┘                │                          │
│           │                          v                          │
│           v                  ┌──────────────────┐               │
│  ┌──────────────────┐       │ should_rotate()  │               │
│  │  Path Generator  │<──────│                  │               │
│  │  (strftime)      │       └──────────────────┘               │
│  └──────────────────┘                                           │
│           │                                                      │
│           v                                                      │
│  ┌──────────────────┐                                           │
│  │ /archive/2026-01-10/2026-01-10-14.opus                │      │
│  └──────────────────┘                                           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Supported Layouts

| Layout | Format String | Example |
|--------|---------------|---------|
| `flat` | `%Y-%m-%d-%H` | `2026-01-10-14.opus` |
| `hierarchy` | `%Y/%m/%d/%H/archive` | `2026/01/10/14/archive.opus` |
| `combo` | `%Y/%m/%d/%H/%Y-%m-%d-%H` | `2026/01/10/14/2026-01-10-14.opus` |
| `dailydir` | `%Y-%m-%d/%Y-%m-%d-%H` | `2026-01-10/2026-01-10-14.opus` |
| `accurate` | `%Y-%m-%d/%Y-%m-%d-%H-%M-%S-00` | `2026-01-10/2026-01-10-14-30-00-00.opus` |
| `custom` | User-defined | Any valid strftime format |

### Clock Sources

| Source | Description |
|--------|-------------|
| `localtime` | System local time (default) |
| `utc` | UTC time |
| `ptp` | PTP/TAI time from hardware clock |

---

## PTP Clock System

### PTP Clock (`core/ptp_clock.c`)

Provides precise timestamps from PTP hardware clocks (PHC) or software PTP.

```
┌─────────────────────────────────────────────────────────────────┐
│                      PTP CLOCK SYSTEM                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Hardware Mode                  Software Mode                    │
│  ┌──────────────────┐          ┌──────────────────┐             │
│  │  /dev/ptp0       │          │  CLOCK_REALTIME  │             │
│  │  (PHC device)    │          │  (linuxptp sync) │             │
│  └──────────────────┘          └──────────────────┘             │
│           │                            │                         │
│           v                            v                         │
│  ┌──────────────────┐          ┌──────────────────┐             │
│  │ clock_gettime()  │          │ clock_gettime()  │             │
│  │ CLOCK_TAI        │          │ + TAI offset     │             │
│  └──────────────────┘          └──────────────────┘             │
│           │                            │                         │
│           └────────────┬───────────────┘                         │
│                        v                                         │
│               ┌──────────────────┐                               │
│               │  TAI Nanoseconds │                               │
│               │  (uint64_t)      │                               │
│               └──────────────────┘                               │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Modes:**
1. **Hardware**: Direct access to NIC's PTP Hardware Clock
2. **Interface Discovery**: Automatically find PHC from network interface
3. **Software**: Use system clock synchronized by linuxptp

---

## Web Application Architecture

### Backend (`web/backend/`)

Built with FastAPI for high-performance async operations.

```
app/
├── main.py              - Application entry, lifespan management
├── models.py            - Pydantic data models
├── auth/
│   └── entra.py         - Entra ID (Azure AD) SSO authentication
├── api/
│   ├── control.py       - Capture control endpoints
│   ├── recorders.py     - Multi-recorder management
│   ├── studios.py       - Studio configuration
│   ├── sources.py       - Audio source management
│   ├── assets.py        - File browser and management
│   └── stream.py        - Audio streaming/preview
├── services/
│   ├── audyn.py         - Audyn process management
│   └── config_store.py  - File-based configuration persistence
└── websocket/
    └── levels.py        - Real-time audio level streaming
```

### Frontend (`web/frontend/`)

Built with Vue.js 3 and Vuetify 3 for a modern Material Design interface.

```
src/
├── main.js              - Application entry
├── App.vue              - Root component, navigation
├── plugins/
│   ├── vuetify.js       - Vuetify theme configuration
│   └── router.js        - Vue Router configuration
├── stores/
│   ├── auth.js          - Authentication state (Pinia)
│   ├── recorders.js     - Recorder state and WebSocket
│   ├── studios.js       - Studio state
│   └── capture.js       - Legacy capture state
├── views/
│   ├── Overview.vue     - Dashboard with all meters
│   ├── Recorders.vue    - Recorder management
│   ├── RecorderDetail.vue - Individual recorder config
│   ├── Studios.vue      - Studio management
│   ├── Files.vue        - File browser
│   └── Settings.vue     - System configuration
└── components/
    ├── AudioMeter.vue   - Full audio level meter
    ├── CompactMeter.vue - Compact meter for cards
    ├── MiniMeter.vue    - Minimal meter for tables
    └── RecorderCard.vue - Recorder status card
```

---

## Configuration Persistence

The web application uses a file-based configuration persistence system to store all settings without requiring a database.

### Config Store (`services/config_store.py`)

```
┌─────────────────────────────────────────────────────────────────┐
│                   CONFIGURATION PERSISTENCE                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Storage Location: ~/.config/audyn/                              │
│                                                                  │
│  ┌──────────────────┐    ┌──────────────────┐                   │
│  │  global.json     │    │  recorders.json  │                   │
│  │  - archive_root  │    │  - active_count  │                   │
│  │  - format        │    │  - recorder cfgs │                   │
│  │  - layout        │    │  - studio assign │                   │
│  └──────────────────┘    └──────────────────┘                   │
│                                                                  │
│  ┌──────────────────┐    ┌──────────────────┐                   │
│  │  studios.json    │    │  sources.json    │                   │
│  │  - studio list   │    │  - AES67 sources │                   │
│  │  - names/colors  │    │  - active source │                   │
│  │  - descriptions  │    │  - multicast cfg │                   │
│  └──────────────────┘    └──────────────────┘                   │
│                                                                  │
│  ┌──────────────────┐                                           │
│  │  auth.json       │                                           │
│  │  - tenant_id     │                                           │
│  │  - client_id     │                                           │
│  │  - breakglass    │                                           │
│  └──────────────────┘                                           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

**Features:**
- Thread-safe file operations with locking
- Atomic writes (temp file + rename)
- Automatic directory creation
- JSON format for easy inspection and backup

---

## Multi-Recorder System

Audyn supports 1-6 simultaneous recorder instances, each independently configurable.

```
┌─────────────────────────────────────────────────────────────────┐
│                    MULTI-RECORDER SYSTEM                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │ Recorder 1  │  │ Recorder 2  │  │ Recorder 3  │             │
│  │ ─────────── │  │ ─────────── │  │ ─────────── │             │
│  │ Studio A    │  │ Studio B    │  │ Studio C    │             │
│  │ 239.69.1.1  │  │ 239.69.1.2  │  │ 239.69.1.3  │             │
│  │ [RECORDING] │  │ [RECORDING] │  │ [STOPPED]   │             │
│  └─────────────┘  └─────────────┘  └─────────────┘             │
│                                                                  │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐             │
│  │ Recorder 4  │  │ Recorder 5  │  │ Recorder 6  │             │
│  │ ─────────── │  │ ─────────── │  │ ─────────── │             │
│  │ (unassigned)│  │ (unassigned)│  │ (unassigned)│             │
│  │ 239.69.1.4  │  │ 239.69.1.5  │  │ 239.69.1.6  │             │
│  │ [STOPPED]   │  │ [STOPPED]   │  │ [STOPPED]   │             │
│  └─────────────┘  └─────────────┘  └─────────────┘             │
│                                                                  │
├─────────────────────────────────────────────────────────────────┤
│                      STUDIO ASSIGNMENT                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Studio A ──────> Recorder 1                                     │
│  Studio B ──────> Recorder 2                                     │
│  Studio C ──────> Recorder 3                                     │
│  Studio D ──────> (none)                                         │
│  Studio E ──────> (none)                                         │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Role-Based Access Control

| Role | Capabilities |
|------|--------------|
| **Admin** | Full access: all recorders, all studios, all files, configuration |
| **Studio** | View assigned recorder, access studio files, delete own recordings |

---

## Data Flow

### Recording Data Flow

```
1. RTP Packet arrives on UDP socket
         │
         v
2. Jitter buffer reorders packets
         │
         v
3. Frame acquired from pool
         │
         v
4. Audio data copied to frame
         │
         v
5. Frame pushed to queue
         │
         v
6. Worker pops frame
         │
         v
7. Archive policy checks rotation
         │
         v
8. Audio encoded (WAV/Opus)
         │
         v
9. Data written to file
         │
         v
10. Frame released to pool
```

### WebSocket Level Data Flow

```
1. Worker reads audio levels
         │
         v
2. Levels broadcast via WebSocket
         │
         v
3. Frontend receives JSON message
         │
         v
4. Vue reactivity updates meters
         │
         v
5. CSS animation renders bars
```

---

## Threading Model

```
┌─────────────────────────────────────────────────────────────────┐
│                      THREADING MODEL                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Main Thread                                                     │
│  ├── Signal handling                                             │
│  ├── Configuration                                               │
│  └── Status monitoring                                           │
│                                                                  │
│  Capture Thread (Real-Time)                                      │
│  ├── UDP socket receive                                          │
│  ├── RTP parsing                                                 │
│  ├── Frame pool acquire                                          │
│  └── Queue push                                                  │
│                                                                  │
│  Worker Thread                                                   │
│  ├── Queue pop                                                   │
│  ├── Archive rotation check                                      │
│  ├── Audio encoding                                              │
│  └── File I/O                                                    │
│                                                                  │
│  (Web) Async Event Loop                                          │
│  ├── HTTP request handling                                       │
│  ├── WebSocket connections                                       │
│  └── Level broadcasting                                          │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Memory Management

### Frame Pool Strategy

```
Startup:
┌──────────────────────────────────────────────┐
│  Allocate N frames (e.g., 256)               │
│  Each frame: channels × frame_size × 2 bytes │
│  Total: 256 × 2 × 1024 × 2 = 1 MB            │
└──────────────────────────────────────────────┘

Runtime:
┌──────────────────────────────────────────────┐
│  Capture acquires frame (refcount = 1)       │
│  Frame pushed to queue                       │
│  Worker pops frame                           │
│  Worker releases frame (refcount = 0)        │
│  Frame automatically returned to pool        │
└──────────────────────────────────────────────┘
```

### Buffer Sizing Guidelines

| Component | Default | Formula |
|-----------|---------|---------|
| Frame Pool | 256 frames | >= queue_capacity |
| Audio Queue | 1024 slots | >= 2 × (rotation_period / frame_duration) |
| Socket Buffer | 2 MB | >= 100ms of audio at max rate |
| Jitter Buffer | 10 packets | >= expected network jitter |

---

## Next Steps

- [Installation Guide](INSTALLATION.md) - How to install Audyn
- [Configuration Guide](CONFIGURATION.md) - Detailed configuration options
- [API Reference](API.md) - REST API and WebSocket documentation
