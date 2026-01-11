# Audyn Developer Guide

This guide is for developers who want to contribute to Audyn, extend its functionality, or integrate with its APIs.

## Table of Contents

1. [Development Setup](#development-setup)
2. [Project Structure](#project-structure)
3. [Core Engine Development](#core-engine-development)
4. [Web Application Development](#web-application-development)
5. [API Integration](#api-integration)
6. [Testing](#testing)
7. [Code Style](#code-style)
8. [Contributing](#contributing)
9. [Release Process](#release-process)

---

## Development Setup

### Prerequisites

```bash
# Build tools
sudo apt install build-essential pkg-config git

# Core dependencies
sudo apt install libpipewire-0.3-dev libopus-dev libogg-dev

# Python for backend
sudo apt install python3 python3-pip python3-venv

# Node.js for frontend (v18+)
curl -fsSL https://deb.nodesource.com/setup_18.x | sudo -E bash -
sudo apt install nodejs

# Optional: Docker
sudo apt install docker.io docker-compose
```

### Clone and Build

```bash
# Clone repository
git clone https://github.com/brianwynne/Audyn.git
cd Audyn

# Build core engine
make

# Setup backend
cd web/backend
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# Setup frontend
cd ../frontend
npm install
```

### Running in Development Mode

**Terminal 1 - Backend:**
```bash
cd web/backend
source venv/bin/activate
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

**Terminal 2 - Frontend:**
```bash
cd web/frontend
npm run dev
```

Access the application at `http://localhost:5173`

---

## Project Structure

```
Audyn/
├── audyn.c                 # Main executable entry point
├── Makefile                # Build configuration
├── README.md               # Project overview
│
├── core/                   # Core library components
│   ├── audio_queue.c/h     # Lock-free SPSC queue
│   ├── frame_pool.c/h      # Pre-allocated frame management
│   ├── ptp_clock.c/h       # PTP clock abstraction
│   ├── jitter_buffer.c/h   # RTP packet reordering
│   ├── archive_policy.c/h  # File rotation logic
│   ├── log.c/h             # Logging subsystem
│   └── worker.c/h          # Worker thread utilities
│
├── input/                  # Input source drivers
│   ├── aes_input.c/h       # AES67/RTP input
│   └── pipewire_input.c/h  # PipeWire local input
│
├── sink/                   # Output format handlers
│   ├── wav_sink.c/h        # WAV file output
│   └── opus_sink.c/h       # Opus/Ogg output
│
├── web/                    # Web application
│   ├── backend/            # Python FastAPI backend
│   │   ├── app/
│   │   │   ├── main.py         # Application entry
│   │   │   ├── models.py       # Pydantic data models
│   │   │   ├── auth/           # Authentication
│   │   │   ├── api/            # REST API endpoints
│   │   │   ├── services/       # Business logic
│   │   │   └── websocket/      # WebSocket handlers
│   │   └── requirements.txt
│   │
│   ├── frontend/           # Vue.js frontend
│   │   ├── src/
│   │   │   ├── main.js         # Application entry
│   │   │   ├── App.vue         # Root component
│   │   │   ├── views/          # Page components
│   │   │   ├── components/     # Reusable components
│   │   │   ├── stores/         # Pinia state stores
│   │   │   └── plugins/        # Vue plugins
│   │   ├── package.json
│   │   └── vite.config.js
│   │
│   └── docker-compose.yml
│
├── packaging/              # Distribution packaging
│   └── audyn.service       # systemd service file
│
├── debian/                 # Debian package files
│   ├── control
│   ├── rules
│   └── changelog
│
├── docs/                   # Documentation
│   ├── ARCHITECTURE.md
│   ├── INSTALLATION.md
│   ├── CONFIGURATION.md
│   ├── API.md
│   ├── USER_GUIDE.md
│   ├── DEVELOPER.md
│   └── FILE_REFERENCE.md
│
└── .github/
    └── workflows/
        └── release.yml     # GitHub Actions CI/CD
```

---

## Core Engine Development

### Adding a New Input Source

1. Create header file `input/new_input.h`:

```c
#ifndef AUDYN_NEW_INPUT_H
#define AUDYN_NEW_INPUT_H

#include "frame_pool.h"
#include "audio_queue.h"

typedef struct audyn_new_input audyn_new_input_t;

audyn_new_input_t *audyn_new_input_create(
    audyn_frame_pool_t *pool,
    audyn_audio_queue_t *queue,
    /* additional config */
);

int audyn_new_input_start(audyn_new_input_t *in);
void audyn_new_input_stop(audyn_new_input_t *in);
void audyn_new_input_destroy(audyn_new_input_t *in);

#endif
```

2. Implement in `input/new_input.c`:

```c
#include "new_input.h"
#include "log.h"
#include <pthread.h>

struct audyn_new_input {
    audyn_frame_pool_t *pool;
    audyn_audio_queue_t *queue;
    pthread_t thread;
    volatile int running;
    // ... additional fields
};

static void *capture_thread(void *arg) {
    audyn_new_input_t *in = (audyn_new_input_t *)arg;

    while (in->running) {
        // Acquire frame from pool
        audyn_audio_frame_t *frame = audyn_frame_acquire(in->pool);
        if (!frame) continue;

        // Fill frame with audio data
        // ...

        // Push to queue
        if (!audyn_audio_queue_push(in->queue, frame)) {
            audyn_frame_release(frame);
            LOG_WARN("Queue full, dropping frame");
        }
    }

    return NULL;
}
```

3. Update `Makefile`:

```makefile
SRCS := audyn.c \
        ...
        input/new_input.c
```

4. Integrate in `audyn.c`:

```c
#include "new_input.h"

// In main()
audyn_new_input_t *new_in = audyn_new_input_create(pool, q, ...);
```

### Adding a New Output Format

1. Create header `sink/new_sink.h`
2. Implement in `sink/new_sink.c`
3. Follow the pattern of `wav_sink.c` or `opus_sink.c`

### Memory Management Rules

1. **Frame Pool**: Always acquire frames from the pool, never malloc
2. **Reference Counting**: Call `audyn_frame_release()` when done
3. **Queue Ownership**: Queue doesn't take ownership; caller releases on failure

### Thread Safety

1. **Audio Queue**: Only safe for single producer, single consumer
2. **Frame Pool**: Thread-safe for acquire/release
3. **Logging**: Thread-safe with mutex protection

---

## Web Application Development

### Backend Development

#### Adding a New API Endpoint

1. Create new file `app/api/new_endpoint.py`:

```python
from fastapi import APIRouter, Depends, HTTPException
from ..auth.entra import get_current_user
from ..models import User

router = APIRouter()

@router.get("/")
async def list_items(user: User = Depends(get_current_user)):
    """List all items."""
    return []

@router.post("/")
async def create_item(user: User = Depends(get_current_user)):
    """Create a new item."""
    if not user.is_admin:
        raise HTTPException(status_code=403, detail="Admin access required")
    return {"id": "new-item"}
```

2. Register in `app/main.py`:

```python
from .api.new_endpoint import router as new_router

app.include_router(new_router, prefix="/api/new", tags=["New"])
```

#### Adding a New Data Model

In `app/models.py`:

```python
from pydantic import BaseModel
from typing import Optional

class NewItem(BaseModel):
    id: str
    name: str
    description: Optional[str] = None
    enabled: bool = True
```

### Frontend Development

#### Adding a New Page

1. Create `src/views/NewPage.vue`:

```vue
<template>
  <v-container fluid class="pa-4">
    <h1 class="text-h4">New Page</h1>
    <!-- Content -->
  </v-container>
</template>

<script setup>
import { onMounted } from 'vue'

onMounted(() => {
  // Fetch data
})
</script>
```

2. Register route in `src/plugins/router.js`:

```javascript
{
  path: '/new-page',
  name: 'new-page',
  component: () => import('@/views/NewPage.vue'),
  meta: { requiresAuth: true, title: 'New Page' }
}
```

3. Add navigation in `src/App.vue`:

```vue
<v-list-item
  prepend-icon="mdi-new-box"
  title="New Page"
  value="new-page"
  :to="{ name: 'new-page' }"
/>
```

#### Adding a New Store

Create `src/stores/newStore.js`:

```javascript
import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useNewStore = defineStore('new', () => {
  const items = ref([])
  const loading = ref(false)
  const error = ref(null)

  async function fetchItems() {
    loading.value = true
    try {
      const response = await fetch('/api/new/')
      if (response.ok) {
        items.value = await response.json()
      }
    } catch (err) {
      error.value = err.message
    } finally {
      loading.value = false
    }
  }

  return { items, loading, error, fetchItems }
})
```

---

## API Integration

### JavaScript/TypeScript Example

```typescript
// Fetch recorders
const response = await fetch('/api/recorders/', {
  headers: {
    'Authorization': `Bearer ${token}`
  }
});
const recorders = await response.json();

// Start a recorder
await fetch('/api/recorders/1/start', {
  method: 'POST',
  headers: {
    'Authorization': `Bearer ${token}`
  }
});

// WebSocket for levels
const ws = new WebSocket('ws://localhost:8000/ws/levels');
ws.onmessage = (event) => {
  const data = JSON.parse(event.data);
  if (data.type === 'all_levels') {
    updateMeters(data.recorders);
  }
};
```

### Python Example

```python
import httpx
import asyncio
import websockets
import json

async def main():
    # REST API
    async with httpx.AsyncClient() as client:
        response = await client.get('http://localhost:8000/api/recorders/')
        recorders = response.json()
        print(f"Found {len(recorders)} recorders")

    # WebSocket
    async with websockets.connect('ws://localhost:8000/ws/levels') as ws:
        while True:
            message = await ws.recv()
            data = json.loads(message)
            if data['type'] == 'all_levels':
                for rec in data['recorders']:
                    print(f"Recorder {rec['recorder_id']}: {rec['state']}")

asyncio.run(main())
```

---

## Testing

### Core Engine Testing

```bash
# Build with debug symbols
make debug

# Run with test input
./audyn --pipewire -o /tmp/test.wav &
sleep 5
kill %1

# Verify output
file /tmp/test.wav
```

### Backend Testing

```bash
cd web/backend
source venv/bin/activate

# Install test dependencies
pip install pytest pytest-asyncio httpx

# Run tests
pytest tests/
```

### Frontend Testing

```bash
cd web/frontend

# Run unit tests
npm run test

# Run with coverage
npm run test:coverage
```

---

## Code Style

### C Code Style

- 4-space indentation
- Opening brace on same line
- Function names: `audyn_module_action`
- Types: `audyn_module_t`
- Constants: `AUDYN_CONSTANT_NAME`

```c
int audyn_module_action(audyn_module_t *mod, int param)
{
    if (!mod) {
        return -1;
    }

    for (int i = 0; i < param; i++) {
        // ...
    }

    return 0;
}
```

### Python Code Style

- Follow PEP 8
- Use type hints
- Docstrings for public functions

```python
async def get_recorder(recorder_id: int) -> Recorder:
    """
    Get a recorder by ID.

    Args:
        recorder_id: The recorder ID (1-6)

    Returns:
        The recorder object

    Raises:
        HTTPException: If recorder not found
    """
    if recorder_id not in _recorders:
        raise HTTPException(status_code=404, detail="Recorder not found")
    return _recorders[recorder_id]
```

### JavaScript/Vue Code Style

- 2-space indentation
- Single quotes
- Composition API with `<script setup>`
- PascalCase for components
- camelCase for functions and variables

---

## Contributing

### Pull Request Process

1. Fork the repository
2. Create a feature branch: `git checkout -b feature/my-feature`
3. Make changes and commit
4. Push to your fork
5. Open a Pull Request

### Commit Messages

```
Add feature description

- Detail about change 1
- Detail about change 2

Fixes #123
```

### Review Checklist

- [ ] Code follows style guidelines
- [ ] Tests pass
- [ ] Documentation updated
- [ ] No security vulnerabilities introduced
- [ ] Backward compatibility maintained

---

## Release Process

### Version Numbering

Follows Semantic Versioning (SemVer):
- MAJOR.MINOR.PATCH
- MAJOR: Breaking changes
- MINOR: New features
- PATCH: Bug fixes

### Creating a Release

1. Update version in:
   - `debian/changelog`
   - `web/frontend/package.json`

2. Create and push tag:
   ```bash
   git tag -a v1.1.0 -m "Release v1.1.0"
   git push origin v1.1.0
   ```

3. GitHub Actions will:
   - Build Debian packages (amd64, arm64)
   - Create GitHub Release
   - Upload packages

### Manual Package Build

```bash
# Build Debian package
dpkg-buildpackage -us -uc -b

# Package is in parent directory
ls ../audyn_*.deb
```

---

## Next Steps

- [File Reference](FILE_REFERENCE.md) - Detailed file documentation
- [API Reference](API.md) - Complete API documentation
- [Architecture](ARCHITECTURE.md) - System design overview
