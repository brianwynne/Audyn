#!/usr/bin/env python3
"""
SAP/SDP Test Announcer

Sends SAP announcements to test stream discovery.

Usage:
    ./sap_announce.py                      # Send default test stream
    ./sap_announce.py --name "Studio A"    # Custom name
    ./sap_announce.py --channels 16        # Multi-channel stream
    ./sap_announce.py --interval 5         # Send every 5 seconds

Copyright: (c) 2026 B. Wynne
License: GPLv2 or later
"""

import argparse
import socket
import struct
import time
import random

SAP_ADDR = "239.255.255.255"
SAP_PORT = 9875
SAP_VERSION = 1

# SMPTE ST 2110-30 Channel Configurations
CHANNEL_CONFIGS = {
    1: "M",           # Mono
    2: "ST",          # Stereo
    6: "51",          # 5.1 Surround
    8: "71",          # 7.1 Surround
}


def get_channel_order(channels: int, channel_config: str = None) -> str:
    """
    Get SMPTE2110 channel-order string.

    Args:
        channels: Number of channels
        channel_config: Override config (e.g., "51", "71", "ST,ST" for 4ch stereo pairs)

    Returns:
        Channel order string for SDP fmtp attribute
    """
    if channel_config:
        return channel_config

    # Use predefined configs for standard channel counts
    if channels in CHANNEL_CONFIGS:
        return CHANNEL_CONFIGS[channels]

    # For other channel counts, use mono symbols
    # e.g., 4 channels = "M,M,M,M"
    return ",".join(["M"] * channels)


def build_sdp(
    session_name: str,
    multicast_addr: str,
    port: int,
    sample_rate: int,
    channels: int,
    encoding: str = "L24",
    ptime: float = 1.0,
    origin_ip: str = "192.168.1.100",
    ptp_grandmaster: str = "00-00-00-00-00-00-00-00",
    ptp_domain: int = 0,
    channel_config: str = None,
    session_info: str = None
) -> str:
    """
    Build RFC 8866 and SMPTE ST 2110-30 compliant SDP payload.

    RFC 8866 Compliance:
    - Fields in mandatory order: v=, o=, s=, i=, c=, t=, m=, a=
    - CRLF line endings (with LF tolerance for parsers)

    SMPTE ST 2110-30 Compliance:
    - mediaclk:direct=0 (zero offset from PTP clock)
    - ts-refclk:ptp=IEEE1588-2008:<GM-ID>:<domain>
    - channel-order=SMPTE2110.(<symbols>)
    """
    session_id = random.randint(100000, 999999)
    payload_type = 96

    # Build channel order for SMPTE 2110
    channel_order = get_channel_order(channels, channel_config)

    # Build SDP per RFC 4566 and SMPTE ST 2110-10/30
    lines = [
        "v=0",
        f"o=- {session_id} {session_id} IN IP4 {origin_ip}",
        f"s={session_name}",
    ]

    # Optional session information (i=)
    if session_info:
        lines.append(f"i={session_info}")

    lines.extend([
        f"c=IN IP4 {multicast_addr}/32",
        "t=0 0",
        f"m=audio {port} RTP/AVP {payload_type}",
        f"a=rtpmap:{payload_type} {encoding}/{sample_rate}/{channels}",
        f"a=ptime:{ptime}",
        f"a=fmtp:{payload_type} channel-order=SMPTE2110.({channel_order})",
        # ST 2110 mandatory: media clock directly referenced to PTP with zero offset
        "a=mediaclk:direct=0",
        # ST 2110 mandatory: PTP timestamp reference clock
        f"a=ts-refclk:ptp=IEEE1588-2008:{ptp_grandmaster}:{ptp_domain}",
    ])

    # RFC 8866: Use CRLF line endings
    return "\r\n".join(lines) + "\r\n"


def build_sap_packet(sdp: str, origin_ip: str, msg_id: int, deletion: bool = False) -> bytes:
    """Build a SAP packet with SDP payload."""

    # SAP header byte 0:
    # Bits 7-5: Version (1)
    # Bit 4: Address type (0 = IPv4)
    # Bit 3: Reserved
    # Bit 2: Message type (0 = announcement, 1 = deletion)
    # Bit 1: Encrypted (0)
    # Bit 0: Compressed (0)

    flags = (SAP_VERSION << 5)
    if deletion:
        flags |= 0x04

    auth_len = 0
    msg_id_hash = msg_id & 0xFFFF

    # Pack header
    header = struct.pack(
        ">BBH4s",
        flags,
        auth_len,
        msg_id_hash,
        socket.inet_aton(origin_ip)
    )

    # Add MIME type and SDP
    payload = b"application/sdp\x00" + sdp.encode('utf-8')

    return header + payload


