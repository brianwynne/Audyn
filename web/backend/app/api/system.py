"""
System Configuration API

Manage system settings: hostname, timezone, NTP, SSL certificates.
"""

import os
import subprocess
import logging
from typing import Optional
from datetime import datetime

from fastapi import APIRouter, Depends, HTTPException, File, UploadFile, Form
from pydantic import BaseModel

from ..auth.entra import get_current_user, User
from ..services.config_store import (
    load_system_config, save_system_config,
    load_ssl_config, save_ssl_config,
    load_network_config, save_network_config,
    load_aes67_network_config, save_aes67_network_config
)

logger = logging.getLogger(__name__)

router = APIRouter()


# ============ Models ============

class SystemConfig(BaseModel):
    """System configuration settings."""
    hostname: Optional[str] = None
    timezone: str = "UTC"
    ntp_servers: list[str] = ["pool.ntp.org"]


class PartialSystemConfig(BaseModel):
    """Partial system configuration for updates."""
    hostname: Optional[str] = None
    timezone: Optional[str] = None
    ntp_servers: Optional[list[str]] = None


class SSLConfig(BaseModel):
    """SSL certificate configuration."""
    enabled: bool = False
    domain: Optional[str] = None
    email: Optional[str] = None
    auto_renew: bool = True
    cert_expiry: Optional[str] = None
    last_renewed: Optional[str] = None
    cert_type: str = "none"  # "none", "letsencrypt", "manual"


class SSLEnableRequest(BaseModel):
    """Request to enable SSL."""
    domain: str
    email: str  # Email for Let's Encrypt notifications


class NetworkInterface(BaseModel):
    """Network interface information."""
    name: str
    ip_address: Optional[str] = None
    netmask: Optional[str] = None
    mac_address: Optional[str] = None
    is_up: bool = True


class NetworkConfig(BaseModel):
    """Network configuration for an interface."""
    interface: str  # Interface name (e.g., "eth0")
    mode: str = "dhcp"  # "dhcp" or "static"
    ip_address: Optional[str] = None
    netmask: Optional[str] = "255.255.255.0"
    gateway: Optional[str] = None
    dns_servers: list[str] = []


class ControlInterfaceConfig(BaseModel):
    """Control interface configuration."""
    interface: Optional[str] = None  # Which NIC is the control interface
    network: Optional[NetworkConfig] = None  # Network settings for that interface
    bind_services: bool = True  # Whether to bind nginx/backend to this interface only


class AES67InterfaceConfig(BaseModel):
    """AES67 interface configuration."""
    interface: Optional[str] = None  # Which NIC is the AES67 interface
    network: Optional[NetworkConfig] = None  # Network settings for that interface


class LogRotationConfig(BaseModel):
    """Log rotation configuration."""
    frequency: str = "monthly"  # daily, weekly, monthly
    rotate_count: int = 12  # Number of rotated logs to keep
    compress: bool = True  # Compress rotated logs
    max_size: Optional[str] = None  # Optional size-based rotation (e.g., "100M")


# ============ In-memory state ============

_system_config: Optional[SystemConfig] = None
_ssl_config: Optional[SSLConfig] = None
_control_interface_config: Optional[ControlInterfaceConfig] = None
_aes67_interface_config: Optional[AES67InterfaceConfig] = None


def _load_system_config_from_store():
    """Load system config from persistent storage."""
    global _system_config
    saved = load_system_config()
    if saved:
        try:
            _system_config = SystemConfig(**saved)
        except Exception as e:
            logger.error(f"Failed to parse saved system config: {e}")
            _system_config = SystemConfig()
    else:
        _system_config = SystemConfig()


def _load_ssl_config_from_store():
    """Load SSL config from persistent storage."""
    global _ssl_config
    saved = load_ssl_config()
    if saved:
        try:
            _ssl_config = SSLConfig(**saved)
        except Exception as e:
            logger.error(f"Failed to parse saved SSL config: {e}")
            _ssl_config = SSLConfig()
    else:
        _ssl_config = SSLConfig()


def _load_network_config_from_store():
    """Load control interface config from persistent storage."""
    global _control_interface_config
    saved = load_network_config()
    if saved:
        try:
            _control_interface_config = ControlInterfaceConfig(**saved)
        except Exception as e:
            logger.error(f"Failed to parse saved network config: {e}")
            _control_interface_config = ControlInterfaceConfig()
    else:
        _control_interface_config = ControlInterfaceConfig()


def _load_aes67_config_from_store():
    """Load AES67 interface config from persistent storage."""
    global _aes67_interface_config
    saved = load_aes67_network_config()
    if saved:
        try:
            _aes67_interface_config = AES67InterfaceConfig(**saved)
        except Exception as e:
            logger.error(f"Failed to parse saved AES67 network config: {e}")
            _aes67_interface_config = AES67InterfaceConfig()
    else:
        _aes67_interface_config = AES67InterfaceConfig()


