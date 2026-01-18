"""
Real-time Audio Levels WebSocket

Provides real-time audio level meters for all recorders via WebSocket.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, WebSocket, WebSocketDisconnect
from typing import Optional
import asyncio
import json
import logging

from ..api.recorders import get_all_recorders, update_recorder_levels
from ..models import ChannelLevel, RecorderState
from ..services.recorder_manager import get_recorder_manager

logger = logging.getLogger(__name__)

router = APIRouter()


class ConnectionManager:
    """Manage WebSocket connections for audio level streaming."""

    def __init__(self):
        self.active_connections: list[WebSocket] = []
        self._level_task: Optional[asyncio.Task] = None
        self._running = False

    async def connect(self, websocket: WebSocket):
        """Accept and register a new WebSocket connection."""
        await websocket.accept()
        self.active_connections.append(websocket)
        logger.info(f"WebSocket connected. Total connections: {len(self.active_connections)}")

        # Start level broadcasting if not already running
        if not self._running:
            self._running = True
            self._level_task = asyncio.create_task(self._broadcast_levels())

    def disconnect(self, websocket: WebSocket):
        """Remove a WebSocket connection."""
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)
        logger.info(f"WebSocket disconnected. Total connections: {len(self.active_connections)}")

        # Stop broadcasting if no connections
        if not self.active_connections:
            self._running = False
            if self._level_task:
                self._level_task.cancel()

    async def _ensure_monitors_running(self):
        """Start monitors for all recorders that aren't currently recording."""
        manager = get_recorder_manager()
        recorders = get_all_recorders()

        for recorder in recorders:
            if recorder.state != RecorderState.RECORDING:
                if not manager.is_monitoring(recorder.id) and not manager.is_running(recorder.id):
                    logger.info(f"Starting level monitor for recorder {recorder.id}")
                    await manager.start_monitor(recorder.id, recorder.config)

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

    async def _broadcast_levels(self):
        """Continuously broadcast audio levels for all recorders."""
        manager = get_recorder_manager()

        # Start monitors for all recorders that aren't recording
        await self._ensure_monitors_running()

        while self._running:
            try:
                recorders = get_all_recorders()
                recorder_levels = []

                for recorder in recorders:
                    if recorder.state == RecorderState.RECORDING:
                        # Get real levels from recorder manager
                        real_levels = manager.get_levels(recorder.id)

                        if real_levels:
                            # Use real levels from audyn process
                            levels = real_levels
                        else:
                            # No real levels available - show silence (no fake data)
                            levels = [
                                ChannelLevel(name="L", level_db=-60, level_linear=0, peak_db=-60, clipping=False),
                                ChannelLevel(name="R", level_db=-60, level_linear=0, peak_db=-60, clipping=False)
                            ]
                    else:
                        # Silence when not recording
                        levels = [
                            ChannelLevel(name="L", level_db=-60, level_linear=0, peak_db=-60, clipping=False),
                            ChannelLevel(name="R", level_db=-60, level_linear=0, peak_db=-60, clipping=False)
                        ]

                    # Update recorder's levels
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

            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"Level broadcast error: {e}")
                await asyncio.sleep(1)


manager = ConnectionManager()


@router.websocket("/levels")
async def websocket_all_levels(websocket: WebSocket):
    """
    WebSocket endpoint for real-time audio levels of all recorders.

    Sends JSON messages with audio level data for all recorders:
    {
        "type": "all_levels",
        "timestamp": 1234567890.123,
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
            },
            ...
        ]
    }
    """
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
    except Exception as e:
        logger.error(f"WebSocket error: {e}")
        manager.disconnect(websocket)


@router.websocket("/levels/{recorder_id}")
async def websocket_recorder_levels(websocket: WebSocket, recorder_id: int):
    """
    WebSocket endpoint for a single recorder's audio levels.
    """
    await websocket.accept()

    try:
        while True:
            recorders = get_all_recorders()
            recorder = next((r for r in recorders if r.id == recorder_id), None)

            if recorder:
                await websocket.send_text(json.dumps({
                    "type": "levels",
                    "recorder_id": recorder_id,
                    "state": recorder.state.value,
                    "channels": [l.model_dump() for l in recorder.levels]
                }))

            await asyncio.sleep(1 / 30)

    except WebSocketDisconnect:
        pass
    except Exception as e:
        logger.error(f"WebSocket error: {e}")
