"""
AES67 Stream Discovery API

Discover AES67 audio streams via SAP (Session Announcement Protocol).

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

from fastapi import APIRouter, Depends, HTTPException, Query
from pydantic import BaseModel
from typing import Optional
import logging

from ..auth.entra import get_current_user, User
from ..services.sap_discovery import (
    get_sap_service,
    start_sap_service,
    stop_sap_service,
    DiscoveredStream,
    SAP_ADDR_ADMIN,
    SAP_ADDR_GLOBAL
)

logger = logging.getLogger(__name__)

router = APIRouter()


class StreamInfo(BaseModel):
    """Discovered stream information for API response (SMPTE ST 2110-30 compliant)."""
    id: str
    session_name: str
    session_info: Optional[str] = None
    multicast_addr: str
    port: int
    sample_rate: int
    channels: int
    encoding: str
    payload_type: int
    samples_per_packet: int
    ptime: float
    source_addr: Optional[str] = None
    is_ssm: bool = False
    channel_labels: list[str] = []
    channel_order_raw: Optional[str] = None  # e.g. "SMPTE2110.(51,ST)"
    origin_ip: str
    first_seen: str
    last_seen: str
    active: bool
    # SMPTE ST 2110-30 specific fields
    ptp_grandmaster: Optional[str] = None  # PTP GM ID from ts-refclk
    ptp_domain: int = 0  # PTP domain
    mediaclk: Optional[str] = None  # Raw mediaclk value
    is_st2110_compliant: bool = False  # True if mediaclk:direct=0
    conformance_level: Optional[str] = None  # A, B, C, AX, BX, CX


class DiscoveryStatus(BaseModel):
    """Discovery service status."""
    running: bool
    multicast_addr: str
    packets_received: int
    packets_invalid: int
    announcements: int
    deletions: int
    sdp_parse_errors: int
    active_streams: int


class DiscoveryConfig(BaseModel):
    """Discovery configuration."""
    multicast_addr: str = SAP_ADDR_ADMIN
    bind_interface: Optional[str] = None


class ChannelSelection(BaseModel):
    """Channel selection for a stream."""
    stream_channels: int
    channel_offset: int
    output_channels: int
    channel_labels: list[str] = []


def _stream_to_info(stream: DiscoveredStream) -> StreamInfo:
    """Convert internal stream to API response."""
    return StreamInfo(
        id=stream.id,
        session_name=stream.sdp.session_name or "(unnamed)",
        session_info=stream.sdp.session_info or None,
        multicast_addr=stream.sdp.multicast_addr,
        port=stream.sdp.port,
        sample_rate=stream.sdp.sample_rate,
        channels=stream.sdp.channels,
        encoding=stream.sdp.encoding,
        payload_type=stream.sdp.payload_type,
        samples_per_packet=stream.sdp.samples_per_packet,
        ptime=stream.sdp.ptime,
        source_addr=stream.sdp.source_addr,
        is_ssm=stream.sdp.is_ssm,
        channel_labels=stream.sdp.channel_labels or [f"Ch {i+1}" for i in range(stream.sdp.channels)],
        channel_order_raw=stream.sdp.channel_order_raw or None,
        origin_ip=stream.origin_ip,
        first_seen=stream.first_seen.isoformat(),
        last_seen=stream.last_seen.isoformat(),
        active=stream.active,
        # SMPTE ST 2110-30 fields
        ptp_grandmaster=stream.sdp.ptp_grandmaster or None,
        ptp_domain=stream.sdp.ptp_domain,
        mediaclk=stream.sdp.mediaclk or None,
        is_st2110_compliant=stream.sdp.is_st2110_compliant,
        conformance_level=stream.sdp.conformance_level or None
    )


@router.get("/status", response_model=DiscoveryStatus)
async def get_discovery_status(user: User = Depends(get_current_user)):
    """Get SAP discovery service status."""
    sap = get_sap_service()

    if not sap:
        return DiscoveryStatus(
            running=False,
            multicast_addr=SAP_ADDR_ADMIN,
            packets_received=0,
            packets_invalid=0,
            announcements=0,
            deletions=0,
            sdp_parse_errors=0,
            active_streams=0
        )

    stats = sap.get_stats()
    return DiscoveryStatus(
        running=sap.is_running,
        multicast_addr=sap.multicast_addr,
        **stats
    )


@router.post("/start")
async def start_discovery(
    config: Optional[DiscoveryConfig] = None,
    user: User = Depends(get_current_user)
):
    """Start SAP discovery service."""
    sap = get_sap_service()
    if sap and sap.is_running:
        return {"message": "Discovery already running", "status": "ok"}

    try:
        multicast = config.multicast_addr if config else SAP_ADDR_ADMIN
        interface = config.bind_interface if config else None

        await start_sap_service(
            bind_interface=interface,
            multicast_addr=multicast
        )

        logger.info(f"SAP discovery started by {user.email}")
        return {"message": "Discovery started", "status": "ok"}

    except Exception as e:
        logger.error(f"Failed to start discovery: {e}")
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/stop")
async def stop_discovery(user: User = Depends(get_current_user)):
    """Stop SAP discovery service."""
    sap = get_sap_service()
    if not sap or not sap.is_running:
        return {"message": "Discovery not running", "status": "ok"}

    await stop_sap_service()
    logger.info(f"SAP discovery stopped by {user.email}")
    return {"message": "Discovery stopped", "status": "ok"}


@router.get("/streams", response_model=list[StreamInfo])
async def list_discovered_streams(
    active_only: bool = Query(True, description="Only return active streams"),
    user: User = Depends(get_current_user)
):
    """List all discovered AES67 streams."""
    sap = get_sap_service()
    if not sap:
        return []

    streams = await sap.get_streams(active_only=active_only)
    return [_stream_to_info(s) for s in streams]


@router.get("/streams/{stream_id}", response_model=StreamInfo)
async def get_stream(
    stream_id: str,
    user: User = Depends(get_current_user)
):
    """Get details for a specific discovered stream."""
    sap = get_sap_service()
    if not sap:
        raise HTTPException(status_code=503, detail="Discovery not running")

    streams = await sap.get_streams(active_only=False)
    for stream in streams:
        if stream.id == stream_id:
            return _stream_to_info(stream)

    raise HTTPException(status_code=404, detail="Stream not found")


@router.get("/streams/{stream_id}/sdp")
async def get_stream_sdp(
    stream_id: str,
    user: User = Depends(get_current_user)
):
    """Get raw SDP for a discovered stream."""
    sap = get_sap_service()
    if not sap:
        raise HTTPException(status_code=503, detail="Discovery not running")

    streams = await sap.get_streams(active_only=False)
    for stream in streams:
        if stream.id == stream_id:
            return {
                "stream_id": stream_id,
                "session_name": stream.sdp.session_name,
                "sdp": stream.sdp.raw_sdp
            }

    raise HTTPException(status_code=404, detail="Stream not found")


@router.get("/search")
async def search_streams(
    name: Optional[str] = Query(None, description="Search by session name"),
    addr: Optional[str] = Query(None, description="Search by multicast address"),
    port: Optional[int] = Query(None, description="Filter by port"),
    user: User = Depends(get_current_user)
):
    """Search for discovered streams."""
    sap = get_sap_service()
    if not sap:
        return []

    streams = await sap.get_streams(active_only=True)
    results = []

    for stream in streams:
        if name and name.lower() not in stream.sdp.session_name.lower():
            continue
        if addr and addr != stream.sdp.multicast_addr:
            continue
        if port and port != stream.sdp.port:
            continue
        results.append(_stream_to_info(stream))

    return results


@router.get("/streams/{stream_id}/channels", response_model=ChannelSelection)
async def get_stream_channels(
    stream_id: str,
    output_channels: int = Query(2, ge=1, le=32, description="Desired output channels"),
    channel_offset: int = Query(0, ge=0, description="First channel to extract (0-based)"),
    user: User = Depends(get_current_user)
):
    """
    Get channel selection info for a stream.

    Use this to configure which channels to extract from a multi-channel stream.
    For example, to record channels 5-6 from a 16-channel stream:
    - stream_channels: 16
    - channel_offset: 4 (0-based, so channels 5-6)
    - output_channels: 2
    """
    sap = get_sap_service()
    if not sap:
        raise HTTPException(status_code=503, detail="Discovery not running")

    streams = await sap.get_streams(active_only=False)
    for stream in streams:
        if stream.id == stream_id:
            # Validate selection
            stream_ch = stream.sdp.channels
            if channel_offset + output_channels > stream_ch:
                raise HTTPException(
                    status_code=400,
                    detail=f"Channel selection out of range: offset {channel_offset} + {output_channels} > {stream_ch} stream channels"
                )

            # Get labels for selected channels
            all_labels = stream.sdp.channel_labels or [f"Ch {i+1}" for i in range(stream_ch)]
            selected_labels = all_labels[channel_offset:channel_offset + output_channels]

            return ChannelSelection(
                stream_channels=stream_ch,
                channel_offset=channel_offset,
                output_channels=output_channels,
                channel_labels=selected_labels
            )

    raise HTTPException(status_code=404, detail="Stream not found")
