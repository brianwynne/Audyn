#!/bin/bash
#
# Audyn System Hardening Script
#
# This script hardens an Ubuntu system for broadcast-grade 24/7 operation.
# Run this after installing the Audyn .deb package.
#
# Usage: sudo /opt/audyn/scripts/harden-system.sh [--non-interactive]
#
# Options:
#   --non-interactive    Apply all hardening without prompts
#   --dry-run           Show what would be done without making changes
#   --help              Show this help message
#
# The script will:
#   1. Configure firewall (UFW)
#   2. Harden SSH configuration
#   3. Apply kernel security parameters
#   4. Install and configure fail2ban
#   5. Enable automatic security updates
#   6. Disable unnecessary services
#   7. Harden systemd service
#   8. Configure audit logging
#   9. Set up real-time audio permissions
#
# All changes are logged to /var/log/audyn/hardening.log
# Original configs are backed up to /etc/audyn/backup/
#

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Script configuration
SCRIPT_VERSION="1.0.0"
LOG_FILE="/var/log/audyn/hardening.log"
BACKUP_DIR="/etc/audyn/backup/hardening-$(date +%Y%m%d-%H%M%S)"
NON_INTERACTIVE=false
DRY_RUN=false

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --non-interactive)
            NON_INTERACTIVE=true
            shift
            ;;
        --dry-run)
            DRY_RUN=true
            shift
            ;;
        --help)
            head -35 "$0" | tail -32
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            exit 1
            ;;
    esac
done

# Logging function
log() {
    local level="$1"
    shift
    local message="$*"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    echo -e "${timestamp} [${level}] ${message}" >> "$LOG_FILE"

    case "$level" in
        INFO)  echo -e "${GREEN}[INFO]${NC} ${message}" ;;
        WARN)  echo -e "${YELLOW}[WARN]${NC} ${message}" ;;
        ERROR) echo -e "${RED}[ERROR]${NC} ${message}" ;;
        STEP)  echo -e "${BLUE}[STEP]${NC} ${message}" ;;
    esac
}

# Check if running as root
check_root() {
    if [[ $EUID -ne 0 ]]; then
        echo -e "${RED}Error: This script must be run as root${NC}"
        echo "Usage: sudo $0"
        exit 1
    fi
}

# Prompt for confirmation
confirm() {
    local message="$1"
    if $NON_INTERACTIVE; then
        return 0
    fi

    echo -en "${YELLOW}${message} [y/N]: ${NC}"
    read -r response
    case "$response" in
        [yY][eE][sS]|[yY]) return 0 ;;
        *) return 1 ;;
    esac
}

# Backup a file before modifying
backup_file() {
    local file="$1"
    if [[ -f "$file" ]]; then
        local backup_path="${BACKUP_DIR}${file}"
        mkdir -p "$(dirname "$backup_path")"
        cp "$file" "$backup_path"
        log INFO "Backed up: $file -> $backup_path"
    fi
}

# Execute or simulate command
run_cmd() {
    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would execute: $*"
    else
        "$@"
    fi
}

# ============================================================================
# Hardening Functions
# ============================================================================

