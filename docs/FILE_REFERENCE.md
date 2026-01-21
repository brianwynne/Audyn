# Audyn File Reference

Complete reference documentation for every source file in the Audyn project.

## Table of Contents

1. [Core Engine Files](#core-engine-files)
2. [Input Source Files](#input-source-files)
3. [Output Sink Files](#output-sink-files)
4. [Web Backend Files](#web-backend-files)
5. [Web Frontend Files](#web-frontend-files)
6. [Configuration Files](#configuration-files)

---

## Core Engine Files

### audyn.c

**Location:** `/audyn.c`

**Purpose:** Main executable and orchestration layer for the Audyn audio capture engine.

**Responsibilities:**
- Parse command-line arguments
- Initialize core resources (frame pool, audio queue, archive policy)
- Create and manage input source (AES67 or PipeWire)
- Create and manage output sink (WAV or Opus)
- Run worker thread for encoding and file I/O
- Handle signals (SIGINT, SIGTERM) for graceful shutdown
- Implement file rotation based on archive policy
- VOX mode: threshold-based recording with separate segment files

**Key Functions:**
| Function | Description |
|----------|-------------|
| `main()` | Entry point, argument parsing, initialization |
| `worker_main()` | Worker thread for encoding and writing |
| `maybe_rotate()` | Check and perform file rotation |
| `open_sink()` | Open appropriate output sink |
| `on_signal()` | Signal handler for graceful shutdown |

**Dependencies:**
- `core/frame_pool.h`
- `core/audio_queue.h`
- `core/ptp_clock.h`
- `core/archive_policy.h`
- `core/level_meter.h`
- `core/vox.h`
- `input/aes_input.h`
- `input/pipewire_input.h`
- `sink/wav_sink.h`
- `sink/opus_sink.h`

---

### core/frame_pool.c / frame_pool.h

**Location:** `/core/frame_pool.c`, `/core/frame_pool.h`

**Purpose:** Pre-allocated pool of audio frames to avoid dynamic allocation in real-time code paths.

**Key Concepts:**
- Frames are pre-allocated at startup
- Reference counting for safe sharing between threads
- Automatic return to pool when released
- Thread-safe acquire/release operations

**Data Structures:**
```c
typedef struct audyn_audio_frame {
    int16_t *data;           // Interleaved PCM samples
    uint32_t sample_frames;  // Number of sample frames
    uint16_t channels;       // Channel count
    uint64_t timestamp_ns;   // PTP timestamp (TAI nanoseconds)
    uint32_t _refcount;      // Reference count
    struct audyn_frame_pool *_pool;  // Parent pool
} audyn_audio_frame_t;
```

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_frame_pool_create()` | Create pool with N frames |
| `audyn_frame_pool_destroy()` | Destroy pool and free memory |
| `audyn_frame_acquire()` | Get a frame from pool |
| `audyn_frame_release()` | Return frame to pool |
| `audyn_frame_retain()` | Increment reference count |

---

### core/audio_queue.c / audio_queue.h

**Location:** `/core/audio_queue.c`, `/core/audio_queue.h`

**Purpose:** Single-producer, single-consumer (SPSC) lock-free ring buffer for passing audio frames between threads.

**Key Concepts:**
- Lock-free for real-time safety
- Bounded capacity prevents memory exhaustion
- Cache-line aligned to prevent false sharing
- NULL is reserved (signals empty queue)

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_audio_queue_create()` | Create queue with capacity N |
| `audyn_audio_queue_destroy()` | Destroy queue |
| `audyn_audio_queue_push()` | Add pointer to queue (producer) |
| `audyn_audio_queue_pop()` | Remove pointer from queue (consumer) |
| `audyn_audio_queue_capacity()` | Get configured capacity |

**Thread Safety:**
- `push()`: Only called by producer thread
- `pop()`: Only called by consumer thread
- Violating SPSC contract causes undefined behavior

---

### core/ptp_clock.c / ptp_clock.h

**Location:** `/core/ptp_clock.c`, `/core/ptp_clock.h`

**Purpose:** Abstraction layer for PTP (Precision Time Protocol) hardware and software clocks.

**Modes:**
| Mode | Description |
|------|-------------|
| `AUDYN_PTP_MODE_HARDWARE` | Direct access to PHC device |
| `AUDYN_PTP_MODE_SOFTWARE` | System clock via linuxptp |

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_ptp_clock_create()` | Create PTP clock instance |
| `audyn_ptp_clock_destroy()` | Destroy clock instance |
| `audyn_ptp_clock_now_ns()` | Get current TAI time in nanoseconds |
| `audyn_ptp_clock_status()` | Get synchronization status |

**Configuration:**
```c
typedef struct audyn_ptp_cfg {
    audyn_ptp_mode_t mode;
    const char *phc_device;   // e.g., "/dev/ptp0"
    const char *interface;    // e.g., "enp1s0"
} audyn_ptp_cfg_t;
```

---

### core/jitter_buffer.c / jitter_buffer.h

**Location:** `/core/jitter_buffer.c`, `/core/jitter_buffer.h`

**Purpose:** Reorder RTP packets that arrive out of sequence due to network jitter.

**Key Concepts:**
- Fixed-size buffer for pending packets
- Uses RTP sequence numbers for ordering
- Handles wrap-around of 16-bit sequence numbers
- Drops late packets that exceed buffer depth

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_jitter_buffer_create()` | Create buffer with depth N |
| `audyn_jitter_buffer_destroy()` | Destroy buffer |
| `audyn_jitter_buffer_push()` | Add packet to buffer |
| `audyn_jitter_buffer_pop()` | Get next ordered packet |

---

### core/archive_policy.c / archive_policy.h

**Location:** `/core/archive_policy.c`, `/core/archive_policy.h`

**Purpose:** Manage automatic file rotation based on time, with multiple naming layouts.

**Layouts:**
| Layout | Constant | Example |
|--------|----------|---------|
| Flat | `AUDYN_ARCHIVE_LAYOUT_FLAT` | `2026-01-10-14.opus` |
| Hierarchy | `AUDYN_ARCHIVE_LAYOUT_HIERARCHY` | `2026/01/10/14/archive.opus` |
| Combo | `AUDYN_ARCHIVE_LAYOUT_COMBO` | `2026/01/10/14/2026-01-10-14.opus` |
| Daily Dir | `AUDYN_ARCHIVE_LAYOUT_DAILYDIR` | `2026-01-10/2026-01-10-14.opus` |
| Accurate | `AUDYN_ARCHIVE_LAYOUT_ACCURATE` | `2026-01-10/2026-01-10-14-30-00-00.opus` |
| Custom | `AUDYN_ARCHIVE_LAYOUT_CUSTOM` | User-defined strftime |

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_archive_policy_create()` | Create policy from config |
| `audyn_archive_policy_destroy()` | Destroy policy |
| `audyn_archive_policy_should_rotate()` | Check if rotation needed |
| `audyn_archive_policy_next_path()` | Generate next file path |
| `audyn_archive_policy_advance()` | Advance to next period |

**Configuration:**
```c
typedef struct audyn_archive_cfg {
    const char *root_dir;
    const char *suffix;
    audyn_archive_layout_t layout;
    const char *custom_format;
    uint32_t rotation_period_sec;
    audyn_archive_clock_t clock_source;
    int create_directories;
} audyn_archive_cfg_t;
```

---

### core/vox.c / vox.h

**Location:** `/core/vox.c`, `/core/vox.h`

**Purpose:** Voice Activity Detection (VOX) module for threshold-based recording with configurable hang time.

**Key Concepts:**
- State machine: IDLE → DETECTING → ACTIVE → HANGOVER → IDLE
- Pre-roll ring buffer captures audio before threshold crossing
- Hysteresis prevents chatter (configurable release threshold)
- Creates separate segment files per speech burst
- Replaces time-based archive rotation when enabled

**States:**
| State | Description |
|-------|-------------|
| `AUDYN_VOX_IDLE` | Waiting for audio activity |
| `AUDYN_VOX_DETECTING` | Activity detected, confirming threshold |
| `AUDYN_VOX_ACTIVE` | Recording active |
| `AUDYN_VOX_HANGOVER` | Silence detected, waiting hangover period |

**Level Modes:**
| Mode | Constant | Description |
|------|----------|-------------|
| RMS | `AUDYN_VOX_LEVEL_RMS` | Use RMS (average loudness) |
| Peak | `AUDYN_VOX_LEVEL_PEAK` | Use peak level (transient response) |
| Any | `AUDYN_VOX_LEVEL_ANY_CHANNEL` | Trigger if any channel exceeds threshold |

**Configuration:**
```c
typedef struct audyn_vox_config {
    float threshold_db;           // Activation threshold (-60 to -5)
    float release_db;             // Release threshold (0 = auto: threshold - 5dB)
    uint32_t detection_ms;        // Time to confirm activity (default 100)
    uint32_t hangover_ms;         // Hang time after silence (default 2000)
    uint32_t preroll_ms;          // Pre-roll buffer (default 500, max 5000)
    audyn_vox_level_mode_t mode;  // Level source (rms, peak, any)
    uint32_t sample_rate;
    uint16_t channels;
} audyn_vox_config_t;
```

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_vox_create()` | Create VOX detector instance |
| `audyn_vox_destroy()` | Destroy VOX detector |
| `audyn_vox_process()` | Process frame, returns frames to write |
| `audyn_vox_get_state()` | Get current state |
| `audyn_vox_should_open_file()` | Check if new segment should start |
| `audyn_vox_should_close_file()` | Check if segment should end |
| `audyn_vox_flush()` | Flush remaining pre-roll frames |
| `audyn_vox_reset()` | Reset to IDLE state |
| `audyn_vox_get_stats()` | Get processing statistics |

**CLI Options:**
| Option | Description |
|--------|-------------|
| `--vox` | Enable VOX mode |
| `--vox-threshold <dB>` | Activation threshold (default -30) |
| `--vox-release <dB>` | Release threshold (0 = auto) |
| `--vox-detection <ms>` | Detection delay (default 100) |
| `--vox-hangover <ms>` | Hang time (default 2000) |
| `--vox-preroll <ms>` | Pre-roll buffer (default 500, max 5000) |
| `--vox-level <mode>` | Level source: rms, peak, any |

---

### core/log.c / log.h

**Location:** `/core/log.c`, `/core/log.h`

**Purpose:** Thread-safe logging subsystem with multiple output destinations.

**Log Levels:**
| Level | Constant | Description |
|-------|----------|-------------|
| Debug | `AUDYN_LOG_DEBUG` | Verbose debugging |
| Info | `AUDYN_LOG_INFO` | Normal operation |
| Warn | `AUDYN_LOG_WARN` | Warning conditions |
| Error | `AUDYN_LOG_ERROR` | Error conditions |

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_log_init()` | Initialize logging |
| `audyn_log_shutdown()` | Shutdown logging |
| `LOG_DEBUG()` | Debug message macro |
| `LOG_INFO()` | Info message macro |
| `LOG_WARN()` | Warning message macro |
| `LOG_ERROR()` | Error message macro |

---

### core/sdp_parser.c / sdp_parser.h

**Location:** `/core/sdp_parser.c`, `/core/sdp_parser.h`

**Purpose:** Parse SDP (Session Description Protocol) payloads for AES67 stream discovery.

**Standards:**
- RFC 4566 (SDP)
- AES67-2018 (AES67 SDP extensions)
- SMPTE ST 2110-30 (Professional media over IP)

**Key Data Structures:**
```c
typedef struct {
    char session_name[128];      // SDP session name (s=)
    char multicast_addr[64];     // Connection address (c=)
    uint16_t port;               // Media port (m=)
    sdp_encoding_t encoding;     // L16, L24, L32, AM824
    uint32_t sample_rate;        // Clock rate from rtpmap
    uint16_t channels;           // Channel count
    float ptime;                 // Packet time (ms)
    uint16_t samples_per_packet; // Computed from ptime
    sdp_channel_t channel_info[64]; // Channel labels
    int has_channel_labels;      // SMPTE 2110 channel-order present
} sdp_stream_t;
```

**Key Functions:**
| Function | Description |
|----------|-------------|
| `sdp_parse()` | Parse SDP text into sdp_stream_t |
| `sdp_encoding_name()` | Get encoding name string |
| `sdp_encoding_bits()` | Get bits per sample |
| `sdp_stream_to_string()` | Format stream info for display |

**Parsed SDP Lines:**
| Line | Description |
|------|-------------|
| `v=` | Version (validated as 0) |
| `o=` | Origin (session ID, version, address) |
| `s=` | Session name |
| `c=` | Connection info (multicast address, TTL) |
| `m=` | Media description (port, payload type) |
| `a=rtpmap:` | RTP mapping (encoding, clock, channels) |
| `a=ptime:` | Packet time in milliseconds |
| `a=fmtp:` | Format parameters (channel-order) |
| `a=source-filter:` | Source-specific multicast (SSM) |
| `a=mediaclk:` | Media clock reference |
| `a=ts-refclk:` | Timestamp reference clock |

---

### core/sap_discovery.c / sap_discovery.h

**Location:** `/core/sap_discovery.c`, `/core/sap_discovery.h`

**Purpose:** Listen for SAP (Session Announcement Protocol) multicast announcements to discover AES67 streams on the network.

**Standards:**
- RFC 2974 (SAP)
- RFC 4566 (SDP payload)

**SAP Multicast Addresses:**
| Address | Scope |
|---------|-------|
| `224.2.127.254` | Global scope |
| `239.255.255.255` | Admin-local scope (commonly used) |

**Default Port:** 9875

**Key Data Structures:**
```c
typedef struct {
    sdp_stream_t sdp;        // Parsed SDP info
    uint16_t msg_id_hash;    // SAP message ID
    char origin_ip[64];      // SAP origin IP
    time_t first_seen;       // Discovery timestamp
    time_t last_seen;        // Last announcement
    int active;              // Stream is active
    char raw_sdp[4096];      // Raw SDP text
} sap_stream_entry_t;

typedef enum {
    SAP_EVENT_NEW,           // New stream discovered
    SAP_EVENT_UPDATE,        // Stream info updated
    SAP_EVENT_DELETE,        // Stream deleted/expired
} sap_event_t;
```

**Key Functions:**
| Function | Description |
|----------|-------------|
| `sap_discovery_create()` | Create discovery instance |
| `sap_discovery_start()` | Start listening thread |
| `sap_discovery_stop()` | Stop listening |
| `sap_discovery_destroy()` | Free resources |
| `sap_discovery_get_streams()` | Get list of discovered streams |
| `sap_discovery_find_stream()` | Find by multicast address |
| `sap_discovery_find_by_name()` | Find by session name |
| `sap_discovery_get_stats()` | Get discovery statistics |

**SAP Header Fields:**
| Field | Description |
|-------|-------------|
| Version | Protocol version (must be 1) |
| Address Type | IPv4 (0) or IPv6 (1) |
| Message Type | Announcement (0) or Deletion (1) |
| Encryption | Encrypted flag (not supported) |
| Compression | Compressed flag (not supported) |
| Auth Length | Authentication data length |
| Msg ID Hash | Message identifier |
| Origin | Originating source address |

---

## Input Source Files

### input/aes_input.c / aes_input.h

**Location:** `/input/aes_input.c`, `/input/aes_input.h`

**Purpose:** Capture audio from AES67 multicast/unicast RTP streams.

**Key Responsibilities:**
- Join multicast group or receive unicast
- Parse RTP packets
- Handle packet loss and reordering
- Apply PTP timestamps
- Fill frames and push to queue

**Configuration:**
```c
typedef struct audyn_aes_input_cfg {
    const char *source_ip;
    uint16_t port;
    uint8_t payload_type;
    uint32_t sample_rate;
    uint16_t channels;           // Output channels (1 or 2)
    uint16_t samples_per_packet;
    uint32_t socket_rcvbuf;
    const char *bind_interface;
    // Channel selection for multi-channel streams
    uint16_t stream_channels;    // Total channels in stream (0 = same as channels)
    uint16_t channel_offset;     // First channel to extract (0-based)
} audyn_aes_input_cfg_t;
```

**Channel Selection:**

For multi-channel streams (e.g., 16-channel Calrec Type R), set `stream_channels` to the total channels in the stream and `channel_offset` to select which channels to extract.

Example: Extract channels 5-6 from a 16-channel stream:
- `channels`: 2 (output stereo)
- `stream_channels`: 16
- `channel_offset`: 4 (0-based, so channels 5-6)

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_aes_input_create()` | Create AES67 input |
| `audyn_aes_input_start()` | Start capture thread |
| `audyn_aes_input_stop()` | Stop capture |
| `audyn_aes_input_destroy()` | Cleanup resources |
| `audyn_aes_input_set_ptp_clock()` | Attach PTP clock |
| `audyn_aes_input_last_error()` | Get error message |

---

### input/pipewire_input.c / pipewire_input.h

**Location:** `/input/pipewire_input.c`, `/input/pipewire_input.h`

**Purpose:** Capture audio from local PipeWire audio server.

**Use Cases:**
- Testing without network audio
- Recording local sound card
- Capturing from other applications

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_pw_input_create()` | Create PipeWire input |
| `audyn_pw_input_start()` | Start capture |
| `audyn_pw_input_stop()` | Stop capture |
| `audyn_pw_input_destroy()` | Cleanup resources |

---

## Output Sink Files

### sink/wav_sink.c / wav_sink.h

**Location:** `/sink/wav_sink.c`, `/sink/wav_sink.h`

**Purpose:** Write audio to RIFF/WAVE files with PCM16 encoding.

**Features:**
- Streaming write (header updated on close)
- Correct WAV header generation
- Optional fsync for durability

**Configuration:**
```c
typedef struct audyn_wav_sink_cfg {
    audyn_wav_format_t format;  // Currently only PCM16
    int enable_fsync;
} audyn_wav_sink_cfg_t;
```

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_wav_sink_create()` | Create WAV sink |
| `audyn_wav_sink_open()` | Open file for writing |
| `audyn_wav_sink_write()` | Write audio samples |
| `audyn_wav_sink_close()` | Finalize and close file |
| `audyn_wav_sink_destroy()` | Cleanup resources |

---

### sink/opus_sink.c / opus_sink.h

**Location:** `/sink/opus_sink.c`, `/sink/opus_sink.h`

**Purpose:** Encode and write audio to Ogg Opus files.

**Features:**
- Real-time Opus encoding
- Ogg container format
- VBR and CBR modes
- Configurable complexity and bitrate

**Configuration:**
```c
typedef struct audyn_opus_cfg {
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t bitrate;
    int vbr;
    int complexity;
    audyn_opus_application_t application;
    int enable_fsync;
} audyn_opus_cfg_t;
```

**Key Functions:**
| Function | Description |
|----------|-------------|
| `audyn_opus_sink_create()` | Create Opus sink |
| `audyn_opus_sink_write()` | Encode and write samples |
| `audyn_opus_sink_flush()` | Flush encoder buffer |
| `audyn_opus_sink_close()` | Finalize and close file |
| `audyn_opus_sink_destroy()` | Cleanup resources |

---

## Web Backend Files

### web/backend/app/main.py

**Purpose:** FastAPI application entry point and configuration.

**Responsibilities:**
- Application initialization and shutdown
- CORS middleware configuration
- Router registration
- Health check endpoint
- Audyn service lifecycle management

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/health` | GET | Health check |
| `/api/status` | GET | System status |

---

### web/backend/app/models.py

**Purpose:** Pydantic data models for API request/response validation.

**Models:**
| Model | Description |
|-------|-------------|
| `User` | User information with roles |
| `UserRole` | Enum: admin, studio |
| `Recorder` | Recorder instance data |
| `RecorderConfig` | Recorder configuration (includes VOX settings) |
| `RecorderState` | Enum: stopped, recording, paused, error |
| `Studio` | Studio configuration |
| `ChannelLevel` | Audio level measurement |
| `OutputFormat` | Enum: wav, flac, mp3, ogg |
| `VoxLevelMode` | Enum: rms, peak, any |

**RecorderConfig VOX Fields:**
| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `vox_enabled` | bool | false | Enable VOX for this recorder |
| `vox_threshold_db` | float | -30.0 | Activation threshold in dB |
| `vox_release_db` | float | 0.0 | Release threshold (0 = auto) |
| `vox_detection_ms` | int | 100 | Detection delay in ms |
| `vox_hangover_ms` | int | 2000 | Hang time in ms |
| `vox_preroll_ms` | int | 500 | Pre-roll buffer in ms |
| `vox_level_mode` | VoxLevelMode | rms | Level detection mode |

---

### web/backend/app/auth/entra.py

**Purpose:** Microsoft Entra ID (Azure AD) OAuth2/OIDC authentication.

**Features:**
- Development mode bypass
- JWT token validation
- User extraction from token claims
- Login/logout flows

**Key Functions:**
| Function | Description |
|----------|-------------|
| `get_current_user()` | FastAPI dependency for auth |
| `validate_token()` | Validate JWT against Entra |
| `login()` | Initiate login flow |
| `auth_callback()` | Handle OAuth callback |
| `logout()` | Logout endpoint |

---

### web/backend/app/api/recorders.py

**Purpose:** REST API for multi-recorder management.

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/recorders/` | GET | List all recorders |
| `/api/recorders/{id}` | GET | Get specific recorder |
| `/api/recorders/active-count` | GET | Get active count |
| `/api/recorders/active-count/{n}` | PUT | Set active count |
| `/api/recorders/{id}/config` | PUT | Update config |
| `/api/recorders/{id}/start` | POST | Start recorder |
| `/api/recorders/{id}/stop` | POST | Stop recorder |
| `/api/recorders/start-all` | POST | Start all |
| `/api/recorders/stop-all` | POST | Stop all |
| `/api/recorders/{id}/levels` | GET | Get audio levels |

---

### web/backend/app/api/studios.py

**Purpose:** REST API for studio management and recorder assignment.

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/studios/` | GET | List all studios |
| `/api/studios/` | POST | Create studio |
| `/api/studios/{id}` | GET | Get specific studio |
| `/api/studios/{id}` | PUT | Update studio |
| `/api/studios/{id}` | DELETE | Delete studio |
| `/api/studios/{id}/assign` | POST | Assign recorder |
| `/api/studios/{id}/recorder` | GET | Get assigned recorder |
| `/api/studios/{id}/recordings` | GET | Get studio recordings |

---

### web/backend/app/api/assets.py

**Purpose:** File browser and management API.

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/assets/browse` | GET | Browse directory |
| `/api/assets/download/{path}` | GET | Download file |
| `/api/assets/file/{path}` | DELETE | Delete file |

---

### web/backend/app/api/sources.py

**Purpose:** REST API for AES67 source configuration.

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/sources/` | GET | List all sources |
| `/api/sources/` | POST | Create source |
| `/api/sources/active` | GET | Get active source |
| `/api/sources/active/{id}` | POST | Set active source |
| `/api/sources/pipewire` | GET | List PipeWire sources |
| `/api/sources/{id}` | GET | Get specific source |
| `/api/sources/{id}` | PUT | Update source |
| `/api/sources/{id}` | DELETE | Delete source |
| `/api/sources/from-discovery` | POST | Import from SAP discovery |

---

### web/backend/app/api/discovery.py

**Purpose:** REST API for SAP/SDP stream discovery.

**Endpoints:**
| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/discovery/status` | GET | Get discovery service status |
| `/api/discovery/start` | POST | Start SAP discovery |
| `/api/discovery/stop` | POST | Stop SAP discovery |
| `/api/discovery/streams` | GET | List discovered streams |
| `/api/discovery/streams/{id}` | GET | Get stream details |
| `/api/discovery/streams/{id}/sdp` | GET | Get raw SDP |
| `/api/discovery/streams/{id}/channels` | GET | Get channel selection info |
| `/api/discovery/search` | GET | Search streams |

---

### web/backend/app/services/sap_discovery.py

**Purpose:** Async SAP discovery service for stream discovery.

**Key Responsibilities:**
- Listen for SAP multicast announcements (239.255.255.255:9875)
- Parse SDP payloads for stream information
- Maintain discovered stream database
- Handle stream timeouts and deletions
- Provide async API for querying discovered streams

**Key Classes:**
| Class | Description |
|-------|-------------|
| `SDPStream` | Parsed SDP stream information |
| `DiscoveredStream` | Stream with discovery metadata |
| `SAPDiscoveryService` | Main discovery service |

**Key Functions:**
| Function | Description |
|----------|-------------|
| `parse_sdp()` | Parse SDP text into SDPStream |
| `start_sap_service()` | Start global discovery service |
| `stop_sap_service()` | Stop global discovery service |
| `get_sap_service()` | Get service instance |

**Configuration:**
| Option | Default | Description |
|--------|---------|-------------|
| `multicast_addr` | 239.255.255.255 | SAP multicast address |
| `port` | 9875 | SAP port |
| `stream_timeout` | 300 | Seconds before stream expires |

---

### web/backend/app/services/config_store.py

**Purpose:** File-based configuration persistence service.

**Key Concepts:**
- All configuration stored in `~/.config/audyn/`
- Thread-safe file operations with `fcntl` locking
- Atomic writes via temp file + rename
- JSON format for human readability

**Configuration Files:**
| File | Description |
|------|-------------|
| `global.json` | Archive settings, PTP config |
| `recorders.json` | Recorder configs, studio assignments |
| `studios.json` | Studio definitions |
| `sources.json` | AES67 source configurations |
| `auth.json` | Auth settings (excluding secrets) |

**Key Functions:**
| Function | Description |
|----------|-------------|
| `ConfigStore.load()` | Load config file by name |
| `ConfigStore.save()` | Save config with atomic write |
| `load_global_config()` | Load global settings |
| `save_global_config()` | Save global settings |
| `load_recorders_config()` | Load recorder configs |
| `save_recorders_config()` | Save recorder configs |
| `load_studios_config()` | Load studio definitions |
| `save_studios_config()` | Save studio definitions |
| `load_sources_config()` | Load source configurations |
| `save_sources_config()` | Save source configurations |
| `load_auth_config()` | Load auth settings |
| `save_auth_config()` | Save auth settings |

---

### web/backend/app/websocket/levels.py

**Purpose:** Real-time audio level streaming via WebSocket.

**Features:**
- Connection management
- Broadcast to all connected clients
- Simulated audio levels (development)
- 30 FPS update rate

**WebSocket Endpoints:**
| Endpoint | Description |
|----------|-------------|
| `/ws/levels` | All recorder levels |
| `/ws/levels/{id}` | Single recorder levels |

---

## Web Frontend Files

### web/frontend/src/main.js

**Purpose:** Vue.js application entry point.

**Responsibilities:**
- Create Vue application
- Register plugins (Pinia, Router, Vuetify)
- Mount application to DOM

---

### web/frontend/src/App.vue

**Purpose:** Root component with navigation and layout.

**Features:**
- Navigation drawer with role-based menu
- App bar with recording status
- Theme toggle
- Snackbar notifications
- Authentication state management

---

### web/frontend/src/stores/recorders.js

**Purpose:** Pinia store for recorder state management.

**State:**
| Field | Type | Description |
|-------|------|-------------|
| `recorders` | Array | List of recorder objects |
| `activeCount` | Number | Number of active recorders |
| `maxRecorders` | Number | Maximum recorders (6) |
| `loading` | Boolean | Loading state |
| `error` | String | Error message |
| `connected` | Boolean | WebSocket connection state |

**Actions:**
| Action | Description |
|--------|-------------|
| `fetchRecorders()` | Load recorders from API |
| `startRecorder()` | Start specific recorder |
| `stopRecorder()` | Stop specific recorder |
| `startAllRecorders()` | Start all recorders |
| `stopAllRecorders()` | Stop all recorders |
| `connectLevels()` | Connect WebSocket |
| `disconnectLevels()` | Disconnect WebSocket |

---

### web/frontend/src/stores/studios.js

**Purpose:** Pinia store for studio state management.

**State:**
| Field | Type | Description |
|-------|------|-------------|
| `studios` | Array | List of studio objects |
| `loading` | Boolean | Loading state |
| `error` | String | Error message |

**Actions:**
| Action | Description |
|--------|-------------|
| `fetchStudios()` | Load studios from API |
| `createStudio()` | Create new studio |
| `updateStudio()` | Update studio |
| `deleteStudio()` | Delete studio |
| `assignRecorder()` | Assign recorder to studio |

---

### web/frontend/src/views/Overview.vue

**Purpose:** Dashboard showing all recorder meters.

**Features:**
- Grid of recorder cards
- Real-time audio meters
- Global start/stop controls
- Summary statistics

---

### web/frontend/src/views/Recorders.vue

**Purpose:** Recorder management table.

**Features:**
- Table with all recorders
- Active count selector
- Start/stop controls
- Mini audio meters
- Statistics cards

---

### web/frontend/src/views/Studios.vue

**Purpose:** Studio configuration interface.

**Features:**
- Studio cards with colors
- Create/edit/delete studios
- Recorder assignment dropdown
- Assigned recorder status

---

### web/frontend/src/views/Files.vue

**Purpose:** File browser for recordings.

**Features:**
- Directory navigation
- Studio filter (admin)
- File list with actions
- Audio preview player
- Delete confirmation

---

### web/frontend/src/components/AudioMeter.vue

**Purpose:** Full-size audio level meter component.

**Props:**
| Prop | Type | Description |
|------|------|-------------|
| `channel` | Object | Channel level data |

**Display:**
- Gradient background (green → red)
- Level fill bar
- Peak indicator
- dB value display

---

### web/frontend/src/components/RecorderCard.vue

**Purpose:** Recorder status card for Overview page.

**Props:**
| Prop | Type | Description |
|------|------|-------------|
| `recorder` | Object | Recorder data |
| `studio` | Object | Assigned studio (optional) |

**Features:**
- Studio color border
- Status chip
- Compact meters
- Start/stop buttons

---

### web/frontend/src/components/StreamBrowser.vue

**Purpose:** SAP/SDP stream discovery dialog with channel selection.

**Props:**
| Prop | Type | Description |
|------|------|-------------|
| `modelValue` | Boolean | Dialog visibility (v-model) |

**Events:**
| Event | Description |
|-------|-------------|
| `update:modelValue` | Dialog visibility changed |
| `imported` | Stream imported as source |

**Features:**
- Start/stop SAP discovery
- Auto-refresh stream list every 5 seconds
- Display discovered streams with format details
- Multi-channel stream detection
- Channel selection for multi-channel streams
- Preview selected channels
- Import stream as source with channel configuration

**Channel Selection Options:**
| Option | Description |
|--------|-------------|
| Output Channels | Number of channels to record (1-8) |
| Start at Channel | First channel to extract (0-based offset) |

---

## Configuration Files

### Makefile

**Purpose:** Build configuration for core engine.

**Targets:**
| Target | Description |
|--------|-------------|
| `all` | Build audyn (default) |
| `clean` | Remove build artifacts |
| `install` | Install to /usr/local/bin |
| `uninstall` | Remove installation |
| `debug` | Build with debug symbols |
| `release` | Optimized build |
| `check-deps` | Verify dependencies |

---

### packaging/audyn.service

**Purpose:** systemd service unit file.

**Features:**
- Type: simple
- Restart: on-failure
- Environment file: /etc/audyn/audyn.conf
- After: network.target

---

### web/frontend/vite.config.js

**Purpose:** Vite build configuration for frontend.

**Features:**
- Vue plugin
- Vuetify plugin
- Path aliases (@/)
- Development proxy to backend

---

### web/docker-compose.yml

**Purpose:** Docker Compose configuration for deployment.

**Services:**
| Service | Description |
|---------|-------------|
| backend | FastAPI application |
| frontend | Nginx serving Vue build |

---

## User Configuration Files

Configuration files stored in `~/.config/audyn/`.

### global.json

**Purpose:** Global capture and archive settings.

**Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `archive_root` | string | Root directory for recordings |
| `source_type` | string | Input type: `aes67` or `pipewire` |
| `format` | string | Output format: `wav` or `opus` |
| `bitrate` | number | Opus bitrate in bps |
| `sample_rate` | number | Audio sample rate in Hz |
| `channels` | number | Channel count (1 or 2) |
| `archive_layout` | string | File naming layout |
| `archive_period` | number | Rotation period in seconds |
| `archive_clock` | string | Clock source for timestamps |
| `vox_facility_enabled` | boolean | Global enable for VOX feature |

**VOX Facility:** When `vox_facility_enabled` is true, individual recorders can enable VOX mode via their `vox_enabled` setting. VOX creates separate segment files for each speech burst instead of time-based rotation.

---

### recorders.json

**Purpose:** Multi-recorder instance configurations.

**Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `active_count` | number | Number of active recorders (1-6) |
| `recorders` | object | Map of recorder ID to configuration |

**Recorder Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Recorder display name |
| `enabled` | boolean | Whether recorder is enabled |
| `studio_id` | string | Assigned studio ID |
| `multicast_addr` | string | AES67 multicast address |
| `port` | number | UDP port number |
| `archive_path` | string | Recording output directory |
| `vox_enabled` | boolean | Enable VOX for this recorder |
| `vox_threshold_db` | number | Activation threshold in dB |
| `vox_release_db` | number | Release threshold (0 = auto) |
| `vox_detection_ms` | number | Detection delay in ms |
| `vox_hangover_ms` | number | Hang time in ms |
| `vox_preroll_ms` | number | Pre-roll buffer in ms |
| `vox_level_mode` | string | Level mode: rms, peak, any |

---

### studios.json

**Purpose:** Studio definitions and recorder assignments.

**Studio Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Studio display name |
| `description` | string | Optional description |
| `color` | string | Hex color code for UI |
| `enabled` | boolean | Whether studio is active |
| `recorder_id` | number | Assigned recorder ID |

---

### sources.json

**Purpose:** AES67 audio source configurations.

**Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `active_source_id` | string | Currently selected source |
| `sources` | object | Map of source ID to configuration |

**Source Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `id` | string | Source identifier |
| `name` | string | Source display name |
| `multicast_addr` | string | Multicast IP address |
| `port` | number | UDP port (default: 5004) |
| `sample_rate` | number | Sample rate in Hz |
| `channels` | number | Channel count |
| `enabled` | boolean | Whether source is enabled |

---

### auth.json

**Purpose:** Authentication configuration (excluding secrets).

**Fields:**
| Field | Type | Description |
|-------|------|-------------|
| `entra_tenant_id` | string | Azure AD tenant ID |
| `entra_client_id` | string | Azure AD client ID |
| `entra_redirect_uri` | string | OAuth redirect URL |
| `breakglass_password_hash` | string | Bcrypt hash of emergency password |

**Security:** Client secrets are NOT stored in this file - they must be provided via environment variables.

---

## See Also

- [Architecture](ARCHITECTURE.md) - System design overview
- [Developer Guide](DEVELOPER.md) - Development setup
- [API Reference](API.md) - REST API documentation
