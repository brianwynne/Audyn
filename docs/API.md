# Audyn API Reference

Complete reference for the Audyn REST API and WebSocket interfaces.

## Table of Contents

1. [Overview](#overview)
2. [Authentication](#authentication)
3. [Recorders API](#recorders-api)
4. [Studios API](#studios-api)
5. [Control API](#control-api)
6. [Sources API](#sources-api)
7. [Assets API](#assets-api)
8. [Stream API](#stream-api)
9. [WebSocket API](#websocket-api)
10. [Data Models](#data-models)
11. [Error Handling](#error-handling)

---

## Overview

### Base URL

```
http://localhost:8000
```

### Content Type

All requests and responses use JSON:

```
Content-Type: application/json
```

### Authentication

In development mode (`AUDYN_DEV_MODE=true`), authentication is bypassed.

In production mode, include the Bearer token in the Authorization header:

```
Authorization: Bearer <access_token>
```

---

## Authentication

### GET /auth/login

Initiates the login flow.

**Response (Dev Mode):**
```json
{
  "dev_mode": true,
  "message": "Development mode - no login required",
  "user": {
    "id": "dev-user-001",
    "email": "admin@audyn.local",
    "name": "Admin User",
    "role": "admin",
    "roles": ["admin"]
  }
}
```

**Response (Production):**
Redirects to Entra ID login page.

### GET /auth/callback

OAuth callback endpoint (production only).

**Query Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `code` | string | Authorization code from Entra ID |

**Response:**
```json
{
  "access_token": "eyJ0eXAiOiJKV1...",
  "token_type": "Bearer",
  "expires_in": 3600,
  "user": {
    "id": "user-uuid",
    "email": "user@example.com",
    "name": "User Name",
    "role": "admin",
    "roles": ["admin"]
  }
}
```

### GET /auth/me

Returns the current authenticated user.

**Response:**
```json
{
  "id": "dev-user-001",
  "email": "admin@audyn.local",
  "name": "Admin User",
  "role": "admin",
  "roles": ["admin"],
  "studio_id": null
}
```

### POST /auth/logout

Logs out the current user.

**Response:**
```json
{
  "message": "Logged out (dev mode)"
}
```

---

## Recorders API

### GET /api/recorders/

List all active recorders.

**Response:**
```json
[
  {
    "id": 1,
    "name": "Recorder 1",
    "enabled": true,
    "state": "recording",
    "config": {
      "multicast_addr": "239.69.1.1",
      "port": 5004,
      "archive_root": "/var/lib/audyn/recorder1"
    },
    "studio_id": "studio-a",
    "levels": [
      {"name": "L", "level_db": -18.5, "level_linear": 0.119, "peak_db": -15.0, "clipping": false},
      {"name": "R", "level_db": -17.2, "level_linear": 0.138, "peak_db": -14.0, "clipping": false}
    ],
    "start_time": "2026-01-10T14:30:00Z",
    "bytes_written": 52428800,
    "current_file": "/var/lib/audyn/recorder1/2026-01-10/2026-01-10-14.opus",
    "errors": []
  }
]
```

### GET /api/recorders/{recorder_id}

Get a specific recorder by ID.

**Path Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `recorder_id` | integer | Recorder ID (1-6) |

**Response:** Single recorder object (same format as list item)

### GET /api/recorders/active-count

Get the number of active recorders.

**Response:**
```json
{
  "active_recorders": 6,
  "max_recorders": 6
}
```

### PUT /api/recorders/active-count/{count}

Set the number of active recorders. **Admin only.**

**Path Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `count` | integer | Number of recorders (1-6) |

**Response:**
```json
{
  "active_recorders": 4
}
```

### PUT /api/recorders/{recorder_id}/config

Update recorder configuration. **Admin only.**

**Request Body:**
```json
{
  "multicast_addr": "239.69.1.1",
  "port": 5004,
  "format": "opus",
  "archive_root": "/var/lib/audyn/recorder1"
}
```

**Response:**
```json
{
  "message": "Configuration updated",
  "recorder": { ... }
}
```

### POST /api/recorders/{recorder_id}/start

Start a recorder. **Admin only.**

**Response:**
```json
{
  "message": "Recording started",
  "recorder": { ... }
}
```

### POST /api/recorders/{recorder_id}/stop

Stop a recorder. **Admin only.**

**Response:**
```json
{
  "message": "Recording stopped",
  "recorder": { ... }
}
```

### POST /api/recorders/start-all

Start all active recorders. **Admin only.**

**Response:**
```json
{
  "message": "Started 4 recorders",
  "started": [1, 2, 3, 4]
}
```

### POST /api/recorders/stop-all

Stop all recorders. **Admin only.**

**Response:**
```json
{
  "message": "Stopped 4 recorders",
  "stopped": [1, 2, 3, 4]
}
```

### GET /api/recorders/{recorder_id}/levels

Get current audio levels for a recorder.

**Response:**
```json
{
  "recorder_id": 1,
  "levels": [
    {"name": "L", "level_db": -18.5, "level_linear": 0.119, "peak_db": -15.0, "clipping": false},
    {"name": "R", "level_db": -17.2, "level_linear": 0.138, "peak_db": -14.0, "clipping": false}
  ]
}
```

---

## Studios API

### GET /api/studios/

List all studios.

**Response:**
```json
[
  {
    "id": "studio-a",
    "name": "Studio A",
    "description": "Main broadcast studio",
    "color": "#F44336",
    "enabled": true,
    "recorder_id": 1
  }
]
```

### GET /api/studios/{studio_id}

Get a specific studio.

**Response:** Single studio object

### POST /api/studios/

Create a new studio. **Admin only.**

**Request Body:**
```json
{
  "name": "Studio F",
  "description": "New voice booth",
  "color": "#9C27B0"
}
```

**Response:**
```json
{
  "id": "studio-abc12345",
  "name": "Studio F",
  "description": "New voice booth",
  "color": "#9C27B0",
  "enabled": true,
  "recorder_id": null
}
```

### PUT /api/studios/{studio_id}

Update a studio. **Admin only.**

**Request Body:**
```json
{
  "name": "Updated Studio Name",
  "description": "Updated description",
  "color": "#2196F3",
  "enabled": true
}
```

**Response:** Updated studio object

### DELETE /api/studios/{studio_id}

Delete a studio. **Admin only.**

**Response:**
```json
{
  "message": "Studio deleted"
}
```

### POST /api/studios/{studio_id}/assign

Assign a recorder to a studio. **Admin only.**

**Request Body:**
```json
{
  "recorder_id": 3
}
```

To unassign:
```json
{
  "recorder_id": null
}
```

**Response:** Updated studio object

### GET /api/studios/{studio_id}/recorder

Get the recorder assigned to a studio.

**Response:**
```json
{
  "studio_id": "studio-a",
  "recorder": { ... }
}
```

### GET /api/studios/{studio_id}/recordings

Get recordings for a studio.

**Response:**
```json
{
  "studio_id": "studio-a",
  "studio_name": "Studio A",
  "recordings": []
}
```

---

## Control API

### GET /api/control/status

Get overall capture status.

**Response:**
```json
{
  "state": "recording",
  "config": {
    "source_type": "aes67",
    "multicast_addr": "239.69.1.1",
    "port": 5004
  },
  "current_file": "/var/lib/audyn/2026-01-10-14.opus",
  "recording_duration": 1823.5,
  "bytes_written": 52428800,
  "errors": []
}
```

### POST /api/control/config

Update capture configuration.

**Request Body:**
```json
{
  "source_type": "aes67",
  "multicast_addr": "239.69.1.1",
  "port": 5004,
  "sample_rate": 48000,
  "channels": 2,
  "format": "opus",
  "bitrate": 128000,
  "archive_root": "/var/lib/audyn",
  "archive_layout": "dailydir",
  "archive_period": 3600,
  "archive_clock": "localtime"
}
```

### POST /api/control/start

Start capture.

**Response:**
```json
{
  "message": "Capture started",
  "state": "recording"
}
```

### POST /api/control/stop

Stop capture.

**Response:**
```json
{
  "message": "Capture stopped",
  "state": "stopped"
}
```

---

## Sources API

### GET /api/sources/

List configured audio sources.

**Response:**
```json
[
  {
    "id": "source-1",
    "name": "Studio A Main",
    "type": "aes67",
    "multicast_addr": "239.69.1.1",
    "port": 5004,
    "enabled": true
  }
]
```

### GET /api/sources/active

Get the currently active source.

**Response:**
```json
{
  "active_source_id": "source-1"
}
```

### POST /api/sources/active/{source_id}

Set the active source.

**Response:**
```json
{
  "message": "Active source set",
  "active_source_id": "source-2"
}
```

### POST /api/sources/

Create a new source. **Admin only.**

**Request Body:**
```json
{
  "name": "Studio B Backup",
  "type": "aes67",
  "multicast_addr": "239.69.1.2",
  "port": 5004
}
```

### DELETE /api/sources/{source_id}

Delete a source. **Admin only.**

---

## Assets API

### GET /api/assets/browse

Browse archive files.

**Query Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | string | Subdirectory path (optional) |
| `studio_id` | string | Filter by studio (optional) |

**Response:**
```json
{
  "path": "2026-01-10",
  "name": "2026-01-10",
  "parent": "",
  "directories": [],
  "files": [
    {
      "name": "2026-01-10-14.opus",
      "path": "2026-01-10/2026-01-10-14.opus",
      "size": 5242880,
      "modified": "2026-01-10T15:00:00Z",
      "format": "opus"
    }
  ],
  "total_size": 52428800,
  "file_count": 10
}
```

### GET /api/assets/download/{path}

Download a file.

**Path Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | string | File path relative to archive root |

**Response:** Binary file download

### DELETE /api/assets/file/{path}

Delete a file.

**Response:**
```json
{
  "message": "File deleted"
}
```

---

## Stream API

### GET /api/stream/preview/{path}

Stream audio for preview playback.

**Path Parameters:**
| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | string | File path relative to archive root |

**Response:** Audio stream with appropriate MIME type

---

## WebSocket API

### WS /ws/levels

Real-time audio levels for all recorders.

**Connection:**
```javascript
const ws = new WebSocket('ws://localhost:8000/ws/levels');
```

**Message Format (Server → Client):**
```json
{
  "type": "all_levels",
  "timestamp": 1704898234.567,
  "recorders": [
    {
      "recorder_id": 1,
      "recorder_name": "Recorder 1",
      "studio_id": "studio-a",
      "state": "recording",
      "channels": [
        {"name": "L", "level_db": -18.5, "level_linear": 0.119, "peak_db": -15.0, "clipping": false},
        {"name": "R", "level_db": -17.2, "level_linear": 0.138, "peak_db": -14.0, "clipping": false}
      ]
    }
  ]
}
```

**Ping/Pong (Client → Server):**
```json
{"type": "ping"}
```

**Response:**
```json
{"type": "pong"}
```

**Keepalive (Server → Client):**
```json
{"type": "keepalive"}
```

### WS /ws/levels/{recorder_id}

Real-time audio levels for a single recorder.

**Message Format:**
```json
{
  "type": "levels",
  "recorder_id": 1,
  "state": "recording",
  "channels": [
    {"name": "L", "level_db": -18.5, "level_linear": 0.119, "peak_db": -15.0, "clipping": false},
    {"name": "R", "level_db": -17.2, "level_linear": 0.138, "peak_db": -14.0, "clipping": false}
  ]
}
```

---

## Data Models

### User

```typescript
interface User {
  id: string;
  email: string;
  name: string;
  role: "admin" | "studio";
  roles: string[];
  studio_id?: string;
}
```

### Recorder

```typescript
interface Recorder {
  id: number;
  name: string;
  enabled: boolean;
  state: "stopped" | "recording" | "paused" | "error";
  config: RecorderConfig;
  studio_id?: string;
  levels: ChannelLevel[];
  start_time?: string;
  bytes_written: number;
  current_file?: string;
  errors: string[];
}
```

### RecorderConfig

```typescript
interface RecorderConfig {
  multicast_addr: string;
  port: number;
  sample_rate?: number;
  channels?: number;
  format?: "wav" | "opus";
  archive_root: string;
}
```

### Studio

```typescript
interface Studio {
  id: string;
  name: string;
  description?: string;
  color: string;
  enabled: boolean;
  recorder_id?: number;
}
```

### ChannelLevel

```typescript
interface ChannelLevel {
  name: string;
  level_db: number;
  level_linear: number;
  peak_db: number;
  clipping: boolean;
}
```

### AudioFile

```typescript
interface AudioFile {
  name: string;
  path: string;
  size: number;
  modified: string;
  format: string;
}
```

---

## Error Handling

### Error Response Format

```json
{
  "detail": "Error message describing what went wrong"
}
```

### HTTP Status Codes

| Code | Description |
|------|-------------|
| 200 | Success |
| 201 | Created |
| 400 | Bad Request - Invalid input |
| 401 | Unauthorized - Not authenticated |
| 403 | Forbidden - Not authorized for this action |
| 404 | Not Found - Resource doesn't exist |
| 500 | Internal Server Error |
| 503 | Service Unavailable - Service not initialized |

### Common Errors

**401 Unauthorized:**
```json
{
  "detail": "Not authenticated"
}
```

**403 Forbidden:**
```json
{
  "detail": "Admin access required"
}
```

**404 Not Found:**
```json
{
  "detail": "Recorder 7 not found"
}
```

**400 Bad Request:**
```json
{
  "detail": "Cannot update config while recording"
}
```

---

## Rate Limiting

Currently, there is no rate limiting implemented. For production deployments, consider adding rate limiting via a reverse proxy (nginx, Traefik) or API gateway.

---

## Next Steps

- [User Guide](USER_GUIDE.md) - Web interface usage guide
- [Developer Guide](DEVELOPER.md) - API integration examples