# Load on module import
_load_system_config_from_store()
_load_ssl_config_from_store()
_load_network_config_from_store()
_load_aes67_config_from_store()


# ============ Helper functions ============

def get_network_interfaces() -> list[NetworkInterface]:
    """Get list of network interfaces with their addresses."""
    interfaces = []

    try:
        import netifaces
        for iface_name in netifaces.interfaces():
            # Skip loopback
            if iface_name == 'lo':
                continue

            addrs = netifaces.ifaddresses(iface_name)

            # Get IPv4 address
            ip_addr = None
            ipv4_addrs = addrs.get(netifaces.AF_INET, [])
            if ipv4_addrs:
                ip_addr = ipv4_addrs[0].get('addr')

            # Get MAC address
            mac_addr = None
            link_addrs = addrs.get(netifaces.AF_LINK, [])
            if link_addrs:
                mac_addr = link_addrs[0].get('addr')

            interfaces.append(NetworkInterface(
                name=iface_name,
                ip_address=ip_addr,
                mac_address=mac_addr,
                is_up=ip_addr is not None
            ))
    except ImportError:
        logger.warning("netifaces not installed, using fallback interface detection")
        # Fallback: parse /proc/net/dev on Linux
        try:
            with open('/proc/net/dev', 'r') as f:
                for line in f:
                    if ':' in line:
                        iface_name = line.split(':')[0].strip()
                        if iface_name != 'lo':
                            interfaces.append(NetworkInterface(
                                name=iface_name,
                                ip_address=None,
                                mac_address=None,
                                is_up=True
                            ))
        except Exception as e:
            logger.error(f"Failed to read interfaces: {e}")

    return interfaces


def get_available_timezones() -> list[str]:
    """Get list of available timezones."""
    try:
        import zoneinfo
        return sorted(zoneinfo.available_timezones())
    except ImportError:
        # Fallback: common timezones
        return [
            "UTC", "Europe/London", "Europe/Dublin", "Europe/Paris",
            "Europe/Berlin", "America/New_York", "America/Chicago",
            "America/Denver", "America/Los_Angeles", "Asia/Tokyo",
            "Asia/Shanghai", "Australia/Sydney"
        ]


