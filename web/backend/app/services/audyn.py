"""
Audyn Process Control Service

Manages the Audyn capture process lifecycle.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

import asyncio
import subprocess
import os
import signal
import logging
from typing import Optional
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

logger = logging.getLogger(__name__)

# Path to Audyn binary
AUDYN_BIN = os.getenv("AUDYN_BIN", "/usr/bin/audyn")


@dataclass
class CaptureConfig:
    """Capture configuration."""
    source_type: str = "aes67"
    multicast_addr: str = "239.69.1.1"
    port: int = 5004
    sample_rate: int = 48000
    channels: int = 2
    payload_type: int = 96
    samples_per_packet: int = 48

    # Output
    format: str = "wav"
    bitrate: int = 128000

    # Archive
    archive_root: str = "/var/lib/audyn"
    archive_layout: str = "dailydir"
    archive_period: int = 3600
    archive_clock: str = "localtime"

    # PTP
    ptp_interface: Optional[str] = None


@dataclass
class CaptureStatus:
    """Current capture status."""
    running: bool = False
    pid: Optional[int] = None
    start_time: Optional[datetime] = None
    current_file: Optional[str] = None
    bytes_written: int = 0
    errors: list[str] = field(default_factory=list)


class AudynService:
    """Service for controlling Audyn capture process."""

    def __init__(self):
        self._process: Optional[asyncio.subprocess.Process] = None
        self._config: Optional[CaptureConfig] = None
        self._status = CaptureStatus()
        self._monitor_task: Optional[asyncio.Task] = None

    async def initialize(self):
        """Initialize the service."""
        logger.info("AudynService initialized")

        # Check if Audyn binary exists
        if not Path(AUDYN_BIN).exists():
            logger.warning(f"Audyn binary not found at {AUDYN_BIN}")

    async def shutdown(self):
        """Shutdown the service."""
        if self._status.running:
            await self.stop_capture()

        if self._monitor_task:
            self._monitor_task.cancel()

        logger.info("AudynService shutdown complete")

    def _build_command(self, config: CaptureConfig) -> list[str]:
        """Build Audyn command line from config."""
        cmd = [AUDYN_BIN]

        # Archive mode
        cmd.extend(["--archive-root", config.archive_root])
        cmd.extend(["--archive-layout", config.archive_layout])
        cmd.extend(["--archive-period", str(config.archive_period)])
        cmd.extend(["--archive-clock", config.archive_clock])
        cmd.extend(["--archive-suffix", config.format])

        # Source
        if config.source_type == "pipewire":
            cmd.append("--pipewire")
        else:
            # AES67
            cmd.extend(["-m", config.multicast_addr])
            cmd.extend(["-p", str(config.port)])
            cmd.extend(["--pt", str(config.payload_type)])
            cmd.extend(["--spp", str(config.samples_per_packet)])

        # Audio format
        cmd.extend(["-r", str(config.sample_rate)])
        cmd.extend(["-c", str(config.channels)])

        # Opus bitrate
        if config.format == "opus":
            cmd.extend(["--bitrate", str(config.bitrate)])

        # PTP
        if config.ptp_interface:
            cmd.extend(["--ptp-iface", config.ptp_interface])

        return cmd

    async def start_capture(self, config: CaptureConfig) -> bool:
        """Start audio capture with given configuration."""
        if self._status.running:
            logger.warning("Capture already running")
            return False

        self._config = config

        # Ensure archive directory exists
        os.makedirs(config.archive_root, exist_ok=True)

        cmd = self._build_command(config)
        logger.info(f"Starting Audyn: {' '.join(cmd)}")

        try:
            self._process = await asyncio.create_subprocess_exec(
                *cmd,
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.PIPE
            )

            self._status = CaptureStatus(
                running=True,
                pid=self._process.pid,
                start_time=datetime.now()
            )

            # Start monitoring task
            self._monitor_task = asyncio.create_task(self._monitor_process())

            logger.info(f"Audyn started with PID {self._process.pid}")
            return True

        except Exception as e:
            logger.error(f"Failed to start Audyn: {e}")
            self._status.errors.append(str(e))
            return False

    async def stop_capture(self) -> bool:
        """Stop audio capture."""
        if not self._status.running or not self._process:
            logger.warning("No capture running")
            return False

        logger.info("Stopping Audyn...")

        try:
            # Send SIGTERM for graceful shutdown
            self._process.send_signal(signal.SIGTERM)

            # Wait up to 10 seconds for graceful shutdown
            try:
                await asyncio.wait_for(self._process.wait(), timeout=10)
            except asyncio.TimeoutError:
                logger.warning("Audyn didn't stop gracefully, sending SIGKILL")
                self._process.kill()
                await self._process.wait()

            self._status = CaptureStatus(running=False)
            logger.info("Audyn stopped")
            return True

        except Exception as e:
            logger.error(f"Error stopping Audyn: {e}")
            self._status.errors.append(str(e))
            return False

    async def restart_capture(self) -> bool:
        """Restart capture with current configuration."""
        if self._config is None:
            logger.error("No configuration set")
            return False

        await self.stop_capture()
        await asyncio.sleep(1)  # Brief pause
        return await self.start_capture(self._config)

    async def switch_source(self, multicast_addr: str, port: int = 5004) -> bool:
        """Switch to a different AES67 source (requires restart)."""
        if self._config is None:
            logger.error("No configuration set")
            return False

        self._config.multicast_addr = multicast_addr
        self._config.port = port

        if self._status.running:
            return await self.restart_capture()

        return True

    async def get_status(self) -> dict:
        """Get current capture status."""
        duration = 0
        if self._status.start_time:
            duration = int((datetime.now() - self._status.start_time).total_seconds())

        return {
            "running": self._status.running,
            "pid": self._status.pid,
            "start_time": self._status.start_time.isoformat() if self._status.start_time else None,
            "duration_seconds": duration,
            "current_file": self._status.current_file,
            "bytes_written": self._status.bytes_written,
            "errors": self._status.errors,
            "config": {
                "source_type": self._config.source_type if self._config else None,
                "multicast_addr": self._config.multicast_addr if self._config else None,
                "format": self._config.format if self._config else None,
                "archive_layout": self._config.archive_layout if self._config else None
            } if self._config else None
        }

    async def _monitor_process(self):
        """Monitor the Audyn process for output and status."""
        if not self._process:
            return

        try:
            while self._status.running:
                # Check if process is still running
                if self._process.returncode is not None:
                    logger.warning(f"Audyn process exited with code {self._process.returncode}")
                    self._status.running = False
                    break

                # Read stderr for any errors/warnings
                if self._process.stderr:
                    try:
                        line = await asyncio.wait_for(
                            self._process.stderr.readline(),
                            timeout=1.0
                        )
                        if line:
                            log_line = line.decode().strip()
                            logger.info(f"Audyn: {log_line}")
                    except asyncio.TimeoutError:
                        pass

                await asyncio.sleep(0.5)

        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.error(f"Monitor error: {e}")
