#!/bin/bash
# Mock Audyn binary for development/testing
# Creates dummy audio files to test the web UI
#
# Usage mimics real audyn:
#   mock_audyn.sh --archive-root /path --archive-layout dailydir --archive-period 60 ...

# Parse arguments
ARCHIVE_ROOT="/var/lib/audyn"
ARCHIVE_LAYOUT="dailydir"
ARCHIVE_PERIOD=60
FORMAT="wav"

while [[ $# -gt 0 ]]; do
    case $1 in
        --archive-root) ARCHIVE_ROOT="$2"; shift 2 ;;
        --archive-layout) ARCHIVE_LAYOUT="$2"; shift 2 ;;
        --archive-period) ARCHIVE_PERIOD="$2"; shift 2 ;;
        --archive-suffix) FORMAT="$2"; shift 2 ;;
        --pipewire) PIPEWIRE=1; shift ;;
        -m) MULTICAST="$2"; shift 2 ;;
        -p) PORT="$2"; shift 2 ;;
        -r) SAMPLE_RATE="$2"; shift 2 ;;
        -c) CHANNELS="$2"; shift 2 ;;
        --pt) shift 2 ;;
        --spp) shift 2 ;;
        --bitrate) shift 2 ;;
        --ptp-iface) shift 2 ;;
        *) shift ;;
    esac
done

echo "Mock Audyn starting..."
echo "  Archive root: $ARCHIVE_ROOT"
echo "  Layout: $ARCHIVE_LAYOUT"
echo "  Period: $ARCHIVE_PERIOD seconds"
echo "  Format: $FORMAT"

# Create archive directory
mkdir -p "$ARCHIVE_ROOT"

# Generate files periodically until killed
cleanup() {
    echo "Mock Audyn shutting down..."
    exit 0
}
trap cleanup SIGTERM SIGINT

file_counter=0
while true; do
    # Get current timestamp components
    YEAR=$(date +%Y)
    MONTH=$(date +%m)
    DAY=$(date +%d)
    HOUR=$(date +%H)
    MIN=$(date +%M)
    SEC=$(date +%S)

    # Build filepath based on layout setting
    # These match the layouts defined in the Settings UI
    case $ARCHIVE_LAYOUT in
        flat)
            # Flat: YYYY-MM-DD-HH.ext in root
            FILEPATH="$ARCHIVE_ROOT/${YEAR}-${MONTH}-${DAY}-${HOUR}${MIN}${SEC}.$FORMAT"
            ;;
        dailydir)
            # Daily Directory: YYYY-MM-DD/YYYY-MM-DD-HHMMSS.ext
            DATE_DIR="${YEAR}-${MONTH}-${DAY}"
            mkdir -p "$ARCHIVE_ROOT/$DATE_DIR"
            FILEPATH="$ARCHIVE_ROOT/$DATE_DIR/${YEAR}-${MONTH}-${DAY}-${HOUR}${MIN}${SEC}.$FORMAT"
            ;;
        hierarchy)
            # Hierarchy: YYYY/MM/DD/HH/archive.ext
            mkdir -p "$ARCHIVE_ROOT/$YEAR/$MONTH/$DAY/$HOUR"
            FILEPATH="$ARCHIVE_ROOT/$YEAR/$MONTH/$DAY/$HOUR/archive_${MIN}${SEC}.$FORMAT"
            ;;
        combo)
            # Combo: YYYY/MM/DD/HH/YYYY-MM-DD-HH.ext
            mkdir -p "$ARCHIVE_ROOT/$YEAR/$MONTH/$DAY/$HOUR"
            FILEPATH="$ARCHIVE_ROOT/$YEAR/$MONTH/$DAY/$HOUR/${YEAR}-${MONTH}-${DAY}-${HOUR}${MIN}${SEC}.$FORMAT"
            ;;
        accurate)
            # Accurate: with full timestamp including seconds
            DATE_DIR="${YEAR}-${MONTH}-${DAY}"
            mkdir -p "$ARCHIVE_ROOT/$DATE_DIR"
            FILEPATH="$ARCHIVE_ROOT/$DATE_DIR/${YEAR}${MONTH}${DAY}_${HOUR}${MIN}${SEC}.$FORMAT"
            ;;
        *)
            # Default to dailydir
            DATE_DIR="${YEAR}-${MONTH}-${DAY}"
            mkdir -p "$ARCHIVE_ROOT/$DATE_DIR"
            FILEPATH="$ARCHIVE_ROOT/$DATE_DIR/${YEAR}-${MONTH}-${DAY}-${HOUR}${MIN}${SEC}.$FORMAT"
            ;;
    esac

    # Create a dummy file with some content
    echo "Mock audio file created at $(date)" > "$FILEPATH"
    # Add some random bytes to simulate audio data (small for testing)
    dd if=/dev/urandom bs=1024 count=100 >> "$FILEPATH" 2>/dev/null

    echo "Created: $FILEPATH"
    file_counter=$((file_counter + 1))

    # Sleep for the archive period from settings
    # Use TEST_PERIOD env var to override for development testing only
    SLEEP_PERIOD=${TEST_PERIOD:-$ARCHIVE_PERIOD}
    sleep $SLEEP_PERIOD
done
