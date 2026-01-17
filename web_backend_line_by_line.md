# Web Backend Implementation Review

## Overview

The Audyn web backend is a FastAPI application that provides REST APIs and WebSocket endpoints for managing audio recorders. It integrates with Microsoft Entra ID for authentication, manages recorder processes, and streams real-time audio levels to connected clients.

**Technology Stack:**
- FastAPI 0.115 - Async Python web framework
- Pydantic 2.x - Data validation and serialization
- Microsoft MSAL - Entra ID authentication
- asyncio - Async subprocess management
- WebSockets - Real-time audio level streaming

**Files:**
- `app/main.py` - Application entry point and configuration
- `app/models.py` - Pydantic data models
- `app/auth/entra.py` - Microsoft Entra ID authentication
- `app/api/` - REST API route handlers
- `app/websocket/` - WebSocket handlers
- `app/services/` - Business logic and process management

## Key Features

- **Recorder Process Management:** Spawn and control multiple `audyn` processes
- **Real-time Level Streaming:** WebSocket broadcast at 30fps
- **Microsoft Entra ID SSO:** Enterprise OAuth2/OIDC authentication
- **Breakglass Access:** Local admin fallback when SSO fails
- **Persistent Configuration:** JSON file-based config storage
- **Studio Management:** Group recorders by studio

## Application Entry Point

### `app/main.py`

FastAPI application setup and configuration.

**Application Creation:**
```python
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from contextlib import asynccontextmanager

@asynccontextmanager
async def lifespan(app: FastAPI):
    # Startup
    manager = get_recorder_manager()
    await manager.initialize()
    yield
    # Shutdown
    await manager.shutdown()

app = FastAPI(
    title="Audyn Control API",
    version="1.0.0",
    lifespan=lifespan
)
```

**Middleware Configuration:**
```python
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"]
)
```

**Router Registration:**
```python
from .api import recorders, studios, sources, control, assets
from .websocket import levels
from .auth import entra

app.include_router(entra.router, prefix="/api/auth", tags=["auth"])
app.include_router(recorders.router, prefix="/api/recorders", tags=["recorders"])
app.include_router(studios.router, prefix="/api/studios", tags=["studios"])
app.include_router(sources.router, prefix="/api/sources", tags=["sources"])
app.include_router(control.router, prefix="/api/control", tags=["control"])
app.include_router(assets.router, prefix="/api/assets", tags=["assets"])
app.include_router(levels.router, prefix="/ws", tags=["websocket"])
```

## Data Models

### `app/models.py`

Pydantic models for data validation and serialization.

**Enums:**
```python
from enum import Enum

class RecorderState(str, Enum):
    STOPPED = "stopped"
    RECORDING = "recording"
    ERROR = "error"

class SourceType(str, Enum):
    AES67 = "aes67"
    PIPEWIRE = "pipewire"

class AudioFormat(str, Enum):
    WAV = "wav"
    OPUS = "opus"
    MP3 = "mp3"

class ArchiveLayout(str, Enum):
    FLAT = "flat"
    DAILYDIR = "dailydir"
    HOURLYDIR = "hourlydir"

class ArchiveClock(str, Enum):
    LOCALTIME = "localtime"
    UTC = "utc"
```

**Channel Level:**
```python
class ChannelLevel(BaseModel):
    name: str = "L"
    level_db: float = -60.0
    level_linear: float = 0.0
    peak_db: float = -60.0
    clipping: bool = False
```

**Recorder Configuration:**
```python
class RecorderConfig(BaseModel):
    source_type: SourceType = SourceType.AES67
    multicast_addr: Optional[str] = None
    port: int = 5004
    payload_type: int = 96
    samples_per_packet: int = 48
    sample_rate: int = 48000
    channels: int = 2
    format: AudioFormat = AudioFormat.WAV
    bitrate: int = 128000
```

