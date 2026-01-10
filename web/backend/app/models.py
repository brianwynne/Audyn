"""
Audyn Web - Data Models

Multi-recorder and studio management models.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from pydantic import BaseModel
from typing import Optional
from enum import Enum
from datetime import datetime


class UserRole(str, Enum):
    ADMIN = "admin"
    STUDIO = "studio"


class RecorderState(str, Enum):
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


class ChannelLevel(BaseModel):
    """Audio level for a single channel."""
    name: str
    level_db: float = -60.0
    level_linear: float = 0.0
    peak_db: float = -60.0
    clipping: bool = False


class RecorderConfig(BaseModel):
    """Configuration for a single recorder."""
    source_type: str = "aes67"
    multicast_addr: str = "239.69.1.1"
    port: int = 5004
    sample_rate: int = 48000
    channels: int = 2
    payload_type: int = 96
    samples_per_packet: int = 48
    format: OutputFormat = OutputFormat.WAV
    bitrate: int = 128000
    archive_root: str = "/var/lib/audyn"
    archive_layout: ArchiveLayout = ArchiveLayout.DAILYDIR
    archive_period: int = 3600
    archive_clock: ArchiveClock = ArchiveClock.LOCALTIME
    ptp_interface: Optional[str] = None


class Recorder(BaseModel):
    """A single recorder instance."""
    id: int  # 1-6
    name: str
    enabled: bool = True
    state: RecorderState = RecorderState.STOPPED
    config: RecorderConfig
    studio_id: Optional[str] = None
    current_file: Optional[str] = None
    start_time: Optional[datetime] = None
    bytes_written: int = 0
    levels: list[ChannelLevel] = []
    errors: list[str] = []


class Studio(BaseModel):
    """A studio that can be assigned to a recorder."""
    id: str
    name: str
    description: Optional[str] = None
    color: str = "#2196F3"  # For UI identification
    recorder_id: Optional[int] = None  # Currently assigned recorder
    enabled: bool = True


class SystemConfig(BaseModel):
    """Global system configuration."""
    max_recorders: int = 6  # 1-6
    active_recorders: int = 6
    archive_base_path: str = "/var/lib/audyn"
    default_format: OutputFormat = OutputFormat.WAV
    default_sample_rate: int = 48000
    default_channels: int = 2
    ptp_enabled: bool = False
    ptp_interface: Optional[str] = None


class User(BaseModel):
    """User with role-based access."""
    id: str
    email: str
    name: str
    role: UserRole = UserRole.STUDIO
    studio_id: Optional[str] = None  # For studio users, their assigned studio
    roles: list[str] = []  # Legacy compatibility

    @property
    def is_admin(self) -> bool:
        return self.role == UserRole.ADMIN or "admin" in self.roles