def send_announcement(
    session_name: str,
    multicast_addr: str,
    port: int,
    sample_rate: int,
    channels: int,
    encoding: str,
    ptime: float,
    origin_ip: str,
    msg_id: int,
    ptp_grandmaster: str = "00-00-00-00-00-00-00-00",
    ptp_domain: int = 0,
    channel_config: str = None,
    session_info: str = None,
    sap_dest: str = SAP_ADDR,
    sap_port: int = SAP_PORT
):
    """Send a single SAP announcement with ST 2110-30 compliant SDP."""

    sdp = build_sdp(
        session_name=session_name,
        multicast_addr=multicast_addr,
        port=port,
        sample_rate=sample_rate,
        channels=channels,
        encoding=encoding,
        ptime=ptime,
        origin_ip=origin_ip,
        ptp_grandmaster=ptp_grandmaster,
        ptp_domain=ptp_domain,
        channel_config=channel_config,
        session_info=session_info
    )

    packet = build_sap_packet(sdp, origin_ip, msg_id)

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 4)
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_LOOP, 1)

    sock.sendto(packet, (sap_dest, sap_port))
    sock.close()

    return len(packet)


def main():
    parser = argparse.ArgumentParser(
        description="Send SMPTE ST 2110-30 compliant SAP/SDP announcements",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Channel configurations (--channel-config):
  M       Mono (1 channel)
  ST      Stereo (2 channels)
  51      5.1 Surround (6 channels)
  71      7.1 Surround (8 channels)
  LtRt    Matrix Stereo (2 channels)
  DM      Dual Mono (2 channels)

  Multiple groups can be combined: ST,ST (4ch as 2 stereo pairs)

Examples:
  ./sap_announce.py --name "Studio A" --channels 2
  ./sap_announce.py --name "Surround Mix" --channels 6 --channel-config 51
  ./sap_announce.py --name "Main Program" --channels 8 --channel-config 51,ST
  ./sap_announce.py --name "MADI Feed" --channels 16 --channel-config ST,ST,ST,ST,ST,ST,ST,ST
"""
    )

    # Basic stream parameters
    parser.add_argument("--name", default="Test Stream", help="Session name")
    parser.add_argument("--info", default=None, help="Session info/description")
    parser.add_argument("--addr", default="239.69.1.100", help="Multicast address")
    parser.add_argument("--port", type=int, default=5004, help="RTP port")
    parser.add_argument("--rate", type=int, default=48000, choices=[44100, 48000, 96000],
                        help="Sample rate (default: 48000)")
    parser.add_argument("--channels", type=int, default=2, help="Channel count")
    parser.add_argument("--encoding", default="L24", choices=["L16", "L24"], help="Encoding")
    parser.add_argument("--ptime", type=float, default=1.0, help="Packet time in ms (1.0 or 0.125)")
    parser.add_argument("--origin", default="192.168.1.100", help="Origin IP address")

    # SMPTE ST 2110 specific
    parser.add_argument("--channel-config", default=None,
                        help="SMPTE2110 channel config (e.g., ST, 51, 71, ST,ST)")
    parser.add_argument("--ptp-gm", default="00-00-00-00-00-00-00-00",
                        help="PTP Grandmaster ID (default: 00-00-00-00-00-00-00-00)")
    parser.add_argument("--ptp-domain", type=int, default=0,
                        help="PTP domain (default: 0)")

    # Announcement control
    parser.add_argument("--interval", type=float, default=0, help="Send interval in seconds (0 = once)")
    parser.add_argument("--count", type=int, default=0, help="Number of announcements (0 = infinite)")
    parser.add_argument("--delete", action="store_true", help="Send deletion announcement")

    args = parser.parse_args()

    msg_id = random.randint(0, 65535)

    # Determine channel config display
    ch_config = args.channel_config or get_channel_order(args.channels)

    # Determine conformance level
    is_96k = args.rate == 96000
    is_1ms = abs(args.ptime - 1.0) < 0.01
    if is_96k:
        level = "AX" if is_1ms else "BX"
    else:
        level = "A" if is_1ms else "B"

    print(f"SMPTE ST 2110-30 SAP Announcer")
    print(f"  Session:      {args.name}")
    print(f"  Address:      {args.addr}:{args.port}")
    print(f"  Format:       {args.encoding} @ {args.rate} Hz, {args.channels} ch, {args.ptime}ms")
    print(f"  Channel Cfg:  SMPTE2110.({ch_config})")
    print(f"  Conformance:  Level {level}")
    print(f"  PTP GM:       {args.ptp_gm}:{args.ptp_domain}")
    print(f"  Sending to:   {SAP_ADDR}:{SAP_PORT}")
    print()

    sent = 0
    try:
        while True:
            size = send_announcement(
                session_name=args.name,
                multicast_addr=args.addr,
                port=args.port,
                sample_rate=args.rate,
                channels=args.channels,
                encoding=args.encoding,
                ptime=args.ptime,
                origin_ip=args.origin,
                msg_id=msg_id,
                ptp_grandmaster=args.ptp_gm,
                ptp_domain=args.ptp_domain,
                channel_config=args.channel_config,
                session_info=args.info
            )
            sent += 1
            print(f"[{sent}] Sent {size} bytes")

            if args.interval <= 0:
                break
            if args.count > 0 and sent >= args.count:
                break

            time.sleep(args.interval)

    except KeyboardInterrupt:
        print("\nStopped")


if __name__ == "__main__":
    main()
