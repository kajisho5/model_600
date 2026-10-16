#!/usr/bin/env python3
# =============================================================================
#  Builds the shipping icon files from the rendered previews of the chosen
#  concept (run make_icons.py and render.sh first):
#    resources/macos/model600.icns      16-1024 px (CFBundleIconFile)
#    resources/windows/model600.ico     16-256 px (embedded by model600.rc)
# =============================================================================
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
PREVIEW = os.path.join(HERE, "preview")
CHOSEN = "icon_a_dial"


def load(name):
    path = os.path.join(PREVIEW, f"{CHOSEN}_{name}.png")
    if not os.path.exists(path):
        sys.exit(f"missing {path}: run make_icons.py and render.sh first")
    im = Image.open(path).convert("RGBA")
    if im.size != (1024, 1024):
        im = im.resize((1024, 1024), Image.LANCZOS)
    return im


def main():
    mac = load("macos_grid")
    os.makedirs(os.path.join(ROOT, "resources", "macos"), exist_ok=True)
    icns = os.path.join(ROOT, "resources", "macos", "model600.icns")
    mac.save(icns, format="ICNS", sizes=[(16, 16), (32, 32), (64, 64), (128, 128), (256, 256), (512, 512), (1024, 1024)])

    win = load("windows")
    os.makedirs(os.path.join(ROOT, "resources", "windows"), exist_ok=True)
    ico = os.path.join(ROOT, "resources", "windows", "model600.ico")
    sizes = [16, 20, 24, 32, 40, 48, 64, 256]
    frames = [win.resize((n, n), Image.LANCZOS) for n in sizes]
    frames[-1].save(ico, format="ICO", sizes=[(n, n) for n in sizes], append_images=frames[:-1])
    for p in (icns, ico):
        print(f"wrote {os.path.relpath(p, ROOT)} ({os.path.getsize(p)} bytes)")


if __name__ == "__main__":
    main()
