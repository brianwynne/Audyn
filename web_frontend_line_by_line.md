# Web Frontend Implementation Review

## Overview

The Audyn web frontend is a Vue.js 3 application that provides a real-time monitoring and control interface for the audio recording system. It uses the Composition API, Pinia for state management, Vuetify 3 for UI components, and WebSockets for live audio level streaming.

**Technology Stack:**
- Vue.js 3.5 with Composition API
- Pinia 3.0 for state management
- Vuetify 3.7 for Material Design components
- Vue Router 4 for navigation
- WebSocket for real-time audio levels

**Files:**
- `src/main.js` - Application entry point
- `src/App.vue` - Root component with navigation
- `src/plugins/router.js` - Route definitions and auth guards
- `src/plugins/vuetify.js` - Theme configuration
- `src/stores/` - Pinia state stores
- `src/components/` - Reusable UI components
- `src/views/` - Page-level components

## Key Features

- **Real-time Audio Meters:** GPU-accelerated level visualization at 30fps
- **Multi-Recorder Management:** Control up to 6 independent recorders
- **Studio-Based Organisation:** Group recorders and files by studio
- **Microsoft Entra ID Authentication:** Enterprise SSO with role-based access
- **File Browser:** Navigate and play recorded audio files
- **Responsive Design:** Works on desktop and mobile devices

## Application Entry Point

### `src/main.js`

Creates and configures the Vue application.

**Process:**
1. Import Vue and plugins (Vuetify, Router, Pinia)
2. Create Vue app with root App component
3. Register plugins in order: Pinia, Router, Vuetify
4. Mount to `#app` element

```javascript
import { createApp } from 'vue'
import { createPinia } from 'pinia'
import App from './App.vue'
import router from './plugins/router'
import vuetify from './plugins/vuetify'

const app = createApp(App)

app.use(createPinia())
app.use(router)
app.use(vuetify)
app.mount('#app')
```

**Note:** Order matters - Pinia must be registered before Router since navigation guards depend on stores.

## Root Component

### `src/App.vue`

Main application shell with navigation drawer and app bar.

**Template Structure:**
- `v-navigation-drawer` - Side navigation (visible when authenticated)
- `v-app-bar` - Top bar with recording status and theme toggle
- `v-main` - Route content area
- `v-snackbar` - Toast notifications

**Key Computed Properties:**

```javascript
const recordingCount = computed(() =>
  recordersStore.recordingRecorders.length
)

const currentStudio = computed(() => {
  if (authStore.selectedStudioId) {
    return studiosStore.getStudioById(authStore.selectedStudioId)
  }
  return null
})

const initials = computed(() => {
  const name = authStore.userName
  return name.split(' ').map(n => n[0]).join('').toUpperCase().slice(0, 2)
})
```

**Initialization (onMounted):**
```javascript
onMounted(async () => {
  if (authStore.isAuthenticated) {
    await Promise.all([
      captureStore.fetchStatus(),
      captureStore.fetchSources(),
      recordersStore.fetchRecorders(),
      studiosStore.fetchStudios(),
      authStore.fetchSelectedStudio()
    ])
    recordersStore.connectLevels()
  }
})
```

**Optimization:** Uses `Promise.all()` to fetch all initial data in parallel rather than sequentially.

**Theme Toggle:**
```javascript
function toggleTheme() {
  theme.global.name.value = isDark.value ? 'audynLightTheme' : 'audynTheme'
}
```

## Router Configuration

### `src/plugins/router.js`

Vue Router 4 configuration with authentication guards.

**Route Definitions:**

| Path | Name | Component | Auth | Admin |
|------|------|-----------|------|-------|
| `/login` | login | LoginView | No | No |
| `/studio-select` | studio-select | StudioSelectView | Yes | No |
| `/studio/:id` | studio-view | StudioView | Yes | No |
| `/files` | files | FilesView | Yes | Yes |
| `/overview` | overview | OverviewView | Yes | Yes |
| `/recorders` | recorders | RecordersView | Yes | Yes |
| `/studios` | studios | StudiosView | Yes | Yes |
| `/sources` | sources | SourcesView | Yes | Yes |
| `/settings` | settings | SettingsView | Yes | Yes |
| `/` | (redirect) | - | - | - |