**Recorder:**
```python
class Recorder(BaseModel):
    id: int
    name: str
    state: RecorderState = RecorderState.STOPPED
    config: Optional[RecorderConfig] = None
    studio_id: Optional[str] = None
    levels: list[ChannelLevel] = []
    current_file: Optional[str] = None
    error: Optional[str] = None
```

**Studio:**
```python
class Studio(BaseModel):
    id: str
    name: str
    description: Optional[str] = None
    color: str = "#2196F3"
    recorder_id: Optional[int] = None
    enabled: bool = True
```

**User:**
```python
class User(BaseModel):
    id: str
    email: str
    name: str
    is_admin: bool = False
    studio_id: Optional[str] = None
```

## Authentication

### `app/auth/entra.py`

Microsoft Entra ID (Azure AD) OAuth2/OIDC authentication.

**Configuration:**
```python
from msal import ConfidentialClientApplication

TENANT_ID = os.getenv("ENTRA_TENANT_ID")
CLIENT_ID = os.getenv("ENTRA_CLIENT_ID")
CLIENT_SECRET = os.getenv("ENTRA_CLIENT_SECRET")
REDIRECT_URI = os.getenv("ENTRA_REDIRECT_URI", "http://localhost:8000/api/auth/callback")

AUTHORITY = f"https://login.microsoftonline.com/{TENANT_ID}"
SCOPES = ["User.Read"]
```

**MSAL Client:**
```python
msal_app = ConfidentialClientApplication(
    CLIENT_ID,
    authority=AUTHORITY,
    client_credential=CLIENT_SECRET
)
```

**Login Endpoint:**
```python
@router.get("/login")
async def login():
    """Redirect to Entra ID login."""
    auth_url = msal_app.get_authorization_request_url(
        scopes=SCOPES,
        redirect_uri=REDIRECT_URI
    )
    return RedirectResponse(auth_url)
```

**Callback Endpoint:**
```python
@router.get("/callback")
async def callback(code: str):
    """Handle OAuth callback and exchange code for tokens."""
    result = msal_app.acquire_token_by_authorization_code(
        code,
        scopes=SCOPES,
        redirect_uri=REDIRECT_URI
    )

    if "access_token" not in result:
        raise HTTPException(status_code=401, detail="Authentication failed")

    # Get user info from Microsoft Graph
    user_info = await get_user_info(result["access_token"])

    # Create session token
    session_token = create_session_token(user_info)

    return {
        "access_token": session_token,
        "user": {
            "id": user_info["id"],
            "email": user_info["mail"],
            "name": user_info["displayName"],
            "is_admin": check_admin_group(user_info)
        }
    }
```

**Token Validation Dependency:**
```python
async def get_current_user(
    authorization: str = Header(None)
) -> User:
    """Validate token and return current user."""
    if not authorization:
        raise HTTPException(status_code=401, detail="Not authenticated")

    try:
        token = authorization.replace("Bearer ", "")
        payload = verify_session_token(token)
        return User(**payload)
    except Exception:
        raise HTTPException(status_code=401, detail="Invalid token")
```

**Breakglass Access:**
```python
@router.post("/breakglass")
async def breakglass_login(credentials: BreakglassCredentials):
    """Emergency local login when Entra ID is unavailable."""
    auth_config = load_auth_config()

    if not auth_config or not auth_config.get("breakglass_enabled"):
        raise HTTPException(status_code=403, detail="Breakglass disabled")

    if not verify_password(credentials.password, auth_config["breakglass_hash"]):
        raise HTTPException(status_code=401, detail="Invalid credentials")

    return {
        "access_token": create_session_token({
            "id": "breakglass",
            "email": "admin@local",
            "name": "Breakglass Admin",
            "is_admin": True
        })
    }
```

## Recorder Manager Service

### `app/services/recorder_manager.py`

Manages `audyn` subprocess lifecycle.

