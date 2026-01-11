"""
Audio Streaming API

Stream audio for in-browser preview and playback.
Supports growing files for live broadcast production.

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
import time
from typing import Optional, AsyncGenerator

from ..auth.entra import get_current_user, User
from ..services.config_store import load_global_config

logger = logging.getLogger(__name__)

router = APIRouter()


def get_archive_root() -> str:
    """
    Get the archive root directory from configuration.
    Falls back to environment variable, then default.
    """
    config = load_global_config()
    if config and config.get("archive_root"):
        return config["archive_root"]
    return os.getenv("AUDYN_ARCHIVE_ROOT", os.path.expanduser("~/audyn-archive"))

# Configuration for growing file handling
GROWING_FILE_POLL_INTERVAL = 0.1  # 100ms polling for new data
GROWING_FILE_TIMEOUT = 5.0  # Wait up to 5 seconds for new data before assuming file is complete
STREAM_CHUNK_SIZE = 8192  # 8KB chunks for streaming

# Ogg page header signature
OGG_PAGE_SIGNATURE = b'OggS'
OGG_HEADER_MIN_SIZE = 27  # Minimum Ogg page header size


def find_complete_ogg_pages(data: bytes) -> tuple[bytes, bytes]:
    """
    Parse Ogg data and return (complete_pages, remainder).
    Only returns data up to the last complete Ogg page boundary.
    This ensures we never send partial/corrupted page data.
    """
    if len(data) < OGG_HEADER_MIN_SIZE:
        return b'', data

    complete_end = 0
    pos = 0

    while pos + OGG_HEADER_MIN_SIZE <= len(data):
        # Check for OggS signature
        if data[pos:pos+4] != OGG_PAGE_SIGNATURE:
            # Not at a page boundary - might be corrupted or mid-page
            # Try to find next page
            next_ogg = data.find(OGG_PAGE_SIGNATURE, pos + 1)
            if next_ogg == -1:
                break
            pos = next_ogg
            continue

        # Parse page header to get total page size
        # Byte 26 is number of segments
        if pos + 27 > len(data):
            break

        num_segments = data[pos + 26]

        # Need segment table (num_segments bytes after header)
        header_size = 27 + num_segments
        if pos + header_size > len(data):
            break

        # Sum segment sizes to get body length
        body_size = sum(data[pos + 27:pos + header_size])
        total_page_size = header_size + body_size

        if pos + total_page_size > len(data):
            # Page extends beyond available data - incomplete
            break

        # Complete page found
        complete_end = pos + total_page_size
        pos = complete_end

    return data[:complete_end], data[complete_end:]


async def stream_growing_ogg_file(file_path: Path) -> AsyncGenerator[bytes, None]:
    """
    Stream a growing Ogg file, only sending complete pages.
    This is critical for reliable playback of Ogg Opus files during recording.
    """
    last_position = 0
    stall_start = None
    buffer = b''

    while True:
        try:
            current_size = file_path.stat().st_size
        except FileNotFoundError:
            logger.warning(f"File disappeared during streaming: {file_path}")
            break

        if current_size > last_position:
            stall_start = None

            with open(file_path, 'rb') as f:
                f.seek(last_position)
                new_data = f.read(current_size - last_position)
                last_position = current_size

            # Combine with any buffered partial page
            buffer += new_data

            # Extract complete pages
            complete, buffer = find_complete_ogg_pages(buffer)

            if complete:
                yield complete

        else:
            if stall_start is None:
                stall_start = time.time()
            elif time.time() - stall_start > GROWING_FILE_TIMEOUT:
                # Send any remaining buffer (file is complete)
                if buffer:
                    yield buffer
                logger.debug(f"Ogg stream complete: {file_path}")
                break

            await asyncio.sleep(GROWING_FILE_POLL_INTERVAL)


async def stream_growing_file(file_path: Path) -> AsyncGenerator[bytes, None]:
    """
    Stream a file that may still be growing (being written to).
    Continuously reads new data as it becomes available.

    This is critical for broadcast production where recordings
    need to be played back while still being captured.
    """
    last_position = 0
    last_size = 0
    stall_start = None

    while True:
        try:
            current_size = file_path.stat().st_size
        except FileNotFoundError:
            logger.warning(f"File disappeared during streaming: {file_path}")
            break

        if current_size > last_position:
            # New data available
            stall_start = None

            with open(file_path, 'rb') as f:
                f.seek(last_position)
                while True:
                    chunk = f.read(STREAM_CHUNK_SIZE)
                    if not chunk:
                        break
                    last_position = f.tell()
                    yield chunk

            last_size = current_size
        else:
            # No new data - file might be complete or writer is slow
            if stall_start is None:
                stall_start = time.time()
            elif time.time() - stall_start > GROWING_FILE_TIMEOUT:
                # No new data for timeout period - assume file is complete
                logger.debug(f"File stream complete (no new data): {file_path}")
                break

            # Wait before checking again
            await asyncio.sleep(GROWING_FILE_POLL_INTERVAL)


async def transcode_growing_file(
    file_path: Path,
    output_format: str = "mp3",
    follow: bool = True
) -> AsyncGenerator[bytes, None]:
    """
    Transcode audio file to browser-compatible format.
    Handles growing files by using FFmpeg's ability to follow input.

    Args:
        file_path: Path to the audio file
        output_format: Output format (mp3 or aac)
        follow: If True, follow growing file; if False, transcode existing content only
    """
    # Build FFmpeg command for growing file support
    cmd = ["ffmpeg", "-hide_banner", "-loglevel", "warning"]

    ext = file_path.suffix.lower()

    # Input options for growing files
    if follow:
        # Options to handle incomplete/growing files:
        # - genpts: generate timestamps if missing
        # - igndts: ignore DTS errors
        # - discardcorrupt: skip corrupted frames
        cmd.extend(["-fflags", "+genpts+igndts+discardcorrupt"])

        # Reduce probing to start faster on growing files
        # Don't wait too long trying to analyze the stream
        cmd.extend(["-probesize", "32768"])  # 32KB probe
        cmd.extend(["-analyzeduration", "500000"])  # 0.5 seconds

        # Specify input format hint based on extension
        if ext == '.wav':
            cmd.extend(["-f", "wav"])
        elif ext in ['.opus', '.ogg']:
            cmd.extend(["-f", "ogg"])
            # For Ogg: be lenient with missing EOS
            cmd.extend(["-err_detect", "ignore_err"])

        # Allow FFmpeg to wait for more data
        cmd.extend(["-thread_queue_size", "512"])

    cmd.extend(["-i", str(file_path)])

    # Output options
    if output_format == "mp3":
        cmd.extend([
            "-c:a", "libmp3lame",
            "-b:a", "192k",
            "-f", "mp3"
        ])
    else:  # aac
        cmd.extend([
            "-c:a", "aac",
            "-b:a", "192k",
            "-f", "adts"
        ])

    # Output to stdout
    cmd.append("-")

    logger.debug(f"Starting transcode: {' '.join(cmd)}")

    process = await asyncio.create_subprocess_exec(
        *cmd,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE
    )

    try:
        while True:
            chunk = await process.stdout.read(STREAM_CHUNK_SIZE)
            if not chunk:
                break
            yield chunk
    finally:
        # Ensure process is terminated
        if process.returncode is None:
            process.terminate()
            try:
                await asyncio.wait_for(process.wait(), timeout=2.0)
            except asyncio.TimeoutError:
                process.kill()


async def transcode_to_mp3(file_path: Path, start_time: float = 0):
    """Legacy transcode function for compatibility."""
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
        chunk = await process.stdout.read(STREAM_CHUNK_SIZE)
        if not chunk:
            break
        yield chunk


@router.get("/preview/{file_path:path}")
async def stream_preview(
    file_path: str,
    start: float = 0,
    follow: bool = True,
    user: User = Depends(get_current_user)
):
    """
    Stream audio file for browser preview.
    Transcodes to MP3 for broad browser compatibility.

    Args:
        file_path: Relative path to audio file within archive
        start: Start time in seconds (for seeking)
        follow: If True, follow growing file for live playback

    This endpoint is designed for broadcast production use:
    - Supports playback of files that are still being recorded
    - Uses chunked transfer encoding (no Content-Length)
    - Minimal buffering for low latency
    """
    full_path = Path(get_archive_root()) / file_path

    # Security: ensure path is within archive root
    try:
        full_path.resolve().relative_to(Path(get_archive_root()).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    if not full_path.is_file():
        raise HTTPException(status_code=400, detail="Not a file")

    # Check if file is likely still being written
    # (recently modified and small or growing)
    is_growing = False
    try:
        stat = full_path.stat()
        age = time.time() - stat.st_mtime
        # Consider file "growing" if modified in last 10 seconds
        is_growing = age < 10
    except:
        pass

    if is_growing:
        logger.info(f"Streaming growing file: {full_path}")

    # Use appropriate streaming method
    if start > 0:
        # Seeking requested - use standard transcode
        generator = transcode_to_mp3(full_path, start)
    else:
        # Start from beginning - use growing file aware transcode
        generator = transcode_growing_file(full_path, "mp3", follow=follow)

    return StreamingResponse(
        generator,
        media_type="audio/mpeg",
        headers={
            # No Content-Length for growing files - use chunked encoding
            "Transfer-Encoding": "chunked",
            # Prevent caching for live content
            "Cache-Control": "no-cache, no-store, must-revalidate",
            "Pragma": "no-cache",
            "Expires": "0",
            # Allow range requests for seeking (when not following)
            "Accept-Ranges": "none" if follow else "bytes",
        }
    )


@router.get("/raw/{file_path:path}")
async def stream_raw(
    file_path: str,
    follow: bool = True,
    user: User = Depends(get_current_user)
):
    """
    Stream raw audio file without transcoding.
    Useful for WAV files that browsers can play natively.
    Supports growing files for live playback.
    """
    full_path = Path(get_archive_root()) / file_path

    try:
        full_path.resolve().relative_to(Path(get_archive_root()).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    if not full_path.is_file():
        raise HTTPException(status_code=400, detail="Not a file")

    # Determine MIME type
    ext = full_path.suffix.lower()
    mime_types = {
        '.wav': 'audio/wav',
        '.mp3': 'audio/mpeg',
        '.ogg': 'audio/ogg',
        '.opus': 'audio/ogg',
        '.aac': 'audio/aac',
        '.m4a': 'audio/mp4',
    }
    media_type = mime_types.get(ext, 'application/octet-stream')

    # Choose appropriate streaming method
    if follow:
        # Use Ogg-aware streaming for Ogg/Opus files to ensure complete pages
        if ext in ['.ogg', '.opus']:
            generator = stream_growing_ogg_file(full_path)
        else:
            generator = stream_growing_file(full_path)
    else:
        generator = open(full_path, 'rb')

    return StreamingResponse(
        generator,
        media_type=media_type,
        headers={
            "Transfer-Encoding": "chunked",
            "Cache-Control": "no-cache, no-store, must-revalidate",
            "Pragma": "no-cache",
            "Expires": "0",
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
    full_path = Path(get_archive_root()) / file_path

    try:
        full_path.resolve().relative_to(Path(get_archive_root()).resolve())
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
    full_path = Path(get_archive_root()) / file_path

    try:
        full_path.resolve().relative_to(Path(get_archive_root()).resolve())
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
            "channel_layout": audio_stream.get("channel_layout") if audio_stream else None,
            # Include flag indicating if file may be growing
            "growing": (time.time() - full_path.stat().st_mtime) < 10
        }
    except Exception as e:
        logger.error(f"FFprobe failed: {e}")
        raise HTTPException(status_code=500, detail="Failed to get audio info")


@router.get("/status/{file_path:path}")
async def get_file_status(
    file_path: str,
    user: User = Depends(get_current_user)
):
    """
    Get status of an audio file including whether it's still being written.
    Useful for UI to show recording indicator and update duration.
    """
    full_path = Path(get_archive_root()) / file_path

    try:
        full_path.resolve().relative_to(Path(get_archive_root()).resolve())
    except ValueError:
        raise HTTPException(status_code=403, detail="Access denied")

    if not full_path.exists():
        raise HTTPException(status_code=404, detail="File not found")

    stat = full_path.stat()
    age = time.time() - stat.st_mtime

    return {
        "path": file_path,
        "size": stat.st_size,
        "modified": stat.st_mtime,
        "age_seconds": age,
        "growing": age < 10,  # Consider growing if modified in last 10 seconds
        "recording": age < 2,  # Consider actively recording if modified in last 2 seconds
    }
