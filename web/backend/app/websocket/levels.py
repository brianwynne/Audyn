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
import random
import math

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

    async def broadcast(self, message: dict):
        """Broadcast message to all connected clients."""
        if not self.active_connections:
            return

        data = json.dumps(message)
        disconnected = []

        for connection in self.active_connections:
            try:
                await connection.send_text(data)
            except Exception:
                disconnected.append(connection)

        # Clean up disconnected clients
        for conn in disconnected:
            self.disconnect(conn)

    async def _broadcast_levels(self):
        """Continuously broadcast audio levels for all recorders."""
        # Phase offsets for each recorder to simulate different audio sources (fallback)
        phases = {i: random.random() * 6.28 for i in range(1, 7)}
        manager = get_recorder_manager()

        while self._running:
            try:
                recorders = get_all_recorders()
                recorder_levels = []

                for recorder in recorders:
                    if recorder.state == RecorderState.RECORDING:
                        # Try to get real levels from recorder manager
                        real_levels = manager.get_levels(recorder.id)

                        if real_levels:
                            # Use real levels from audyn process
                            levels = real_levels
                        else:
                            # Fallback to simulated levels (for mock or when real not yet available)
                            phase_l = phases.get(recorder.id, 0)
                            phase_r = phase_l + 0.3

                            phases[recorder.id] = phase_l + 0.12

                            base_level = -18
                            variation = 12

                            level_l_db = base_level + (math.sin(phase_l) * variation * 0.5) + \
                                         (random.random() - 0.5) * 6
                            level_r_db = base_level + (math.sin(phase_r) * variation * 0.5) + \
                                         (random.random() - 0.5) * 6

                            # Clamp to realistic range
                            level_l_db = max(-60, min(0, level_l_db))
                            level_r_db = max(-60, min(0, level_r_db))

                            # Occasional peaks
                            if random.random() > 0.95:
                                level_l_db = min(0, level_l_db + 10)
                            if random.random() > 0.95:
                                level_r_db = min(0, level_r_db + 10)

                            levels = [
                                ChannelLevel(
                                    name="L",
                                    level_db=round(level_l_db, 1),
                                    level_linear=round(10 ** (level_l_db / 20), 3),
                                    peak_db=round(level_l_db + 3, 1),
                                    clipping=level_l_db > -1
                                ),
                                ChannelLevel(
                                    name="R",
                                    level_db=round(level_r_db, 1),
                                    level_linear=round(10 ** (level_r_db / 20), 3),
                                    peak_db=round(level_r_db + 3, 1),
                                    clipping=level_r_db > -1
                                )
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