configure_firewall() {
    log STEP "Configuring UFW firewall..."

    if ! command -v ufw &> /dev/null; then
        log INFO "Installing UFW..."
        run_cmd apt-get install -y ufw
    fi

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would configure UFW firewall rules"
        return
    fi

    # Reset UFW to defaults
    ufw --force reset

    # Default policies
    ufw default deny incoming
    ufw default allow outgoing

    # Get network interfaces
    local interfaces=$(ip -o link show | awk -F': ' '{print $2}' | grep -v lo)

    echo ""
    echo "Available network interfaces:"
    echo "$interfaces"
    echo ""

    # Control interface rules
    local control_iface=""
    if ! $NON_INTERACTIVE; then
        echo -en "Enter the CONTROL interface (for web UI/SSH) [eth0]: "
        read -r control_iface
    fi
    control_iface=${control_iface:-eth0}

    # AES67 interface rules
    local aes67_iface=""
    if ! $NON_INTERACTIVE; then
        echo -en "Enter the AES67 interface (for audio) [eth1]: "
        read -r aes67_iface
    fi
    aes67_iface=${aes67_iface:-eth1}

    # Control interface - management access
    ufw allow in on "$control_iface" to any port 22 proto tcp comment 'SSH'
    ufw allow in on "$control_iface" to any port 80 proto tcp comment 'HTTP'
    ufw allow in on "$control_iface" to any port 443 proto tcp comment 'HTTPS'

    # AES67 interface - audio and PTP only
    ufw allow in on "$aes67_iface" to any port 5004 proto udp comment 'RTP Audio'
    ufw allow in on "$aes67_iface" to any port 319 proto udp comment 'PTP Event'
    ufw allow in on "$aes67_iface" to any port 320 proto udp comment 'PTP General'

    # Allow multicast for AES67
    ufw allow in on "$aes67_iface" to 224.0.0.0/4 comment 'Multicast'

    # Enable UFW
    ufw --force enable

    log INFO "Firewall configured successfully"
    log INFO "Control interface ($control_iface): SSH, HTTP, HTTPS"
    log INFO "AES67 interface ($aes67_iface): RTP, PTP, Multicast"
}

harden_ssh() {
    log STEP "Hardening SSH configuration..."

    local ssh_config="/etc/ssh/sshd_config.d/99-audyn-hardening.conf"
    backup_file "/etc/ssh/sshd_config"

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would create SSH hardening config at $ssh_config"
        return
    fi

    cat > "$ssh_config" << 'EOF'
# Audyn SSH Hardening Configuration
# Generated by harden-system.sh

# Authentication
PermitRootLogin no
MaxAuthTries 3
PubkeyAuthentication yes
PasswordAuthentication yes
PermitEmptyPasswords no

# Session
ClientAliveInterval 300
ClientAliveCountMax 2
LoginGraceTime 60

# Security
X11Forwarding no
AllowTcpForwarding no
AllowAgentForwarding no
PermitUserEnvironment no

# Logging
LogLevel VERBOSE

# Ciphers (strong only)
Ciphers chacha20-poly1305@openssh.com,aes256-gcm@openssh.com,aes128-gcm@openssh.com
MACs hmac-sha2-512-etm@openssh.com,hmac-sha2-256-etm@openssh.com
KexAlgorithms curve25519-sha256,curve25519-sha256@libssh.org
EOF

    chmod 600 "$ssh_config"

    # Test SSH config before restarting
    if sshd -t; then
        systemctl reload sshd
        log INFO "SSH hardening applied successfully"
    else
        rm -f "$ssh_config"
        log ERROR "SSH config test failed, changes reverted"
        return 1
    fi
}

