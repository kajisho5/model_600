#!/bin/bash
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  install_mac.command  -  macOS installer + Gatekeeper quarantine release.
#
#  Double-clickable from Finder. Clears the com.apple.quarantine xattr that
#  Gatekeeper stamps on downloaded bundles (the "cannot be opened because the
#  developer cannot be verified" dialog) and installs the plugin into the
#  per-user OBS plugins folder. No sudo required.
# =============================================================================
set -euo pipefail

# Resolve the directory this script lives in (handles spaces + symlinks).
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

PLUGIN_NAME="model600.plugin"
DEST="$HOME/Library/Application Support/obs-studio/plugins"

echo "================================================================"
echo " MODEL 600 // BROADCAST SUITE  -  macOS Installer"
echo " SEVENTHWELL"
echo "================================================================"

# Locate the .plugin bundle (next to this script, or in ./build).
BUNDLE=""
for cand in "$SCRIPT_DIR/$PLUGIN_NAME" \
            "$SCRIPT_DIR/build/$PLUGIN_NAME" \
            "$SCRIPT_DIR"/*/"$PLUGIN_NAME"; do
    if [ -d "$cand" ]; then BUNDLE="$cand"; break; fi
done

if [ -z "$BUNDLE" ]; then
    echo "ERROR: $PLUGIN_NAME not found next to this installer."
    echo "Build it first (./setup_project.py) or place the .plugin here."
    read -r -p "Press Return to close."
    exit 1
fi

echo "Found plugin: $BUNDLE"
echo "Releasing Gatekeeper quarantine..."
xattr -dr com.apple.quarantine "$BUNDLE" 2>/dev/null || true

echo "Installing to: $DEST"
mkdir -p "$DEST"
rm -rf "$DEST/$PLUGIN_NAME"
cp -R "$BUNDLE" "$DEST/"

# Re-clear quarantine on the installed copy for good measure.
xattr -dr com.apple.quarantine "$DEST/$PLUGIN_NAME" 2>/dev/null || true

echo ""
echo "DONE. Restart OBS Studio, then add a filter:"
echo "  MODEL 600 - [01] VOCAL MASTER  (and [02]-[05] as needed)."
echo ""
read -r -p "Press Return to close."
