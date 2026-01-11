"""
Recorder Manager Service

Manages multiple recorder processes (one per recorder instance).

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

import asyncio
import subprocess
import os
import signal
import logging
import json
from typing import Optional
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

from ..models import RecorderConfig, SourceType, ChannelLevel
from ..services.config_store import load_global_config
from ..api.control import CaptureConfig

logger = logging.getLogger(__name__)


def get_global_config():
    """Get the current global config fresh from the config store."""
    saved = load_global_config()
    if saved:
        try:
            return CaptureConfig(**saved)
        except Exception as e:
            logger.error(f"Failed to parse global config: {e}")
            return None
    return None

# Path to Audyn binary - check for mock first, then real
MOCK_AUDYN = Path(__file__).parent.parent.parent / "scripts" / "mock_audyn.sh"
AUDYN_BIN = os.getenv("AUDYN_BIN", str(MOCK_AUDYN) if MOCK_AUDYN.exists() else "/usr/bin/audyn")


@dataclass
class RecorderProcess:
    """Tracks a running recorder process."""
    recorder_id: int
    process: Optional[asyncio.subprocess.Process] = None
    config: Optional[RecorderConfig] = None
    start_time: Optional[datetime] = None
    current_file: Optional[str] = None
    monitor_task: Optional[asyncio.Task] = None
    stdout_task: Optional[asyncio.Task] = None
    levels: list = field(default_factory=list)  # Current audio levels


class RecorderManager:
    """Manages multiple recorder processes."""

    def __init__(self):
        self._processes: dict[int, RecorderProcess] = {}
        self._lock = asyncio.Lock()

    async def initialize(self):
        """Initialize the manager."""
        logger.info(f"RecorderManager initialized. Using binary: {AUDYN_BIN}")
        if not Path(AUDYN_BIN).exists():
            logger.warning(f"Audyn binary not found at {AUDYN_BIN}")

    async def shutdown(self):
        """Shutdown all recorders."""
        for recorder_id in list(self._processes.keys()):
            await self.stop_recorder(recorder_id)
        logger.info("RecorderManager shutdown complete")

    def _build_command(self, recorder_id: int, config: RecorderConfig, studio_id: str = None) -> list[str]:
        """Build command line for recorder."""
        cmd = [AUDYN_BIN]

        # Use global settings from Settings tab for archive config
        # Fall back to recorder config if global not set
        archive_layout = "dailydir"
        archive_period = 3600
        archive_clock = "localtime"
        archive_base = os.path.expanduser("~/audyn-archive")

        global_cfg = get_global_config()
        if global_cfg:
            archive_layout = global_cfg.archive_layout.value if global_cfg.archive_layout else "dailydir"
            archive_period = global_cfg.archive_period or 3600
            archive_clock = global_cfg.archive_clock.value if global_cfg.archive_clock else "localtime"
            archive_base = global_cfg.archive_root or archive_base

        # Archive settings - use studio-based path if studio_id is provided
        if studio_id:
            archive_root = f"{archive_base}/{studio_id}"
        else:
            archive_root = f"{archive_base}/recorder{recorder_id}"

        cmd.extend(["--archive-root", archive_root])
        cmd.extend(["--archive-layout", archive_layout])
        cmd.extend(["--archive-period", str(archive_period)])
        cmd.extend(["--archive-clock", archive_clock])
        cmd.extend(["--archive-suffix", config.format.value if config.format else "wav"])

        # Source configuration
        if config.source_type == SourceType.PIPEWIRE:
            cmd.append("--pipewire")
            # Note: --pipewire-target not yet implemented in audyn binary
        else:
            # AES67
            if config.multicast_addr:
                cmd.extend(["-m", config.multicast_addr])
            cmd.extend(["-p", str(config.port or 5004)])
            cmd.extend(["--pt", str(config.payload_type or 96)])
            cmd.extend(["--spp", str(config.samples_per_packet or 48)])

        # Audio format
        cmd.extend(["-r", str(config.sample_rate or 48000)])
        cmd.extend(["-c", str(config.channels or 2)])

        # Format-specific options
        if config.format and config.format.value in ["opus", "mp3"]:
            cmd.extend(["--bitrate", str(config.bitrate or 128000)])

        # Always enable level metering for web UI
        cmd.append("--levels")

        return cmd

    async def start_recorder(self, recorder_id: int, config: RecorderConfig, studio_id: str = None) -> bool:
        """Start a recorder process."""
        async with self._lock:
            if recorder_id in self._processes:
                proc = self._processes[recorder_id]
                if proc.process and proc.process.returncode is None:
                    logger.warning(f"Recorder {recorder_id} already running")
                    return False

            # Use global archive root from Settings, fall back to default
            archive_base = os.path.expanduser("~/audyn-archive")
            global_cfg = get_global_config()
            if global_cfg and global_cfg.archive_root:
                archive_base = global_cfg.archive_root

            # Determine archive root based on studio_id
            if studio_id:
                archive_root = f"{archive_base}/{studio_id}"
            else:
                archive_root = f"{archive_base}/recorder{recorder_id}"

            # Ensure archive directory exists
            os.makedirs(archive_root, exist_ok=True)

            cmd = self._build_command(recorder_id, config, studio_id)
            logger.info(f"Starting recorder {recorder_id}: {' '.join(cmd)}")

            try:
                process = await asyncio.create_subprocess_exec(
                    *cmd,
                    stdout=asyncio.subprocess.PIPE,
                    stderr=asyncio.subprocess.PIPE
                )

                rec_proc = RecorderProcess(
                    recorder_id=recorder_id,
                    process=process,
                    config=config,
                    start_time=datetime.now()
                )

                # Start monitor task (stderr)
                rec_proc.monitor_task = asyncio.create_task(
                    self._monitor_process(recorder_id)
                )

                # Start stdout reader for level data
                rec_proc.stdout_task = asyncio.create_task(
                    self._read_levels(recorder_id)
                )

                self._processes[recorder_id] = rec_proc
                logger.info(f"Recorder {recorder_id} started with PID {process.pid}")
                return True

            except Exception as e:
                logger.error(f"Failed to start recorder {recorder_id}: {e}")
                return False

    async def stop_recorder(self, recorder_id: int) -> bool:
        """Stop a recorder process."""
        async with self._lock:
            if recorder_id not in self._processes:
                logger.warning(f"Recorder {recorder_id} not running")
                return False

            proc = self._processes[recorder_id]
            if not proc.process:
                del self._processes[recorder_id]
                return True

            logger.info(f"Stopping recorder {recorder_id}...")

            try:
                # Cancel monitor tasks
                if proc.monitor_task:
                    proc.monitor_task.cancel()
                if proc.stdout_task:
                    proc.stdout_task.cancel()

                # Send SIGTERM for graceful shutdown
                proc.process.send_signal(signal.SIGTERM)

                # Wait up to 5 seconds
                try:
                    await asyncio.wait_for(proc.process.wait(), timeout=5)
                except asyncio.TimeoutError:
                    logger.warning(f"Recorder {recorder_id} didn't stop gracefully, killing")
                    proc.process.kill()
                    await proc.process.wait()

                del self._processes[recorder_id]
                logger.info(f"Recorder {recorder_id} stopped")
                return True

            except Exception as e:
                logger.error(f"Error stopping recorder {recorder_id}: {e}")
                return False

    def is_running(self, recorder_id: int) -> bool:
        """Check if a recorder is running."""
        if recorder_id not in self._processes:
            return False
        proc = self._processes[recorder_id]
        return proc.process is not None and proc.process.returncode is None

    def get_process_info(self, recorder_id: int) -> Optional[dict]:
        """Get info about a running recorder process."""
        if recorder_id not in self._processes:
            return None

        proc = self._processes[recorder_id]
        return {
            "pid": proc.process.pid if proc.process else None,
            "start_time": proc.start_time.isoformat() if proc.start_time else None,
            "running": proc.process.returncode is None if proc.process else False
        }

    async def _monitor_process(self, recorder_id: int):
        """Monitor a recorder process for output and status."""
        if recorder_id not in self._processes:
            return

        proc = self._processes[recorder_id]
        if not proc.process:
            return

        try:
            while proc.process.returncode is None:
                # Read stderr for logs
                if proc.process.stderr:
                    try:
                        line = await asyncio.wait_for(
                            proc.process.stderr.readline(),
                            timeout=1.0
                        )
                        if line:
                            log_line = line.decode().strip()
                            logger.debug(f"Recorder {recorder_id}: {log_line}")
                    except asyncio.TimeoutError:
                        pass

                await asyncio.sleep(0.5)

            # Process exited
            logger.warning(f"Recorder {recorder_id} process exited with code {proc.process.returncode}")

        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.error(f"Monitor error for recorder {recorder_id}: {e}")

    async def _read_levels(self, recorder_id: int):
        """Read and parse level JSON from stdout."""
        if recorder_id not in self._processes:
            return

        proc = self._processes[recorder_id]
        if not proc.process or not proc.process.stdout:
            return

        try:
            while proc.process.returncode is None:
                try:
                    line = await asyncio.wait_for(
                        proc.process.stdout.readline(),
                        timeout=1.0
                    )
                    if line:
                        line_str = line.decode().strip()
                        if line_str.startswith('{') and '"type":"levels"' in line_str:
                            try:
                                data = json.loads(line_str)
                                self._update_levels(recorder_id, data)
                            except json.JSONDecodeError:
                                pass
                except asyncio.TimeoutError:
                    pass

                await asyncio.sleep(0.01)  # Small yield

        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.error(f"Level reader error for recorder {recorder_id}: {e}")

    def _update_levels(self, recorder_id: int, data: dict):
        """Update stored levels for a recorder from JSON data."""
        if recorder_id not in self._processes:
            return

        proc = self._processes[recorder_id]
        channels = data.get("channels", 2)
        levels = []

        if "left" in data:
            left = data["left"]
            levels.append(ChannelLevel(
                name="L",
                level_db=left.get("rms_db", -60),
                level_linear=10 ** (left.get("rms_db", -60) / 20),
                peak_db=left.get("peak_db", -60),
                clipping=left.get("clipping", False)
            ))

        if "right" in data and channels >= 2:
            right = data["right"]
            levels.append(ChannelLevel(
                name="R",
                level_db=right.get("rms_db", -60),
                level_linear=10 ** (right.get("rms_db", -60) / 20),
                peak_db=right.get("peak_db", -60),
                clipping=right.get("clipping", False)
            ))

        proc.levels = levels

    def get_levels(self, recorder_id: int) -> list:
        """Get current audio levels for a recorder."""
        if recorder_id not in self._processes:
            return []
        return self._processes[recorder_id].levels


# Singleton instance
_manager: Optional[RecorderManager] = None


def get_recorder_manager() -> RecorderManager:
    """Get the recorder manager singleton."""
    global _manager
    if _manager is None:
        _manager = RecorderManager()
    return _manager