apply_kernel_hardening() {
    log STEP "Applying kernel security parameters..."

    local sysctl_file="/etc/sysctl.d/99-audyn-hardening.conf"

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would create sysctl config at $sysctl_file"
        return
    fi

    cat > "$sysctl_file" << 'EOF'
# Audyn Kernel Hardening Configuration
# Generated by harden-system.sh

# ============ Network Security ============

# Disable IP forwarding (not a router)
net.ipv4.ip_forward = 0

# Disable ICMP redirects
net.ipv4.conf.all.accept_redirects = 0
net.ipv4.conf.default.accept_redirects = 0
net.ipv4.conf.all.send_redirects = 0
net.ipv4.conf.default.send_redirects = 0

# Disable source routing
net.ipv4.conf.all.accept_source_route = 0
net.ipv4.conf.default.accept_source_route = 0

# Enable TCP SYN cookies (protect against SYN floods)
net.ipv4.tcp_syncookies = 1

# Log martian packets (impossible addresses)
net.ipv4.conf.all.log_martians = 1
net.ipv4.conf.default.log_martians = 1

# Ignore ICMP broadcasts
net.ipv4.icmp_echo_ignore_broadcasts = 1

# Ignore bogus ICMP errors
net.ipv4.icmp_ignore_bogus_error_responses = 1

# Enable reverse path filtering
net.ipv4.conf.all.rp_filter = 1
net.ipv4.conf.default.rp_filter = 1

# ============ IPv6 ============
# Disable IPv6 if not needed (reduces attack surface)
net.ipv6.conf.all.disable_ipv6 = 1
net.ipv6.conf.default.disable_ipv6 = 1
net.ipv6.conf.lo.disable_ipv6 = 1

# ============ Kernel Security ============

# Enable ASLR
kernel.randomize_va_space = 2

# Restrict kernel pointer exposure
kernel.kptr_restrict = 2

# Restrict dmesg access
kernel.dmesg_restrict = 1

# Restrict ptrace scope
kernel.yama.ptrace_scope = 1

# Disable magic SysRq key
kernel.sysrq = 0

# ============ File System ============

# Restrict core dumps
fs.suid_dumpable = 0

# Protect hardlinks and symlinks
fs.protected_hardlinks = 1
fs.protected_symlinks = 1

# ============ Real-Time Audio (Don't Restrict) ============
# Allow unlimited real-time scheduling for audio
# kernel.sched_rt_runtime_us = -1
EOF

    chmod 644 "$sysctl_file"
    sysctl --system > /dev/null 2>&1

    log INFO "Kernel hardening applied successfully"
}

install_fail2ban() {
    log STEP "Installing and configuring fail2ban..."

    if ! command -v fail2ban-client &> /dev/null; then
        log INFO "Installing fail2ban..."
        run_cmd apt-get install -y fail2ban
    fi

    local jail_file="/etc/fail2ban/jail.d/audyn.conf"

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would create fail2ban config at $jail_file"
        return
    fi

    cat > "$jail_file" << 'EOF'
# Audyn fail2ban Configuration
# Generated by harden-system.sh

[DEFAULT]
bantime = 3600
findtime = 600
maxretry = 3
banaction = ufw

[sshd]
enabled = true
port = ssh
filter = sshd
logpath = /var/log/auth.log
maxretry = 3
bantime = 3600

[nginx-http-auth]
enabled = true
port = http,https
filter = nginx-http-auth
logpath = /var/log/nginx/error.log
maxretry = 5
bantime = 1800
EOF

    chmod 644 "$jail_file"

    systemctl enable fail2ban
    systemctl restart fail2ban

    log INFO "fail2ban configured successfully"
}

enable_auto_updates() {
    log STEP "Enabling automatic security updates..."

    if ! dpkg -l | grep -q unattended-upgrades; then
        log INFO "Installing unattended-upgrades..."
        run_cmd apt-get install -y unattended-upgrades
    fi

    local config_file="/etc/apt/apt.conf.d/50unattended-upgrades"
    local auto_file="/etc/apt/apt.conf.d/20auto-upgrades"

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would configure automatic security updates"
        return
    fi

    backup_file "$config_file"

    cat > "$config_file" << 'EOF'
// Audyn Unattended Upgrades Configuration
// Generated by harden-system.sh

Unattended-Upgrade::Allowed-Origins {
    "${distro_id}:${distro_codename}";
    "${distro_id}:${distro_codename}-security";
    "${distro_id}ESMApps:${distro_codename}-apps-security";
    "${distro_id}ESM:${distro_codename}-infra-security";
};

// Do not automatically reboot (24/7 operation)
Unattended-Upgrade::Automatic-Reboot "false";

// If automatic reboot is enabled, do it at this time
Unattended-Upgrade::Automatic-Reboot-Time "03:00";

// Remove unused dependencies
Unattended-Upgrade::Remove-Unused-Dependencies "true";

// Remove unused kernel packages
Unattended-Upgrade::Remove-Unused-Kernel-Packages "true";

// Email notifications (configure if needed)
// Unattended-Upgrade::Mail "admin@example.com";
// Unattended-Upgrade::MailReport "on-change";

// Logging
Unattended-Upgrade::SyslogEnable "true";
Unattended-Upgrade::SyslogFacility "daemon";
EOF

    cat > "$auto_file" << 'EOF'
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
APT::Periodic::Download-Upgradeable-Packages "1";
APT::Periodic::AutocleanInterval "7";
EOF

    systemctl enable unattended-upgrades

    log INFO "Automatic security updates enabled"
    log WARN "System will NOT auto-reboot. Manual reboot required for kernel updates."
}

