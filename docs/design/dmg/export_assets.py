#!/usr/bin/env python3
# =============================================================================
#  Builds the shipping DMG / installer assets from the approved design
#  (plan 1; run make_dmg.py first):
#    packaging/macos/dmg/background.png, background@2x.png
#        Finder window background. dmgbuild combines the pair into one
#        HiDPI TIFF (tiffutil -cathidpicheck) when it finds the @2x file.
#    packaging/macos/installer/installer.icns
#        icon of "MODEL 600 Installer.app", 16-1024 px.
# =============================================================================
import os
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
SVG = os.path.join(HERE, "svg")
CHROME = os.environ.get("CHROME", "/opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell")


def render(svg, out, w, h, scale):
    subprocess.run([CHROME, "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", f"--force-device-scale-factor={scale}",
                    f"--window-size={w},{h}", f"--screenshot={out}", f"file://{svg}"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    im = Image.open(out)
    if im.size != (w * scale, h * scale):
        sys.exit(f"{out}: got {im.size}, expected {(w * scale, h * scale)}")


def main():
    bg = os.path.join(SVG, "bg_plan1.svg")
    icon = os.path.join(SVG, "installer_icon.svg")
    for p in (bg, icon):
        if not os.path.exists(p):
            sys.exit(f"missing {p}: run make_dmg.py first")

    dmg = os.path.join(ROOT, "packaging", "macos", "dmg")
    os.makedirs(dmg, exist_ok=True)
    for scale, name in ((1, "background.png"), (2, "background@2x.png")):
        out = os.path.join(dmg, name)
        render(bg, out, 660, 420, scale)
        # Opaque RGB: Finder draws the image as is; no alpha needed.
        Image.open(out).convert("RGB").save(out, optimize=True)
        print("wrote", os.path.relpath(out, ROOT))

    inst = os.path.join(ROOT, "packaging", "macos", "installer")
    os.makedirs(inst, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "icon.png")
        render(icon, png, 1024, 1024, 1)
        im = Image.open(png).convert("RGBA")
        out = os.path.join(inst, "installer.icns")
        im.save(out, format="ICNS",
                sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])
        print("wrote", os.path.relpath(out, ROOT))


if __name__ == "__main__":
    main()