**Constants:**
```python
# Check for mock first, then real binary
MOCK_AUDYN = Path(__file__).parent.parent.parent / "scripts" / "mock_audyn.sh"
AUDYN_BIN = os.getenv("AUDYN_BIN", str(MOCK_AUDYN) if MOCK_AUDYN.exists() else "/usr/bin/audyn")
```

**Recorder Process Data:**
```python
@dataclass
class RecorderProcess:
    recorder_id: int
    process: Optional[asyncio.subprocess.Process] = None
    config: Optional[RecorderConfig] = None
    start_time: Optional[datetime] = None
    current_file: Optional[str] = None
    monitor_task: Optional[asyncio.Task] = None
    stdout_task: Optional[asyncio.Task] = None
    levels: list = field(default_factory=list)
```

**Manager Class:**
```python
class RecorderManager:
    def __init__(self):
        self._processes: dict[int, RecorderProcess] = {}
        self._lock = asyncio.Lock()

    async def initialize(self):
        logger.info(f"RecorderManager initialized. Using binary: {AUDYN_BIN}")

    async def shutdown(self):
        for recorder_id in list(self._processes.keys()):
            await self.stop_recorder(recorder_id)
```

**Command Builder:**
```python
def _build_command(self, recorder_id: int, config: RecorderConfig, studio_id: str = None):
    cmd = [AUDYN_BIN]

    # Load global config once for all settings
    global_cfg = get_global_config()

    # Archive settings
    archive_base = global_cfg.archive_root if global_cfg else "/var/lib/audyn/archive"
    archive_root = f"{archive_base}/{studio_id}" if studio_id else f"{archive_base}/recorder{recorder_id}"

    cmd.extend(["--archive-root", archive_root])
    cmd.extend(["--archive-layout", global_cfg.archive_layout.value])
    cmd.extend(["--archive-period", str(global_cfg.archive_period)])
    cmd.extend(["--archive-clock", global_cfg.archive_clock.value])
    cmd.extend(["--archive-suffix", config.format.value])

    # Source configuration
    if config.source_type == SourceType.PIPEWIRE:
        cmd.append("--pipewire")
    else:
        # AES67
        if config.multicast_addr:
            cmd.extend(["-m", config.multicast_addr])
        cmd.extend(["-p", str(config.port)])
        cmd.extend(["--pt", str(config.payload_type)])
        cmd.extend(["--spp", str(config.samples_per_packet)])

        if global_cfg and global_cfg.aes67_interface:
            cmd.extend(["--interface", global_cfg.aes67_interface])

    # Audio format
    cmd.extend(["-r", str(config.sample_rate)])
    cmd.extend(["-c", str(config.channels)])

    # Format-specific options
    if config.format.value in ["opus", "mp3"]:
        cmd.extend(["--bitrate", str(config.bitrate)])

    # Enable level metering
    cmd.append("--levels")

    return cmd
```

**Start Recorder:**
```python
async def start_recorder(self, recorder_id: int, config: RecorderConfig, studio_id: str = None):
    async with self._lock:
        if recorder_id in self._processes:
            proc = self._processes[recorder_id]
            if proc.process and proc.process.returncode is None:
                logger.warning(f"Recorder {recorder_id} already running")
                return False

        # Ensure archive directory exists
        os.makedirs(archive_root, exist_ok=True)

        cmd = self._build_command(recorder_id, config, studio_id)
        logger.info(f"Starting recorder {recorder_id}: {' '.join(cmd)}")

        process = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )

        rec_proc = RecorderProcess(
            recorder_id=recorder_id,
            process=process,
            config=config,
            start_time=datetime.now()
        )

        # Start monitor tasks
        rec_proc.monitor_task = asyncio.create_task(self._monitor_process(recorder_id))
        rec_proc.stdout_task = asyncio.create_task(self._read_levels(recorder_id))

        self._processes[recorder_id] = rec_proc
        return True
```

