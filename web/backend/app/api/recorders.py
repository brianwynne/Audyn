"""
Recorder Management API

Manage multiple recorder instances (1-6).

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, Depends, HTTPException
from typing import Optional
import logging
import os
from datetime import datetime

from ..auth.entra import get_current_user
from ..models import (
    Recorder, RecorderConfig, RecorderState,
    ChannelLevel, User, OutputFormat, SourceType
)
from .sources import _sources as available_sources
from ..services.recorder_manager import get_recorder_manager
from ..services.config_store import load_recorders_config, save_recorders_config

logger = logging.getLogger(__name__)

router = APIRouter()

# In-memory recorder storage (would be replaced by actual process management)
MAX_RECORDERS = 6

# Default archive root - use system path in production
DEFAULT_ARCHIVE_BASE = os.getenv("AUDYN_ARCHIVE_ROOT", "/var/lib/audyn/archive")


def create_default_recorder(id: int) -> Recorder:
    """Create a default recorder configuration."""
    return Recorder(
        id=id,
        name=f"Recorder {id}",
        enabled=True,
        state=RecorderState.STOPPED,
        config=RecorderConfig(
            multicast_addr=f"239.69.1.{id}",
            port=5004,
            archive_root=f"{DEFAULT_ARCHIVE_BASE}/recorder{id}"
        ),
        levels=[
            ChannelLevel(name="L"),
            ChannelLevel(name="R")
        ]
    )


def _serialize_recorders() -> dict:
    """Serialize recorder configs for persistence (excludes transient state)."""
    return {
        "active_count": _active_recorder_count,
        "recorders": {
            str(r.id): {
                "name": r.name,
                "enabled": r.enabled,
                "studio_id": r.studio_id,
                "config": r.config.model_dump() if r.config else None
            }
            for r in _recorders.values()
        }
    }


def _load_recorders_from_store():
    """Load recorder configs from persistent storage."""
    global _recorders, _active_recorder_count

    # Start with defaults
    _recorders = {i: create_default_recorder(i) for i in range(1, MAX_RECORDERS + 1)}
    _active_recorder_count = 6

    saved = load_recorders_config()
    if saved:
        try:
            _active_recorder_count = saved.get("active_count", 6)
            saved_recorders = saved.get("recorders", {})

            for id_str, data in saved_recorders.items():
                rec_id = int(id_str)
                if rec_id in _recorders:
                    recorder = _recorders[rec_id]
                    recorder.name = data.get("name", recorder.name)
                    recorder.enabled = data.get("enabled", True)
                    recorder.studio_id = data.get("studio_id")
                    if data.get("config"):
                        recorder.config = RecorderConfig(**data["config"])

            logger.info(f"Loaded {len(saved_recorders)} recorder configs from storage")
        except Exception as e:
            logger.error(f"Failed to parse saved recorder config: {e}")
    else:
        # Save defaults
        save_recorders_config(_serialize_recorders())
        logger.info("Initialized default recorder configs")


def _save_recorders():
    """Save current recorder state to persistent storage."""
    if not save_recorders_config(_serialize_recorders()):
        logger.warning("Failed to persist recorder configs")


# Initialize recorders from storage
_recorders: dict[int, Recorder] = {}
_active_recorder_count: int = 6
_load_recorders_from_store()


def get_recorder(recorder_id: int) -> Recorder:
    """Get a recorder by ID."""
    if recorder_id not in _recorders:
        raise HTTPException(status_code=404, detail=f"Recorder {recorder_id} not found")
    return _recorders[recorder_id]


def require_admin(user: User):
    """Check if user is admin."""
    if not user.is_admin:
        raise HTTPException(status_code=403, detail="Admin access required")


@router.get("/", response_model=list[Recorder])
async def list_recorders(user: User = Depends(get_current_user)):
    """List all recorders."""
    return [r for r in _recorders.values() if r.id <= _active_recorder_count]


@router.get("/active-count")
async def get_active_count(user: User = Depends(get_current_user)):
    """Get number of active recorders."""
    return {"active_recorders": _active_recorder_count, "max_recorders": MAX_RECORDERS}


@router.put("/active-count/{count}")
async def set_active_count(count: int, user: User = Depends(get_current_user)):
    """Set number of active recorders (1-6)."""
    require_admin(user)
    global _active_recorder_count

    if count < 1 or count > MAX_RECORDERS:
        raise HTTPException(status_code=400, detail=f"Count must be between 1 and {MAX_RECORDERS}")

    # Stop any recorders that will be deactivated
    for i in range(count + 1, _active_recorder_count + 1):
        if _recorders[i].state == RecorderState.RECORDING:
            _recorders[i].state = RecorderState.STOPPED

    _active_recorder_count = count
    _save_recorders()  # Persist change
    logger.info(f"Active recorder count set to {count} by {user.email}")

    return {"active_recorders": _active_recorder_count}


@router.get("/{recorder_id}", response_model=Recorder)
async def get_recorder_by_id(recorder_id: int, user: User = Depends(get_current_user)):
    """Get a specific recorder."""
    return get_recorder(recorder_id)


@router.put("/{recorder_id}/config")
async def update_recorder_config(
    recorder_id: int,
    config: RecorderConfig,
    user: User = Depends(get_current_user)
):
    """Update recorder configuration."""
    require_admin(user)
    recorder = get_recorder(recorder_id)

    if recorder.state == RecorderState.RECORDING:
        raise HTTPException(status_code=400, detail="Cannot update config while recording")

    # Validate and populate source configuration
    if config.source_type == SourceType.AES67 and config.source_id:
        if config.source_id not in available_sources:
            raise HTTPException(status_code=400, detail=f"Source '{config.source_id}' not found")
        # Auto-populate from source if source_id is provided
        source = available_sources[config.source_id]
        config.multicast_addr = source.multicast_addr
        config.port = source.port
        config.sample_rate = source.sample_rate
        config.channels = source.channels
        config.payload_type = source.payload_type
        config.samples_per_packet = source.samples_per_packet

    recorder.config = config
    _save_recorders()  # Persist change
    logger.info(f"Recorder {recorder_id} config updated by {user.email}: source_type={config.source_type}")

    return {"message": "Configuration updated", "recorder": recorder}


@router.post("/{recorder_id}/start")
async def start_recorder(recorder_id: int, user: User = Depends(get_current_user)):
    """Start a recorder."""
    require_admin(user)
    recorder = get_recorder(recorder_id)

    if recorder_id > _active_recorder_count:
        raise HTTPException(status_code=400, detail="Recorder not active")

    if recorder.state == RecorderState.RECORDING:
        raise HTTPException(status_code=400, detail="Already recording")

    # Actually start the recorder process
    manager = get_recorder_manager()
    success = await manager.start_recorder(recorder_id, recorder.config, recorder.studio_id)

    if not success:
        raise HTTPException(status_code=500, detail="Failed to start recorder process")

    recorder.state = RecorderState.RECORDING
    recorder.start_time = datetime.now()
    recorder.bytes_written = 0
    recorder.errors = []

    logger.info(f"Recorder {recorder_id} started by {user.email}")

    return {"message": "Recording started", "recorder": recorder}


@router.post("/{recorder_id}/stop")
async def stop_recorder(recorder_id: int, user: User = Depends(get_current_user)):
    """Stop a recorder."""
    require_admin(user)
    recorder = get_recorder(recorder_id)

    if recorder.state == RecorderState.STOPPED:
        raise HTTPException(status_code=400, detail="Not recording")

    # Actually stop the recorder process
    manager = get_recorder_manager()
    await manager.stop_recorder(recorder_id)

    recorder.state = RecorderState.STOPPED
    recorder.current_file = None

    logger.info(f"Recorder {recorder_id} stopped by {user.email}")

    return {"message": "Recording stopped", "recorder": recorder}


@router.post("/start-all")
async def start_all_recorders(user: User = Depends(get_current_user)):
    """Start all active recorders."""
    require_admin(user)

    manager = get_recorder_manager()
    started = []
    for i in range(1, _active_recorder_count + 1):
        recorder = _recorders[i]
        if recorder.enabled and recorder.state == RecorderState.STOPPED:
            success = await manager.start_recorder(i, recorder.config, recorder.studio_id)
            if success:
                recorder.state = RecorderState.RECORDING
                recorder.start_time = datetime.now()
                started.append(i)

    logger.info(f"Started recorders {started} by {user.email}")
    return {"message": f"Started {len(started)} recorders", "started": started}


@router.post("/stop-all")
async def stop_all_recorders(user: User = Depends(get_current_user)):
    """Stop all recorders."""
    require_admin(user)

    manager = get_recorder_manager()
    stopped = []
    for recorder in _recorders.values():
        if recorder.state == RecorderState.RECORDING:
            await manager.stop_recorder(recorder.id)
            recorder.state = RecorderState.STOPPED
            recorder.current_file = None
            stopped.append(recorder.id)

    logger.info(f"Stopped recorders {stopped} by {user.email}")
    return {"message": f"Stopped {len(stopped)} recorders", "stopped": stopped}


@router.get("/{recorder_id}/levels")
async def get_recorder_levels(recorder_id: int, user: User = Depends(get_current_user)):
    """Get current audio levels for a recorder."""
    recorder = get_recorder(recorder_id)
    return {"recorder_id": recorder_id, "levels": recorder.levels}


# Helper function for WebSocket to update levels
def update_recorder_levels(recorder_id: int, levels: list[ChannelLevel]):
    """Update levels for a recorder (called by level monitoring)."""
    if recorder_id in _recorders:
        _recorders[recorder_id].levels = levels


def get_all_recorders() -> list[Recorder]:
    """Get all active recorders (for WebSocket broadcasting)."""
    return [r for r in _recorders.values() if r.id <= _active_recorder_count]
