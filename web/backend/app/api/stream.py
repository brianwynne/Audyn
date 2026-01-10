"""
Audio Streaming API

Stream audio for in-browser preview and playback.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, Depends, HTTPException, Request
from fastapi.responses import StreamingResponse, Response
from pathlib import Path
import os
import subprocess
import asyncio
import logging
from typing import Optional

from ..auth.entra import get_current_user, User

logger = logging.getLogger(__name__)

router = APIRouter()

ARCHIVE_ROOT = os.getenv("AUDYN_ARCHIVE_ROOT", "/var/lib/audyn")


async def transcode_to_mp3(file_path: Path, start_time: float = 0):
    """Transcode audio file to MP3 for browser playback."""
    cmd = [
        "ffmpeg",
        "-i", str(file_path),
        "-ss", str(start_time),
        "-c:a", "libmp3lame",
        "-b:a", "192k",
        "-f", "mp3",
        "-"
    ]

    process = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE
    )

    while True:
        chunk = await process.stdout.read(8192)
        if not chunk:
            break
        yield chunk


async def transcode_to_aac(file_path: Path, start_time: float = 0):
    """Transcode audio file to AAC for HLS streaming."""
    cmd = [
        "ffmpeg",
        "-i", str(file_path),
        "-ss", str(start_time),
        "-c:a", "aac",
        "-b:a", "192k",
        "-f", "adts",
        "-"
    ]

    process = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE
    )

    while True:
        chunk = await process.stdout.read(8192)
        if not chunk:
            break
        yield chunk


@router.get("/preview/{file_path:path}")
async def stream_preview(
    file_path: str,
    start: float = 0,
    user: User = Depends(get_current_user)
):
    """
    Stream audio file for browser preview.
    Transcodes to MP3 for broad browser compatibility.
    """
    full_path = Path(ARCHIVE_ROOT) / file_path

    try:
        full_path.resolve().relative_to(Path(ARCHIVE_ROOT).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    if not full_path.is_file():
        raise HTTPException(status_code=400, detail="Not a file")

    return StreamingResponse(
        transcode_to_mp3(full_path, start),
        media_type="audio/mpeg",
        headers={
            "Accept-Ranges": "bytes",
            "Cache-Control": "no-cache"
        }
    )


@router.get("/live")
async def stream_live(user: User = Depends(get_current_user)):
    """
    Stream live audio from current capture.
    Returns real-time audio transcoded to MP3.
    """
    # TODO: Implement live streaming from Audyn process
    # This would tap into the audio queue and transcode in real-time
    raise HTTPException(
        status_code=501,
        detail="Live streaming not yet implemented"
    )


@router.get("/waveform/{file_path:path}")
async def get_waveform(
    file_path: str,
    width: int = 800,
    height: int = 100,
    user: User = Depends(get_current_user)
):
    """
    Generate waveform visualization data for an audio file.
    Returns peak values for rendering a waveform display.
    """
    full_path = Path(ARCHIVE_ROOT) / file_path

    try:
        full_path.resolve().relative_to(Path(ARCHIVE_ROOT).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    # Use FFmpeg to extract peak data
    cmd = [
        "ffprobe",
        "-f", "lavfi",
        "-i", f"amovie={str(full_path)},asetnsamples={width},astats=metadata=1:reset=1",
        "-show_entries", "frame_tags=lavfi.astats.Overall.Peak_level",
        "-of", "csv=p=0",
        "-v", "quiet"
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
        peaks = []
        for line in result.stdout.strip().split('\n'):
            if line:
                try:
                    db = float(line)
                    # Convert dB to linear scale (0-1)
                    linear = 10 ** (db / 20) if db > -60 else 0
                    peaks.append(min(1.0, linear))
                except ValueError:
                    peaks.append(0)

        return {
            "width": width,
            "peaks": peaks
        }
    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=504, detail="Waveform generation timeout")
    except Exception as e:
        logger.error(f"Waveform generation failed: {e}")
        raise HTTPException(status_code=500, detail="Waveform generation failed")


@router.get("/info/{file_path:path}")
async def get_audio_info(
    file_path: str,
    user: User = Depends(get_current_user)
):
    """Get detailed audio file information using FFprobe."""
    full_path = Path(ARCHIVE_ROOT) / file_path

    try:
        full_path.resolve().relative_to(Path(ARCHIVE_ROOT).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    cmd = [
        "ffprobe",
        "-v", "quiet",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        str(full_path)
    ]

    try:
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        import json
        info = json.loads(result.stdout)

        # Extract relevant audio info
        audio_stream = None
        for stream in info.get("streams", []):
            if stream.get("codec_type") == "audio":
                audio_stream = stream
                break

        format_info = info.get("format", {})

        return {
            "filename": full_path.name,
            "format": format_info.get("format_name"),
            "duration": float(format_info.get("duration", 0)),
            "size": int(format_info.get("size", 0)),
            "bitrate": int(format_info.get("bit_rate", 0)),
            "codec": audio_stream.get("codec_name") if audio_stream else None,
            "sample_rate": int(audio_stream.get("sample_rate", 0)) if audio_stream else None,
            "channels": audio_stream.get("channels") if audio_stream else None,
            "channel_layout": audio_stream.get("channel_layout") if audio_stream else None
        }
    except Exception as e:
        logger.error(f"FFprobe failed: {e}")
        raise HTTPException(status_code=500, detail="Failed to get audio info")