def set_system_hostname(hostname: str) -> bool:
    """Set the system hostname (requires root)."""
    try:
        result = subprocess.run(
            ["hostnamectl", "set-hostname", hostname],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode == 0:
            logger.info(f"Hostname set to: {hostname}")
            return True
        else:
            logger.error(f"Failed to set hostname: {result.stderr}")
            return False
    except Exception as e:
        logger.error(f"Failed to set hostname: {e}")
        return False


def set_system_timezone(timezone: str) -> bool:
    """Set the system timezone (requires root)."""
    try:
        result = subprocess.run(
            ["timedatectl", "set-timezone", timezone],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode == 0:
            logger.info(f"Timezone set to: {timezone}")
            return True
        else:
            logger.error(f"Failed to set timezone: {result.stderr}")
            return False
    except Exception as e:
        logger.error(f"Failed to set timezone: {e}")
        return False


def configure_ntp_servers(servers: list[str]) -> bool:
    """Configure NTP servers (requires root)."""
    try:
        # Write to /etc/systemd/timesyncd.conf
        ntp_line = " ".join(servers)
        config = f"[Time]\nNTP={ntp_line}\n"

        with open('/etc/systemd/timesyncd.conf', 'w') as f:
            f.write(config)

        # Restart timesyncd
        subprocess.run(
            ["systemctl", "restart", "systemd-timesyncd"],
            capture_output=True, timeout=10
        )

        logger.info(f"NTP servers configured: {servers}")
        return True
    except Exception as e:
        logger.error(f"Failed to configure NTP: {e}")
        return False


def apply_network_config(config: NetworkConfig) -> tuple[bool, str]:
    """
    Apply network configuration using netplan (Ubuntu/Debian).

    Creates a netplan YAML config and applies it.
    """
    try:
        netplan_dir = "/etc/netplan"
        netplan_file = f"{netplan_dir}/99-audyn-control.yaml"

        if config.mode == "dhcp":
            netplan_config = f"""# Audyn control interface configuration
# Auto-generated - do not edit manually
network:
  version: 2
  renderer: networkd
  ethernets:
    {config.interface}:
      dhcp4: true
"""
        else:
            # Static configuration
            dns_line = ""
            if config.dns_servers:
                dns_list = ", ".join(config.dns_servers)
                dns_line = f"\n      nameservers:\n        addresses: [{dns_list}]"

            routes_line = ""
            if config.gateway:
                routes_line = f"\n      routes:\n        - to: default\n          via: {config.gateway}"

            netplan_config = f"""# Audyn control interface configuration
# Auto-generated - do not edit manually
network:
  version: 2
  renderer: networkd
  ethernets:
    {config.interface}:
      dhcp4: false
      addresses:
        - {config.ip_address}/{netmask_to_cidr(config.netmask)}{routes_line}{dns_line}
"""

        # Write netplan config
        with open(netplan_file, 'w') as f:
            f.write(netplan_config)
        os.chmod(netplan_file, 0o600)

        # Apply netplan
        result = subprocess.run(
            ["netplan", "apply"],
            capture_output=True, text=True, timeout=30
        )

        if result.returncode != 0:
            logger.error(f"Netplan apply failed: {result.stderr}")
            return False, result.stderr

        logger.info(f"Network config applied for {config.interface}")
        return True, "Network configuration applied successfully"

    except FileNotFoundError:
        # Netplan not available, try ifupdown
        return apply_network_config_ifupdown(config)
    except Exception as e:
        logger.error(f"Failed to apply network config: {e}")
        return False, str(e)


def apply_network_config_ifupdown(config: NetworkConfig) -> tuple[bool, str]:
    """
    Apply network configuration using ifupdown (fallback for non-netplan systems).
    """
    try:
        interfaces_file = "/etc/network/interfaces.d/audyn-control"

        if config.mode == "dhcp":
            iface_config = f"""# Audyn control interface configuration
auto {config.interface}
iface {config.interface} inet dhcp
"""
        else:
            dns_line = ""
            if config.dns_servers:
                dns_line = f"\n    dns-nameservers {' '.join(config.dns_servers)}"

            gateway_line = ""
            if config.gateway:
                gateway_line = f"\n    gateway {config.gateway}"

            iface_config = f"""# Audyn control interface configuration
auto {config.interface}
iface {config.interface} inet static
    address {config.ip_address}
    netmask {config.netmask}{gateway_line}{dns_line}
"""

        with open(interfaces_file, 'w') as f:
            f.write(iface_config)

        # Restart networking
        subprocess.run(
            ["ifdown", config.interface],
            capture_output=True, timeout=10
        )
        result = subprocess.run(
            ["ifup", config.interface],
            capture_output=True, text=True, timeout=30
        )

        if result.returncode != 0:
            return False, result.stderr

        logger.info(f"Network config applied for {config.interface} (ifupdown)")
        return True, "Network configuration applied successfully"

    except Exception as e:
        logger.error(f"Failed to apply network config (ifupdown): {e}")
        return False, str(e)


def netmask_to_cidr(netmask: str) -> int:
    """Convert netmask to CIDR prefix length."""
    if not netmask:
        return 24
    try:
        parts = netmask.split('.')
        binary = ''.join([bin(int(x)).count('1') for x in parts])
        return sum([bin(int(x)).count('1') for x in parts])
    except:
        return 24


def update_nginx_binding(ip_address: str, ssl_enabled: bool = False, domain: str = None) -> bool:
    """
    Update nginx to bind to a specific IP address.
    """
    try:
        if ssl_enabled and domain:
            # Get current SSL config to find cert paths
            ssl_config = _ssl_config
            if ssl_config and ssl_config.cert_type == "manual":
                cert_path = f"/etc/audyn/ssl/{domain}.crt"
                key_path = f"/etc/audyn/ssl/{domain}.key"
            else:
                cert_path = f"/etc/letsencrypt/live/{domain}/fullchain.pem"
                key_path = f"/etc/letsencrypt/live/{domain}/privkey.pem"

            nginx_config = f"""# Audyn Web Interface - Bound to {ip_address}
# Auto-configured by Audyn

upstream audyn_backend {{
    server 127.0.0.1:8000;
    keepalive 32;
}}

# Redirect HTTP to HTTPS
server {{
    listen {ip_address}:80;
    server_name {domain} _;
    return 301 https://$host$request_uri;
}}

server {{
    listen {ip_address}:443 ssl http2;
    server_name {domain} _;

    ssl_certificate {cert_path};
    ssl_certificate_key {key_path};
    ssl_session_timeout 1d;
    ssl_session_cache shared:SSL:50m;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256;
    ssl_prefer_server_ciphers off;

    add_header Strict-Transport-Security "max-age=63072000" always;

    root /opt/audyn/frontend;
    index index.html;

    gzip on;
    gzip_types text/plain text/css application/json application/javascript;
    gzip_min_length 1000;

    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;

    location /api/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 60s;
        proxy_send_timeout 300s;
        proxy_read_timeout 300s;
        proxy_buffering off;
    }}

    location /auth/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }}

    location /ws/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
    }}

    location /health {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
    }}

    location / {{
        try_files $uri $uri/ /index.html;
        location ~* \\.(js|css|png|jpg|jpeg|gif|ico|svg|woff|woff2)$ {{
            expires 1y;
            add_header Cache-Control "public, immutable";
        }}
    }}

    location ~ /\\. {{
        deny all;
    }}
}}
"""
        else:
            # HTTP only
            nginx_config = f"""# Audyn Web Interface - Bound to {ip_address}
# Auto-configured by Audyn

upstream audyn_backend {{
    server 127.0.0.1:8000;
    keepalive 32;
}}

server {{
    listen {ip_address}:80;

    server_name _;

    root /opt/audyn/frontend;
    index index.html;

    gzip on;
    gzip_types text/plain text/css application/json application/javascript;
    gzip_min_length 1000;

    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;

    location /api/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 60s;
        proxy_send_timeout 300s;
        proxy_read_timeout 300s;
        proxy_buffering off;
        proxy_request_buffering off;
    }}

    location /auth/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }}

    location /ws/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
    }}

    location /health {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
    }}

    location / {{
        try_files $uri $uri/ /index.html;
        location ~* \\.(js|css|png|jpg|jpeg|gif|ico|svg|woff|woff2)$ {{
            expires 1y;
            add_header Cache-Control "public, immutable";
        }}
    }}

    location ~ /\\. {{
        deny all;
    }}
}}
"""

        with open('/etc/nginx/sites-available/audyn', 'w') as f:
            f.write(nginx_config)

        # Test and reload nginx
        test_result = subprocess.run(
            ["nginx", "-t"],
            capture_output=True, text=True, timeout=10
        )

        if test_result.returncode != 0:
            logger.error(f"Nginx config test failed: {test_result.stderr}")
            return False

        subprocess.run(["systemctl", "reload", "nginx"], timeout=10)
        logger.info(f"Nginx bound to {ip_address}")
        return True

    except Exception as e:
        logger.error(f"Failed to update nginx binding: {e}")
        return False


def enable_letsencrypt_ssl(domain: str, email: str) -> tuple[bool, str]:
    """Enable Let's Encrypt SSL certificate."""
    try:
        # Run certbot
        result = subprocess.run([
            "certbot", "certonly",
            "--nginx",
            "-d", domain,
            "-m", email,
            "--agree-tos",
            "--non-interactive",
            "--redirect"
        ], capture_output=True, text=True, timeout=120)

        if result.returncode != 0:
            error_msg = result.stderr or result.stdout or "Unknown error"
            logger.error(f"Certbot failed: {error_msg}")
            return False, error_msg

        # Update nginx config to use SSL
        nginx_ssl_config = f"""# Audyn Web Interface - SSL enabled
# Auto-configured by Audyn for {domain}

upstream audyn_backend {{
    server 127.0.0.1:8000;
    keepalive 32;
}}

# Redirect HTTP to HTTPS
server {{
    listen 80;
    listen [::]:80;
    server_name {domain};
    return 301 https://$host$request_uri;
}}

server {{
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name {domain};

    ssl_certificate /etc/letsencrypt/live/{domain}/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/{domain}/privkey.pem;
    ssl_session_timeout 1d;
    ssl_session_cache shared:SSL:50m;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256;
    ssl_prefer_server_ciphers off;

    # HSTS
    add_header Strict-Transport-Security "max-age=63072000" always;

    root /opt/audyn/frontend;
    index index.html;

    gzip on;
    gzip_types text/plain text/css application/json application/javascript;
    gzip_min_length 1000;

    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;

    location /api/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 60s;
        proxy_send_timeout 300s;
        proxy_read_timeout 300s;
        proxy_buffering off;
    }}

    location /auth/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }}

    location /ws/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
    }}

    location /health {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
    }}

    location / {{
        try_files $uri $uri/ /index.html;
        location ~* \\.(js|css|png|jpg|jpeg|gif|ico|svg|woff|woff2)$ {{
            expires 1y;
            add_header Cache-Control "public, immutable";
        }}
    }}

    location ~ /\\. {{
        deny all;
    }}
}}
"""

        with open('/etc/nginx/sites-available/audyn', 'w') as f:
            f.write(nginx_ssl_config)

        # Test and reload nginx
        test_result = subprocess.run(
            ["nginx", "-t"],
            capture_output=True, text=True, timeout=10
        )

        if test_result.returncode != 0:
            logger.error(f"Nginx config test failed: {test_result.stderr}")
            return False, "Nginx configuration test failed"

        subprocess.run(["systemctl", "reload", "nginx"], timeout=10)

        logger.info(f"SSL enabled for {domain}")
        return True, "SSL certificate installed successfully"

    except subprocess.TimeoutExpired:
        return False, "Certificate request timed out"
    except Exception as e:
        logger.error(f"SSL enable failed: {e}")
        return False, str(e)


def disable_ssl() -> bool:
    """Disable SSL and revert to HTTP-only."""
    try:
        # Restore default HTTP-only nginx config
        nginx_http_config = """# Audyn Web Interface
# Serves frontend and proxies API to localhost backend

upstream audyn_backend {
    server 127.0.0.1:8000;
    keepalive 32;
}

server {
    listen 80 default_server;
    listen [::]:80 default_server;

    server_name _;

    root /opt/audyn/frontend;
    index index.html;

    gzip on;
    gzip_types text/plain text/css application/json application/javascript;
    gzip_min_length 1000;

    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;
    add_header X-XSS-Protection "1; mode=block" always;

    location /api/ {
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 60s;
        proxy_send_timeout 300s;
        proxy_read_timeout 300s;
        proxy_buffering off;
        proxy_request_buffering off;
    }

    location /auth/ {
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }

    location /ws/ {
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
    }

    location /health {
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
    }

    location / {
        try_files $uri $uri/ /index.html;
        location ~* \\.(js|css|png|jpg|jpeg|gif|ico|svg|woff|woff2)$ {
            expires 1y;
            add_header Cache-Control "public, immutable";
        }
    }

    location ~ /\\. {
        deny all;
    }
}
"""

        with open('/etc/nginx/sites-available/audyn', 'w') as f:
            f.write(nginx_http_config)

        subprocess.run(["systemctl", "reload", "nginx"], timeout=10)
        logger.info("SSL disabled, reverted to HTTP")
        return True

    except Exception as e:
        logger.error(f"Failed to disable SSL: {e}")
        return False


def install_manual_certificate(domain: str, cert_content: str, key_content: str) -> tuple[bool, str]:
    """Install manually uploaded SSL certificate and key."""
    try:
        # Create directory for manual certs
        cert_dir = "/etc/audyn/ssl"
        os.makedirs(cert_dir, exist_ok=True)

        cert_path = f"{cert_dir}/{domain}.crt"
        key_path = f"{cert_dir}/{domain}.key"

        # Write certificate and key files
        with open(cert_path, 'w') as f:
            f.write(cert_content)
        os.chmod(cert_path, 0o644)

        with open(key_path, 'w') as f:
            f.write(key_content)
        os.chmod(key_path, 0o600)  # Private key - restricted permissions

        # Validate certificate with openssl
        result = subprocess.run(
            ["openssl", "x509", "-in", cert_path, "-noout", "-checkend", "0"],
            capture_output=True, text=True, timeout=10
        )
        if result.returncode != 0:
            os.remove(cert_path)
            os.remove(key_path)
            return False, "Invalid or expired certificate"

        # Check that key matches certificate
        cert_modulus = subprocess.run(
            ["openssl", "x509", "-noout", "-modulus", "-in", cert_path],
            capture_output=True, text=True, timeout=10
        )
        key_modulus = subprocess.run(
            ["openssl", "rsa", "-noout", "-modulus", "-in", key_path],
            capture_output=True, text=True, timeout=10
        )
        if cert_modulus.stdout != key_modulus.stdout:
            os.remove(cert_path)
            os.remove(key_path)
            return False, "Certificate and private key do not match"

        # Get certificate expiry date
        expiry_result = subprocess.run(
            ["openssl", "x509", "-enddate", "-noout", "-in", cert_path],
            capture_output=True, text=True, timeout=10
        )
        expiry_date = None
        if expiry_result.returncode == 0:
            # Parse: notAfter=Mar 15 12:00:00 2025 GMT
            expiry_str = expiry_result.stdout.replace("notAfter=", "").strip()
            try:
                from datetime import datetime
                expiry_date = datetime.strptime(expiry_str, "%b %d %H:%M:%S %Y %Z").isoformat()
            except:
                pass

        # Update nginx config to use manual SSL certificate
        nginx_ssl_config = f"""# Audyn Web Interface - SSL enabled (manual certificate)
# Configured for {domain}

upstream audyn_backend {{
    server 127.0.0.1:8000;
    keepalive 32;
}}

# Redirect HTTP to HTTPS
server {{
    listen 80;
    listen [::]:80;
    server_name {domain} _;
    return 301 https://$host$request_uri;
}}

server {{
    listen 443 ssl http2;
    listen [::]:443 ssl http2;
    server_name {domain} _;

    ssl_certificate {cert_path};
    ssl_certificate_key {key_path};
    ssl_session_timeout 1d;
    ssl_session_cache shared:SSL:50m;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_ciphers ECDHE-ECDSA-AES128-GCM-SHA256:ECDHE-RSA-AES128-GCM-SHA256;
    ssl_prefer_server_ciphers off;

    # HSTS
    add_header Strict-Transport-Security "max-age=63072000" always;

    root /opt/audyn/frontend;
    index index.html;

    gzip on;
    gzip_types text/plain text/css application/json application/javascript;
    gzip_min_length 1000;

    add_header X-Frame-Options "SAMEORIGIN" always;
    add_header X-Content-Type-Options "nosniff" always;

    location /api/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
        proxy_connect_timeout 60s;
        proxy_send_timeout 300s;
        proxy_read_timeout 300s;
        proxy_buffering off;
    }}

    location /auth/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_set_header X-Forwarded-For $proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto $scheme;
    }}

    location /ws/ {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
    }}

    location /health {{
        proxy_pass http://audyn_backend;
        proxy_http_version 1.1;
        proxy_set_header Host $host;
    }}

    location / {{
        try_files $uri $uri/ /index.html;
        location ~* \\.(js|css|png|jpg|jpeg|gif|ico|svg|woff|woff2)$ {{
            expires 1y;
            add_header Cache-Control "public, immutable";
        }}
    }}

    location ~ /\\. {{
        deny all;
    }}
}}
"""

        with open('/etc/nginx/sites-available/audyn', 'w') as f:
            f.write(nginx_ssl_config)

        # Test and reload nginx
        test_result = subprocess.run(
            ["nginx", "-t"],
            capture_output=True, text=True, timeout=10
        )

        if test_result.returncode != 0:
            logger.error(f"Nginx config test failed: {test_result.stderr}")
            return False, f"Nginx configuration test failed: {test_result.stderr}"

        subprocess.run(["systemctl", "reload", "nginx"], timeout=10)

        logger.info(f"Manual SSL certificate installed for {domain}")
        return True, expiry_date

    except Exception as e:
        logger.error(f"Manual SSL install failed: {e}")
        return False, str(e)


# ============ API Endpoints ============

@router.get("/config", response_model=SystemConfig)
async def get_system_config(user: User = Depends(get_current_user)):
    """Get current system configuration."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    return _system_config


@router.post("/config")
async def set_system_config(
    config: PartialSystemConfig,
    user: User = Depends(get_current_user)
):
    """Update system configuration."""
    global _system_config

    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    # Merge with existing config
    existing = load_system_config() or {}
    updates = config.model_dump(exclude_none=True)
    merged = {**existing, **updates}

    _system_config = SystemConfig(**merged)

    # Apply changes to system
    warnings = []

    if config.hostname is not None:
        if not set_system_hostname(config.hostname):
            warnings.append("Failed to set hostname (may require root)")

    if config.timezone is not None:
        if not set_system_timezone(config.timezone):
            warnings.append("Failed to set timezone (may require root)")

    if config.ntp_servers is not None:
        if not configure_ntp_servers(config.ntp_servers):
            warnings.append("Failed to configure NTP (may require root)")

    # Persist config
    if not save_system_config(_system_config.model_dump()):
        logger.warning("Failed to persist system config")

    logger.info(f"System config updated by {user.email}")

    return {
        "message": "System configuration updated",
        "config": _system_config,
        "warnings": warnings if warnings else None
    }


@router.get("/interfaces", response_model=list[NetworkInterface])
async def list_interfaces(user: User = Depends(get_current_user)):
    """List available network interfaces."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    return get_network_interfaces()


@router.get("/timezones", response_model=list[str])
async def list_timezones(user: User = Depends(get_current_user)):
    """List available timezones."""
    return get_available_timezones()


@router.get("/ssl", response_model=SSLConfig)
async def get_ssl_config(user: User = Depends(get_current_user)):
    """Get SSL certificate status."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    return _ssl_config


@router.post("/ssl/enable")
async def enable_ssl(
    request: SSLEnableRequest,
    user: User = Depends(get_current_user)
):
    """Enable Let's Encrypt SSL certificate."""
    global _ssl_config

    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    success, message = enable_letsencrypt_ssl(request.domain, request.email)

    if success:
        _ssl_config = SSLConfig(
            enabled=True,
            domain=request.domain,
            email=request.email,
            auto_renew=True,
            cert_type="letsencrypt",
            last_renewed=datetime.now().isoformat()
        )
        save_ssl_config(_ssl_config.model_dump())
        logger.info(f"SSL enabled for {request.domain} by {user.email}")

        return {
            "message": "SSL certificate installed successfully",
            "config": _ssl_config
        }
    else:
        raise HTTPException(status_code=500, detail=message)


@router.post("/ssl/disable")
async def disable_ssl_endpoint(user: User = Depends(get_current_user)):
    """Disable SSL and revert to HTTP."""
    global _ssl_config

    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    if disable_ssl():
        _ssl_config = SSLConfig(enabled=False)
        save_ssl_config(_ssl_config.model_dump())
        logger.info(f"SSL disabled by {user.email}")

        return {"message": "SSL disabled, reverted to HTTP"}
    else:
        raise HTTPException(status_code=500, detail="Failed to disable SSL")


@router.post("/ssl/renew")
async def renew_ssl(user: User = Depends(get_current_user)):
    """Renew SSL certificate."""
    global _ssl_config

    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    if not _ssl_config or not _ssl_config.enabled:
        raise HTTPException(status_code=400, detail="SSL is not enabled")

    try:
        result = subprocess.run(
            ["certbot", "renew", "--nginx", "--non-interactive"],
            capture_output=True, text=True, timeout=120
        )

        if result.returncode == 0:
            _ssl_config.last_renewed = datetime.now().isoformat()
            save_ssl_config(_ssl_config.model_dump())
            logger.info(f"SSL certificate renewed by {user.email}")
            return {"message": "Certificate renewed successfully"}
        else:
            raise HTTPException(status_code=500, detail=result.stderr or "Renewal failed")

    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=500, detail="Certificate renewal timed out")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/ssl/upload")
async def upload_ssl_certificate(
    domain: str = Form(...),
    certificate: UploadFile = File(...),
    private_key: UploadFile = File(...),
    user: User = Depends(get_current_user)
):
    """Upload and install a manual SSL certificate."""
    global _ssl_config

    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    # Validate file types
    if not certificate.filename.endswith(('.crt', '.pem', '.cer')):
        raise HTTPException(
            status_code=400,
            detail="Certificate must be a .crt, .pem, or .cer file"
        )

    if not private_key.filename.endswith(('.key', '.pem')):
        raise HTTPException(
            status_code=400,
            detail="Private key must be a .key or .pem file"
        )

    # Read file contents
    cert_content = (await certificate.read()).decode('utf-8')
    key_content = (await private_key.read()).decode('utf-8')

    # Validate PEM format
    if "-----BEGIN CERTIFICATE-----" not in cert_content:
        raise HTTPException(
            status_code=400,
            detail="Invalid certificate format. Must be PEM format."
        )

    if "-----BEGIN" not in key_content or "PRIVATE KEY-----" not in key_content:
        raise HTTPException(
            status_code=400,
            detail="Invalid private key format. Must be PEM format."
        )

    # Install the certificate
    success, result = install_manual_certificate(domain, cert_content, key_content)

    if success:
        _ssl_config = SSLConfig(
            enabled=True,
            domain=domain,
            email=None,
            auto_renew=False,
            cert_type="manual",
            cert_expiry=result,  # result contains expiry date on success
            last_renewed=datetime.now().isoformat()
        )
        save_ssl_config(_ssl_config.model_dump())
        logger.info(f"Manual SSL certificate installed for {domain} by {user.email}")

        return {
            "message": "SSL certificate installed successfully",
            "config": _ssl_config
        }
    else:
        raise HTTPException(status_code=400, detail=result)  # result contains error message


# ============ Network Configuration Endpoints ============

@router.get("/network", response_model=ControlInterfaceConfig)
async def get_network_config(user: User = Depends(get_current_user)):
    """Get control interface network configuration."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    return _control_interface_config


@router.post("/network")
async def set_network_config(
    config: ControlInterfaceConfig,
    user: User = Depends(get_current_user)
):
    """Set control interface network configuration."""
    global _control_interface_config

    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    warnings = []

    # Apply network configuration if provided
    if config.network:
        success, message = apply_network_config(config.network)
        if not success:
            raise HTTPException(
                status_code=500,
                detail=f"Failed to apply network configuration: {message}"
            )

    # Update nginx binding if requested
    if config.bind_services and config.network and config.network.ip_address:
        ssl_enabled = _ssl_config and _ssl_config.enabled
        domain = _ssl_config.domain if ssl_enabled else None

        if not update_nginx_binding(config.network.ip_address, ssl_enabled, domain):
            warnings.append("Failed to update nginx binding")

    # Save configuration
    _control_interface_config = config
    if not save_network_config(config.model_dump()):
        warnings.append("Failed to persist network configuration")

    logger.info(f"Network config updated by {user.email}")

    return {
        "message": "Network configuration updated",
        "config": _control_interface_config,
        "warnings": warnings if warnings else None
    }


@router.get("/network/current")
async def get_current_network_info(user: User = Depends(get_current_user)):
    """Get current network status for all interfaces."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    interfaces = get_network_interfaces()

    # Add gateway info
    gateway = None
    try:
        import netifaces
        gws = netifaces.gateways()
        if 'default' in gws and netifaces.AF_INET in gws['default']:
            gateway = gws['default'][netifaces.AF_INET][0]
    except:
        pass

    return {
        "interfaces": interfaces,
        "default_gateway": gateway,
        "configured": _control_interface_config.interface if _control_interface_config else None
    }


# ============ AES67 Network Configuration Endpoints ============

@router.get("/aes67", response_model=AES67InterfaceConfig)
async def get_aes67_config(user: User = Depends(get_current_user)):
    """Get AES67 interface network configuration."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    return _aes67_interface_config


@router.post("/aes67")
async def set_aes67_config(
    config: AES67InterfaceConfig,
    user: User = Depends(get_current_user)
):
    """Set AES67 interface network configuration."""
    global _aes67_interface_config

    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    warnings = []

    # Apply network configuration if provided
    if config.network:
        success, message = apply_network_config(config.network)
        if not success:
            raise HTTPException(
                status_code=500,
                detail=f"Failed to apply AES67 network configuration: {message}"
            )

    # Save configuration
    _aes67_interface_config = config
    if not save_aes67_network_config(config.model_dump()):
        warnings.append("Failed to persist AES67 network configuration")

    logger.info(f"AES67 network config updated by {user.email}")

    return {
        "message": "AES67 network configuration updated",
        "config": _aes67_interface_config,
        "warnings": warnings if warnings else None
    }


# ============ Log Rotation Configuration ============

LOGROTATE_CONFIG_PATH = "/etc/logrotate.d/audyn"

def get_current_logrotate_config() -> LogRotationConfig:
    """Parse current logrotate configuration."""
    config = LogRotationConfig()

    try:
        if not os.path.exists(LOGROTATE_CONFIG_PATH):
            return config

        with open(LOGROTATE_CONFIG_PATH, 'r') as f:
            content = f.read()

        # Parse frequency
        if 'daily' in content:
            config.frequency = 'daily'
        elif 'weekly' in content:
            config.frequency = 'weekly'
        else:
            config.frequency = 'monthly'

        # Parse rotate count
        import re
        rotate_match = re.search(r'rotate\s+(\d+)', content)
        if rotate_match:
            config.rotate_count = int(rotate_match.group(1))

        # Parse compress
        config.compress = 'compress' in content and 'nocompress' not in content

        # Parse size
        size_match = re.search(r'size\s+(\S+)', content)
        if size_match:
            config.max_size = size_match.group(1)

    except Exception as e:
        logger.error(f"Failed to parse logrotate config: {e}")

    return config


def update_logrotate_config(config: LogRotationConfig) -> tuple[bool, str]:
    """Update the logrotate configuration file."""
    try:
        # Build logrotate config
        size_line = f"\n    size {config.max_size}" if config.max_size else ""
        compress_lines = "compress\n    delaycompress" if config.compress else "nocompress"

        logrotate_content = f"""/var/log/audyn/*.log {{
    {config.frequency}
    rotate {config.rotate_count}
    {compress_lines}
    missingok
    notifempty
    create 640 audyn audyn{size_line}
    sharedscripts
    postrotate
        systemctl reload audyn-web.service > /dev/null 2>&1 || true
    endscript
}}
"""

        with open(LOGROTATE_CONFIG_PATH, 'w') as f:
            f.write(logrotate_content)

        logger.info(f"Logrotate config updated: frequency={config.frequency}, rotate={config.rotate_count}")
        return True, "Log rotation configuration updated"

    except PermissionError:
        return False, "Permission denied. Cannot write to logrotate config."
    except Exception as e:
        logger.error(f"Failed to update logrotate config: {e}")
        return False, str(e)


@router.get("/logrotate", response_model=LogRotationConfig)
async def get_logrotate_config(user: User = Depends(get_current_user)):
    """Get current log rotation configuration."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    return get_current_logrotate_config()


@router.post("/logrotate")
async def set_logrotate_config(
    config: LogRotationConfig,
    user: User = Depends(get_current_user)
):
    """Update log rotation configuration."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    # Validate frequency
    if config.frequency not in ['daily', 'weekly', 'monthly']:
        raise HTTPException(
            status_code=400,
            detail="Frequency must be 'daily', 'weekly', or 'monthly'"
        )

    # Validate rotate count
    if config.rotate_count < 1 or config.rotate_count > 365:
        raise HTTPException(
            status_code=400,
            detail="Rotate count must be between 1 and 365"
        )

    # Validate max_size format if provided
    if config.max_size:
        import re
        if not re.match(r'^\d+[kKmMgG]?$', config.max_size):
            raise HTTPException(
                status_code=400,
                detail="Invalid size format. Use format like '100M', '1G', '500k'"
            )

    success, message = update_logrotate_config(config)

    if success:
        logger.info(f"Log rotation config updated by {user.email}")
        return {
            "message": message,
            "config": config
        }
    else:
        raise HTTPException(status_code=500, detail=message)


@router.post("/logrotate/force")
async def force_log_rotation(user: User = Depends(get_current_user)):
    """Force immediate log rotation."""
    if user.role != "admin":
        raise HTTPException(status_code=403, detail="Admin access required")

    try:
        result = subprocess.run(
            ["logrotate", "-f", LOGROTATE_CONFIG_PATH],
            capture_output=True, text=True, timeout=30
        )

        if result.returncode == 0:
            logger.info(f"Log rotation forced by {user.email}")
            return {"message": "Log rotation completed successfully"}
        else:
            raise HTTPException(
                status_code=500,
                detail=result.stderr or "Log rotation failed"
            )

    except subprocess.TimeoutExpired:
        raise HTTPException(status_code=500, detail="Log rotation timed out")
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))