disable_unnecessary_services() {
    log STEP "Disabling unnecessary services..."

    local services_to_disable=(
        "cups"
        "cups-browsed"
        "avahi-daemon"
        "bluetooth"
        "ModemManager"
        "whoopsie"
        "apport"
        "kerneloops"
    )

    for service in "${services_to_disable[@]}"; do
        if systemctl is-enabled "$service" &> /dev/null 2>&1; then
            log INFO "Disabling $service..."
            run_cmd systemctl disable --now "$service" 2>/dev/null || true
        fi
    done

    # Mask ctrl-alt-del to prevent accidental reboots
    run_cmd systemctl mask ctrl-alt-del.target 2>/dev/null || true

    log INFO "Unnecessary services disabled"
}

harden_systemd_service() {
    log STEP "Enhancing systemd service security..."

    local service_file="/lib/systemd/system/audyn-web.service"
    local override_dir="/etc/systemd/system/audyn-web.service.d"
    local override_file="$override_dir/hardening.conf"

    if [[ ! -f "$service_file" ]]; then
        log WARN "Audyn service not found, skipping systemd hardening"
        return
    fi

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would create systemd override at $override_file"
        return
    fi

    mkdir -p "$override_dir"

    cat > "$override_file" << 'EOF'
# Audyn Service Hardening Override
# Generated by harden-system.sh

[Service]
# Additional security restrictions
CapabilityBoundingSet=CAP_NET_BIND_SERVICE CAP_SYS_NICE
AmbientCapabilities=

# Kernel hardening
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectKernelLogs=true
ProtectControlGroups=true
ProtectClock=true
ProtectHostname=true

# Namespace restrictions
RestrictNamespaces=uts ipc pid user cgroup
PrivateUsers=true

# System call filtering
SystemCallFilter=@system-service
SystemCallFilter=~@privileged @resources
SystemCallArchitectures=native
SystemCallErrorNumber=EPERM

# Memory protection
MemoryDenyWriteExecute=true

# Allow real-time for audio processing
RestrictRealtime=false
LockPersonality=true

# Limit resource usage
LimitNPROC=64
LimitCORE=0
EOF

    chmod 644 "$override_file"

    systemctl daemon-reload
    systemctl restart audyn-web.service

    log INFO "Systemd service hardening applied"
}

