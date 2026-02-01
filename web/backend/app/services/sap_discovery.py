"""
SAP/SDP Stream Discovery Service

Listens for SAP (Session Announcement Protocol) announcements and
parses SDP (Session Description Protocol) payloads to discover
AES67 audio streams on the network.

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

import asyncio
import socket
import struct
import logging
import re
from dataclasses import dataclass, field
from datetime import datetime
from typing import Optional, Callable
import ipaddress

logger = logging.getLogger(__name__)

# SAP Constants (RFC 2974)
SAP_ADDR_GLOBAL = "224.2.127.254"
SAP_ADDR_ADMIN = "239.255.255.255"
SAP_PORT = 9875
SAP_VERSION = 1


# SMPTE ST 2110-30 Channel Grouping Symbols (Table 1)
SMPTE2110_CHANNEL_GROUPS = {
    "M": ["M"],                                          # Mono
    "DM": ["M1", "M2"],                                  # Dual Mono
    "ST": ["L", "R"],                                    # Stereo
    "LtRt": ["Lt", "Rt"],                                # Matrix Stereo
    "51": ["L", "R", "C", "LFE", "Ls", "Rs"],           # 5.1 Surround
    "71": ["L", "R", "C", "LFE", "Lss", "Rss", "Lrs", "Rrs"],  # 7.1 Surround (DS)
    "222": ["L", "R", "C", "LFE", "Lss", "Rss", "Lrs", "Rrs",  # 22.2
            "Tfl", "Tfr", "Tfc", "Tsl", "Tsr", "Tbl", "Tbr",
            "Tbc", "Ltf", "Rtf", "Ltr", "Rtr", "Lw", "Rw", "LFE2", "Cb"],
    "SGRP": ["St1L", "St1R", "St2L", "St2R"],           # Standard Stereo Group (SDI)
}


@dataclass
class SDPStream:
    """Parsed SDP stream information per SMPTE ST 2110-30."""
    session_name: str = ""
    session_id: str = ""
    session_version: str = ""
    origin_addr: str = ""
    session_info: str = ""  # i= line
    multicast_addr: str = ""
    port: int = 0
    payload_type: int = 96
    encoding: str = "L24"
    sample_rate: int = 48000
    channels: int = 2
    ptime: float = 1.0
    samples_per_packet: int = 48
    source_addr: Optional[str] = None  # For SSM
    is_ssm: bool = False
    channel_labels: list[str] = field(default_factory=list)
    channel_order_raw: str = ""  # Raw channel-order string e.g. "SMPTE2110.(51,ST)"
    mediaclk: str = ""
    mediaclk_offset: int = 0  # Should be 0 for ST 2110
    ts_refclk: str = ""
    ptp_grandmaster: str = ""  # PTP GM ID from ts-refclk
    ptp_domain: int = 0  # PTP domain from ts-refclk
    is_st2110_compliant: bool = False  # True if mediaclk:direct=0
    conformance_level: str = ""  # A, B, C, AX, BX, CX
    raw_sdp: str = ""


@dataclass
class DiscoveredStream:
    """A discovered stream with metadata."""
    id: str  # Hash-based ID
    sdp: SDPStream
    origin_ip: str
    first_seen: datetime
    last_seen: datetime
    active: bool = True


def parse_channel_order(channel_order_str: str) -> list[str]:
    """
    Parse SMPTE2110 channel-order into channel labels.

    Format: SMPTE2110.(symbol1,symbol2,...)
    Example: SMPTE2110.(51,ST) -> ['L','R','C','LFE','Ls','Rs','L','R']
    """
    labels = []

    # Extract symbols from parentheses
    match = re.search(r'\(([^)]+)\)', channel_order_str)
    if not match:
        return labels

    symbols = match.group(1).split(',')

    for symbol in symbols:
        symbol = symbol.strip()

        # Check for undefined groups U01-U64
        if re.match(r'^U(\d+)$', symbol):
            count = int(symbol[1:])
            for i in range(count):
                labels.append(f"U{len(labels)+1}")
        # Check known SMPTE2110 symbols
        elif symbol in SMPTE2110_CHANNEL_GROUPS:
            labels.extend(SMPTE2110_CHANNEL_GROUPS[symbol])
        # Unknown symbol - add as-is
        elif symbol:
            labels.append(symbol)

    return labels


def parse_ts_refclk(ts_refclk: str) -> tuple[str, int]:
    """
    Parse ts-refclk to extract PTP grandmaster ID and domain.

    Format: ptp=IEEE1588-2008:GM-ID:domain
    Example: ptp=IEEE1588-2008:00-11-22-FF-FE-33-44-55:0

    Returns: (grandmaster_id, domain)
    """
    grandmaster = ""
    domain = 0

    match = re.search(r'ptp=IEEE1588-\d+:([0-9A-Fa-f-]+):(\d+)', ts_refclk)
    if match:
        grandmaster = match.group(1).upper()
        domain = int(match.group(2))

    return grandmaster, domain


def parse_mediaclk(mediaclk: str) -> tuple[bool, int]:
    """
    Parse mediaclk to check ST 2110 compliance.

    ST 2110 requires: mediaclk:direct=0

    Returns: (is_st2110_compliant, offset)
    """
    is_compliant = False
    offset = -1

    if mediaclk.startswith('direct='):
        try:
            # Extract offset value
            match = re.match(r'direct=(\d+)', mediaclk)
            if match:
                offset = int(match.group(1))
                is_compliant = (offset == 0)
        except ValueError:
            pass

    return is_compliant, offset


def detect_conformance_level(channels: int, ptime: float, sample_rate: int) -> str:
    """
    Detect SMPTE ST 2110-30 conformance level.

    Level A: 1-8 ch, 1ms ptime, 48kHz (also AES67)
    Level B: 1-8 ch, 0.125ms ptime, 48kHz
    Level C: 1-64 ch, 0.125ms ptime, 48kHz
    Level AX: 1-4 ch, 1ms ptime, 96kHz
    Level BX: 1-4 ch, 0.125ms ptime, 96kHz
    Level CX: 1-32 ch, 0.125ms ptime, 96kHz
    """
    is_96k = sample_rate == 96000
    is_1ms = abs(ptime - 1.0) < 0.01
    is_125us = abs(ptime - 0.125) < 0.01

    if is_96k:
        if is_1ms and channels <= 4:
            return "AX"
        elif is_125us:
            if channels <= 4:
                return "BX"
            elif channels <= 32:
                return "CX"
    else:  # 48kHz (or 44.1kHz)
        if is_1ms and channels <= 8:
            return "A"
        elif is_125us:
            if channels <= 8:
                return "B"
            elif channels <= 64:
                return "C"

    return ""  # Non-conformant


def parse_sdp(sdp_text: str) -> Optional[SDPStream]:
    """
    Parse SDP text into SDPStream structure.

    Compliant with:
    - RFC 8866 (SDP: Session Description Protocol)
    - SMPTE ST 2110-30 (PCM Digital Audio)
    - SMPTE ST 2110-10 (System Timing and Definitions)

    RFC 8866 compliance:
    - Accepts both CRLF and LF line endings
    - Parses mandatory fields: v=, o=, s=, c=, t=, m=
    - Parses optional fields: i=, a=
    """
    stream = SDPStream(raw_sdp=sdp_text)
    in_audio_media = False

    # RFC 8866: Accept both CRLF and LF line endings
    for line in sdp_text.replace('\r\n', '\n').split('\n'):
        line = line.strip()
        if len(line) < 2 or line[1] != '=':
            continue

        type_char = line[0]
        value = line[2:]

        if type_char == 'o':
            # o=<username> <sess-id> <sess-version> <nettype> <addrtype> <addr>
            parts = value.split()
            if len(parts) >= 6:
                stream.session_id = parts[1]
                stream.session_version = parts[2]
                stream.origin_addr = parts[5]

        elif type_char == 's':
            stream.session_name = value

        elif type_char == 'i':
            stream.session_info = value

        elif type_char == 'c':
            # c=<nettype> <addrtype> <connection-address>[/<ttl>][/<num>]
            parts = value.split()
            if len(parts) >= 3:
                addr = parts[2].split('/')[0]
                stream.multicast_addr = addr

        elif type_char == 'm':
            # m=audio <port> RTP/AVP <fmt>
            parts = value.split()
            if len(parts) >= 4 and parts[0] == 'audio':
                in_audio_media = True
                stream.port = int(parts[1])
                stream.payload_type = int(parts[3])

        elif type_char == 'a' and in_audio_media:
            if value.startswith('rtpmap:'):
                # a=rtpmap:<pt> <encoding>/<clock>[/<channels>]
                match = re.match(r'rtpmap:(\d+)\s+(\w+)/(\d+)(?:/(\d+))?', value)
                if match:
                    stream.payload_type = int(match.group(1))
                    stream.encoding = match.group(2)
                    stream.sample_rate = int(match.group(3))
                    stream.channels = int(match.group(4)) if match.group(4) else 2

            elif value.startswith('ptime:'):
                try:
                    stream.ptime = float(value[6:])
                    if stream.sample_rate > 0:
                        stream.samples_per_packet = int(stream.sample_rate * stream.ptime / 1000)
                except ValueError:
                    pass

            elif value.startswith('source-filter:'):
                # a=source-filter: incl IN IP4 <dest> <source>
                match = re.search(r'incl\s+IN\s+IP\d\s+\S+\s+(\S+)', value)
                if match:
                    stream.source_addr = match.group(1)
                    stream.is_ssm = True

            elif value.startswith('mediaclk:'):
                stream.mediaclk = value[9:]
                stream.is_st2110_compliant, stream.mediaclk_offset = parse_mediaclk(stream.mediaclk)

            elif value.startswith('ts-refclk:'):
                stream.ts_refclk = value[10:]
                stream.ptp_grandmaster, stream.ptp_domain = parse_ts_refclk(stream.ts_refclk)

            elif value.startswith('fmtp:'):
                # Look for channel-order=SMPTE2110.(...)
                match = re.search(r'channel-order=(\S+\.\([^)]+\))', value)
                if match:
                    stream.channel_order_raw = match.group(1)
                    stream.channel_labels = parse_channel_order(stream.channel_order_raw)

    # Validate we got minimum info
    if stream.multicast_addr and stream.port > 0:
        # Defaults
        if stream.sample_rate == 0:
            stream.sample_rate = 48000
        if stream.channels == 0:
            stream.channels = 2
        if stream.samples_per_packet == 0:
            stream.samples_per_packet = 48

        # Generate default channel labels if not provided
        if not stream.channel_labels:
            if stream.channels == 1:
                stream.channel_labels = ["M"]
            elif stream.channels == 2:
                stream.channel_labels = ["L", "R"]
            else:
                stream.channel_labels = [f"Ch {i+1}" for i in range(stream.channels)]

        # Detect conformance level
        stream.conformance_level = detect_conformance_level(
            stream.channels, stream.ptime, stream.sample_rate
        )

        return stream

    return None


class SAPDiscoveryService:
    """SAP discovery service that listens for stream announcements."""

    def __init__(
        self,
        bind_interface: Optional[str] = None,
        multicast_addr: str = SAP_ADDR_ADMIN,
        port: int = SAP_PORT,
        stream_timeout: int = 300
    ):
        self.bind_interface = bind_interface
        self.multicast_addr = multicast_addr
        self.port = port
        self.stream_timeout = stream_timeout

        self._streams: dict[str, DiscoveredStream] = {}
        self._lock = asyncio.Lock()
        self._running = False
        self._socket: Optional[socket.socket] = None
        self._listener_task: Optional[asyncio.Task] = None
        self._cleanup_task: Optional[asyncio.Task] = None
        self._callbacks: list[Callable] = []

        # Statistics
        self.packets_received = 0
        self.packets_invalid = 0
        self.announcements = 0
        self.deletions = 0
        self.sdp_parse_errors = 0

    def add_callback(self, callback: Callable):
        """Add callback for stream events."""
        self._callbacks.append(callback)

    async def start(self):
        """Start the SAP discovery service."""
        if self._running:
            return

        try:
            # Create UDP socket
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
            self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self._socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
            self._socket.setblocking(False)

            # Bind to SAP port
            self._socket.bind(('', self.port))

            # Join multicast group
            mreq = struct.pack(
                '4s4s',
                socket.inet_aton(self.multicast_addr),
                socket.inet_aton('0.0.0.0')
            )
            self._socket.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

            self._running = True
            self._listener_task = asyncio.create_task(self._listener_loop())
            self._cleanup_task = asyncio.create_task(self._cleanup_loop())

            logger.info(f"SAP discovery started on {self.multicast_addr}:{self.port}")

        except Exception as e:
            logger.error(f"Failed to start SAP discovery: {e}")
            if self._socket:
                self._socket.close()
                self._socket = None
            raise

    async def stop(self):
        """Stop the SAP discovery service."""
        self._running = False

        if self._listener_task:
            self._listener_task.cancel()
            try:
                await self._listener_task
            except asyncio.CancelledError:
                pass

        if self._cleanup_task:
            self._cleanup_task.cancel()
            try:
                await self._cleanup_task
            except asyncio.CancelledError:
                pass

        if self._socket:
            self._socket.close()
            self._socket = None

        logger.info("SAP discovery stopped")

    async def _listener_loop(self):
        """Main listener loop using add_reader for reliable multicast."""
        loop = asyncio.get_running_loop()
        data_queue = asyncio.Queue()

        def on_readable():
            try:
                data, addr = self._socket.recvfrom(65535)
                loop.call_soon_threadsafe(data_queue.put_nowait, (data, addr))
            except BlockingIOError:
                pass
            except Exception as e:
                logger.error(f"SAP socket read error: {e}")

        loop.add_reader(self._socket.fileno(), on_readable)
        logger.info("SAP listener loop started")

        try:
            while self._running:
                try:
                    data, addr = await asyncio.wait_for(data_queue.get(), timeout=1.0)
                    self.packets_received += 1
                    await self._handle_packet(data, addr[0])
                except asyncio.TimeoutError:
                    continue
                except asyncio.CancelledError:
                    break
                except Exception as e:
                    logger.error(f"SAP listener error: {e}")
        finally:
            loop.remove_reader(self._socket.fileno())

    async def _handle_packet(self, data: bytes, origin_ip: str):
        """Handle a SAP packet."""
        if len(data) < 8:
            self.packets_invalid += 1
            return

        # Parse SAP header
        b0 = data[0]
        version = (b0 >> 5) & 0x07
        is_ipv6 = bool((b0 >> 4) & 0x01)
        is_deletion = bool((b0 >> 2) & 0x01)
        is_encrypted = bool((b0 >> 1) & 0x01)
        is_compressed = bool(b0 & 0x01)

        if version != SAP_VERSION:
            self.packets_invalid += 1
            return

        if is_encrypted or is_compressed:
            # Not supported
            self.packets_invalid += 1
            return

        auth_len = data[1]
        msg_id_hash = struct.unpack('>H', data[2:4])[0]

        # Origin IP (4 bytes for IPv4, 16 for IPv6)
        origin_offset = 4
        if is_ipv6:
            origin_bytes = data[4:20]
            origin_offset = 20
        else:
            origin_bytes = data[4:8]
            origin_offset = 8

        try:
            sap_origin = str(ipaddress.ip_address(origin_bytes))
        except ValueError:
            sap_origin = origin_ip

        # Skip auth data
        payload_offset = origin_offset + (auth_len * 4)
        if payload_offset >= len(data):
            self.packets_invalid += 1
            return

        # Find payload type (optional MIME type)
        payload_data = data[payload_offset:]

        # Check for MIME type header (null-terminated)
        if b'\x00' in payload_data[:64]:
            null_pos = payload_data.index(b'\x00')
            payload_data = payload_data[null_pos + 1:]

        # Try to decode as SDP
        try:
            sdp_text = payload_data.decode('utf-8', errors='ignore')
        except Exception:
            self.packets_invalid += 1
            return

        # Generate stream ID from origin + msg_id_hash
        stream_id = f"{sap_origin}:{msg_id_hash:04x}"

        if is_deletion:
            await self._handle_deletion(stream_id)
        else:
            await self._handle_announcement(stream_id, sdp_text, sap_origin)

    async def _handle_announcement(self, stream_id: str, sdp_text: str, origin_ip: str):
        """Handle a stream announcement."""
        sdp = parse_sdp(sdp_text)
        if not sdp:
            self.sdp_parse_errors += 1
            return

        now = datetime.now()
        async with self._lock:
            if stream_id in self._streams:
                # Update existing
                self._streams[stream_id].sdp = sdp
                self._streams[stream_id].last_seen = now
                self._streams[stream_id].active = True
            else:
                # New stream
                self._streams[stream_id] = DiscoveredStream(
                    id=stream_id,
                    sdp=sdp,
                    origin_ip=origin_ip,
                    first_seen=now,
                    last_seen=now
                )
                self.announcements += 1
                logger.info(f"Discovered stream: {sdp.session_name} ({sdp.multicast_addr}:{sdp.port})")

                # Notify callbacks
                for cb in self._callbacks:
                    try:
                        await cb('new', self._streams[stream_id])
                    except Exception as e:
                        logger.error(f"Callback error: {e}")

    async def _handle_deletion(self, stream_id: str):
        """Handle a stream deletion."""
        async with self._lock:
            if stream_id in self._streams:
                stream = self._streams[stream_id]
                stream.active = False
                self.deletions += 1
                logger.info(f"Stream deleted: {stream.sdp.session_name}")

                for cb in self._callbacks:
                    try:
                        await cb('delete', stream)
                    except Exception as e:
                        logger.error(f"Callback error: {e}")

    async def _cleanup_loop(self):
        """Periodically clean up expired streams."""
        while self._running:
            await asyncio.sleep(60)  # Check every minute

            now = datetime.now()
            expired = []

            async with self._lock:
                for stream_id, stream in self._streams.items():
                    age = (now - stream.last_seen).total_seconds()
                    if age > self.stream_timeout:
                        expired.append(stream_id)

                for stream_id in expired:
                    stream = self._streams[stream_id]
                    stream.active = False
                    logger.info(f"Stream expired: {stream.sdp.session_name}")

    async def get_streams(self, active_only: bool = True) -> list[DiscoveredStream]:
        """Get list of discovered streams."""
        async with self._lock:
            if active_only:
                return [s for s in self._streams.values() if s.active]
            return list(self._streams.values())

    async def find_stream(self, multicast_addr: str, port: int = 0) -> Optional[DiscoveredStream]:
        """Find a stream by multicast address."""
        async with self._lock:
            for stream in self._streams.values():
                if stream.sdp.multicast_addr == multicast_addr:
                    if port == 0 or stream.sdp.port == port:
                        return stream
        return None

    async def find_by_name(self, name: str) -> Optional[DiscoveredStream]:
        """Find a stream by session name."""
        async with self._lock:
            for stream in self._streams.values():
                if stream.sdp.session_name == name:
                    return stream
        return None

    def get_stats(self) -> dict:
        """Get discovery statistics."""
        return {
            "packets_received": self.packets_received,
            "packets_invalid": self.packets_invalid,
            "announcements": self.announcements,
            "deletions": self.deletions,
            "sdp_parse_errors": self.sdp_parse_errors,
            "active_streams": len([s for s in self._streams.values() if s.active])
        }

    @property
    def is_running(self) -> bool:
        return self._running


# Global instance
_sap_service: Optional[SAPDiscoveryService] = None


def get_sap_service() -> Optional[SAPDiscoveryService]:
    """Get the global SAP service instance."""
    return _sap_service


async def start_sap_service(
    bind_interface: Optional[str] = None,
    multicast_addr: str = SAP_ADDR_ADMIN
) -> SAPDiscoveryService:
    """Start the global SAP discovery service."""
    global _sap_service

    if _sap_service is not None:
        return _sap_service

    _sap_service = SAPDiscoveryService(
        bind_interface=bind_interface,
        multicast_addr=multicast_addr
    )
    await _sap_service.start()
    return _sap_service


async def stop_sap_service():
    """Stop the global SAP discovery service."""
    global _sap_service

    if _sap_service:
        await _sap_service.stop()
        _sap_service = None
