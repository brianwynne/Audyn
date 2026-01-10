"""
Asset Management API

Browse, download, and manage archived audio recordings.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, Depends, HTTPException, Query
from fastapi.responses import FileResponse, StreamingResponse
from pydantic import BaseModel
from typing import Optional
from pathlib import Path
from datetime import datetime, date
import os
import logging
import mimetypes

from ..auth.entra import get_current_user, User

logger = logging.getLogger(__name__)

router = APIRouter()

# Archive root directory
ARCHIVE_ROOT = os.getenv("AUDYN_ARCHIVE_ROOT", "/var/lib/audyn")


class AudioFile(BaseModel):
    """Audio file metadata."""
    name: str
    path: str
    size: int
    created: datetime
    modified: datetime
    duration: Optional[float] = None  # seconds
    format: str
    sample_rate: Optional[int] = None
    channels: Optional[int] = None


class Directory(BaseModel):
    """Directory listing."""
    path: str
    name: str
    parent: Optional[str]
    directories: list[str]
    files: list[AudioFile]
    total_size: int
    file_count: int


class DateRange(BaseModel):
    """Date range for filtering."""
    start: date
    end: date


def get_file_info(file_path: Path) -> AudioFile:
    """Get metadata for an audio file."""
    stat = file_path.stat()
    suffix = file_path.suffix.lower().lstrip('.')

    return AudioFile(
        name=file_path.name,
        path=str(file_path.relative_to(ARCHIVE_ROOT)),
        size=stat.st_size,
        created=datetime.fromtimestamp(stat.st_ctime),
        modified=datetime.fromtimestamp(stat.st_mtime),
        format=suffix,
        # TODO: Extract duration, sample_rate, channels from file metadata
    )


def is_audio_file(path: Path) -> bool:
    """Check if file is a supported audio format."""
    return path.suffix.lower() in ['.wav', '.opus', '.ogg', '.mp3', '.flac']


@router.get("/browse", response_model=Directory)
async def browse_directory(
    path: str = "",
    user: User = Depends(get_current_user)
):
    """Browse archive directory."""
    # Sanitize path to prevent directory traversal
    clean_path = Path(path).resolve()
    full_path = Path(ARCHIVE_ROOT) / path

    # Ensure we're still within ARCHIVE_ROOT
    try:
        full_path.resolve().relative_to(Path(ARCHIVE_ROOT).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="Directory not found")

    if not full_path.is_dir():
        raise HTTPException(status_code=400, detail="Not a directory")

    directories = []
    files = []
    total_size = 0

    for item in sorted(full_path.iterdir()):
        if item.is_dir():
            directories.append(item.name)
        elif item.is_file() and is_audio_file(item):
            file_info = get_file_info(item)
            files.append(file_info)
            total_size += file_info.size

    parent = None
    if path:
        parent_path = Path(path).parent
        parent = str(parent_path) if str(parent_path) != '.' else ""

    return Directory(
        path=path,
        name=full_path.name if path else "Archive",
        parent=parent,
        directories=directories,
        files=files,
        total_size=total_size,
        file_count=len(files)
    )


@router.get("/search")
async def search_files(
    query: Optional[str] = None,
    date_from: Optional[date] = None,
    date_to: Optional[date] = None,
    format: Optional[str] = None,
    limit: int = Query(default=100, le=500),
    offset: int = 0,
    user: User = Depends(get_current_user)
):
    """Search for audio files."""
    results = []
    archive_path = Path(ARCHIVE_ROOT)

    if not archive_path.exists():
        return {"files": [], "total": 0}

    for file_path in archive_path.rglob("*"):
        if not file_path.is_file() or not is_audio_file(file_path):
            continue

        # Apply filters
        if query and query.lower() not in file_path.name.lower():
            continue

        stat = file_path.stat()
        file_date = datetime.fromtimestamp(stat.st_mtime).date()

        if date_from and file_date < date_from:
            continue
        if date_to and file_date > date_to:
            continue

        if format and file_path.suffix.lower().lstrip('.') != format.lower():
            continue

        results.append(get_file_info(file_path))

    # Sort by modified date, newest first
    results.sort(key=lambda x: x.modified, reverse=True)

    total = len(results)
    paginated = results[offset:offset + limit]

    return {
        "files": paginated,
        "total": total,
        "limit": limit,
        "offset": offset
    }


@router.get("/file/{file_path:path}")
async def get_file_metadata(
    file_path: str,
    user: User = Depends(get_current_user)
):
    """Get metadata for a specific file."""
    full_path = Path(ARCHIVE_ROOT) / file_path

    try:
        full_path.resolve().relative_to(Path(ARCHIVE_ROOT).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    if not full_path.is_file():
        raise HTTPException(status_code=400, detail="Not a file")

    return get_file_info(full_path)


@router.get("/download/{file_path:path}")
async def download_file(
    file_path: str,
    user: User = Depends(get_current_user)
):
    """Download an audio file."""
    full_path = Path(ARCHIVE_ROOT) / file_path

    try:
        full_path.resolve().relative_to(Path(ARCHIVE_ROOT).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    if not full_path.is_file():
        raise HTTPException(status_code=400, detail="Not a file")

    media_type = mimetypes.guess_type(str(full_path))[0] or "application/octet-stream"

    return FileResponse(
        path=full_path,
        filename=full_path.name,
        media_type=media_type
    )


@router.delete("/file/{file_path:path}")
async def delete_file(
    file_path: str,
    user: User = Depends(get_current_user)
):
    """Delete an audio file."""
    full_path = Path(ARCHIVE_ROOT) / file_path

    try:
        full_path.resolve().relative_to(Path(ARCHIVE_ROOT).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    if not full_path.is_file():
        raise HTTPException(status_code=400, detail="Not a file")

    full_path.unlink()
    logger.info(f"File deleted: {file_path} by {user.email}")

    return {"message": "File deleted", "path": file_path}


@router.get("/stats")
async def get_archive_stats(user: User = Depends(get_current_user)):
    """Get archive statistics."""
    archive_path = Path(ARCHIVE_ROOT)

    if not archive_path.exists():
        return {
            "total_files": 0,
            "total_size": 0,
            "formats": {},
            "oldest_file": None,
            "newest_file": None
        }

    total_files = 0
    total_size = 0
    formats = {}
    oldest = None
    newest = None

    for file_path in archive_path.rglob("*"):
        if not file_path.is_file() or not is_audio_file(file_path):
            continue

        total_files += 1
        stat = file_path.stat()
        total_size += stat.st_size

        fmt = file_path.suffix.lower().lstrip('.')
        formats[fmt] = formats.get(fmt, 0) + 1

        mtime = datetime.fromtimestamp(stat.st_mtime)
        if oldest is None or mtime < oldest:
            oldest = mtime
        if newest is None or mtime > newest:
            newest = mtime

    return {
        "total_files": total_files,
        "total_size": total_size,
        "total_size_human": f"{total_size / (1024**3):.2f} GB",
        "formats": formats,
        "oldest_file": oldest.isoformat() if oldest else None,
        "newest_file": newest.isoformat() if newest else None
    }
