# Audyn Installation Guide

This guide provides detailed instructions for installing Audyn on Ubuntu 22.04 LTS and Debian 12+.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Installation Methods](#installation-methods)
3. [Building from Source](#building-from-source)
4. [Installing from Debian Package](#installing-from-debian-package)
5. [Web Application Setup](#web-application-setup)
6. [Docker Deployment](#docker-deployment)
7. [PTP Clock Configuration](#ptp-clock-configuration)
8. [Network Configuration](#network-configuration)
9. [Verification](#verification)
10. [Troubleshooting](#troubleshooting)

---

## Prerequisites

### Operating System

- Ubuntu 22.04 LTS or later (recommended)
- Debian 12 (Bookworm) or later
- Linux kernel 5.15 or later (for optimal PTP support)

### Hardware Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| CPU | 2 cores | 4+ cores |
| RAM | 2 GB | 4+ GB |
| Storage | 10 GB | 100+ GB SSD |
| Network | 1 Gbps Ethernet | Intel i225/i226 with PTP |

### Required Packages

```bash
# Update package lists
sudo apt update

# Build essentials
sudo apt install -y build-essential pkg-config git

# Audio libraries
sudo apt install -y libpipewire-0.3-dev libopus-dev libogg-dev

# PTP support (optional but recommended)
sudo apt install -y linuxptp ethtool
```

---

## Installation Methods

Audyn can be installed in three ways:

| Method | Best For | Complexity |
|--------|----------|------------|
| From Source | Development, customization | Medium |
| Debian Package | Production deployment | Low |
| Docker | Isolated deployment | Low |

---

## Building from Source

### Step 1: Clone the Repository

```bash
git clone https://github.com/brianwynne/Audyn.git
cd Audyn
```

### Step 2: Verify Dependencies

```bash
make check-deps
```

Expected output:
```
Checking dependencies...
  libpipewire-0.3: OK
  opus: OK
  ogg: OK
```

If any dependencies are missing, install them:

```bash
# If libpipewire-0.3 is missing
sudo apt install libpipewire-0.3-dev

# If opus is missing
sudo apt install libopus-dev

# If ogg is missing
sudo apt install libogg-dev
```

### Step 3: Build

```bash
# Standard build
make

# Or for optimized release build
make release
```

### Step 4: Install

```bash
# Install to /usr/local/bin
sudo make install

# Verify installation
audyn --help
```

### Step 5: Install systemd Service (Optional)

```bash
# Copy service file
sudo cp packaging/audyn.service /etc/systemd/system/

# Create configuration directory
sudo mkdir -p /etc/audyn

# Create default configuration
sudo tee /etc/audyn/audyn.conf << 'EOF'
# Audyn Configuration
MULTICAST_ADDR=239.69.1.1
PORT=5004
ARCHIVE_ROOT=/var/lib/audyn
ARCHIVE_LAYOUT=dailydir
ARCHIVE_SUFFIX=opus
EOF

# Create archive directory
sudo mkdir -p /var/lib/audyn
sudo chown $USER:$USER /var/lib/audyn

# Enable and start service
sudo systemctl daemon-reload
sudo systemctl enable audyn
sudo systemctl start audyn
```

---

## Installing from Debian Package

### Download Package

Download the latest `.deb` package from [GitHub Releases](https://github.com/brianwynne/Audyn/releases):

```bash
# For AMD64
wget https://github.com/brianwynne/Audyn/releases/latest/download/audyn_1.0.0_amd64.deb

# For ARM64
wget https://github.com/brianwynne/Audyn/releases/latest/download/audyn_1.0.0_arm64.deb
```

### Install Package

```bash
# Install with dependencies
sudo apt install ./audyn_1.0.0_amd64.deb

# Or for ARM64
sudo apt install ./audyn_1.0.0_arm64.deb
```

### Verify Installation

```bash
# Check version
audyn --help

# Check service status
sudo systemctl status audyn
```

### Building the Package Locally

```bash
# Install build dependencies
sudo apt install debhelper devscripts

# Build package
dpkg-buildpackage -us -uc -b

# Package will be in parent directory
ls ../audyn_*.deb
```

---

## Web Application Setup

### Backend Setup

```bash
# Navigate to backend directory
cd web/backend

# Create virtual environment
python3 -m venv venv

# Activate virtual environment
source venv/bin/activate

# Install dependencies
pip install -r requirements.txt
```

#### Create requirements.txt (if not exists)

```bash
cat > requirements.txt << 'EOF'
fastapi>=0.104.0
uvicorn[standard]>=0.24.0
pydantic>=2.0.0
httpx>=0.25.0
PyJWT>=2.8.0
python-multipart>=0.0.6
websockets>=12.0
aiofiles>=23.0.0
EOF

pip install -r requirements.txt
```

### Frontend Setup

```bash
# Navigate to frontend directory
cd web/frontend

# Install Node.js dependencies
npm install

# Build for production (optional)
npm run build
```

### Running the Application

#### Development Mode

```bash
# Terminal 1: Start backend
cd web/backend
source venv/bin/activate
uvicorn app.main:app --reload --host 0.0.0.0 --port 8000

# Terminal 2: Start frontend
cd web/frontend
npm run dev
```

Access the application at `http://localhost:5173`

#### Production Mode

```bash
# Build frontend
cd web/frontend
npm run build

# Start backend with production settings
cd web/backend
source venv/bin/activate
uvicorn app.main:app --host 0.0.0.0 --port 8000 --workers 4
```

### Environment Variables

```bash
# Create .env file for production
cat > web/backend/.env << 'EOF'
# Development mode (set to "false" for production)
AUDYN_DEV_MODE=true

# Entra ID Configuration (production only)
ENTRA_TENANT_ID=your-tenant-id
ENTRA_CLIENT_ID=your-client-id
ENTRA_CLIENT_SECRET=your-client-secret
ENTRA_REDIRECT_URI=https://your-domain.com/auth/callback
EOF
```

---

## Docker Deployment

### Using Docker Compose

```bash
cd web

# Start all services
docker-compose up -d

# View logs
docker-compose logs -f

# Stop services
docker-compose down
```

### Docker Compose Configuration

```yaml
# web/docker-compose.yml
version: '3.8'

services:
  backend:
    build: ./backend
    ports:
      - "8000:8000"
    volumes:
      - /var/lib/audyn:/var/lib/audyn:ro
    environment:
      - AUDYN_DEV_MODE=true

  frontend:
    build: ./frontend
    ports:
      - "80:80"
    depends_on:
      - backend
```

---

## PTP Clock Configuration

### Hardware PTP (Recommended)

For NICs with hardware PTP support (Intel i225, i226, Mellanox, etc.):

```bash
# Check PTP capability
ethtool -T enp1s0

# Expected output should show:
#   hardware-transmit
#   hardware-receive
#   hardware-raw-clock

# Find PHC device
ls /dev/ptp*

# Start PTP daemon
sudo ptp4l -i enp1s0 -m -H

# Use hardware clock with Audyn
audyn --ptp-interface enp1s0 --archive-clock ptp ...
```

### Software PTP

For NICs without hardware PTP:

```bash
# Start PTP in software mode
sudo ptp4l -i enp1s0 -m -S

# Sync system clock to PTP
sudo phc2sys -s CLOCK_REALTIME -c enp1s0 -O 0

# Use software clock with Audyn
audyn --ptp-software --archive-clock ptp ...
```

### PTP Configuration File

```bash
# /etc/ptp4l.conf
sudo tee /etc/ptp4l.conf << 'EOF'
[global]
twoStepFlag             1
priority1               128
priority2               128
domainNumber            0
slaveOnly               1
time_stamping           hardware
network_transport       UDPv4
EOF

# Start with configuration
sudo ptp4l -f /etc/ptp4l.conf -i enp1s0 -m
```

---

## Network Configuration

### Multicast Setup

```bash
# Enable multicast routing
sudo sysctl -w net.ipv4.conf.all.force_igmp_version=2

# Add multicast route (if needed)
sudo ip route add 239.0.0.0/8 dev enp1s0

# Make persistent
echo "net.ipv4.conf.all.force_igmp_version=2" | sudo tee -a /etc/sysctl.conf
```

### Firewall Configuration

```bash
# Allow AES67 traffic
sudo ufw allow 5004/udp comment "AES67 RTP"
sudo ufw allow 319/udp comment "PTP Event"
sudo ufw allow 320/udp comment "PTP General"

# Allow web application
sudo ufw allow 8000/tcp comment "Audyn Backend"
sudo ufw allow 80/tcp comment "Audyn Frontend"
```

### Socket Buffer Tuning

```bash
# Increase socket buffer limits
sudo sysctl -w net.core.rmem_max=26214400
sudo sysctl -w net.core.rmem_default=26214400

# Make persistent
echo "net.core.rmem_max=26214400" | sudo tee -a /etc/sysctl.conf
echo "net.core.rmem_default=26214400" | sudo tee -a /etc/sysctl.conf
```

---

## Verification

### Test Core Engine

```bash
# Test with PipeWire (local audio)
audyn --pipewire -o /tmp/test.wav &
sleep 5
kill %1

# Check output
file /tmp/test.wav
# Should show: RIFF (little-endian) data, WAVE audio
```

### Test AES67 Input

```bash
# Start capture from multicast stream
audyn -m 239.69.1.1 -p 5004 -o /tmp/test-aes67.wav &
sleep 10
kill %1

# Check output
file /tmp/test-aes67.wav
```

### Test Web Application

```bash
# Check backend health
curl http://localhost:8000/health
# Expected: {"status":"healthy","service":"audyn-web"}

# Check authentication (dev mode)
curl http://localhost:8000/auth/me
# Expected: User JSON with dev user info
```

### Test WebSocket

```bash
# Install websocat
sudo apt install websocat

# Connect to levels WebSocket
websocat ws://localhost:8000/ws/levels

# Should receive JSON messages with audio levels
```

---

## Troubleshooting

### Common Issues

#### "libpipewire-0.3.so: cannot open shared object file"

```bash
# Install PipeWire development package
sudo apt install libpipewire-0.3-dev
```

#### "Permission denied: /dev/ptp0"

```bash
# Add user to appropriate group
sudo usermod -aG audio $USER

# Or set device permissions
sudo chmod 666 /dev/ptp0
```

#### "Address already in use" (port 8000)

```bash
# Find and kill process using port
sudo lsof -i :8000
sudo kill -9 <PID>
```

#### "No multicast packets received"

```bash
# Check multicast group membership
netstat -g | grep 239

# Check for packets
sudo tcpdump -i enp1s0 -n host 239.69.1.1

# Verify IGMP
cat /proc/net/igmp
```

#### PTP "timed out while polling for tx timestamp"

```bash
# Check if hardware timestamping is supported
ethtool -T enp1s0 | grep hardware

# If not supported, use software mode
sudo ptp4l -i enp1s0 -m -S
```

### Log Files

```bash
# View systemd service logs
sudo journalctl -u audyn -f

# View backend logs
tail -f web/backend/audyn.log

# Enable debug logging
audyn -v ...
```

### Getting Help

- **GitHub Issues**: [https://github.com/brianwynne/Audyn/issues](https://github.com/brianwynne/Audyn/issues)
- **Documentation**: Check other docs in this directory

---

## Next Steps

- [Configuration Guide](CONFIGURATION.md) - Configure Audyn for your environment
- [User Guide](USER_GUIDE.md) - Learn to use the web interface
- [API Reference](API.md) - Integrate with the REST API