**Stop Recorder:**
```python
async def stop_recorder(self, recorder_id: int):
    async with self._lock:
        if recorder_id not in self._processes:
            return False

        proc = self._processes[recorder_id]

        # Cancel monitor tasks
        if proc.monitor_task:
            proc.monitor_task.cancel()
        if proc.stdout_task:
            proc.stdout_task.cancel()

        # Send SIGTERM for graceful shutdown
        proc.process.send_signal(signal.SIGTERM)

        # Wait up to 5 seconds
        try:
            await asyncio.wait_for(proc.process.wait(), timeout=5)
        except asyncio.TimeoutError:
            proc.process.kill()
            await proc.process.wait()

        del self._processes[recorder_id]
        return True
```

**Level Reader:**
```python
async def _read_levels(self, recorder_id: int):
    """Read and parse level JSON from stdout."""
    proc = self._processes[recorder_id]

    while proc.process.returncode is None:
        try:
            line = await asyncio.wait_for(
                proc.process.stdout.readline(),
                timeout=1.0
            )
            if line:
                line_str = line.decode().strip()
                if line_str.startswith('{') and '"type":"levels"' in line_str:
                    data = json.loads(line_str)
                    self._update_levels(recorder_id, data)
        except asyncio.TimeoutError:
            pass
        await asyncio.sleep(0.01)
```

**Update Levels:**
```python
def _update_levels(self, recorder_id: int, data: dict):
    """Update stored levels from JSON data."""
    proc = self._processes[recorder_id]
    levels = []

    if "left" in data:
        left = data["left"]
        levels.append(ChannelLevel(
            name="L",
            level_db=left.get("rms_db", -60),
            level_linear=10 ** (left.get("rms_db", -60) / 20),
            peak_db=left.get("peak_db", -60),
            clipping=left.get("clipping", False)
        ))

    if "right" in data:
        right = data["right"]
        levels.append(ChannelLevel(
            name="R",
            level_db=right.get("rms_db", -60),
            level_linear=10 ** (right.get("rms_db", -60) / 20),
            peak_db=right.get("peak_db", -60),
            clipping=right.get("clipping", False)
        ))

    proc.levels = levels
```

**Singleton Pattern:**
```python
_manager: Optional[RecorderManager] = None

def get_recorder_manager() -> RecorderManager:
    global _manager
    if _manager is None:
        _manager = RecorderManager()
    return _manager
```

## Configuration Store

### `app/services/config_store.py`

Thread-safe file-based configuration persistence.

**Configuration Files:**
```python
CONFIG_FILES = {
    "global": "global.json",        # Global capture settings
    "recorders": "recorders.json",  # Recorder configurations
    "studios": "studios.json",      # Studio definitions
    "sources": "sources.json",      # AES67 source configurations
    "auth": "auth.json",            # Authentication settings
    "system": "system.json",        # System settings
    "ssl": "ssl.json",              # SSL certificate configuration
    "network": "network.json",      # Control interface network
    "aes67_network": "aes67_network.json"  # AES67 interface network
}
```

**Load with File Locking:**
```python
def load(self, config_name: str, default: Any = None) -> Any:
    path = self._get_path(config_name)

    if not path.exists():
        return deepcopy(default) if default is not None else None

    with open(path, 'r') as f:
        # Acquire shared lock for reading
        fcntl.flock(f.fileno(), fcntl.LOCK_SH)
        try:
            data = json.load(f)
            self._cache[config_name] = data
            return data
        finally:
            fcntl.flock(f.fileno(), fcntl.LOCK_UN)
```

**Atomic Save:**
```python
def save(self, config_name: str, data: Any) -> bool:
    path = self._get_path(config_name)
    temp_path = path.with_suffix('.tmp')

    # Write to temp file first
    with open(temp_path, 'w') as f:
        fcntl.flock(f.fileno(), fcntl.LOCK_EX)
        try:
            json.dump(data, f, indent=2, default=self._json_serializer)
            f.flush()
            os.fsync(f.fileno())
        finally:
            fcntl.flock(f.fileno(), fcntl.LOCK_UN)

    # Atomic rename
    os.rename(temp_path, path)
    return True
```

