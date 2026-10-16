#!/bin/bash
# Renders docs/design/icon/svg/*.svg to preview/*.png (transparent) with headless Chromium,
# plus a size sheet (512 / 128 / 64 / 32 / 16) for legibility checks.
set -euo pipefail
DIR="$(cd "$(dirname "$0")" && pwd)"
CHROME="${CHROME:-/opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell}"
mkdir -p "$DIR/preview"
for f in "$DIR"/svg/*.svg; do
  read -r w h < <(python3 -I -c 'import re,sys; m=re.search(r"<svg[^>]*width=\"(\d+)\"[^>]*height=\"(\d+)\"", open(sys.argv[1]).read()); print(m.group(1), m.group(2))' "$f")
  scale=1; case "$(basename "$f")" in dmg_*) scale=2 ;; esac
  out="$DIR/preview/$(basename "${f%.svg}").png"
  "$CHROME" --no-sandbox --disable-gpu --hide-scrollbars --default-background-color=00000000 \
    --force-device-scale-factor=$scale --window-size="$w,$h" --screenshot="$out" "file://$f" >/dev/null 2>&1
  echo "rendered $(basename "$out")"
done