**Navigation Guard:**
```javascript
router.beforeEach(async (to, from, next) => {
  const authStore = useAuthStore()

  // Check if route requires auth
  if (to.meta.requiresAuth) {
    // Redirect to login if not authenticated
    if (!authStore.isAuthenticated) {
      return next({ name: 'login', query: { redirect: to.fullPath } })
    }

    // Check admin requirement
    if (to.meta.requiresAdmin && !authStore.isAdmin) {
      return next({ name: 'studio-select' })
    }
  }

  // Redirect authenticated users away from login
  if (to.name === 'login' && authStore.isAuthenticated) {
    return next({ name: 'studio-select' })
  }

  next()
})
```

## Theme Configuration

### `src/plugins/vuetify.js`

Vuetify 3 theme with dark and light variants.

**Dark Theme (Default):**
```javascript
audynTheme: {
  dark: true,
  colors: {
    primary: '#1976D2',
    secondary: '#424242',
    accent: '#FF4081',
    error: '#FF5252',
    info: '#2196F3',
    success: '#4CAF50',
    warning: '#FB8C00',
    background: '#121212',
    surface: '#1E1E1E'
  }
}
```

**Light Theme:**
```javascript
audynLightTheme: {
  dark: false,
  colors: {
    primary: '#1976D2',
    secondary: '#757575',
    accent: '#FF4081',
    error: '#FF5252',
    info: '#2196F3',
    success: '#4CAF50',
    warning: '#FB8C00',
    background: '#FAFAFA',
    surface: '#FFFFFF'
  }
}
```

## State Management (Pinia Stores)

### `src/stores/auth.js`

Manages authentication state and user session.

**State:**
```javascript
const user = ref(null)
const token = ref(localStorage.getItem('authToken'))
const loading = ref(false)
const error = ref(null)
const selectedStudioId = ref(localStorage.getItem('selectedStudio'))
```

**Computed Properties:**
- `isAuthenticated` - Token exists and is valid
- `userName` - Display name from user object
- `userEmail` - Email address
- `isAdmin` - User has admin role
- `hasSelectedStudio` - Studio is selected

**Key Methods:**

```javascript
async function login() {
  // Redirect to Entra ID OAuth flow
  window.location.href = '/api/auth/login'
}

async function handleCallback(code) {
  // Exchange authorization code for tokens
  const response = await fetch(`/api/auth/callback?code=${code}`)
  if (response.ok) {
    const data = await response.json()
    token.value = data.access_token
    user.value = data.user
    localStorage.setItem('authToken', data.access_token)
    return true
  }
  return false
}

async function logout() {
  token.value = null
  user.value = null
  localStorage.removeItem('authToken')
  localStorage.removeItem('selectedStudio')
  window.location.href = '/api/auth/logout'
}
```

### `src/stores/recorders.js`

Manages multiple recorder instances with real-time level updates.

**State:**
```javascript
const recorders = ref([])
const activeCount = ref(6)
const maxRecorders = ref(6)
const loading = ref(false)
const error = ref(null)
const connected = ref(false)
let reconnectAttempts = 0
const maxReconnectDelay = 30000
```

**WebSocket Connection:**
```javascript
function connectLevels() {
  const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  const wsUrl = `${protocol}//${window.location.host}/ws/levels`
  levelsWebSocket = new WebSocket(wsUrl)

  levelsWebSocket.onopen = () => {
    connected.value = true
    reconnectAttempts = 0  // Reset on success
  }

  levelsWebSocket.onmessage = (event) => {
    const data = JSON.parse(event.data)
    if (data.type === 'all_levels') {
      for (const recorderData of data.recorders) {
        const recorder = recorders.value.find(r => r.id === recorderData.recorder_id)
        if (recorder) {
          recorder.levels = recorderData.channels
          recorder.state = recorderData.state
        }
      }
    }
  }

  levelsWebSocket.onclose = () => {
    connected.value = false
    // Exponential backoff reconnection
    reconnectAttempts++
    const delay = Math.min(1000 * Math.pow(2, reconnectAttempts - 1), maxReconnectDelay)
    setTimeout(() => connectLevels(), delay)
  }
}
```

**Optimization:** Uses exponential backoff (1s, 2s, 4s... up to 30s) for reconnection attempts to prevent server overload.

**Recorder Control Methods:**
```javascript
async function startRecorder(recorderId) {
  const response = await fetch(`/api/recorders/${recorderId}/start`, { method: 'POST' })
  if (response.ok) {
    await fetchRecorders()
    return true
  }
  return false
}