**Convenience Functions:**
```python
def load_global_config() -> Optional[dict]:
    return get_config_store().load("global")

def save_global_config(config: dict) -> bool:
    return get_config_store().save("global", config)

def load_recorders_config() -> Optional[dict]:
    return get_config_store().load("recorders")
```

## WebSocket Handler

### `app/websocket/levels.py`

Real-time audio level streaming via WebSocket.

**Connection Manager:**
```python
class ConnectionManager:
    def __init__(self):
        self.active_connections: list[WebSocket] = []
        self._level_task: Optional[asyncio.Task] = None
        self._running = False

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)

        # Start level broadcasting if not already running
        if not self._running:
            self._running = True
            self._level_task = asyncio.create_task(self._broadcast_levels())

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)

        if not self.active_connections:
            self._running = False
            if self._level_task:
                self._level_task.cancel()
```

**Parallel Broadcast:**
```python
async def broadcast(self, message: dict):
    """Broadcast message to all connected clients in parallel."""
    if not self.active_connections:
        return

    data = json.dumps(message)

    async def send_to_client(connection: WebSocket):
        try:
            await connection.send_text(data)
            return None
        except Exception:
            return connection

    # Send to all clients in parallel
    results = await asyncio.gather(
        *[send_to_client(conn) for conn in self.active_connections],
        return_exceptions=True
    )

    # Clean up disconnected clients
    for result in results:
        if result is not None and isinstance(result, WebSocket):
            self.disconnect(result)
```

**Optimization:** Uses `asyncio.gather()` to send to all clients in parallel rather than sequentially.

**Level Broadcasting Loop:**
```python
async def _broadcast_levels(self):
    """Continuously broadcast audio levels for all recorders."""
    manager = get_recorder_manager()
    phases = {i: random.random() * 6.28 for i in range(1, 7)}  # For fallback simulation

    while self._running:
        recorders = get_all_recorders()
        recorder_levels = []

        for recorder in recorders:
            if recorder.state == RecorderState.RECORDING:
                # Try real levels from audyn process
                real_levels = manager.get_levels(recorder.id)

                if real_levels:
                    levels = real_levels
                else:
                    # Fallback to simulated levels
                    levels = generate_simulated_levels(phases, recorder.id)
            else:
                # Silence when not recording
                levels = [
                    ChannelLevel(name="L", level_db=-60, level_linear=0, peak_db=-60, clipping=False),
                    ChannelLevel(name="R", level_db=-60, level_linear=0, peak_db=-60, clipping=False)
                ]

            update_recorder_levels(recorder.id, levels)

            recorder_levels.append({
                "recorder_id": recorder.id,
                "recorder_name": recorder.name,
                "studio_id": recorder.studio_id,
                "state": recorder.state.value,
                "channels": [l.model_dump() for l in levels]
            })

        await self.broadcast({
            "type": "all_levels",
            "timestamp": asyncio.get_event_loop().time(),
            "recorders": recorder_levels
        })

        # Update at ~30 fps
        await asyncio.sleep(1 / 30)
```

**WebSocket Endpoint:**
```python
@router.websocket("/levels")
async def websocket_all_levels(websocket: WebSocket):
    await manager.connect(websocket)

    try:
        while True:
            try:
                data = await asyncio.wait_for(websocket.receive_text(), timeout=30)
                message = json.loads(data)
                if message.get("type") == "ping":
                    await websocket.send_text(json.dumps({"type": "pong"}))
            except asyncio.TimeoutError:
                await websocket.send_text(json.dumps({"type": "keepalive"}))

    except WebSocketDisconnect:
        manager.disconnect(websocket)
```

## REST API Routes

### `app/api/recorders.py`

Recorder CRUD and control operations.

**List Recorders:**
```python
@router.get("/", response_model=list[Recorder])
async def list_recorders(user: User = Depends(get_current_user)):
    return list(_recorders.values())
```