configure_audit_logging() {
    log STEP "Configuring audit logging..."

    if ! command -v auditctl &> /dev/null; then
        log INFO "Installing auditd..."
        run_cmd apt-get install -y auditd audispd-plugins
    fi

    local audit_rules="/etc/audit/rules.d/audyn.rules"

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would create audit rules at $audit_rules"
        return
    fi

    cat > "$audit_rules" << 'EOF'
# Audyn Audit Rules
# Generated by harden-system.sh

# Delete all existing rules
-D

# Set buffer size
-b 8192

# Failure mode (1 = printk, 2 = panic)
-f 1

# ============ Authentication & Authorization ============

# Monitor authentication logs
-w /var/log/auth.log -p wa -k auth_log
-w /var/log/faillog -p wa -k auth_log
-w /var/log/lastlog -p wa -k auth_log

# Monitor identity files
-w /etc/passwd -p wa -k identity
-w /etc/shadow -p wa -k identity
-w /etc/group -p wa -k identity
-w /etc/gshadow -p wa -k identity

# Monitor sudoers
-w /etc/sudoers -p wa -k sudoers
-w /etc/sudoers.d/ -p wa -k sudoers

# ============ Audyn Specific ============

# Monitor Audyn configuration
-w /etc/audyn/ -p wa -k audyn_config

# Monitor Audyn archive (directory changes only)
-w /var/lib/audyn/ -p wa -k audyn_archive

# Monitor Audyn logs
-w /var/log/audyn/ -p wa -k audyn_logs

# ============ System ============

# Monitor cron
-w /etc/crontab -p wa -k cron
-w /etc/cron.d/ -p wa -k cron
-w /var/spool/cron/ -p wa -k cron

# Monitor SSH
-w /etc/ssh/sshd_config -p wa -k sshd_config
-w /etc/ssh/sshd_config.d/ -p wa -k sshd_config

# Monitor network configuration
-w /etc/network/ -p wa -k network
-w /etc/netplan/ -p wa -k network

# Monitor systemd services
-w /lib/systemd/system/audyn-web.service -p wa -k audyn_service
-w /etc/systemd/system/audyn-web.service.d/ -p wa -k audyn_service

# ============ Make config immutable (must be last) ============
-e 2
EOF

    chmod 640 "$audit_rules"

    systemctl enable auditd
    systemctl restart auditd

    log INFO "Audit logging configured"
}

configure_realtime_permissions() {
    log STEP "Configuring real-time audio permissions..."

    local limits_file="/etc/security/limits.d/99-audyn-realtime.conf"

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would create limits config at $limits_file"
        return
    fi

    cat > "$limits_file" << 'EOF'
# Audyn Real-Time Audio Permissions
# Generated by harden-system.sh

# Allow audyn user real-time scheduling
audyn    -    rtprio    99
audyn    -    nice      -20
audyn    -    memlock   unlimited

# Allow audio group members (if used)
@audio   -    rtprio    99
@audio   -    nice      -20
@audio   -    memlock   unlimited
EOF

    chmod 644 "$limits_file"

    # Add audyn user to audio group if exists
    if getent group audio > /dev/null; then
        usermod -a -G audio audyn 2>/dev/null || true
    fi

    log INFO "Real-time audio permissions configured"
}

harden_tmp_mounts() {
    log STEP "Hardening temporary filesystem mounts..."

    local fstab="/etc/fstab"

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would update /etc/fstab for secure tmp mounts"
        return
    fi

    backup_file "$fstab"

    # Check if /tmp is already a separate mount
    if ! grep -q "^tmpfs.*/tmp" "$fstab"; then
        echo "tmpfs  /tmp  tmpfs  defaults,noexec,nosuid,nodev,size=512M  0  0" >> "$fstab"
        log INFO "Added secure /tmp mount"
    fi

    # Check if /var/tmp is already a separate mount
    if ! grep -q "^tmpfs.*/var/tmp" "$fstab"; then
        echo "tmpfs  /var/tmp  tmpfs  defaults,noexec,nosuid,nodev,size=512M  0  0" >> "$fstab"
        log INFO "Added secure /var/tmp mount"
    fi

    log WARN "Reboot required to apply tmp mount changes"
}

