# Audyn User Guide

This guide explains how to use the Audyn web interface for managing audio recorders, studios, and recordings.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Overview Dashboard](#overview-dashboard)
3. [Managing Recorders](#managing-recorders)
4. [Studio Configuration](#studio-configuration)
5. [Stream Discovery](#stream-discovery)
6. [File Browser](#file-browser)
7. [Audio Playback](#audio-playback)
8. [Settings](#settings)
9. [User Roles](#user-roles)
10. [Keyboard Shortcuts](#keyboard-shortcuts)
11. [Tips and Best Practices](#tips-and-best-practices)

---

## Getting Started

### Accessing the Interface

Open your web browser and navigate to:

- **Development**: `http://localhost:5173`
- **Production**: Your configured domain (e.g., `https://audyn.example.com`)

### Logging In

**Development Mode:**
- Login is automatic
- You're logged in as "Admin User" with full access

**Production Mode:**
- Click "Sign in with Microsoft"
- Authenticate with your organization's Entra ID
- Your permissions are based on your assigned role

### Navigation

The sidebar contains the main navigation:

| Menu Item | Description | Access |
|-----------|-------------|--------|
| Overview | Dashboard with all recorder meters | All users |
| Files | Browse and play recordings | All users |
| Recorders | Manage recorder instances | Admin only |
| Studios | Configure studios | Admin only |
| Sources | Configure audio sources | Admin only |
| Settings | System configuration | Admin only |

---

## Overview Dashboard

The Overview page provides a real-time view of all active recorders.

### Recorder Cards

Each recorder is displayed as a card showing:

- **Name**: Recorder identifier (e.g., "Recorder 1")
- **Status**: Current state (Recording, Stopped, or Error)
- **Studio**: Assigned studio name and color
- **Audio Meters**: Real-time left/right channel levels
- **Source**: Configured multicast address

### Status Indicators

| Indicator | Meaning |
|-----------|---------|
| Red pulse | Recording in progress |
| Gray | Stopped |
| Orange | Warning or paused |
| Red border | Clipping detected |

### Audio Level Meters

The meters show:
- **Green zone** (-60 to -12 dB): Normal levels
- **Yellow zone** (-12 to -6 dB): Elevated levels
- **Orange zone** (-6 to -3 dB): High levels
- **Red zone** (-3 to 0 dB): Peak levels
- **Peak indicator**: White line showing recent peak

**Healthy levels**: Aim for average levels around -18 dB with peaks not exceeding -6 dB.

### Global Controls (Admin)

At the top of the Overview page:

- **Start All**: Start recording on all active recorders
- **Stop All**: Stop all recorders

### Summary Statistics

At the bottom of the page:
- **Active Recorders**: Number of configured recorders
- **Currently Recording**: Number actively recording
- **Studios Assigned**: Studios with recorders

---

## Managing Recorders

*Admin access required*

### Recorders Page

Access via sidebar: **Admin → Recorders**

### Recorder Table

The table displays:
| Column | Description |
|--------|-------------|
| ID | Recorder number (1-6) |
| Name | Recorder name |
| Status | Current state with icon |
| Studio | Assigned studio |
| Source | Multicast address and port |
| Levels | Mini audio meters |
| Actions | Start/Stop and Settings buttons |

### Configuring Active Recorders

Use the **Active Recorders** dropdown to set how many recorders are enabled (1-6).

*Note: Reducing the count will stop any active recordings on removed recorders.*

### Starting/Stopping Recorders

**Individual Control:**
- Click the play button (▶) to start a recorder
- Click the stop button (■) to stop a recorder

**Bulk Control:**
- Use "Start All" to start all active recorders
- Use "Stop All" to stop all recorders

### Recorder Configuration

Click the settings icon (⚙) on a recorder to access its configuration page.

**Configurable Options:**
- Multicast Address
- Port
- Output Format (WAV/Opus)
- Archive Path

*Note: Configuration cannot be changed while recording.*

---

## Studio Configuration

*Admin access required*

### Studios Page

Access via sidebar: **Admin → Studios**

### Creating a Studio

1. Click **Add Studio**
2. Enter studio details:
   - **Name**: Studio identifier (e.g., "Studio A")
   - **Description**: Optional description
   - **Color**: Select a color for visual identification
3. Click **Create**

### Editing a Studio

1. Click **Edit** on the studio card
2. Modify the details
3. Click **Save**

### Assigning Recorders

Each studio can have one recorder assigned:

1. Find the studio card
2. Use the **Assign Recorder** dropdown
3. Select a recorder (only unassigned recorders are shown)

To unassign:
1. Use the dropdown
2. Select "None" or clear the selection

### Deleting a Studio

1. Click **Delete** on the studio card
2. Confirm the deletion

*Note: Deleting a studio automatically unassigns its recorder.*

### Studio Colors

Colors help identify studios throughout the interface:
- Recorder cards show the studio color on the left border
- The Overview shows studio assignment with colored chips

---

## Stream Discovery

*Admin access required*

Stream Discovery allows you to find AES67 audio streams on your network using SAP (Session Announcement Protocol) and import them as sources with channel selection.

### Accessing Stream Discovery

Access via sidebar: **Admin → Sources**, then click **Discover Streams**

### Starting Discovery

1. Click **Discover Streams** on the Sources page
2. The Stream Browser dialog opens
3. Click **Start Discovery** to begin listening for SAP announcements
4. Discovered streams appear in the list automatically

### Discovery Status

The status indicator shows:
| Indicator | Meaning |
|-----------|---------|
| Green "Listening" | Discovery is active and receiving announcements |
| Red "Stopped" | Discovery is not running |

The stats show:
- **Active streams**: Number of streams currently discovered
- **Packets received**: Total SAP packets processed

### Discovered Streams

Each discovered stream displays:
| Field | Description |
|-------|-------------|
| Name | Session name from SDP |
| Address | Multicast address and port |
| Format | Encoding type (L24, L16) and sample rate |
| Channels | Number of channels in the stream |

### Importing a Stream as a Source

1. Find the desired stream in the list
2. Click **Add as Source**
3. The import dialog opens

### Channel Selection

For multi-channel streams (more than 2 channels), you can select which channels to record:

| Option | Description |
|--------|-------------|
| Output Channels | Number of channels to record (1, 2, etc.) |
| Start at Channel | First channel to capture (0-based offset) |

**Example:** To record channels 5-6 from a 16-channel stream:
- Output Channels: 2 (Stereo)
- Start at Channel: 4 (channels are 0-indexed)

The **Selected Channels** preview shows which channels will be recorded.

### Import Options

| Field | Description |
|-------|-------------|
| Source Name | Custom name (defaults to stream session name) |
| Description | Optional description |

### Completing the Import

1. Configure channel selection (if needed)
2. Optionally customize the name
3. Click **Add Source**
4. The stream is now available as a source

### Stopping Discovery

Click **Stop Discovery** to stop listening for SAP announcements. Discovered streams remain in the list until the dialog is closed.

### Use Cases

**Calrec Type R Networks:**
- Discover streams published by Calrec Connect
- Select specific channels from multi-channel audio beds
- No need to manually enter multicast addresses

**Multi-Studio Recording:**
- Discover all available studio outputs
- Import each as a separate source
- Assign sources to different recorders

---

## File Browser

### Accessing Files

Access via sidebar: **Files**

### Navigation

**Breadcrumb Navigation:**
- Click any level to jump to that directory
- Click "Archive" to return to the root

**Folder Navigation:**
- Click folder icons to enter subdirectories
- The current path is shown in the breadcrumb

### Studio Filter (Admin)

Admins can filter files by studio using the **Studio** dropdown.

*Note: Studio users automatically see only their studio's files.*

### File List

The table displays:
| Column | Description |
|--------|-------------|
| Name | Filename with format icon |
| Size | File size (KB, MB, GB) |
| Modified | Last modification date |
| Format | File format (wav, opus) |
| Actions | Play, Download, Delete |

### File Format Icons

| Icon Color | Format |
|------------|--------|
| Blue | WAV |
| Purple | Opus/Ogg |
| Green | MP3 |

### Sorting and Searching

- Click column headers to sort
- Use the search box to filter by filename

---

## Audio Playback

### Preview Player

1. Click the play button (▶) on any file
2. The player appears at the bottom of the page
3. Use standard audio controls:
   - Play/Pause
   - Seek bar
   - Volume control

### Downloading Files

Click the download button (↓) to download the file.

### Deleting Files

*Permissions required*

1. Click the delete button (🗑)
2. Confirm the deletion

**Deletion Permissions:**
- Admins can delete any file
- Studio users can delete only their studio's files

---

## Settings

*Admin access required*

The Settings page is organized into four main sections for easy navigation.

### Accessing Settings

Access via sidebar: **Admin → Settings**

### Network Configuration

#### Control Interface

Configure the management network interface:

| Setting | Description |
|---------|-------------|
| Network Interface | Select the NIC for web UI access |
| IP Mode | DHCP or Static |
| IP Address | Static IP address (if static mode) |
| Netmask | Subnet mask (default: 255.255.255.0) |
| Gateway | Default gateway for outbound traffic |
| DNS Servers | DNS servers for name resolution |
| Bind to IP only | Restrict web interface to this IP |

**Warning:** Changing the IP address may disconnect you. Ensure you can access the new address.

#### AES67 Interface

Configure the audio network interface:

| Setting | Description |
|---------|-------------|
| Network Interface | Select the NIC for AES67 multicast |
| IP Mode | DHCP or Static |
| IP Address | Static IP for audio network |
| Netmask | Subnet mask |
| Gateway | Usually not needed for AES67 |

**Note:** AES67 networks are typically isolated and don't require gateway or DNS.

### Recording Settings

#### Archive Storage

| Setting | Description |
|---------|-------------|
| Archive Directory | Root path for recordings |
| File Naming Layout | How files are organized (flat, dailydir, etc.) |
| Rotation Period | How often to create new files (in seconds) |

#### Timing & Sync

| Setting | Description |
|---------|-------------|
| Clock Source | Local Time, UTC, or PTP/TAI |
| PTP Interface | Network interface for PTP synchronization |

### System Configuration

#### Hostname & Time

| Setting | Description |
|---------|-------------|
| Hostname | System hostname (requires restart) |
| Timezone | System timezone |
| NTP Servers | Time servers for synchronization |

#### SSL Certificate

Two options for enabling HTTPS:

**Let's Encrypt (Automatic):**
1. Enter your domain name
2. Enter your email address
3. Click "Enable HTTPS"
4. Requires port 80 to be accessible from the internet

**Manual Upload:**
1. Enter your domain name
2. Upload your certificate file (.crt, .pem)
3. Upload your private key file (.key, .pem)
4. Click "Upload Certificate"

Use manual upload when the server is not publicly accessible.

### Security & Authentication

#### Microsoft Entra ID

Configure Single Sign-On:

| Setting | Description |
|---------|-------------|
| Tenant ID | Azure AD tenant identifier |
| Client ID | Application (client) ID |
| Client Secret | Application secret (leave empty to keep existing) |
| Redirect URI | OAuth callback URL |

#### Emergency Access

The breakglass password provides admin access when Entra ID is unavailable:

1. Enter a strong password
2. Confirm the password
3. Click "Save All Settings"

**Important:** Change the default breakglass password immediately after installation.

### Saving Settings

- Click **Save All Settings** at the top right to save changes
- Network changes require clicking the individual "Apply" buttons
- Some changes (like hostname) require a system restart

---

## User Roles

### Administrator

Full access to all features:
- View all recorders and studios
- Start/stop any recorder
- Configure all settings
- Access all files
- Delete any recording

### Studio User

Limited access based on assigned studio:
- View all recorder meters (read-only)
- Access own studio's files
- Preview and download recordings
- Delete own studio's recordings

*Note: Studio users cannot access admin pages (Recorders, Studios, Sources, Settings).*

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `/` | Focus search box |
| `Esc` | Close dialog/modal |
| `Space` | Play/Pause (when player focused) |

---

## Tips and Best Practices

### Monitoring Audio Levels

1. **Check regularly**: Glance at the Overview page periodically
2. **Watch for clipping**: Red indicators mean audio is too hot
3. **Verify silence**: Flat meters during expected audio may indicate problems

### Recording Best Practices

1. **Start before needed**: Begin recording slightly before the scheduled time
2. **Monitor continuously**: Keep the Overview page visible
3. **Check file sizes**: Growing file sizes confirm active recording

### File Management

1. **Regular cleanup**: Delete old recordings to free space
2. **Download important files**: Backup critical recordings
3. **Check before deleting**: Verify you're deleting the correct file

### Troubleshooting

**No audio levels showing:**
- Check if the recorder is started
- Verify the source is transmitting
- Check network connectivity

**Recording won't start:**
- Verify configuration is complete
- Check available disk space
- Review error messages

**Cannot access admin features:**
- Verify your account has admin role
- Try logging out and back in
- Contact your administrator

### Performance Tips

1. **Close unused browser tabs**: WebSocket connections use resources
2. **Use a modern browser**: Chrome, Firefox, or Edge recommended
3. **Stable network**: Avoid recording control over unstable connections

---

## Getting Help

If you encounter issues:

1. Check the error message displayed
2. Review this documentation
3. Contact your system administrator
4. File an issue on [GitHub](https://github.com/brianwynne/Audyn/issues)

---

## Next Steps

- [API Reference](API.md) - For integrating with other systems
- [Configuration Guide](CONFIGURATION.md) - For advanced configuration
