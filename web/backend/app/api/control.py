"""
Audyn Process Control API

Start, stop, and configure Audyn capture instances.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from typing import Optional
from enum import Enum
import logging
import os

from ..auth.entra import get_current_user, User
from ..services.config_store import load_global_config, save_global_config

logger = logging.getLogger(__name__)

router = APIRouter()


class CaptureState(str, Enum):
    STOPPED = "stopped"
    STARTING = "starting"
    RECORDING = "recording"
    STOPPING = "stopping"
    ERROR = "error"


class OutputFormat(str, Enum):
    WAV = "wav"
    OPUS = "opus"


class ArchiveLayout(str, Enum):
    FLAT = "flat"
    HIERARCHY = "hierarchy"
    COMBO = "combo"
    DAILYDIR = "dailydir"
    ACCURATE = "accurate"


class ArchiveClock(str, Enum):
    LOCALTIME = "localtime"
    UTC = "utc"
    PTP = "ptp"


class CaptureConfig(BaseModel):
    """Capture configuration."""
    # Source settings
    source_type: str = "aes67"  # aes67 or pipewire
    multicast_addr: Optional[str] = "239.69.1.1"
    port: int = 5004
    sample_rate: int = 48000
    channels: int = 2

    # Output settings
    format: OutputFormat = OutputFormat.WAV
    bitrate: Optional[int] = 128000  # For Opus

    # Archive settings
    archive_root: str = "/var/lib/audyn"
    archive_layout: ArchiveLayout = ArchiveLayout.DAILYDIR
    archive_period: int = 3600  # seconds
    archive_clock: ArchiveClock = ArchiveClock.LOCALTIME

    # PTP settings
    ptp_interface: Optional[str] = None

    # AES67 network settings
    aes67_interface: Optional[str] = None  # Network interface for multicast


class PartialCaptureConfig(BaseModel):
    """Partial capture configuration for updates (all fields optional)."""
    source_type: Optional[str] = None
    multicast_addr: Optional[str] = None
    port: Optional[int] = None
    sample_rate: Optional[int] = None
    channels: Optional[int] = None
    format: Optional[OutputFormat] = None
    bitrate: Optional[int] = None
    archive_root: Optional[str] = None
    archive_layout: Optional[ArchiveLayout] = None
    archive_period: Optional[int] = None
    archive_clock: Optional[ArchiveClock] = None
    ptp_interface: Optional[str] = None
    aes67_interface: Optional[str] = None


class CaptureStatus(BaseModel):
    """Current capture status."""
    state: CaptureState
    config: Optional[CaptureConfig] = None
    current_file: Optional[str] = None
    recording_duration: int = 0  # seconds
    bytes_written: int = 0
    errors: list[str] = []


# In-memory state (would be replaced by actual Audyn control)
_current_status = CaptureStatus(state=CaptureState.STOPPED)
_current_config: Optional[CaptureConfig] = None


def _load_config_from_store():
    """Load global config from persistent storage."""
    global _current_config
    saved = load_global_config()
    if saved:
        try:
            _current_config = CaptureConfig(**saved)
            logger.info("Loaded global config from persistent storage")
        except Exception as e:
            logger.error(f"Failed to parse saved config: {e}")
            _current_config = None
    else:
        # Initialize with sensible defaults
        _current_config = CaptureConfig(
            archive_root=os.getenv("AUDYN_ARCHIVE_ROOT", "/var/lib/audyn/archive")
        )
        # Save defaults to file
        save_global_config(_current_config.model_dump())
        logger.info("Initialized default global config")


# Load config on module import
_load_config_from_store()


@router.get("/status", response_model=CaptureStatus)
async def get_capture_status(user: User = Depends(get_current_user)):
    """Get current capture status."""
    return _current_status


@router.get("/config", response_model=Optional[CaptureConfig])
async def get_capture_config(user: User = Depends(get_current_user)):
    """Get current capture configuration."""
    return _current_config


@router.post("/config")
async def set_capture_config(
    config: PartialCaptureConfig,
    user: User = Depends(get_current_user)
):
    """Update capture configuration (partial updates supported)."""
    global _current_config

    if _current_status.state == CaptureState.RECORDING:
        raise HTTPException(
            status_code=400,
            detail="Cannot change config while recording. Stop capture first."
        )

    # Load existing config from disk to merge with
    existing = load_global_config() or {}

    # Merge incoming changes with existing config
    # Only update fields that were explicitly provided (not None)
    updates = config.model_dump(exclude_none=True)
    merged = {**existing, **updates}

    # Create full config from merged data
    _current_config = CaptureConfig(**merged)

    # Persist to file storage
    if not save_global_config(_current_config.model_dump()):
        logger.warning("Failed to persist config to file storage")

    logger.info(f"Config updated by {user.email}: {updates}")
    return {"message": "Configuration updated", "config": _current_config}


@router.post("/start")
async def start_capture(user: User = Depends(get_current_user)):
    """Start audio capture."""
    global _current_status, _current_config

    if _current_status.state == CaptureState.RECORDING:
        raise HTTPException(status_code=400, detail="Already recording")

    if _current_config is None:
        raise HTTPException(status_code=400, detail="No configuration set")

    _current_status.state = CaptureState.STARTING
    logger.info(f"Capture started by {user.email}")

    # TODO: Actually start Audyn process
    # For now, simulate starting
    _current_status.state = CaptureState.RECORDING
    _current_status.config = _current_config

    return {"message": "Capture started", "state": _current_status.state}


@router.post("/stop")
async def stop_capture(user: User = Depends(get_current_user)):
    """Stop audio capture."""
    global _current_status

    if _current_status.state == CaptureState.STOPPED:
        raise HTTPException(status_code=400, detail="Not recording")

    _current_status.state = CaptureState.STOPPING
    logger.info(f"Capture stopped by {user.email}")

    # TODO: Actually stop Audyn process
    _current_status.state = CaptureState.STOPPED
    _current_status.current_file = None
    _current_status.recording_duration = 0

    return {"message": "Capture stopped", "state": _current_status.state}


@router.post("/restart")
async def restart_capture(user: User = Depends(get_current_user)):
    """Restart audio capture (stop then start)."""
    await stop_capture(user)
    return await start_capture(user)
