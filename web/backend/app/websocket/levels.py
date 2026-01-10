"""
Real-time Audio Levels WebSocket

Provides real-time audio level meters via WebSocket.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Depends
from typing import Optional
import asyncio
import json
import logging
import random
import math

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
        """Continuously broadcast audio levels."""
        # Simulated levels for demo (replace with actual Audyn integration)
        phase_l = 0
        phase_r = 0.3

        while self._running:
            try:
                # TODO: Get actual levels from Audyn process
                # For now, simulate realistic audio levels

                # Simulate varying audio levels (in dB)
                base_level = -18  # Average level around -18 dB
                variation = 12  # ±12 dB variation

                # Create smooth, natural-looking level changes
                phase_l += 0.15
                phase_r += 0.12

                level_l_db = base_level + (math.sin(phase_l) * variation * 0.5) + \
                             (random.random() - 0.5) * 6
                level_r_db = base_level + (math.sin(phase_r) * variation * 0.5) + \
                             (random.random() - 0.5) * 6

                # Clamp to realistic range
                level_l_db = max(-60, min(0, level_l_db))
                level_r_db = max(-60, min(0, level_r_db))

                # Peak detection (occasional peaks)
                if random.random() > 0.95:
                    level_l_db = min(0, level_l_db + 10)
                if random.random() > 0.95:
                    level_r_db = min(0, level_r_db + 10)

                # Convert to linear (0-1) for meter display
                level_l_linear = 10 ** (level_l_db / 20)
                level_r_linear = 10 ** (level_r_db / 20)

                await self.broadcast({
                    "type": "levels",
                    "timestamp": asyncio.get_event_loop().time(),
                    "channels": [
                        {
                            "name": "L",
                            "level_db": round(level_l_db, 1),
                            "level_linear": round(level_l_linear, 3),
                            "peak_db": round(level_l_db + 3, 1),
                            "clipping": level_l_db > -1
                        },
                        {
                            "name": "R",
                            "level_db": round(level_r_db, 1),
                            "level_linear": round(level_r_linear, 3),
                            "peak_db": round(level_r_db + 3, 1),
                            "clipping": level_r_db > -1
                        }
                    ]
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
async def websocket_levels(websocket: WebSocket):
    """
    WebSocket endpoint for real-time audio levels.

    Sends JSON messages with audio level data:
    {
        "type": "levels",
        "timestamp": 1234567890.123,
        "channels": [
            {"name": "L", "level_db": -18.5, "level_linear": 0.119, "peak_db": -15.0, "clipping": false},
            {"name": "R", "level_db": -17.2, "level_linear": 0.138, "peak_db": -14.0, "clipping": false}
        ]
    }
    """
    await manager.connect(websocket)

    try:
        while True:
            # Keep connection alive, handle any client messages
            try:
                data = await asyncio.wait_for(websocket.receive_text(), timeout=30)
                # Handle client commands if needed
                message = json.loads(data)
                if message.get("type") == "ping":
                    await websocket.send_text(json.dumps({"type": "pong"}))
            except asyncio.TimeoutError:
                # Send keepalive
                await websocket.send_text(json.dumps({"type": "keepalive"}))

    except WebSocketDisconnect:
        manager.disconnect(websocket)
    except Exception as e:
        logger.error(f"WebSocket error: {e}")
        manager.disconnect(websocket)