generate_report() {
    log STEP "Generating hardening report..."

    local report_file="/var/log/audyn/hardening-report-$(date +%Y%m%d-%H%M%S).txt"

    if $DRY_RUN; then
        log INFO "[DRY-RUN] Would generate report at $report_file"
        return
    fi

    {
        echo "=========================================="
        echo " Audyn System Hardening Report"
        echo " Generated: $(date)"
        echo " Script Version: $SCRIPT_VERSION"
        echo "=========================================="
        echo ""
        echo "## System Information"
        echo "Hostname: $(hostname)"
        echo "OS: $(lsb_release -ds 2>/dev/null || cat /etc/os-release | grep PRETTY_NAME | cut -d'"' -f2)"
        echo "Kernel: $(uname -r)"
        echo ""
        echo "## Firewall Status"
        ufw status verbose 2>/dev/null || echo "UFW not installed"
        echo ""
        echo "## SSH Configuration"
        if [[ -f /etc/ssh/sshd_config.d/99-audyn-hardening.conf ]]; then
            echo "Custom SSH hardening: APPLIED"
        else
            echo "Custom SSH hardening: NOT APPLIED"
        fi
        echo ""
        echo "## Kernel Hardening"
        if [[ -f /etc/sysctl.d/99-audyn-hardening.conf ]]; then
            echo "Kernel hardening: APPLIED"
        else
            echo "Kernel hardening: NOT APPLIED"
        fi
        echo ""
        echo "## fail2ban Status"
        systemctl is-active fail2ban 2>/dev/null || echo "fail2ban not running"
        echo ""
        echo "## Automatic Updates"
        systemctl is-active unattended-upgrades 2>/dev/null || echo "Not configured"
        echo ""
        echo "## Audit Daemon"
        systemctl is-active auditd 2>/dev/null || echo "auditd not running"
        echo ""
        echo "## Listening Ports"
        ss -tlnp 2>/dev/null | head -20
        echo ""
        echo "## Backup Location"
        echo "$BACKUP_DIR"
        echo ""
        echo "=========================================="
    } > "$report_file"

    chmod 640 "$report_file"
    log INFO "Report generated: $report_file"
}

# ============================================================================
# Main Execution
# ============================================================================

main() {
    check_root

    # Create directories
    mkdir -p "$(dirname "$LOG_FILE")"
    mkdir -p "$BACKUP_DIR"

    echo ""
    echo "=========================================="
    echo " Audyn System Hardening Script v${SCRIPT_VERSION}"
    echo "=========================================="
    echo ""

    if $DRY_RUN; then
        echo -e "${YELLOW}DRY-RUN MODE: No changes will be made${NC}"
        echo ""
    fi

    log INFO "Starting system hardening (v${SCRIPT_VERSION})"
    log INFO "Backup directory: $BACKUP_DIR"

    echo "This script will apply security hardening to your system."
    echo "All original configurations will be backed up."
    echo ""

    if ! $NON_INTERACTIVE && ! confirm "Do you want to continue?"; then
        echo "Aborted."
        exit 0
    fi

    echo ""

    # Run hardening functions
    local functions=(
        "configure_firewall:Configure UFW firewall"
        "harden_ssh:Harden SSH configuration"
        "apply_kernel_hardening:Apply kernel security parameters"
        "install_fail2ban:Install and configure fail2ban"
        "enable_auto_updates:Enable automatic security updates"
        "disable_unnecessary_services:Disable unnecessary services"
        "harden_systemd_service:Harden Audyn systemd service"
        "configure_audit_logging:Configure audit logging"
        "configure_realtime_permissions:Configure real-time audio permissions"
        "harden_tmp_mounts:Harden temporary filesystem mounts"
    )

    for func_desc in "${functions[@]}"; do
        local func="${func_desc%%:*}"
        local desc="${func_desc##*:}"

        echo ""
        if $NON_INTERACTIVE || confirm "Apply: ${desc}?"; then
            $func || log ERROR "Failed: $desc"
        else
            log INFO "Skipped: $desc"
        fi
    done

    echo ""
    generate_report

    echo ""
    echo "=========================================="
    echo -e "${GREEN} Hardening Complete!${NC}"
    echo "=========================================="
    echo ""
    echo "Log file: $LOG_FILE"
    echo "Backups:  $BACKUP_DIR"
    echo ""
    echo -e "${YELLOW}IMPORTANT:${NC}"
    echo "  - Some changes require a reboot to take effect"
    echo "  - Test SSH access before closing current session"
    echo "  - Review the hardening report for details"
    echo ""

    log INFO "Hardening completed successfully"
}

main "$@"
