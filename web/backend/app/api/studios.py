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
from .recorders import _recorders, get_recorder, _save_recorders
from ..services.config_store import load_studios_config, save_studios_config

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


# Default studios for initial setup
DEFAULT_STUDIOS = {
    "studio-a": {"name": "Studio A", "description": "Main broadcast studio", "color": "#F44336", "recorder_id": 1},
    "studio-b": {"name": "Studio B", "description": "Secondary studio", "color": "#4CAF50", "recorder_id": 2},
    "studio-c": {"name": "Studio C", "description": "Production studio", "color": "#2196F3", "recorder_id": 3},
    "studio-d": {"name": "Studio D", "description": "Edit suite", "color": "#FF9800"},
    "studio-e": {"name": "Studio E", "description": "Voice booth", "color": "#9C27B0"}
}


def _serialize_studios() -> dict:
    """Serialize studios for persistence."""
    return {
        studio_id: {
            "name": s.name,
            "description": s.description,
            "color": s.color,
            "recorder_id": s.recorder_id,
            "enabled": s.enabled
        }
        for studio_id, s in _studios.items()
    }


def _load_studios_from_store():
    """Load studios from persistent storage."""
    global _studios

    saved = load_studios_config()
    if saved:
        try:
            _studios = {}
            for studio_id, data in saved.items():
                _studios[studio_id] = Studio(
                    id=studio_id,
                    name=data.get("name", studio_id),
                    description=data.get("description"),
                    color=data.get("color", "#2196F3"),
                    recorder_id=data.get("recorder_id"),
                    enabled=data.get("enabled", True)
                )
            logger.info(f"Loaded {len(_studios)} studios from storage")
        except Exception as e:
            logger.error(f"Failed to parse saved studios: {e}")
            _studios = {}
    else:
        # Initialize with defaults
        _studios = {}
        for studio_id, data in DEFAULT_STUDIOS.items():
            _studios[studio_id] = Studio(
                id=studio_id,
                name=data["name"],
                description=data.get("description"),
                color=data.get("color", "#2196F3"),
                recorder_id=data.get("recorder_id"),
                enabled=True
            )
        # Save defaults
        save_studios_config(_serialize_studios())
        logger.info("Initialized default studios")


def _save_studios():
    """Save current studios to persistent storage."""
    if not save_studios_config(_serialize_studios()):
        logger.warning("Failed to persist studios config")


def _sync_recorder_assignments():
    """Sync recorder studio_id fields with studio assignments."""
    # Clear all recorder studio assignments
    for recorder in _recorders.values():
        recorder.studio_id = None

    # Set from studio assignments
    for studio in _studios.values():
        if studio.recorder_id and studio.recorder_id in _recorders:
            _recorders[studio.recorder_id].studio_id = studio.id


# Initialize studios from storage
_studios: dict[str, Studio] = {}
_load_studios_from_store()
_sync_recorder_assignments()

# User session storage for selected studios (in production, use a proper session store)
_user_selected_studios: dict[str, str] = {}


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


@router.get("/accessible", response_model=list[Studio])
async def get_accessible_studios(user: User = Depends(get_current_user)):
    """Get studios accessible to the current user."""
    # Admins can access all studios
    if user.is_admin:
        return list(_studios.values())

    # Studio users can access all studios (for viewing files)
    # But their primary studio is highlighted
    return list(_studios.values())


@router.get("/current-selection")
async def get_current_selection(user: User = Depends(get_current_user)):
    """Get the user's current studio selection."""
    studio_id = _user_selected_studios.get(user.id)
    if studio_id and studio_id in _studios:
        return {
            "studio_id": studio_id,
            "studio": _studios[studio_id]
        }
    return {"studio_id": None, "studio": None}


@router.post("/select/{studio_id}")
async def select_studio(studio_id: str, user: User = Depends(get_current_user)):
    """Select a studio for the current session."""
    # Verify studio exists
    studio = get_studio(studio_id)

    # Store selection
    _user_selected_studios[user.id] = studio_id
    logger.info(f"User {user.email} selected studio {studio_id}")

    return {
        "message": "Studio selected",
        "studio_id": studio_id,
        "studio": studio
    }


@router.post("/clear-selection")
async def clear_studio_selection(user: User = Depends(get_current_user)):
    """Clear the current studio selection."""
    if user.id in _user_selected_studios:
        del _user_selected_studios[user.id]
        logger.info(f"User {user.email} cleared studio selection")

    return {"message": "Selection cleared"}


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
    _save_studios()  # Persist change
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

    _save_studios()  # Persist change
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
    _save_studios()  # Persist change
    _save_recorders()  # Persist recorder changes
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

    _save_studios()  # Persist change
    _save_recorders()  # Persist recorder changes

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


def get_user_selected_studio(user_id: str) -> Optional[str]:
    """Get the selected studio ID for a user."""
    return _user_selected_studios.get(user_id)
