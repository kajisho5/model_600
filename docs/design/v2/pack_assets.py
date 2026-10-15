#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 GUI v2 - pack the Blender renders into the Qt resource set.
#
#  Input : docs/design/v2/_frames/ (render_hardware.py -- sprites / backgrounds)
#          docs/design/v2/tex/layout.json (make_textures.py)
#  Output: resources/hw/
#            bg_NN.png            static faceplate per unit (2x, RGB)
#            <strip>.png          filmstrips packed in a grid (knobs, selector)
#            <sprite>.png         single-state sprites (levers, caps, lamps, LEDs)
#            hw.json              layout + sprite manifest read by the app
#
#    python3 docs/design/v2/pack_assets.py
# =============================================================================
import json
import math
import os
import shutil
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
FRAMES = os.environ.get("M600_FRAMES", os.path.join(HERE, "_frames"))
OUT = os.path.normpath(os.path.join(HERE, "..", "..", "..", "resources", "hw"))
SCALE = 2                                   # Pixels per logical px in every asset.
STRIPS = ("knob_big", "knob_mid", "knob_trim", "selector")


def main():
    with open(os.path.join(FRAMES, "sprites.json")) as f:
        sprites = json.load(f)
    with open(os.path.join(HERE, "tex", "layout.json")) as f:
        layout = json.load(f)
    if os.path.isdir(OUT):
        shutil.rmtree(OUT)
    os.makedirs(OUT)

    manifest = {"scale": SCALE, "units": layout["units"], "sprites": {}, "backgrounds": {}}
    files = []

    for num in layout["units"]:
        src = os.path.join(FRAMES, f"bg_{num}.png")
        if not os.path.exists(src):
            sys.exit(f"missing background {src}")
        im = Image.open(src).convert("RGBA")
        flat = Image.new("RGB", im.size, (0, 0, 0))
        flat.paste(im, mask=im.split()[3])
        name = f"bg_{num}.png"
        flat.save(os.path.join(OUT, name), optimize=True)
        manifest["backgrounds"][num] = name
        files.append(name)

    for strip in STRIPS:
        spec = sprites[strip]
        n = len(spec["angles"])
        frames = [Image.open(os.path.join(FRAMES, f"{strip}_{i:03d}.png")).convert("RGBA") for i in range(n)]
        fw, fh = frames[0].size
        cols = math.ceil(math.sqrt(n))
        rows = math.ceil(n / cols)
        sheet = Image.new("RGBA", (cols * fw, rows * fh), (0, 0, 0, 0))
        for i, fr in enumerate(frames):
            sheet.paste(fr, ((i % cols) * fw, (i // cols) * fh))
        name = f"{strip}.png"
        sheet.save(os.path.join(OUT, name), optimize=True)
        manifest["sprites"][strip] = {"file": name, "size": spec["size"], "radius": spec["radius"],
                                      "cols": cols, "frames": n, "angles": spec["angles"]}
        files.append(name)

    for key, spec in sprites.items():
        if key in STRIPS:
            continue
        name = f"{key}.png"
        Image.open(os.path.join(FRAMES, name)).convert("RGBA").save(os.path.join(OUT, name), optimize=True)
        w = spec.get("w", spec.get("size"))
        h = spec.get("h", spec.get("size"))
        manifest["sprites"][key] = {"file": name, "w": w, "h": h}
        files.append(name)

    with open(os.path.join(OUT, "hw.json"), "w") as f:
        json.dump(manifest, f, separators=(",", ":"))
    files.append("hw.json")
    total = sum(os.path.getsize(os.path.join(OUT, n)) for n in files)
    print(f"packed {len(files)} files into {OUT} ({total / 1e6:.1f} MB)")
    # File list for CMake (one per line, relative to resources/).
    with open(os.path.join(OUT, "FILES.txt"), "w") as f:
        f.write("\n".join(f"hw/{n}" for n in sorted(files)) + "\n")


if __name__ == "__main__":
    main()
