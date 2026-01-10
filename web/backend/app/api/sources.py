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

from ..auth.entra import get_current_user, User

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


# In-memory source storage (would be replaced by database)
_sources: dict[str, AES67Source] = {
    "default": AES67Source(
        id="default",
        name="Default AES67",
        multicast_addr="239.69.1.1",
        port=5004,
        sample_rate=48000,
        channels=2,
        description="Default AES67 multicast source"
    ),
    "studio-a": AES67Source(
        id="studio-a",
        name="Studio A",
        multicast_addr="239.69.1.10",
        port=5004,
        sample_rate=48000,
        channels=2,
        description="Studio A main output"
    ),
    "studio-b": AES67Source(
        id="studio-b",
        name="Studio B",
        multicast_addr="239.69.1.20",
        port=5004,
        sample_rate=48000,
        channels=2,
        description="Studio B main output"
    )
}

_active_source_id: Optional[str] = "default"


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

    logger.info(f"Source switched from {old_source_id} to {source_id} by {user.email}")

    # TODO: Signal Audyn to switch sources
    # This would involve updating the multicast subscription

    return {
        "message": f"Switched to source: {source.name}",
        "previous_source_id": old_source_id,
        "active_source_id": _active_source_id,
        "source": source
    }


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
    return {"message": "Source disabled", "source": _sources[source_id]}