**Start Recording:**
```python
@router.post("/{recorder_id}/start")
async def start_recording(recorder_id: int, user: User = Depends(get_current_user)):
    recorder = get_recorder(recorder_id)

    manager = get_recorder_manager()
    success = await manager.start_recorder(
        recorder_id,
        recorder.config,
        recorder.studio_id
    )

    if success:
        recorder.state = RecorderState.RECORDING
        _save_recorders()
        return {"message": "Recording started", "recorder_id": recorder_id}
    else:
        raise HTTPException(status_code=500, detail="Failed to start recording")
```

**Stop Recording:**
```python
@router.post("/{recorder_id}/stop")
async def stop_recording(recorder_id: int, user: User = Depends(get_current_user)):
    recorder = get_recorder(recorder_id)

    manager = get_recorder_manager()
    success = await manager.stop_recorder(recorder_id)

    if success:
        recorder.state = RecorderState.STOPPED
        _save_recorders()
        return {"message": "Recording stopped", "recorder_id": recorder_id}
    else:
        raise HTTPException(status_code=500, detail="Failed to stop recording")
```

**Update Configuration:**
```python
@router.put("/{recorder_id}/config")
async def update_config(
    recorder_id: int,
    config: RecorderConfig,
    user: User = Depends(get_current_user)
):
    recorder = get_recorder(recorder_id)

    if recorder.state == RecorderState.RECORDING:
        raise HTTPException(status_code=400, detail="Cannot update while recording")

    recorder.config = config
    _save_recorders()
    return recorder
```

### `app/api/studios.py`

Studio management with recorder assignments.

**Create Studio:**
```python
@router.post("/", response_model=Studio)
async def create_studio(
    studio: StudioCreate,
    user: User = Depends(get_current_user)
):
    require_admin(user)

    studio_id = f"studio-{uuid.uuid4().hex[:8]}"
    new_studio = Studio(
        id=studio_id,
        name=studio.name,
        description=studio.description,
        color=studio.color
    )

    _studios[studio_id] = new_studio
    _save_studios()
    return new_studio
```

**Assign Recorder:**
```python
@router.post("/{studio_id}/assign", response_model=Studio)
async def assign_recorder(
    studio_id: str,
    assignment: AssignRecorder,
    user: User = Depends(get_current_user)
):
    require_admin(user)
    studio = get_studio(studio_id)

    # Unassign current recorder if any
    if studio.recorder_id and studio.recorder_id in _recorders:
        _recorders[studio.recorder_id].studio_id = None

    if assignment.recorder_id:
        recorder = get_recorder(assignment.recorder_id)

        # Unassign from previous studio
        if recorder.studio_id:
            prev_studio = _studios.get(recorder.studio_id)
            if prev_studio:
                prev_studio.recorder_id = None

        # Assign to new studio
        recorder.studio_id = studio_id
        studio.recorder_id = assignment.recorder_id

    _save_studios()
    _save_recorders()
    return studio
```

**Studio Selection:**
```python
@router.post("/select/{studio_id}")
async def select_studio(studio_id: str, user: User = Depends(get_current_user)):
    studio = get_studio(studio_id)
    _user_selected_studios[user.id] = studio_id
    return {"message": "Studio selected", "studio": studio}

@router.get("/current-selection")
async def get_current_selection(user: User = Depends(get_current_user)):
    studio_id = _user_selected_studios.get(user.id)
    if studio_id and studio_id in _studios:
        return {"studio_id": studio_id, "studio": _studios[studio_id]}
    return {"studio_id": None, "studio": None}
```

## Data Flow

### Recording Start Flow

1. Frontend calls `POST /api/recorders/{id}/start`
2. API handler gets recorder from in-memory store
3. Calls `recorder_manager.start_recorder()`
4. Manager builds command line from config
5. Creates archive directory
6. Spawns `audyn` subprocess via `asyncio.create_subprocess_exec()`
7. Starts monitor task for stderr (logs)
8. Starts stdout task for level JSON
9. Updates recorder state to RECORDING
10. Persists state to `recorders.json`
11. Returns success response

