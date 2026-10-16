#!/bin/bash
# Renders docs/design/dmg/svg/*.svg to preview/*.png at 2x (Retina) with headless Chromium.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CHROME="${CHROME:-/opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell}"
mkdir -p "$DIR/preview"
for f in "$DIR"/svg/*.svg; do
  read -r w h < <(python3 -I -c 'import re,sys; m=re.search(r"<svg[^>]*width=\"([\d.]+)\"[^>]*height=\"([\d.]+)\"", open(sys.argv[1]).read()); print(int(float(m.group(1))), int(float(m.group(2))))' "$f")
  out="$DIR/preview/$(basename "${f%.svg}").png"
  "$CHROME" --no-sandbox --disable-gpu --hide-scrollbars --default-background-color=00000000 \
    --force-device-scale-factor=2 --window-size="$w,$h" --screenshot="$out" "file://$f" >/dev/null 2>&1
  echo "rendered $(basename "$out")"
done
