"""
AES67 Source Management API

Manage and switch between AES67 audio sources.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from typing import Optional
import logging
import uuid
import asyncio
import json
import re

from ..auth.entra import get_current_user, User
from ..services.config_store import load_sources_config, save_sources_config

logger = logging.getLogger(__name__)

router = APIRouter()


class AES67Source(BaseModel):
    """AES67 source definition."""
    id: str
    name: str
    multicast_addr: str
    port: int = 5004
    sample_rate: int = 48000
    channels: int = 2
    payload_type: int = 96
    samples_per_packet: int = 48
    description: Optional[str] = None
    enabled: bool = True


class SourceCreate(BaseModel):
    """Create source request."""
    name: str
    multicast_addr: str
    port: int = 5004
    sample_rate: int = 48000
    channels: int = 2
    payload_type: int = 96
    samples_per_packet: int = 48
    description: Optional[str] = None


class PipeWireSource(BaseModel):
    """PipeWire audio source."""
    id: str
    name: str
    description: Optional[str] = None
    node_name: str  # The actual PipeWire node name to use
    media_class: str  # e.g., "Audio/Source", "Audio/Sink"
    channels: Optional[int] = None
    sample_rate: Optional[int] = None


async def detect_pipewire_sources() -> list[PipeWireSource]:
    """Detect available PipeWire audio sources using pw-cli."""
    sources = []

    try:
        # Try pw-cli first (native PipeWire)
        proc = await asyncio.create_subprocess_exec(
            "pw-cli", "list-objects",
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE
        )
        stdout, stderr = await proc.communicate()

        if proc.returncode == 0:
            # Parse pw-cli output
            output = stdout.decode()
            current_obj = {}
            current_id = None

            for line in output.split('\n'):
                # New object starts with "id X, type ..."
                id_match = re.match(r'\s*id\s+(\d+),\s+type\s+(\S+)', line)
                if id_match:
                    # Save previous object if it's an audio node
                    if current_obj and current_obj.get('media_class', '').startswith('Audio/'):
                        sources.append(PipeWireSource(
                            id=str(current_obj.get('object_id', current_id)),
                            name=current_obj.get('node_description', current_obj.get('node_name', f'Node {current_id}')),
                            description=current_obj.get('node_description'),
                            node_name=current_obj.get('node_name', ''),
                            media_class=current_obj.get('media_class', ''),
                            channels=current_obj.get('channels'),
                            sample_rate=current_obj.get('sample_rate')
                        ))
                    current_id = id_match.group(1)
                    current_obj = {'object_id': current_id}
                    continue

                # Property lines
                prop_match = re.match(r'\s*(\S+)\s*=\s*"?([^"]*)"?', line)
                if prop_match:
                    key = prop_match.group(1).replace('.', '_').replace('-', '_')
                    value = prop_match.group(2).strip('"')
                    current_obj[key] = value

            # Don't forget the last object
            if current_obj and current_obj.get('media_class', '').startswith('Audio/'):
                sources.append(PipeWireSource(
                    id=str(current_obj.get('object_id', current_id)),
                    name=current_obj.get('node_description', current_obj.get('node_name', f'Node {current_id}')),
                    description=current_obj.get('node_description'),
                    node_name=current_obj.get('node_name', ''),
                    media_class=current_obj.get('media_class', ''),
                    channels=current_obj.get('channels'),
                    sample_rate=current_obj.get('sample_rate')
                ))

    except FileNotFoundError:
        logger.warning("pw-cli not found, trying pactl")

    # If pw-cli didn't work, try pactl (PulseAudio compatibility)
    if not sources:
        try:
            proc = await asyncio.create_subprocess_exec(
                "pactl", "-f", "json", "list", "sources",
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )
            stdout, stderr = await proc.communicate()

            if proc.returncode == 0:
                data = json.loads(stdout.decode())
                for src in data:
                    sources.append(PipeWireSource(
                        id=str(src.get('index', '')),
                        name=src.get('description', src.get('name', 'Unknown')),
                        description=src.get('description'),
                        node_name=src.get('name', ''),
                        media_class='Audio/Source',
                        channels=src.get('channel_map', {}).get('channels'),
                        sample_rate=src.get('sample_spec', {}).get('rate')
                    ))
        except FileNotFoundError:
            logger.warning("pactl not found")
        except json.JSONDecodeError:
            logger.warning("Failed to parse pactl JSON output")

    return sources


# Default sources for initial setup
DEFAULT_SOURCES = {
    "default": {
        "name": "Default AES67",
        "multicast_addr": "239.69.1.1",
        "port": 5004,
        "sample_rate": 48000,
        "channels": 2,
        "description": "Default AES67 multicast source"
    },
    "studio-a": {
        "name": "Studio A",
        "multicast_addr": "239.69.1.10",
        "port": 5004,
        "sample_rate": 48000,
        "channels": 2,
        "description": "Studio A main output"
    },
    "studio-b": {
        "name": "Studio B",
        "multicast_addr": "239.69.1.20",
        "port": 5004,
        "sample_rate": 48000,
        "channels": 2,
        "description": "Studio B main output"
    }
}


def _serialize_sources() -> dict:
    """Serialize sources for persistence."""
    return {
        "active_source_id": _active_source_id,
        "sources": {
            source_id: s.model_dump()
            for source_id, s in _sources.items()
        }
    }


def _load_sources_from_store():
    """Load sources from persistent storage."""
    global _sources, _active_source_id

    saved = load_sources_config()
    if saved:
        try:
            _active_source_id = saved.get("active_source_id", "default")
            saved_sources = saved.get("sources", {})

            _sources = {}
            for source_id, data in saved_sources.items():
                _sources[source_id] = AES67Source(id=source_id, **{
                    k: v for k, v in data.items() if k != "id"
                })

            logger.info(f"Loaded {len(_sources)} sources from storage")
        except Exception as e:
            logger.error(f"Failed to parse saved sources: {e}")
            _sources = {}
            _active_source_id = "default"
    else:
        # Initialize with defaults
        _sources = {}
        for source_id, data in DEFAULT_SOURCES.items():
            _sources[source_id] = AES67Source(id=source_id, **data)
        _active_source_id = "default"
        # Save defaults
        save_sources_config(_serialize_sources())
        logger.info("Initialized default sources")


def _save_sources():
    """Save current sources to persistent storage."""
    if not save_sources_config(_serialize_sources()):
        logger.warning("Failed to persist sources config")


# Initialize sources from storage
_sources: dict[str, AES67Source] = {}
_active_source_id: Optional[str] = "default"
_load_sources_from_store()


@router.get("/", response_model=list[AES67Source])
async def list_sources(user: User = Depends(get_current_user)):
    """List all configured AES67 sources."""
    return list(_sources.values())


@router.get("/active")
async def get_active_source(user: User = Depends(get_current_user)):
    """Get currently active source."""
    if _active_source_id and _active_source_id in _sources:
        return {
            "active_source_id": _active_source_id,
            "source": _sources[_active_source_id]
        }
    return {"active_source_id": None, "source": None}


@router.post("/active/{source_id}")
async def set_active_source(
    source_id: str,
    user: User = Depends(get_current_user)
):
    """Switch to a different AES67 source."""
    global _active_source_id

    if source_id not in _sources:
        raise HTTPException(status_code=404, detail="Source not found")

    source = _sources[source_id]
    if not source.enabled:
        raise HTTPException(status_code=400, detail="Source is disabled")

    old_source_id = _active_source_id
    _active_source_id = source_id
    _save_sources()  # Persist change

    logger.info(f"Source switched from {old_source_id} to {source_id} by {user.email}")

    # TODO: Signal Audyn to switch sources
    # This would involve updating the multicast subscription

    return {
        "message": f"Switched to source: {source.name}",
        "previous_source_id": old_source_id,
        "active_source_id": _active_source_id,
        "source": source
    }


@router.get("/pipewire", response_model=list[PipeWireSource])
async def list_pipewire_sources(user: User = Depends(get_current_user)):
    """List available PipeWire audio sources."""
    sources = await detect_pipewire_sources()

    # Filter to only show audio sources (inputs), not sinks (outputs)
    audio_sources = [s for s in sources if 'Source' in s.media_class or 'Input' in s.media_class]

    # If no sources found, return a default option
    if not audio_sources:
        return [PipeWireSource(
            id="default",
            name="Default Audio Input",
            description="System default audio input",
            node_name="",
            media_class="Audio/Source"
        )]

    return audio_sources


@router.get("/{source_id}", response_model=AES67Source)
async def get_source(
    source_id: str,
    user: User = Depends(get_current_user)
):
    """Get a specific source by ID."""
    if source_id not in _sources:
        raise HTTPException(status_code=404, detail="Source not found")
    return _sources[source_id]


@router.post("/", response_model=AES67Source)
async def create_source(
    source: SourceCreate,
    user: User = Depends(get_current_user)
):
    """Create a new AES67 source."""
    source_id = str(uuid.uuid4())[:8]

    new_source = AES67Source(
        id=source_id,
        **source.model_dump()
    )

    _sources[source_id] = new_source
    _save_sources()  # Persist change
    logger.info(f"Source created: {source_id} by {user.email}")

    return new_source


@router.put("/{source_id}", response_model=AES67Source)
async def update_source(
    source_id: str,
    source: SourceCreate,
    user: User = Depends(get_current_user)
):
    """Update an existing source."""
    if source_id not in _sources:
        raise HTTPException(status_code=404, detail="Source not found")

    updated_source = AES67Source(
        id=source_id,
        **source.model_dump()
    )

    _sources[source_id] = updated_source
    _save_sources()  # Persist change
    logger.info(f"Source updated: {source_id} by {user.email}")

    return updated_source


@router.delete("/{source_id}")
async def delete_source(
    source_id: str,
    user: User = Depends(get_current_user)
):
    """Delete a source."""
    if source_id not in _sources:
        raise HTTPException(status_code=404, detail="Source not found")

    if source_id == _active_source_id:
        raise HTTPException(
            status_code=400,
            detail="Cannot delete active source. Switch to another source first."
        )

    del _sources[source_id]
    _save_sources()  # Persist change
    logger.info(f"Source deleted: {source_id} by {user.email}")

    return {"message": "Source deleted"}


@router.post("/{source_id}/enable")
async def enable_source(
    source_id: str,
    user: User = Depends(get_current_user)
):
    """Enable a source."""
    if source_id not in _sources:
        raise HTTPException(status_code=404, detail="Source not found")

    _sources[source_id].enabled = True
    _save_sources()  # Persist change
    return {"message": "Source enabled", "source": _sources[source_id]}


@router.post("/{source_id}/disable")
async def disable_source(
    source_id: str,
    user: User = Depends(get_current_user)
):
    """Disable a source."""
    if source_id not in _sources:
        raise HTTPException(status_code=404, detail="Source not found")

    if source_id == _active_source_id:
        raise HTTPException(
            status_code=400,
            detail="Cannot disable active source. Switch to another source first."
        )

    _sources[source_id].enabled = False
    _save_sources()  # Persist change
    return {"message": "Source disabled", "source": _sources[source_id]}
