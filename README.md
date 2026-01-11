# Audyn

**Professional AES67 Audio Capture & Archival Engine**

Audyn is an enterprise-grade audio capture system designed for broadcast and professional audio environments. It provides reliable, high-quality recording of AES67 network audio streams with precise PTP timestamping, Rotter-compatible archive rotation, and a modern web interface for multi-recorder management.

## Key Features

- **AES67/RTP Audio Capture**: Native support for AES67 multicast and unicast streams
- **PTP Precision Timing**: Hardware and software PTP clock support for accurate timestamping
- **Multiple Output Formats**: WAV (PCM16) and Opus (Ogg) with configurable quality
- **Flexible Archive Rotation**: Rotter-compatible file naming with multiple layout options
- **Multi-Recorder Support**: Run up to 6 simultaneous recording instances
- **Studio Management**: Assign recorders to studios with role-based access control
- **Real-Time Monitoring**: Live audio level meters via WebSocket
- **Web Interface**: Modern Vue.js interface with Entra ID SSO support
- **PipeWire Fallback**: Local audio capture when AES67 is unavailable

## System Requirements

### Minimum Requirements

- Ubuntu 22.04 LTS or Debian 12+
- 2 CPU cores
- 2 GB RAM
- Network interface with multicast support

### Recommended for Production

- Ubuntu 22.04 LTS or later
- 4+ CPU cores
- 4+ GB RAM
- Intel i225/i226 NIC with hardware PTP support
- Dedicated storage for recordings

### Dependencies

```bash
# Core dependencies
sudo apt install build-essential pkg-config

# Audio libraries
sudo apt install libpipewire-0.3-dev libopus-dev libogg-dev

# Web application (optional)
sudo apt install python3 python3-pip python3-venv nodejs npm
```

## Quick Start

### Building from Source

```bash
git clone https://github.com/brianwynne/Audyn.git
cd Audyn
make
sudo make install
```

### Basic Recording

```bash
# Single file recording
audyn -o recording.wav -m 239.69.1.1 -p 5004

# Archive mode with hourly rotation
audyn --archive-root /var/lib/audyn --archive-layout dailydir \
      --archive-suffix opus -m 239.69.1.1
```

### Running as a Service

```bash
# Enable and start the systemd service
sudo systemctl enable audyn
sudo systemctl start audyn

# Check status
sudo systemctl status audyn
```

### Web Interface

```bash
cd web/backend
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt

# Start backend
uvicorn app.main:app --host 0.0.0.0 --port 8000

# In another terminal, start frontend
cd web/frontend
npm install
npm run dev
```

Access the web interface at `http://localhost:5173`

## Documentation

Comprehensive documentation is available in the [docs/](docs/) directory:

| Document | Description |
|----------|-------------|
| [Architecture](docs/ARCHITECTURE.md) | System design and component overview |
| [Installation](docs/INSTALLATION.md) | Detailed installation instructions |
| [Configuration](docs/CONFIGURATION.md) | Configuration options and examples |
| [API Reference](docs/API.md) | REST API and WebSocket documentation |
| [User Guide](docs/USER_GUIDE.md) | Web interface usage guide |
| [Developer Guide](docs/DEVELOPER.md) | Contributing and development setup |
| [File Reference](docs/FILE_REFERENCE.md) | Complete source file documentation |

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        AUDYN SYSTEM                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │  AES67/RTP   │    │  Audio Queue │    │  WAV/Opus    │       │
│  │    Input     │───>│  (Lock-free) │───>│    Sink      │       │
│  └──────────────┘    └──────────────┘    └──────────────┘       │
│         │                                        │               │
│         v                                        v               │
│  ┌──────────────┐                        ┌──────────────┐       │
│  │  PTP Clock   │                        │   Archive    │       │
│  │  (HW/SW)     │                        │   Policy     │       │
│  └──────────────┘                        └──────────────┘       │
│                                                                  │
├─────────────────────────────────────────────────────────────────┤
│                      WEB APPLICATION                             │
├─────────────────────────────────────────────────────────────────┤
│                                                                  │
│  ┌──────────────┐    ┌──────────────┐    ┌──────────────┐       │
│  │   Vue.js     │<──>│   FastAPI    │<──>│   Audyn      │       │
│  │   Frontend   │    │   Backend    │    │   Service    │       │
│  └──────────────┘    └──────────────┘    └──────────────┘       │
│         │                   │                                    │
│         v                   v                                    │
│  ┌──────────────┐    ┌──────────────┐                           │
│  │  WebSocket   │    │  Entra ID    │                           │
│  │  Levels      │    │  SSO Auth    │                           │
│  └──────────────┘    └──────────────┘                           │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

