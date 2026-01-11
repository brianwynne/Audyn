# Audyn Debian Package

This directory contains the files needed to build a Debian package (.deb) for Audyn.

## Quick Build

```bash
./build-deb.sh 1.0.0
```

This produces `audyn_1.0.0_amd64.deb`.

## Installation

```bash
# Install the package (will pull dependencies automatically)
sudo apt install ./audyn_1.0.0_amd64.deb
```

## First Login

After installation, access the web interface at `http://<management-ip>/`

**Default breakglass password:** `audyn`

**Change this password immediately** via Settings > Authentication.

## What Gets Installed

| Path | Description |
|------|-------------|
| `/usr/bin/audyn` | The audio capture binary |
| `/opt/audyn/backend/` | Python FastAPI backend |
| `/opt/audyn/frontend/` | Vue.js web interface |
| `/etc/audyn/` | Configuration files |
| `/var/lib/audyn/archive/` | Default recording archive |
| `/var/log/audyn/` | Log files |
| `/lib/systemd/system/audyn-web.service` | systemd service |
| `/etc/nginx/sites-available/audyn` | nginx configuration |

## Services

- **audyn-web.service** - FastAPI backend (localhost:8000)
- **nginx** - Serves frontend, proxies API

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Management Network                        │
│                     (Web UI Access)                          │
└─────────────────────┬───────────────────────────────────────┘
                      │
                      ▼
              ┌───────────────┐
              │    nginx      │ :80/:443
              │  (frontend +  │
              │  API proxy)   │
              └───────┬───────┘
                      │ proxy to localhost
                      ▼
              ┌───────────────┐
              │ audyn-web     │ 127.0.0.1:8000
              │ (FastAPI)     │
              └───────┬───────┘
                      │ spawns
                      ▼
              ┌───────────────┐
              │    audyn      │
              │  (recorder)   │
              └───────┬───────┘
                      │
┌─────────────────────┴───────────────────────────────────────┐
│                     AES67 Network                            │
│               (Dedicated Audio Interface)                    │
└─────────────────────────────────────────────────────────────┘
```

## Security

- **API bound to localhost only** - Not accessible from network
- **nginx proxies requests** - Frontend/API served through nginx
- **Entra ID authentication** - Microsoft SSO with role-based access
- **Breakglass password** - Emergency admin access
- **Dedicated service user** - `audyn` user with minimal privileges

## Network Configuration

The appliance expects two network interfaces:

1. **Management interface** - Web UI access (nginx listens on all interfaces)
2. **AES67 interface** - Audio network (multicast capture)

Configure the AES67 interface in Settings > Audio Source.

## Configuration

All configuration is done through the web interface:

1. Open `http://<management-ip>/` in a browser
2. Log in with Entra ID or breakglass credentials
3. Configure:
   - **Settings** - Archive location, naming format, PTP
   - **Sources** - AES67 multicast addresses
   - **Studios** - Studio definitions and assignments
   - **Authentication** - Entra ID and breakglass password

## Logs

```bash
# Backend logs
sudo journalctl -u audyn-web -f

# Or log files
sudo tail -f /var/log/audyn/web.log
sudo tail -f /var/log/audyn/web-error.log

# nginx access/error logs
sudo tail -f /var/log/nginx/access.log
sudo tail -f /var/log/nginx/error.log
```

## Uninstall

```bash
# Remove but keep config and archives
sudo apt remove audyn

# Remove everything (except archives - removed manually)
sudo apt purge audyn
```

## Build Dependencies

To build the package, you need:

```bash
sudo apt install build-essential dpkg-dev
sudo apt install libpipewire-0.3-dev libspa-0.2-dev libopus-dev libogg-dev
sudo apt install python3 python3-venv nodejs npm
```

## Runtime Dependencies

Installed automatically by apt:

- python3 (>= 3.10)
- nginx
- pipewire
- libopus0
- libogg0
