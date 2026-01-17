#!/bin/bash
#
# Audyn Debian Package Builder
#
# Builds a .deb package containing:
#   - audyn binary
#   - Python backend
#   - Vue.js frontend (built)
#   - systemd service
#   - nginx configuration
#
# Usage: ./build-deb.sh [version]
#

set -ex

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$SCRIPT_DIR/build"
DEBIAN_DIR="$SCRIPT_DIR/debian"

VERSION="${1:-1.0.0}"
ARCH="$(dpkg --print-architecture)"
PACKAGE_NAME="audyn_${VERSION}_${ARCH}"

echo "========================================"
echo "  Building Audyn $VERSION ($ARCH)"
echo "========================================"
echo ""
echo "SCRIPT_DIR: $SCRIPT_DIR"
echo "PROJECT_DIR: $PROJECT_DIR"
echo "BUILD_DIR: $BUILD_DIR"
echo "DEBIAN_DIR: $DEBIAN_DIR"
echo ""

# Clean previous build
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR/$PACKAGE_NAME"

# Copy DEBIAN control files
echo "[1/6] Copying DEBIAN control files..."
cp -r "$DEBIAN_DIR/DEBIAN" "$BUILD_DIR/$PACKAGE_NAME/"
chmod 755 "$BUILD_DIR/$PACKAGE_NAME/DEBIAN/postinst"
chmod 755 "$BUILD_DIR/$PACKAGE_NAME/DEBIAN/prerm"
chmod 755 "$BUILD_DIR/$PACKAGE_NAME/DEBIAN/postrm"

# Update version and architecture in control file
sed -i "s/^Version:.*/Version: $VERSION/" "$BUILD_DIR/$PACKAGE_NAME/DEBIAN/control"
sed -i "s/^Architecture:.*/Architecture: $ARCH/" "$BUILD_DIR/$PACKAGE_NAME/DEBIAN/control"

# Build audyn binary
echo "[2/6] Building audyn binary..."
cd "$PROJECT_DIR"
make clean || true
make
ls -la audyn
mkdir -p "$BUILD_DIR/$PACKAGE_NAME/usr/bin"
cp "$PROJECT_DIR/audyn" "$BUILD_DIR/$PACKAGE_NAME/usr/bin/"
chmod 755 "$BUILD_DIR/$PACKAGE_NAME/usr/bin/audyn"

# Copy backend
echo "[3/6] Copying backend..."
mkdir -p "$BUILD_DIR/$PACKAGE_NAME/opt/audyn/backend"
cp -r "$PROJECT_DIR/web/backend/app" "$BUILD_DIR/$PACKAGE_NAME/opt/audyn/backend/"
cp "$PROJECT_DIR/web/backend/requirements.txt" "$BUILD_DIR/$PACKAGE_NAME/opt/audyn/backend/"

# Build frontend
echo "[4/6] Building frontend..."
cd "$PROJECT_DIR/web/frontend"
echo "Node version: $(node --version)"
echo "NPM version: $(npm --version)"
echo "Running npm ci..."
npm ci --loglevel verbose 2>&1 || { echo "npm ci failed"; exit 1; }
echo "Running npm run build..."
npm run build 2>&1 || { echo "npm run build failed"; exit 1; }
ls -la dist/
mkdir -p "$BUILD_DIR/$PACKAGE_NAME/opt/audyn/frontend"
cp -r "$PROJECT_DIR/web/frontend/dist/"* "$BUILD_DIR/$PACKAGE_NAME/opt/audyn/frontend/"

# Copy systemd service
echo "[5/6] Copying systemd service..."
mkdir -p "$BUILD_DIR/$PACKAGE_NAME/lib/systemd/system"
cp "$DEBIAN_DIR/lib/systemd/system/audyn-web.service" \
   "$BUILD_DIR/$PACKAGE_NAME/lib/systemd/system/"

# Copy nginx config
mkdir -p "$BUILD_DIR/$PACKAGE_NAME/etc/nginx/sites-available"
cp "$DEBIAN_DIR/etc/nginx/sites-available/audyn" \
   "$BUILD_DIR/$PACKAGE_NAME/etc/nginx/sites-available/"

# Copy logrotate config
mkdir -p "$BUILD_DIR/$PACKAGE_NAME/etc/logrotate.d"
cp "$DEBIAN_DIR/etc/logrotate.d/audyn" \
   "$BUILD_DIR/$PACKAGE_NAME/etc/logrotate.d/"

# Copy scripts
mkdir -p "$BUILD_DIR/$PACKAGE_NAME/opt/audyn/scripts"
cp "$DEBIAN_DIR/opt/audyn/scripts/"*.sh \
   "$BUILD_DIR/$PACKAGE_NAME/opt/audyn/scripts/"
chmod 755 "$BUILD_DIR/$PACKAGE_NAME/opt/audyn/scripts/"*.sh

# Create empty config directory (will be populated by postinst)
mkdir -p "$BUILD_DIR/$PACKAGE_NAME/etc/audyn"

# Set permissions
find "$BUILD_DIR/$PACKAGE_NAME" -type d -exec chmod 755 {} \;
find "$BUILD_DIR/$PACKAGE_NAME/opt" -type f -exec chmod 644 {} \;
find "$BUILD_DIR/$PACKAGE_NAME/opt" -name "*.py" -exec chmod 644 {} \;

# Build the .deb package
echo "[6/6] Building .deb package..."
cd "$BUILD_DIR"
dpkg-deb --build --root-owner-group "$PACKAGE_NAME"

# Move to packaging directory
mv "$PACKAGE_NAME.deb" "$SCRIPT_DIR/"

# Show package info
echo ""
ls -la "$SCRIPT_DIR/$PACKAGE_NAME.deb"
dpkg-deb --info "$SCRIPT_DIR/$PACKAGE_NAME.deb"

# Cleanup
rm -rf "$BUILD_DIR"

echo ""
echo "========================================"
echo "  Build complete!"
echo "========================================"
echo ""
echo "Package: $SCRIPT_DIR/$PACKAGE_NAME.deb"
echo ""
echo "Install with:"
echo "  sudo dpkg -i $PACKAGE_NAME.deb"
echo "  sudo apt-get install -f  # Install dependencies"
echo ""
echo "Or in one step:"
echo "  sudo apt install ./$PACKAGE_NAME.deb"
echo ""
