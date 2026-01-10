"""
Studio Management API

Manage studios and their recorder assignments.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from typing import Optional
import logging
import uuid

from ..auth.entra import get_current_user
from ..models import Studio, User
from .recorders import _recorders, get_recorder

logger = logging.getLogger(__name__)

router = APIRouter()


class StudioCreate(BaseModel):
    """Create studio request."""
    name: str
    description: Optional[str] = None
    color: str = "#2196F3"


class StudioUpdate(BaseModel):
    """Update studio request."""
    name: Optional[str] = None
    description: Optional[str] = None
    color: Optional[str] = None
    enabled: Optional[bool] = None


class AssignRecorder(BaseModel):
    """Assign recorder to studio."""
    recorder_id: Optional[int] = None  # None to unassign


# In-memory studio storage
_studios: dict[str, Studio] = {
    "studio-a": Studio(
        id="studio-a",
        name="Studio A",
        description="Main broadcast studio",
        color="#F44336",
        recorder_id=1
    ),
    "studio-b": Studio(
        id="studio-b",
        name="Studio B",
        description="Secondary studio",
        color="#4CAF50",
        recorder_id=2
    ),
    "studio-c": Studio(
        id="studio-c",
        name="Studio C",
        description="Production studio",
        color="#2196F3",
        recorder_id=3
    ),
    "studio-d": Studio(
        id="studio-d",
        name="Studio D",
        description="Edit suite",
        color="#FF9800"
    ),
    "studio-e": Studio(
        id="studio-e",
        name="Studio E",
        description="Voice booth",
        color="#9C27B0"
    )
}

# Update recorders with studio assignments
for studio in _studios.values():
    if studio.recorder_id and studio.recorder_id in _recorders:
        _recorders[studio.recorder_id].studio_id = studio.id


def require_admin(user: User):
    """Check if user is admin."""
    if not user.is_admin:
        raise HTTPException(status_code=403, detail="Admin access required")


def get_studio(studio_id: str) -> Studio:
    """Get a studio by ID."""
    if studio_id not in _studios:
        raise HTTPException(status_code=404, detail="Studio not found")
    return _studios[studio_id]


@router.get("/", response_model=list[Studio])
async def list_studios(user: User = Depends(get_current_user)):
    """List all studios."""
    return list(_studios.values())


@router.get("/{studio_id}", response_model=Studio)
async def get_studio_by_id(studio_id: str, user: User = Depends(get_current_user)):
    """Get a specific studio."""
    return get_studio(studio_id)


@router.post("/", response_model=Studio)
async def create_studio(
    studio: StudioCreate,
    user: User = Depends(get_current_user)
):
    """Create a new studio."""
    require_admin(user)

    studio_id = f"studio-{uuid.uuid4().hex[:8]}"
    new_studio = Studio(
        id=studio_id,
        name=studio.name,
        description=studio.description,
        color=studio.color
    )

    _studios[studio_id] = new_studio
    logger.info(f"Studio created: {studio_id} by {user.email}")

    return new_studio


@router.put("/{studio_id}", response_model=Studio)
async def update_studio(
    studio_id: str,
    update: StudioUpdate,
    user: User = Depends(get_current_user)
):
    """Update a studio."""
    require_admin(user)
    studio = get_studio(studio_id)

    if update.name is not None:
        studio.name = update.name
    if update.description is not None:
        studio.description = update.description
    if update.color is not None:
        studio.color = update.color
    if update.enabled is not None:
        studio.enabled = update.enabled

    logger.info(f"Studio updated: {studio_id} by {user.email}")
    return studio


@router.delete("/{studio_id}")
async def delete_studio(studio_id: str, user: User = Depends(get_current_user)):
    """Delete a studio."""
    require_admin(user)
    studio = get_studio(studio_id)

    # Unassign recorder if assigned
    if studio.recorder_id and studio.recorder_id in _recorders:
        _recorders[studio.recorder_id].studio_id = None

    del _studios[studio_id]
    logger.info(f"Studio deleted: {studio_id} by {user.email}")

    return {"message": "Studio deleted"}


@router.post("/{studio_id}/assign", response_model=Studio)
async def assign_recorder(
    studio_id: str,
    assignment: AssignRecorder,
    user: User = Depends(get_current_user)
):
    """Assign a recorder to a studio."""
    require_admin(user)
    studio = get_studio(studio_id)

    # Unassign current recorder if any
    if studio.recorder_id and studio.recorder_id in _recorders:
        _recorders[studio.recorder_id].studio_id = None

    # Check if new recorder is already assigned
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
        logger.info(f"Recorder {assignment.recorder_id} assigned to {studio_id} by {user.email}")
    else:
        studio.recorder_id = None
        logger.info(f"Recorder unassigned from {studio_id} by {user.email}")

    return studio


@router.get("/{studio_id}/recorder")
async def get_studio_recorder(studio_id: str, user: User = Depends(get_current_user)):
    """Get the recorder assigned to a studio."""
    studio = get_studio(studio_id)

    if not studio.recorder_id:
        return {"studio_id": studio_id, "recorder": None}

    recorder = _recorders.get(studio.recorder_id)
    return {"studio_id": studio_id, "recorder": recorder}


@router.get("/{studio_id}/recordings")
async def get_studio_recordings(
    studio_id: str,
    user: User = Depends(get_current_user)
):
    """Get recordings for a studio."""
    studio = get_studio(studio_id)

    # Check access for studio users
    if not user.is_admin and user.studio_id != studio_id:
        raise HTTPException(status_code=403, detail="Access denied to this studio's recordings")

    # TODO: Query actual recordings from archive
    # For now return empty list
    return {
        "studio_id": studio_id,
        "studio_name": studio.name,
        "recordings": []
    }


def get_user_studio(user: User) -> Optional[Studio]:
    """Get the studio assigned to a user."""
    if user.studio_id and user.studio_id in _studios:
        return _studios[user.studio_id]
    return None


def get_all_studios() -> list[Studio]:
    """Get all studios."""
    return list(_studios.values())
