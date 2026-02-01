# AES67 Stream Discovery

This document describes Audyn's SAP/SDP stream discovery system, which enables automatic detection and configuration of AES67 audio streams on the network.

## Table of Contents

1. [Overview](#overview)
2. [Standards Compliance](#standards-compliance)
3. [Architecture](#architecture)
4. [SDP Parsing](#sdp-parsing)
5. [SMPTE ST 2110-30 Support](#smpte-st-2110-30-support)
6. [API Reference](#api-reference)
7. [SAP Announcer Tool](#sap-announcer-tool)
8. [Network Requirements](#network-requirements)
9. [Troubleshooting](#troubleshooting)

---

## Overview

Audyn discovers AES67 audio streams using the Session Announcement Protocol (SAP), which broadcasts Session Description Protocol (SDP) data over multicast. This enables automatic detection of streams from devices like:

- Calrec Type R radio production systems
- Lawo audio consoles
- DHD audio mixers
- Telos Alliance equipment
- Any SMPTE ST 2110-30 or AES67 compliant device

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        STREAM DISCOVERY                                   │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Network                          Audyn                                  │
│  ┌──────────────────┐            ┌──────────────────────────────────┐   │
│  │  AES67 Device 1  │            │                                   │   │
│  │  ───────────────>│───SAP────>│  SAP Listener (239.255.255.255)   │   │
│  │  Stream: Mix L/R │            │         │                         │   │
│  └──────────────────┘            │         v                         │   │
│                                  │  ┌──────────────────┐             │   │
│  ┌──────────────────┐            │  │   SDP Parser     │             │   │
│  │  AES67 Device 2  │            │  │   (RFC 8866)     │             │   │
│  │  ───────────────>│───SAP────>│  │   ────────────>  │             │   │
│  │  Stream: 5.1     │            │  └──────────────────┘             │   │
│  └──────────────────┘            │         │                         │   │
│                                  │         v                         │   │
│  ┌──────────────────┐            │  ┌──────────────────┐             │   │
│  │  AES67 Device 3  │            │  │  Stream Database │             │   │
│  │  ───────────────>│───SAP────>│  │  ────────────>   │             │   │
│  │  Stream: MADI    │            │  └──────────────────┘             │   │
│  └──────────────────┘            │         │                         │   │
│                                  │         v                         │   │
│                                  │  ┌──────────────────┐             │   │
│                                  │  │   REST API       │             │   │
│                                  │  │   /api/discovery │             │   │
│                                  │  └──────────────────┘             │   │
│                                  └──────────────────────────────────┘   │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Standards Compliance

Audyn's stream discovery implementation is fully compliant with the following standards:

### RFC 8866 — SDP: Session Description Protocol

| Requirement | Implementation |
|-------------|----------------|
| Field order (v, o, s, i, c, t, m, a) | Enforced in parser and generator |
| CRLF line endings | Generated; CRLF/LF tolerated in parsing |
| Mandatory fields (v=, o=, s=, t=) | Validated on parse |
| UTF-8 encoding | Native Python string handling |
| Media description parsing | Full support for audio media type |

### RFC 2974 — Session Announcement Protocol

| Requirement | Implementation |
|-------------|----------------|
| SAP Version 2 | Fully supported |
| Multicast address (administratively scoped) | 239.255.255.255:9875 (default) |
| Global scope address | 224.2.127.254:9875 (optional) |
| IPv4/IPv6 addressing | IPv4 implemented |
| MIME type `application/sdp` | Required in packet parsing |
| Message ID hash | Used for stream identification |
| Deletion announcements | Supported |

### SMPTE ST 2110-10 — System Timing and Definitions

| Requirement | Implementation |
|-------------|----------------|
| `ts-refclk` attribute | Parsed: PTP GM ID and domain extracted |
| `mediaclk` attribute | Parsed: offset value extracted |
| PTP reference (IEEE1588-2008) | Full format parsing |
| Zero clock offset requirement | Validated for compliance check |

### SMPTE ST 2110-30 — PCM Digital Audio

| Requirement | Implementation |
|-------------|----------------|
| Channel order (SMPTE2110 convention) | Full symbol set supported |
| Conformance levels (A/B/C/AX/BX/CX) | Automatically detected |
| Sample rates (44.1k, 48k, 96k) | All supported |
| Bit depths (L16, L24) | All supported |
| Packet times (1ms, 0.125ms) | All supported |

---

## Architecture

### Service Components

```
web/backend/app/
├── api/
│   └── discovery.py          # REST API endpoints
└── services/
    └── sap_discovery.py      # Core SAP/SDP service
```

### SAP Discovery Service

```python
# Service lifecycle
┌─────────────────────────────────────────────────────────────────┐
│                    SAP DISCOVERY SERVICE                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  Initialization                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  1. Create UDP socket                                      │   │
│  │  2. Set SO_REUSEADDR + SO_REUSEPORT                       │   │
│  │  3. Bind to port 9875                                      │   │
│  │  4. Join multicast group 239.255.255.255                   │   │
│  │  5. Start async listener task                              │   │
│  │  6. Start cleanup task (stale stream removal)              │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  Runtime Loop                                                    │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  while running:                                            │   │
│  │    packet = await receive()                                │   │
│  │    sap_header = parse_sap(packet)                         │   │
│  │    sdp_text = extract_payload(packet)                     │   │
│  │    stream = parse_sdp(sdp_text)                           │   │
│  │    if sap_header.is_deletion:                             │   │
│  │      remove_stream(stream.id)                              │   │
│  │    else:                                                   │   │
│  │      upsert_stream(stream)                                 │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  Cleanup Task                                                    │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  every 30 seconds:                                         │   │
│  │    for stream in streams:                                  │   │
│  │      if now - stream.last_seen > timeout:                  │   │
│  │        mark_inactive(stream)                               │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Data Structures

```python
@dataclass
class SDPStream:
    """Parsed SDP stream information per SMPTE ST 2110-30."""

    # RFC 8866 Session Description
    session_name: str           # s= line
    session_id: str             # From o= line
    session_version: str        # From o= line
    session_info: str           # i= line (optional)
    origin_addr: str            # From o= line

    # Media Description
    multicast_addr: str         # From c= line
    port: int                   # From m= line
    payload_type: int           # From m= line
    encoding: str               # L16 or L24
    sample_rate: int            # 44100, 48000, or 96000
    channels: int               # 1-64
    ptime: float                # Packet time in ms
    samples_per_packet: int     # Calculated from ptime

    # Source-Specific Multicast (SSM)
    source_addr: str            # From source-filter attribute
    is_ssm: bool                # True if SSM enabled

    # SMPTE ST 2110-30 Channel Mapping
    channel_labels: list[str]   # Expanded from channel-order
    channel_order_raw: str      # e.g., "SMPTE2110.(51,ST)"

    # SMPTE ST 2110-10 Timing
    mediaclk: str               # Raw mediaclk value
    mediaclk_offset: int        # Offset value (0 for ST 2110)
    ts_refclk: str              # Raw ts-refclk value
    ptp_grandmaster: str        # PTP GM ID (e.g., "00-1D-C1-FF-FE-12-34-56")
    ptp_domain: int             # PTP domain (0-127)

    # Compliance
    is_st2110_compliant: bool   # True if mediaclk:direct=0
    conformance_level: str      # A, B, C, AX, BX, or CX

    # Raw Data
    raw_sdp: str                # Original SDP text


@dataclass
class DiscoveredStream:
    """A discovered stream with metadata."""
    id: str                     # Unique identifier (origin:hash)
    sdp: SDPStream              # Parsed SDP data
    origin_ip: str              # IP address of announcer
    first_seen: datetime        # First discovery timestamp
    last_seen: datetime         # Last announcement timestamp
    active: bool                # True if recently seen
```

---

## SDP Parsing

### Parsed Fields

The SDP parser extracts the following fields per RFC 8866:

| SDP Line | Field | Example |
|----------|-------|---------|
| `v=` | Protocol version | `0` |
| `o=` | Origin (username, session-id, version, addr) | `- 123456 123456 IN IP4 192.168.1.100` |
| `s=` | Session name | `Studio A Output` |
| `i=` | Session information | `Main program feed` |
| `c=` | Connection address | `IN IP4 239.69.1.10/32` |
| `t=` | Time (start, stop) | `0 0` (permanent) |
| `m=` | Media (type, port, proto, format) | `audio 5004 RTP/AVP 96` |
| `a=rtpmap` | RTP mapping | `96 L24/48000/2` |
| `a=ptime` | Packet time | `1` (ms) |
| `a=fmtp` | Format parameters | `96 channel-order=SMPTE2110.(ST)` |
| `a=mediaclk` | Media clock | `direct=0` |
| `a=ts-refclk` | Timestamp reference clock | `ptp=IEEE1588-2008:00-1D-C1-FF-FE-12-34-56:0` |
| `a=source-filter` | SSM source filter | `incl IN IP4 239.69.1.10 192.168.1.100` |

### Example SDP

```
v=0
o=- 123456 123456 IN IP4 192.168.1.100
s=Studio A Output
i=Main program stereo feed
c=IN IP4 239.69.1.10/32
t=0 0
m=audio 5004 RTP/AVP 96
a=rtpmap:96 L24/48000/2
a=ptime:1
a=fmtp:96 channel-order=SMPTE2110.(ST)
a=mediaclk:direct=0
a=ts-refclk:ptp=IEEE1588-2008:00-1D-C1-FF-FE-12-34-56:0
```

---

## SMPTE ST 2110-30 Support

### Channel Order Symbols

The parser supports all SMPTE ST 2110-30 channel grouping symbols:

| Symbol | Channels | Labels | Description |
|--------|----------|--------|-------------|
| `M` | 1 | M | Mono |
| `DM` | 2 | M1, M2 | Dual Mono |
| `ST` | 2 | L, R | Stereo |
| `LtRt` | 2 | Lt, Rt | Matrix Stereo (Dolby Pro Logic) |
| `51` | 6 | L, R, C, LFE, Ls, Rs | 5.1 Surround |
| `71` | 8 | L, R, C, LFE, Lss, Rss, Lrs, Rrs | 7.1 Surround (DS) |
| `222` | 24 | (22.2 NHK layout) | 22.2 Surround |
| `U01`-`U64` | 1-64 | U1, U2, ... | Undefined/Generic |

### Channel Order Examples

| SDP Attribute | Interpretation |
|---------------|----------------|
| `channel-order=SMPTE2110.(ST)` | 2ch Stereo (L, R) |
| `channel-order=SMPTE2110.(51)` | 6ch 5.1 Surround |
| `channel-order=SMPTE2110.(51,ST)` | 8ch: 5.1 + Stereo |
| `channel-order=SMPTE2110.(ST,ST,ST,ST)` | 8ch: 4 Stereo pairs |
| `channel-order=SMPTE2110.(M,M,M,M,ST)` | 6ch: 4 Mono + Stereo |

### Conformance Levels

The parser automatically detects SMPTE ST 2110-30 conformance level:

| Level | Channels | Packet Time | Sample Rate | Description |
|-------|----------|-------------|-------------|-------------|
| **A** | 1-8 | 1.000 ms | 48 kHz | Standard (AES67 compatible) |
| **B** | 1-8 | 0.125 ms | 48 kHz | Low latency |
| **C** | 1-64 | 0.125 ms | 48 kHz | High channel count |
| **AX** | 1-4 | 1.000 ms | 96 kHz | High sample rate |
| **BX** | 1-4 | 0.125 ms | 96 kHz | High rate + low latency |
| **CX** | 1-32 | 0.125 ms | 96 kHz | High rate + high channels |

### ST 2110 Compliance Check

A stream is marked as `is_st2110_compliant: true` when:

1. `mediaclk:direct=0` is present (zero offset between media and RTP clocks)
2. `ts-refclk:ptp=IEEE1588-2008:...` is present (PTP timestamp reference)

---

## API Reference

### Endpoints

#### GET `/api/discovery/status`

Get the current status of the SAP discovery service.

**Response:**
```json
{
  "running": true,
  "multicast_addr": "239.255.255.255",
  "packets_received": 42,
  "packets_invalid": 0,
  "announcements": 35,
  "deletions": 2,
  "sdp_parse_errors": 0,
  "active_streams": 4
}
```

#### POST `/api/discovery/start`

Start the SAP discovery service.

**Request Body (optional):**
```json
{
  "multicast_addr": "239.255.255.255",
  "bind_interface": "eth1"
}
```

**Response:**
```json
{
  "message": "Discovery started",
  "status": "ok"
}
```

#### POST `/api/discovery/stop`

Stop the SAP discovery service.

**Response:**
```json
{
  "message": "Discovery stopped",
  "status": "ok"
}
```

#### GET `/api/discovery/streams`

List all discovered streams.

**Query Parameters:**
- `active_only` (bool, default: true): Only return active streams

**Response:**
```json
[
  {
    "id": "192.168.1.100:266f",
    "session_name": "Studio A Main",
    "session_info": null,
    "multicast_addr": "239.69.1.10",
    "port": 5004,
    "sample_rate": 48000,
    "channels": 2,
    "encoding": "L24",
    "payload_type": 96,
    "samples_per_packet": 48,
    "ptime": 1.0,
    "source_addr": null,
    "is_ssm": false,
    "channel_labels": ["L", "R"],
    "channel_order_raw": "SMPTE2110.(ST)",
    "origin_ip": "192.168.1.100",
    "first_seen": "2026-02-01T10:00:00.000000",
    "last_seen": "2026-02-01T10:05:00.000000",
    "active": true,
    "ptp_grandmaster": "00-1D-C1-FF-FE-12-34-56",
    "ptp_domain": 0,
    "mediaclk": "direct=0",
    "is_st2110_compliant": true,
    "conformance_level": "A"
  }
]
```

#### GET `/api/discovery/streams/{stream_id}`

Get details for a specific stream.

#### GET `/api/discovery/streams/{stream_id}/sdp`

Get the raw SDP for a stream.

**Response:**
```json
{
  "stream_id": "192.168.1.100:266f",
  "session_name": "Studio A Main",
  "sdp": "v=0\r\no=- 123456 123456 IN IP4 192.168.1.100\r\n..."
}
```

#### POST `/api/sources/from-discovery`

Create an audio source from a discovered stream.

**Request Body:**
```json
{
  "stream_id": "192.168.1.100:266f",
  "name": "Studio A Input",
  "description": "Optional description",
  "channels": 2,
  "channel_offset": 0,
  "stream_channels": null
}
```

**Response:**
```json
{
  "id": "709a6eac",
  "name": "Studio A Input",
  "multicast_addr": "239.69.1.10",
  "port": 5004,
  "sample_rate": 48000,
  "channels": 2,
  "payload_type": 96,
  "samples_per_packet": 48,
  "description": "Imported from SAP discovery",
  "enabled": true,
  "discovered_name": "Studio A Main"
}
```

---

## SAP Announcer Tool

A testing tool is provided for sending SAP/SDP announcements:

### Location

```
tools/sap_announce.py
```

### Usage

```bash
# Basic stereo stream
python3 sap_announce.py --name "Studio A" --addr 239.69.1.10 --channels 2

# 5.1 surround with custom PTP
python3 sap_announce.py --name "Surround Mix" --addr 239.69.1.20 \
    --channels 6 --channel-config 51 \
    --ptp-gm "00-1D-C1-FF-FE-12-34-56" --ptp-domain 127

# Multiple stereo pairs (MADI-style)
python3 sap_announce.py --name "MADI Feed" --addr 239.69.1.30 \
    --channels 8 --channel-config "ST,ST,ST,ST" \
    --info "16-channel MADI bridge"

# 96kHz low latency
python3 sap_announce.py --name "Low Latency" --addr 239.69.1.40 \
    --rate 96000 --ptime 0.125

# Continuous announcements (every 30 seconds)
python3 sap_announce.py --name "Persistent Stream" --addr 239.69.1.50 \
    --interval 30
```

### Options

| Option | Default | Description |
|--------|---------|-------------|
| `--name` | "Test Stream" | Session name |
| `--info` | None | Session description (i= line) |
| `--addr` | 239.69.1.100 | Multicast address |
| `--port` | 5004 | RTP port |
| `--rate` | 48000 | Sample rate (44100, 48000, 96000) |
| `--channels` | 2 | Channel count |
| `--encoding` | L24 | Audio encoding (L16, L24) |
| `--ptime` | 1.0 | Packet time in ms (1.0, 0.125) |
| `--channel-config` | Auto | SMPTE2110 symbols (ST, 51, 71, etc.) |
| `--ptp-gm` | 00-00-... | PTP Grandmaster ID |
| `--ptp-domain` | 0 | PTP domain (0-127) |
| `--interval` | 0 | Send interval (0 = once) |
| `--origin` | 192.168.1.100 | Origin IP address |

### Example Output

```
SMPTE ST 2110-30 SAP Announcer
  Session:      Studio A Main
  Address:      239.69.1.10:5004
  Format:       L24 @ 48000 Hz, 2 ch, 1.0ms
  Channel Cfg:  SMPTE2110.(ST)
  Conformance:  Level A
  PTP GM:       00-1D-C1-FF-FE-12-34-56:0
  Sending to:   239.255.255.255:9875

[1] Sent 299 bytes
```

---

## Network Requirements

### Multicast Addresses

| Address | Port | Purpose |
|---------|------|---------|
| 239.255.255.255 | 9875 | SAP announcements (administratively scoped) |
| 224.2.127.254 | 9875 | SAP announcements (global scope, optional) |
| 239.x.x.x | 5004+ | AES67/ST 2110 audio streams |

### Switch Configuration

For SAP discovery to function properly, network switches must:

1. **Enable IGMP Snooping**: Prevents multicast flooding
2. **Configure IGMP Querier**: Ensures group membership is maintained
3. **Allow Multicast Routing**: If discovery spans VLANs

### Firewall Rules

```bash
# SAP discovery (multicast)
sudo ufw allow 9875/udp comment "SAP stream discovery"

# Or with iptables
iptables -A INPUT -p udp -d 239.255.255.255 --dport 9875 -j ACCEPT
iptables -A INPUT -p igmp -j ACCEPT
```

---

## Troubleshooting

### Discovery Not Starting

**Symptom:** POST to `/api/discovery/start` fails or returns error.

**Causes and Solutions:**

1. **Port already in use:**
   ```bash
   # Check what's using port 9875
   ss -ulnp | grep 9875
   ```
   Solution: Stop conflicting service or enable SO_REUSEPORT.

2. **Multicast not supported:**
   ```bash
   # Check interface supports multicast
   ip link show eth0 | grep MULTICAST
   ```
   Solution: Enable multicast on interface or use different interface.

### No Streams Discovered

**Symptom:** Discovery running but no streams appear.

**Causes and Solutions:**

1. **No SAP announcements on network:**
   ```bash
   # Listen for SAP packets
   tcpdump -i eth0 udp port 9875
   ```
   Solution: Verify AES67 devices are configured to send SAP.

2. **Multicast not reaching host:**
   ```bash
   # Check IGMP group membership
   cat /proc/net/igmp | grep eth0
   ```
   Solution: Check switch IGMP snooping configuration.

3. **Firewall blocking:**
   ```bash
   # Check iptables
   iptables -L -n | grep 9875
   ```
   Solution: Add firewall rule for UDP port 9875.

### Streams Disappearing

**Symptom:** Streams appear briefly then become inactive.

**Causes and Solutions:**

1. **SAP announcements not repeating:**
   - AES67 devices should announce every 30-300 seconds
   - Check device SAP configuration

2. **Cleanup timeout too short:**
   - Default timeout is 5 minutes
   - Streams are marked inactive if no announcement received

### WSL2 Multicast Issues

When running in WSL2, multicast loopback may not work. Solutions:

1. **Enable mirrored networking:**
   Create `C:\Users\<username>\.wslconfig`:
   ```ini
   [wsl2]
   networkingMode=mirrored
   ```
   Then restart WSL: `wsl --shutdown`

2. **Use SO_REUSEPORT:**
   Allows multiple processes to bind to the same port (already implemented).

---

## References

- [RFC 8866 — SDP: Session Description Protocol](https://datatracker.ietf.org/doc/html/rfc8866)
- [RFC 2974 — Session Announcement Protocol](https://datatracker.ietf.org/doc/html/rfc2974)
- [SMPTE ST 2110-10 — System Timing and Definitions](https://pub.smpte.org/pub/st2110-10/)
- [SMPTE ST 2110-30 — PCM Digital Audio](https://pub.smpte.org/pub/st2110-30/)
- [AES67 Standard](https://www.aes.org/publications/standards/search.cfm?docID=96)
- [AIMS Alliance — AES67 & ST 2110 Explained](https://www.aimsalliance.org/)