async function stopRecorder(recorderId) {
  const response = await fetch(`/api/recorders/${recorderId}/stop`, { method: 'POST' })
  if (response.ok) {
    await fetchRecorders()
    return true
  }
  return false
}
```

### `src/stores/studios.js`

Manages studio configurations.

**State:**
```javascript
const studios = ref([])
const loading = ref(false)
const error = ref(null)
```

**Getters:**
```javascript
const getStudioById = (id) => studios.value.find(s => s.id === id)
const getStudioByRecorderId = (recorderId) =>
  studios.value.find(s => s.recorder_id === recorderId)
const enabledStudios = computed(() =>
  studios.value.filter(s => s.enabled)
)
```

### `src/stores/capture.js`

Manages capture configuration and status.

**State:**
```javascript
const status = ref({})
const sources = ref([])
const config = ref(null)
const loading = ref(false)
const error = ref(null)
```

## Components

### `src/components/AudioMeter.vue`

GPU-accelerated audio level meter component.

**Props:**
```javascript
const props = defineProps({
  channel: {
    type: Object,  // { name, level_db, peak_db, clipping }
    required: true
  }
})
```

**Level Calculations:**
```javascript
// Convert dB to percentage (0-100)
// -60 dB = 0%, 0 dB = 100%
const levelPercent = computed(() => {
  const db = Math.max(-60, Math.min(0, props.channel.level_db))
  return ((db + 60) / 60) * 100
})

const peakPercent = computed(() => {
  const db = Math.max(-60, Math.min(0, props.channel.peak_db))
  return ((db + 60) / 60) * 100
})

const meterClass = computed(() => {
  if (props.channel.clipping) return 'clip'
  if (props.channel.level_db > -6) return 'peak'
  if (props.channel.level_db > -12) return 'high'
  return 'normal'
})
```

**Meter Visualization Technique:**

Uses a CSS transform-based approach for GPU acceleration:

```vue
<div class="meter-background">
  <!-- Gradient background (green to red) -->
  <div
    class="meter-fill"
    :style="{ transform: `scaleX(${(100 - levelPercent) / 100})` }"
  />
  <!-- Peak indicator -->
  <div class="meter-peak" :style="{ left: `${peakPercent}%` }" />
</div>
```

**CSS Implementation:**
```css
.meter-background {
  height: 24px;
  background: linear-gradient(to right,
    #1b5e20 0%,     /* Dark green at -60dB */
    #4caf50 50%,    /* Green at -30dB */
    #ffc107 75%,    /* Yellow at -15dB */
    #ff9800 85%,    /* Orange at -9dB */
    #f44336 95%,    /* Red at -3dB */
    #d32f2f 100%    /* Dark red at 0dB */
  );
  overflow: hidden;
}

.meter-fill {
  position: absolute;
  right: 0;
  width: 100%;
  height: 100%;
  background: rgba(0, 0, 0, 0.7);
  transform-origin: right center;
  will-change: transform;  /* Hint for GPU optimization */
}
```

**Optimization:** Uses `transform: scaleX()` instead of `width` for smooth, GPU-accelerated animation. The meter-fill is a dark overlay that scales from the right, revealing the gradient beneath.

### `src/components/AudioPlayer.vue`

HTML5 audio player with waveform visualization.

**Features:**
- Play/pause/seek controls
- Volume control
- Waveform display
- Time display (current/duration)
- Download button

## Views

### `src/views/StudioView.vue`

Main studio interface showing recorders and files.

**Layout:**
- Left panel (4 cols): Recorder list with level meters
- Right panel (8 cols): Tabbed file browser (My Studio / Other Studios)
- Bottom: Audio player (when file selected)

**Recorder Control:**
```javascript
async function startRecorder(recorderId) {
  await recordersStore.startRecorder(recorderId)
}

async function stopRecorder(recorderId) {
  await recordersStore.stopRecorder(recorderId)
}

async function recordAllStudioRecorders() {
  for (const recorder of studioRecorders.value) {
    if (recorder.state !== 'recording') {
      await recordersStore.startRecorder(recorder.id)
    }
  }
}
```

**File Polling:**
```javascript
// Poll for new files while recording
let filePollingInterval = null

