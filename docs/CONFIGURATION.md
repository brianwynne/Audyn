# Audyn Configuration Guide

This guide covers all configuration options for the Audyn audio capture system, including command-line options, environment variables, and web application settings.

## Table of Contents

1. [Command-Line Options](#command-line-options)
2. [Archive Configuration](#archive-configuration)
3. [Audio Parameters](#audio-parameters)
4. [PTP Configuration](#ptp-configuration)
5. [Web Application Configuration](#web-application-configuration)
6. [Environment Variables](#environment-variables)
7. [Configuration Examples](#configuration-examples)
8. [Best Practices](#best-practices)

---

## Command-Line Options

### Output Options

| Option | Description | Default |
|--------|-------------|---------|
| `-o <path>` | Single file output path | None |
| `--archive-root <dir>` | Archive root directory | None |

**Note:** You must specify either `-o` or `--archive-root`, but not both.

### Archive Options

| Option | Description | Default |
|--------|-------------|---------|
| `--archive-layout <L>` | File naming layout | `flat` |
| `--archive-format <F>` | Custom strftime format | None |
| `--archive-period <S>` | Rotation period (seconds) | `3600` |
| `--archive-clock <C>` | Clock source | `localtime` |
| `--archive-suffix <S>` | File extension | `wav` |

### Input Options

| Option | Description | Default |
|--------|-------------|---------|
| `--pipewire` | Use PipeWire input | AES67 |

### AES67 Options

| Option | Description | Default |
|--------|-------------|---------|
| `-m <ip>` | Multicast/source IP | Required |
| `-p <port>` | UDP port | `5004` |
| `--pt <type>` | RTP payload type | `96` |
| `--spp <frames>` | Samples per packet | `48` |
| `--rcvbuf <bytes>` | Socket buffer size | `2097152` |
| `--interface <if>` | Bind to network interface | All interfaces |

### PTP Options

| Option | Description | Default |
|--------|-------------|---------|
| `--ptp-device <path>` | PHC device path | None |
| `--ptp-interface <if>` | Network interface | None |
| `--ptp-software` | Software PTP mode | Off |

### Audio Parameters

| Option | Description | Default |
|--------|-------------|---------|
| `-r <rate>` | Sample rate (Hz) | `48000` |
| `-c <channels>` | Channel count | `2` |

### Opus Encoding

| Option | Description | Default |
|--------|-------------|---------|
| `--bitrate <bps>` | Target bitrate | `128000` |
| `--vbr` | Variable bitrate | Enabled |
| `--cbr` | Constant bitrate | Disabled |
| `--complexity <n>` | Complexity (0-10) | `5` |

### Buffer Tuning

| Option | Description | Default |
|--------|-------------|---------|
| `-Q <cap>` | Queue capacity | `1024` |
| `-P <cap>` | Pool frame count | `256` |
| `-F <size>` | Frame size (samples) | `1024` |

### Logging

| Option | Description | Default |
|--------|-------------|---------|
| `-v` | Debug logging | Info |
| `-q` | Errors only | Info |
| `--syslog` | Log to syslog | Disabled |

---

## Archive Configuration

### Layout Types

#### Flat Layout
All files in root directory with date-hour names.

```
--archive-layout flat
```

Output: `/archive/2026-01-10-14.opus`

#### Hierarchy Layout
Deep directory structure by date components.

```
--archive-layout hierarchy
```

Output: `/archive/2026/01/10/14/archive.opus`

#### Combo Layout
Hierarchy with full timestamp in filename.

```
--archive-layout combo
```

Output: `/archive/2026/01/10/14/2026-01-10-14.opus`

#### Daily Directory Layout
One directory per day, files named with full timestamp.

```
--archive-layout dailydir
```

Output: `/archive/2026-01-10/2026-01-10-14.opus`

**This is the recommended layout for most broadcast applications.**

#### Accurate Layout
Sub-hourly precision with minute/second in filename.

```
--archive-layout accurate --archive-period 1800
```

Output: `/archive/2026-01-10/2026-01-10-14-30-00-00.opus`

#### Custom Layout
User-defined strftime format string.

```
--archive-layout custom --archive-format "%Y/week%W/%A_%H"
```

Output: `/archive/2026/week02/Friday_14.opus`

### Rotation Periods

| Period | Use Case |
|--------|----------|
| `3600` (1 hour) | Standard broadcast logging |
| `1800` (30 min) | Higher granularity |
| `900` (15 min) | Short segments |
| `86400` (24 hour) | Daily files |
| `0` | No rotation (single file) |

### Clock Sources

| Clock | Description | Use Case |
|-------|-------------|----------|
| `localtime` | System local time | Most applications |
| `utc` | UTC time | International operations |
| `ptp` | PTP/TAI time | Precision timing |

---

## Audio Parameters

### Sample Rates

| Rate | Description |
|------|-------------|
| `44100` | CD quality |
| `48000` | Professional audio (recommended) |
| `96000` | High resolution |

### Channel Configurations

| Channels | Description |
|----------|-------------|
| `1` | Mono |
| `2` | Stereo |

### Output Formats

#### WAV (PCM16)
- Uncompressed, highest quality
- ~10 MB per minute (stereo 48kHz)
- Universal compatibility

#### Opus
- Compressed, excellent quality
- ~1 MB per minute at 128 kbps
- Open format, lower storage requirements

### Opus Quality Settings

| Bitrate | Quality | Use Case |
|---------|---------|----------|
| `64000` | Good | Voice, talk radio |
| `96000` | Very Good | Music with limited bandwidth |
| `128000` | Excellent | General purpose (recommended) |
| `192000` | Transparent | Critical listening |
| `256000` | Maximum | Archival quality |

| Complexity | Speed | Quality |
|------------|-------|---------|
| `0-2` | Fastest | Lower |
| `5` | Balanced | Good (default) |
| `8-10` | Slowest | Highest |

---

## PTP Configuration

### Hardware PTP Mode

For NICs with IEEE 1588 hardware timestamping:

```bash
# Using device path
audyn --ptp-device /dev/ptp0 ...

# Using network interface (auto-discovers PHC)
audyn --ptp-interface enp1s0 ...
```

### Software PTP Mode

For NICs without hardware PTP support:

```bash
audyn --ptp-software ...
```

Requires linuxptp `ptp4l` to synchronize system clock.

### Verifying PTP Support

```bash
# Check hardware capabilities
ethtool -T enp1s0

# Required capabilities for hardware mode:
#   hardware-transmit
#   hardware-receive
#   hardware-raw-clock
```

---

## Web Application Configuration

### Backend Configuration

#### Environment Variables

| Variable | Description | Default |
|----------|-------------|---------|
| `AUDYN_DEV_MODE` | Development mode | `true` |
| `ENTRA_TENANT_ID` | Azure AD tenant ID | None |
| `ENTRA_CLIENT_ID` | Azure AD client ID | None |
| `ENTRA_CLIENT_SECRET` | Azure AD client secret | None |
| `ENTRA_REDIRECT_URI` | OAuth redirect URI | `http://localhost:8000/auth/callback` |

#### Development Mode

When `AUDYN_DEV_MODE=true`:
- Authentication is bypassed
- Default admin user is automatically logged in
- No Entra ID configuration required

#### Production Mode

When `AUDYN_DEV_MODE=false`:
- Entra ID SSO is required
- All API endpoints require valid JWT tokens
- User roles are determined by Entra ID claims

### Frontend Configuration

#### Vite Configuration (`vite.config.js`)

```javascript
export default defineConfig({
  plugins: [vue(), vuetify()],
  server: {
    proxy: {
      '/api': 'http://localhost:8000',
      '/auth': 'http://localhost:8000',
      '/ws': {
        target: 'ws://localhost:8000',
        ws: true
      }
    }
  }
})
```

### CORS Configuration

In `app/main.py`:

```python
app.add_middleware(
    CORSMiddleware,
    allow_origins=["http://localhost:5173", "http://localhost:3000"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)
```

For production, update `allow_origins` to your actual domain.

---

## Persisted Configuration Files

The web application stores all configuration settings in JSON files located in `~/.config/audyn/`. These files are automatically created and updated as you configure the system through the web interface.

### Configuration Directory

```
~/.config/audyn/
├── global.json         # Archive and capture settings
├── recorders.json      # Recorder instances and assignments
├── studios.json        # Studio definitions
├── sources.json        # AES67 source configurations
├── auth.json           # Authentication settings (excl. secrets)
├── system.json         # System settings (hostname, timezone, NTP)
├── ssl.json            # SSL certificate configuration
├── network.json        # Control interface network configuration
└── aes67_network.json  # AES67 interface network configuration
```

### global.json

Stores global capture configuration settings.

```json
{
  "archive_root": "/home/user/audyn-archive",
  "source_type": "aes67",
  "format": "opus",
  "bitrate": 128000,
  "sample_rate": 48000,
  "channels": 2,
  "archive_layout": "dailydir",
  "archive_period": 3600,
  "archive_clock": "localtime"
}
```

### recorders.json

Stores recorder instance configurations and studio assignments.

```json
{
  "active_count": 6,
  "recorders": {
    "1": {
      "name": "Recorder 1",
      "enabled": true,
      "studio_id": "studio-a",
      "multicast_addr": "239.69.1.1",
      "port": 5004,
      "archive_path": "/home/user/audyn-archive/studio-a"
    }
  }
}
```

### studios.json

Stores studio definitions and their properties.

```json
{
  "studios": {
    "studio-a": {
      "name": "Studio A",
      "description": "Main broadcast studio",
      "color": "#F44336",
      "enabled": true,
      "recorder_id": 1
    }
  }
}
```

### sources.json

Stores AES67 source configurations.

```json
{
  "active_source_id": "default",
  "sources": {
    "default": {
      "id": "default",
      "name": "Default AES67",
      "multicast_addr": "239.69.1.1",
      "port": 5004,
      "sample_rate": 48000,
      "channels": 2,
      "enabled": true
    }
  }
}
```

### auth.json

Stores authentication configuration (excluding secrets).

```json
{
  "entra_tenant_id": "your-tenant-id",
  "entra_client_id": "your-client-id",
  "entra_redirect_uri": "http://localhost:8000/auth/callback",
  "breakglass_password_hash": "$2b$12$..."
}
```

**Security Note:** The `auth.json` file stores bcrypt-hashed passwords, not plaintext. Client secrets should remain in environment variables.

### system.json

Stores system configuration settings.

```json
{
  "hostname": "audyn-recorder",
  "timezone": "Europe/London",
  "ntp_servers": ["pool.ntp.org", "time.google.com"]
}
```

| Field | Description |
|-------|-------------|
| `hostname` | System hostname (requires restart to apply) |
| `timezone` | System timezone (e.g., "UTC", "Europe/London") |
| `ntp_servers` | List of NTP servers for time synchronization |

### ssl.json

Stores SSL certificate configuration.

```json
{
  "enabled": true,
  "domain": "audyn.example.com",
  "email": "admin@example.com",
  "auto_renew": true,
  "cert_type": "letsencrypt",
  "cert_expiry": "2026-04-10T00:00:00Z"
}
```

| Field | Description |
|-------|-------------|
| `enabled` | Whether SSL is enabled |
| `domain` | Domain name for the certificate |
| `email` | Contact email for Let's Encrypt notifications |
| `auto_renew` | Enable automatic certificate renewal |
| `cert_type` | Certificate type: "letsencrypt" or "manual" |
| `cert_expiry` | Certificate expiration date |

**Certificate Types:**
- **Let's Encrypt**: Automatic certificate issuance (requires port 80 internet access)
- **Manual**: Upload your own certificate and private key (PEM format)

### network.json

Stores control interface network configuration.

```json
{
  "interface": "eth0",
  "bind_services": true,
  "network": {
    "interface": "eth0",
    "mode": "static",
    "ip_address": "192.168.1.100",
    "netmask": "255.255.255.0",
    "gateway": "192.168.1.1",
    "dns_servers": ["8.8.8.8", "8.8.4.4"]
  }
}
```

| Field | Description |
|-------|-------------|
| `interface` | Network interface name (e.g., "eth0") |
| `bind_services` | Bind web interface to this IP only |
| `network.mode` | "dhcp" or "static" |
| `network.ip_address` | Static IP address |
| `network.netmask` | Subnet mask |
| `network.gateway` | Default gateway |
| `network.dns_servers` | List of DNS servers |

### aes67_network.json

Stores AES67 interface network configuration.

```json
{
  "interface": "eth1",
  "network": {
    "interface": "eth1",
    "mode": "static",
    "ip_address": "192.168.2.100",
    "netmask": "255.255.255.0",
    "gateway": null,
    "dns_servers": []
  }
}
```

| Field | Description |
|-------|-------------|
| `interface` | AES67 network interface name |
| `network.mode` | "dhcp" or "static" |
| `network.ip_address` | Static IP address for AES67 network |
| `network.netmask` | Subnet mask |
| `network.gateway` | Usually not needed for AES67 |
| `network.dns_servers` | Usually empty for AES67 |

**Note:** AES67 networks are typically isolated and don't require a gateway or DNS servers.

### Configuration Precedence

Settings are loaded in this order (later values override earlier):

1. **Defaults** - Built-in application defaults
2. **Persisted Config** - Values from `~/.config/audyn/` files
3. **Environment Variables** - `AUDYN_*` and `ENTRA_*` variables

This allows environment variables to override persisted settings for testing or deployment flexibility.

---

## Environment Variables

### Core Engine

| Variable | Description |
|----------|-------------|
| `AUDYN_LOG_LEVEL` | Log level (debug, info, warn, error) |

### Web Backend

| Variable | Description |
|----------|-------------|
| `AUDYN_DEV_MODE` | Enable development mode |
| `AUDYN_ARCHIVE_ROOT` | Default archive root path |
| `ENTRA_TENANT_ID` | Azure AD tenant ID |
| `ENTRA_CLIENT_ID` | Azure AD application ID |
| `ENTRA_CLIENT_SECRET` | Azure AD client secret |
| `ENTRA_REDIRECT_URI` | OAuth callback URL |

### Example .env File

```bash
# Development settings
AUDYN_DEV_MODE=true
AUDYN_ARCHIVE_ROOT=/var/lib/audyn

# Production settings (uncomment for production)
# AUDYN_DEV_MODE=false
# ENTRA_TENANT_ID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
# ENTRA_CLIENT_ID=xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
# ENTRA_CLIENT_SECRET=your-client-secret
# ENTRA_REDIRECT_URI=https://audyn.example.com/auth/callback
```

---

## Configuration Examples

### 24/7 Broadcast Logger

```bash
audyn \
  --archive-root /mnt/archive \
  --archive-layout dailydir \
  --archive-suffix opus \
  --archive-period 3600 \
  --archive-clock utc \
  -m 239.69.1.1 \
  -p 5004 \
  -r 48000 \
  -c 2 \
  --bitrate 128000 \
  --ptp-interface enp1s0
```

### High-Quality Music Recording

```bash
audyn \
  -o "/recordings/$(date +%Y%m%d_%H%M%S).wav" \
  -m 239.69.1.1 \
  -p 5004 \
  -r 48000 \
  -c 2
```

### Voice Recording (Lower Bitrate)

```bash
audyn \
  --archive-root /var/lib/audyn/voice \
  --archive-layout flat \
  --archive-suffix opus \
  -m 239.69.1.2 \
  --bitrate 64000 \
  -c 1
```

### Local Audio Test

```bash
audyn \
  --pipewire \
  -o /tmp/test-recording.opus \
  --bitrate 96000
```

### Multiple Recorders (systemd)

Create separate service files for each recorder:

```bash
# /etc/systemd/system/audyn@.service
[Unit]
Description=Audyn Recorder %i
After=network.target

[Service]
Type=simple
EnvironmentFile=/etc/audyn/recorder-%i.conf
ExecStart=/usr/bin/audyn \
  --archive-root ${ARCHIVE_ROOT} \
  --archive-layout dailydir \
  --archive-suffix opus \
  -m ${MULTICAST_ADDR} \
  -p ${PORT}
Restart=always

[Install]
WantedBy=multi-user.target
```

```bash
# /etc/audyn/recorder-1.conf
ARCHIVE_ROOT=/var/lib/audyn/studio-a
MULTICAST_ADDR=239.69.1.1
PORT=5004

# /etc/audyn/recorder-2.conf
ARCHIVE_ROOT=/var/lib/audyn/studio-b
MULTICAST_ADDR=239.69.1.2
PORT=5004
```

```bash
# Enable recorders
sudo systemctl enable audyn@1 audyn@2
sudo systemctl start audyn@1 audyn@2
```

---

## Best Practices

### Storage

1. **Use fast storage**: SSD recommended for continuous recording
2. **Separate partitions**: Use dedicated partition for recordings
3. **Monitor disk space**: Set up alerts before disk fills
4. **Use Opus for long-term**: 10x smaller than WAV

### Network

1. **Dedicated VLAN**: Isolate AES67 traffic
2. **QoS configuration**: Prioritize audio packets
3. **Multicast optimization**: Enable IGMP snooping

### PTP

1. **Use hardware PTP** when available for best accuracy
2. **Verify synchronization** before critical recordings
3. **Monitor PTP offset** in production

### Security

1. **Use Entra ID** in production (disable dev mode)
2. **Configure CORS** properly for your domain
3. **Use HTTPS** in production with valid certificates
4. **Restrict network access** to authorized users

### Monitoring

1. **Check audio levels** via WebSocket meters
2. **Monitor file sizes** for silence detection
3. **Set up log aggregation** for troubleshooting
4. **Use systemd watchdog** for automatic restarts

---

## Next Steps

- [API Reference](API.md) - REST API documentation
- [User Guide](USER_GUIDE.md) - Web interface guide
- [File Reference](FILE_REFERENCE.md) - Source code documentation