## Command Line Usage

```
audyn — AES67 Audio Capture & Archival Engine

Usage:
  audyn -o <file> [options]           Single file mode
  audyn --archive-root <dir> [options] Archive mode with rotation

Output (choose one):
  -o <path>              Output file path (single file, no rotation)
  --archive-root <dir>   Root directory for archive files

Archive Options:
  --archive-layout <L>   Layout: flat, hierarchy, combo, dailydir, accurate, custom
  --archive-period <S>   Rotation period in seconds (default: 3600)
  --archive-clock <C>    Clock: localtime, utc, ptp (default: localtime)
  --archive-suffix <S>   File suffix: wav, opus (default: wav)

AES67 Options:
  -m <ip>                Multicast/source IP address (required)
  -p <port>              UDP port (default: 5004)
  --pt <type>            RTP payload type (default: 96)
  --interface <if>       Bind to specific network interface (e.g., eth1)

PTP Options:
  --ptp-device <path>    Hardware PTP clock device
  --ptp-interface <if>   Discover PHC from network interface
  --ptp-software         Use software PTP (CLOCK_REALTIME)

Audio Parameters:
  -r <rate>              Sample rate (default: 48000)
  -c <channels>          Channels: 1 or 2 (default: 2)

Opus Options:
  --bitrate <bps>        Target bitrate (default: 128000)
  --vbr / --cbr          Variable or constant bitrate
  --complexity <n>       Encoder complexity 0-10 (default: 5)
```

## Archive Layouts

| Layout | Example Output |
|--------|----------------|
| `flat` | `/archive/2026-01-10-14.opus` |
| `hierarchy` | `/archive/2026/01/10/14/archive.opus` |
| `combo` | `/archive/2026/01/10/14/2026-01-10-14.opus` |
| `dailydir` | `/archive/2026-01-10/2026-01-10-14.opus` |
| `accurate` | `/archive/2026-01-10/2026-01-10-14-30-00-00.opus` |
| `custom` | User-defined strftime format |

## Web Interface Features

### For Administrators
- **Overview Dashboard**: Real-time audio meters for all recorders
- **Recorder Management**: Start/stop recorders, configure sources
- **Studio Configuration**: Create studios, assign recorders
- **Source Management**: Configure AES67 multicast sources
- **File Management**: Browse, play, download, and delete recordings
- **Network Configuration**: Configure control and AES67 network interfaces (DHCP/Static IP)
- **System Settings**: Hostname, timezone, NTP servers, SSL certificates
- **Recording Settings**: Archive paths, formats, rotation, PTP timing

### For Studio Users
- **Studio View**: Audio meters for assigned recorder
- **File Browser**: Access studio-specific recordings
- **Playback**: Preview recordings in browser
- **Download**: Export recordings for editing

## License

Copyright (c) 2026 B. Wynne

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

See [LICENSE](LICENSE) for the full license text.

## Contributing

Contributions are welcome! Please see [DEVELOPER.md](docs/DEVELOPER.md) for
development setup and contribution guidelines.

## Support

- **Issues**: [GitHub Issues](https://github.com/brianwynne/Audyn/issues)
- **Documentation**: [docs/](docs/)