function startFilePolling() {
  filePollingInterval = setInterval(async () => {
    if (anyRecording.value) {
      const response = await fetch(`/api/assets/browse?studio_id=${route.params.id}`)
      if (response.ok) {
        const data = await response.json()
        myStudioFiles.value = data.files || []
      }
    }
  }, 3000)
}
```

**Helper Functions:**
```javascript
function levelToPercent(db) {
  return Math.max(0, Math.min(100, ((db + 60) / 60) * 100))
}

function getLevelColor(db) {
  if (db > -3) return 'red'
  if (db > -6) return 'orange'
  if (db > -12) return 'yellow'
  return 'green'
}

function formatSize(bytes) {
  if (bytes < 1024) return `${bytes} B`
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / (1024 * 1024)).toFixed(1)} MB`
  return `${(bytes / (1024 * 1024 * 1024)).toFixed(2)} GB`
}
```

## Data Flow

### Authentication Flow

1. User navigates to protected route
2. Router guard checks `authStore.isAuthenticated`
3. If not authenticated, redirect to `/login`
4. Login button triggers `/api/auth/login` (OAuth redirect)
5. Entra ID authenticates user
6. Callback to `/api/auth/callback` with authorization code
7. Backend exchanges code for tokens
8. Frontend stores token in localStorage
9. User redirected to studio selection

### Audio Levels Flow

1. `App.vue` calls `recordersStore.connectLevels()` on mount
2. WebSocket connects to `/ws/levels`
3. Backend streams level data at 30fps:
   ```json
   {
     "type": "all_levels",
     "recorders": [
       {
         "recorder_id": 1,
         "state": "recording",
         "channels": [
           {"name": "L", "level_db": -18.5, "peak_db": -6.2, "clipping": false},
           {"name": "R", "level_db": -19.1, "peak_db": -7.0, "clipping": false}
         ]
       }
     ]
   }
   ```
4. Store updates `recorder.levels` for each recorder
5. Vue reactivity triggers component re-renders
6. `AudioMeter.vue` updates CSS transform

### Recording Control Flow

1. User clicks Record button on StudioView
2. `startRecorder(id)` called on store
3. POST request to `/api/recorders/{id}/start`
4. Backend spawns `audyn` process
5. Store fetches updated recorder list
6. UI updates to show "recording" state
7. WebSocket starts receiving real level data

## Performance Optimizations

### 1. Parallel Data Fetching
```javascript
await Promise.all([
  captureStore.fetchStatus(),
  recordersStore.fetchRecorders(),
  studiosStore.fetchStudios()
])
```
Instead of sequential fetches, all initial data loads in parallel.

### 2. GPU-Accelerated Meters
- Uses `transform: scaleX()` instead of `width`
- `will-change: transform` hints browser for GPU optimization
- `transform-origin: right center` for correct scaling direction

### 3. WebSocket Reconnection Backoff
- Starts at 1 second delay
- Doubles each attempt (exponential)
- Caps at 30 seconds maximum
- Resets to 0 on successful connection

### 4. Conditional File Polling
- Only polls during active recording
- Polls current studio only (not all studios)
- Stops polling when recording stops
- Single final refresh after recording ends

## Dependencies

### Production
- `vue` 3.5.x - Reactive UI framework
- `pinia` 3.0.x - State management
- `vue-router` 4.x - Client-side routing
- `vuetify` 3.7.x - Material Design components
- `@mdi/font` - Material Design Icons

### Development
- `vite` - Build tool and dev server
- `@vitejs/plugin-vue` - Vue SFC support
- `sass` - SCSS preprocessing

## Build Configuration

**Vite Config (`vite.config.js`):**
```javascript
export default defineConfig({
  plugins: [vue()],
  server: {
    proxy: {
      '/api': 'http://localhost:8000',
      '/ws': {
        target: 'ws://localhost:8000',
        ws: true
      }
    }
  }
})
```

**Production Build:**
```bash
npm run build  # Outputs to dist/
```

Static files are served by the FastAPI backend in production.

## Limitations

1. **Browser Storage:** Auth tokens in localStorage (consider HttpOnly cookies)
2. **Session State:** Studio selection stored per-browser, not server-side
3. **WebSocket:** Single connection for all meters (efficient but single point of failure)
4. **File Polling:** 3-second interval may miss rapid file changes
5. **Mobile:** Level meters may be too small on phone screens
