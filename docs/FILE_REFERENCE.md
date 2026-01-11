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
    uint16_t channels;
    uint16_t samples_per_packet;
    uint32_t socket_rcvbuf;
} audyn_aes_input_cfg_t;
```

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
| `RecorderConfig` | Recorder configuration |
| `RecorderState` | Enum: stopped, recording, paused, error |
| `Studio` | Studio configuration |
| `ChannelLevel` | Audio level measurement |
| `OutputFormat` | Enum: wav, flac, mp3, ogg |

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

## See Also

- [Architecture](ARCHITECTURE.md) - System design overview
- [Developer Guide](DEVELOPER.md) - Development setup
- [API Reference](API.md) - REST API documentation