### Level Streaming Flow

1. `audyn` process outputs JSON to stdout:
   ```json
   {"type":"levels","channels":2,"left":{"rms_db":-18.5,"peak_db":-6.2},"right":{"rms_db":-19.1,"peak_db":-7.0}}
   ```
2. `_read_levels()` task parses JSON
3. Calls `_update_levels()` to store in `RecorderProcess.levels`
4. `_broadcast_levels()` loop (running at 30fps):
   - Gets all recorders
   - Fetches levels from manager for each
   - Builds broadcast message
   - Calls `broadcast()` to all WebSocket clients
5. Frontend receives message and updates store

### Authentication Flow

1. User navigates to protected route
2. Frontend redirects to `/api/auth/login`
3. Backend redirects to Entra ID OAuth endpoint
4. User authenticates with Microsoft
5. Entra ID redirects to `/api/auth/callback?code=...`
6. Backend exchanges code for tokens via MSAL
7. Backend queries Microsoft Graph for user info
8. Backend checks admin group membership
9. Backend creates JWT session token
10. Frontend stores token in localStorage
11. Subsequent requests include `Authorization: Bearer <token>`

## Performance Optimizations

### 1. Single Config Load
The `_build_command()` method loads global config once and reuses it:
```python
global_cfg = get_global_config()
# Use global_cfg throughout instead of calling get_global_config() multiple times
```

### 2. Parallel WebSocket Broadcast
Uses `asyncio.gather()` to send to all clients simultaneously:
```python
results = await asyncio.gather(
    *[send_to_client(conn) for conn in self.active_connections],
    return_exceptions=True
)
```

### 3. File Locking with fcntl
Prevents configuration corruption from concurrent access:
- Shared locks (`LOCK_SH`) for reads
- Exclusive locks (`LOCK_EX`) for writes
- Atomic rename for crash safety

### 4. Async Subprocess Management
All subprocess operations use async/await:
- `asyncio.create_subprocess_exec()` for non-blocking spawn
- `asyncio.wait_for()` with timeout for graceful shutdown
- Separate tasks for stdout and stderr reading

## Configuration

### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `AUDYN_BIN` | Path to audyn binary | `/usr/bin/audyn` |
| `AUDYN_ARCHIVE_ROOT` | Base archive directory | `/var/lib/audyn/archive` |
| `AUDYN_CONFIG_DIR` | Config file directory | `~/.config/audyn` |
| `ENTRA_TENANT_ID` | Azure AD tenant ID | Required |
| `ENTRA_CLIENT_ID` | Azure AD app client ID | Required |
| `ENTRA_CLIENT_SECRET` | Azure AD app secret | Required |
| `ENTRA_REDIRECT_URI` | OAuth callback URL | `http://localhost:8000/api/auth/callback` |

### Running the Server

**Development:**
```bash
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000
```

**Production:**
```bash
uvicorn app.main:app --host 0.0.0.0 --port 8000 --workers 4
```

## Dependencies

### Production
- `fastapi` 0.115.x - Web framework
- `uvicorn` - ASGI server
- `pydantic` 2.x - Data validation
- `msal` - Microsoft authentication
- `python-multipart` - Form data parsing
- `python-jose[cryptography]` - JWT handling

### Development
- `pytest` - Testing
- `pytest-asyncio` - Async test support
- `httpx` - Async HTTP client for testing

## Limitations

1. **In-Memory State:** Recorder states stored in memory; lost on restart
2. **Single Process:** WebSocket connections bound to single worker
3. **Session Storage:** User studio selections in-memory dict
4. **No Database:** All persistence via JSON files
5. **Signal Handling:** Only SIGTERM for graceful shutdown
6. **Max 6 Recorders:** Hardcoded limit in default configuration
