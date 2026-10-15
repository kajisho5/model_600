#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 GUI v2 ("hardware") - 2D layers for the Blender panel render.
#
#  One layout drives both the printed panel legends (SVG -> PNG decal) and the
#  3D part placement in Blender (layout.json), so scales always line up with
#  the knobs. Also draws the VU meter dial face.
#
#    python3 docs/design/v2/make_textures.py
# =============================================================================
import json
import math
import os
import subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
TEX = os.path.join(HERE, "tex")
CHROME = os.environ.get("CHROME", "/opt/pw-browsers/chromium_headless_shell-1194/chrome-linux/headless_shell")

W, H = 1100, 480                  # Logical panel size (px).
INK = "#E9E3D3"                   # Off-white panel print.
INK_DIM = "#A9A497"
AMBER = "#F0A33A"
FONT = "Inter, 'Noto Sans CJK JP', sans-serif"

# Knob sweep: 7 o'clock (-135 deg) .. 5 o'clock (+135 deg); 0 deg = 12 o'clock.
SWEEP = 135.0

LAYOUT = {
    "panel": {"w": W, "h": H, "ear": 40},
    "screws": [[20, 64], [20, 416], [1080, 64], [1080, 416]],
    "slots": [[20, 64], [20, 416], [1080, 64], [1080, 416]],
    "vu": {"cx": 550, "cy": 172, "w": 320, "h": 184, "face_w": 284, "face_h": 148, "needle_vu": -1.0},
    "knobs_big": [
        {"id": "input", "cx": 168, "cy": 318, "r": 60, "label": "INPUT GAIN", "unit": "dB",
         "min": -18, "max": 24, "value": 3.5, "marks": [-18, -12, -6, 0, 6, 12, 18, 24]},
        {"id": "output", "cx": 940, "cy": 318, "r": 60, "label": "OUTPUT TRIM", "unit": "dB",
         "min": -24, "max": 24, "value": 0.0, "marks": [-24, -18, -12, -6, 0, 6, 12, 18, 24]},
    ],
    "knobs_step": [
        {"id": "character", "cx": 330, "cy": 352, "r": 28, "label": "CHARACTER",
         "positions": ["SOFT", "BCAST", "LOUD"], "spread": 55, "value": 1},
        {"id": "space", "cx": 758, "cy": 352, "r": 28, "label": "SPACE FX",
         "positions": ["DRY", "KARAOKE", "BATH", "HALL", "CAVE", "RADIO"], "spread": 120, "value": 0},
    ],
    "toggles": [
        {"id": "meter", "cx": 550, "cy": 300, "label_l": "OUT", "label_r": "GR", "title": "METER", "horizontal": True, "on": False},
        {"id": "mono", "cx": 846, "cy": 104, "label_up": "AUTO", "label_dn": "OFF", "title": "MONO", "on": True},
        {"id": "latency", "cx": 924, "cy": 104, "label_up": "1.5ms", "label_dn": "0ms", "title": "LOOK-AHEAD", "on": True},
    ],
    "buttons": [
        {"id": "cal", "cx": 433, "cy": 388, "w": 54, "h": 34, "label": "CAL 10s", "color": "amber", "lit": False},
        {"id": "ab", "cx": 511, "cy": 388, "w": 54, "h": 34, "label": "A/B", "color": "amber", "lit": True},
        {"id": "bleep", "cx": 589, "cy": 388, "w": 54, "h": 34, "label": "BLEEP", "color": "red", "lit": False},
        {"id": "cough", "cx": 667, "cy": 388, "w": 54, "h": 34, "label": "COUGH", "color": "amber", "lit": False},
    ],
    "jewel": {"cx": 1002, "cy": 104, "label": "POWER"},
    "links": {"x": 806, "y": 168, "dx": 38, "lit": [True, True, True, True, True]},
}


def knob_angle(value, lo, hi):
    return -SWEEP + (value - lo) / (hi - lo) * 2 * SWEEP


def polar(cx, cy, r, deg):
    t = math.radians(deg - 90)
    return cx + r * math.cos(t), cy + r * math.sin(t)


def text(x, y, s, size, fill=INK, weight=600, anchor="middle", ls=0.0, family=FONT):
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" font-weight="{weight}" '
            f'text-anchor="{anchor}" letter-spacing="{ls}" font-family="{family}">{s}</text>')


def panel_svg():
    o = []
    # Brand block.
    o.append(text(78, 66, "SEVENTHWELL", 11, AMBER, 700, "start", 3.2))
    o.append(text(76, 108, "MODEL 600", 40, INK, 800, "start", 1.5))
    o.append(text(78, 132, "VOCAL MASTER  ·  UNIT 01", 11, INK_DIM, 600, "start", 2.4))
    o.append(f'<rect x="78" y="144" width="236" height="1.2" fill="{INK_DIM}" opacity="0.6"/>')
    o.append(text(78, 160, "BROADCAST LEVELING AMPLIFIER", 8.5, INK_DIM, 600, "start", 1.8))
    # VU caption.
    vu = LAYOUT["vu"]
    o.append(text(vu["cx"], vu["cy"] + vu["h"] / 2 + 16, "OUTPUT LEVEL", 8.5, INK_DIM, 600, "middle", 2.2))
    # Big knobs: ticks + numbers + labels.
    for k in LAYOUT["knobs_big"]:
        cx, cy, r = k["cx"], k["cy"], k["r"]
        for v in range(k["min"], k["max"] + 1, 3):
            a = knob_angle(v, k["min"], k["max"])
            major = v in k["marks"]
            x1, y1 = polar(cx, cy, r + 8, a)
            x2, y2 = polar(cx, cy, r + (16 if major else 12), a)
            o.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{INK}" '
                     f'stroke-width="{1.6 if major else 1.0}" stroke-linecap="round"/>')
        for v in k["marks"]:
            a = knob_angle(v, k["min"], k["max"])
            x, y = polar(cx, cy, r + 28, a)
            lab = f"+{v}" if v > 0 else str(v)
            o.append(text(x, y + 4, lab, 10.5, INK, 600))
        o.append(text(cx, cy + r + 52, k["label"], 11, INK, 700, "middle", 2.4))
        o.append(text(cx, cy + r + 66, k["unit"], 8.5, INK_DIM, 600, "middle", 1.5))
    # Stepped knobs: position marks + names.
    for k in LAYOUT["knobs_step"]:
        cx, cy, r = k["cx"], k["cy"], k["r"]
        n = len(k["positions"])
        for i, name in enumerate(k["positions"]):
            a = -k["spread"] + (2 * k["spread"] * i / (n - 1) if n > 1 else 0)
            x1, y1 = polar(cx, cy, r + 6, a)
            x2, y2 = polar(cx, cy, r + 12, a)
            o.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{INK}" stroke-width="1.6" stroke-linecap="round"/>')
            sa = math.sin(math.radians(a))
            anchor = "end" if sa < -0.5 else ("start" if sa > 0.5 else "middle")
            x, y = polar(cx, cy, r + (17 if anchor != "middle" else 24), a)
            o.append(text(x, y + 3.5, name, 8.5, INK, 700, anchor, 0.8))
        o.append(text(cx, cy + r + 34, k["label"], 10, INK, 700, "middle", 2.2))
    # Toggles.
    for t in LAYOUT["toggles"]:
        cx, cy = t["cx"], t["cy"]
        if t.get("horizontal"):
            o.append(text(cx - 30, cy + 4, t["label_l"], 9, INK, 700, "end", 1.2))
            o.append(text(cx + 30, cy + 4, t["label_r"], 9, INK, 700, "start", 1.2))
            o.append(text(cx, cy + 30, t["title"], 8.5, INK_DIM, 600, "middle", 2.0))
        else:
            o.append(text(cx, cy - 26, t["label_up"], 8.5, INK, 700, "middle", 1.0))
            o.append(text(cx, cy + 34, t["label_dn"], 8.5, INK, 700, "middle", 1.0))
            o.append(text(cx, cy + 50, t["title"], 8, INK_DIM, 600, "middle", 1.6))
    # Buttons.
    for b in LAYOUT["buttons"]:
        o.append(text(b["cx"], b["cy"] + b["h"] / 2 + 16, b["label"], 8.5, INK, 700, "middle", 1.2))
    # Jewel lamp.
    j = LAYOUT["jewel"]
    o.append(text(j["cx"], j["cy"] + 34, j["label"], 8, INK_DIM, 600, "middle", 1.6))
    # LINK lamps.
    L = LAYOUT["links"]
    o.append(text(L["x"] - 20, L["y"] + 4, "LINK", 8, INK_DIM, 600, "end", 1.8))
    for i in range(5):
        o.append(text(L["x"] + i * L["dx"], L["y"] + 22, f"0{i + 1}", 8, INK, 700, "middle", 0.8))
    # Serial plate text.
    o.append(text(W / 2, H - 18, "DESIGNED BY SEVENTHWELL  ·  SERIAL 600-00001", 7.5, INK_DIM, 600, "middle", 2.0))
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">'
            + "".join(o) + "</svg>")


def vu_deflection(vu_db):
    """Classic VU law: deflection proportional to voltage; 0 VU at ~71% of +3."""
    v = 10 ** (vu_db / 20) / 10 ** (3 / 20)
    return -46 + 92 * v


def vu_face_svg():
    fw, fh = LAYOUT["vu"]["face_w"], LAYOUT["vu"]["face_h"]
    cx, pivot_y = fw / 2, fh + 40          # Pivot sits below the visible face.
    r_scale = fh - 6
    o = [f'<defs><radialGradient id="g" cx="50%" cy="85%" r="80%">'
         f'<stop offset="0%" stop-color="#FFF4D6"/><stop offset="70%" stop-color="#F2DFAE"/>'
         f'<stop offset="100%" stop-color="#C9A866"/></radialGradient></defs>',
         f'<rect width="{fw}" height="{fh}" fill="url(#g)"/>']

    def arc(r, a0, a1, color, width):
        x0, y0 = polar(cx, pivot_y, r, a0)
        x1, y1 = polar(cx, pivot_y, r, a1)
        return (f'<path d="M{x0:.2f} {y0:.2f} A{r} {r} 0 0 1 {x1:.2f} {y1:.2f}" fill="none" '
                f'stroke="{color}" stroke-width="{width}"/>')
    o.append(arc(r_scale, vu_deflection(-20), vu_deflection(0), "#1A1A1A", 1.6))
    o.append(arc(r_scale, vu_deflection(0), vu_deflection(3), "#C62F25", 1.6))
    o.append(arc(r_scale + 4, vu_deflection(0), vu_deflection(3), "#C62F25", 6))
    for db, lab in [(-20, "20"), (-10, "10"), (-7, "7"), (-5, "5"), (-3, "3"), (-2, "2"), (-1, "1"),
                    (0, "0"), (1, "1"), (2, "2"), (3, "3")]:
        a = vu_deflection(db)
        red = db >= 0
        col = "#C62F25" if red else "#1A1A1A"
        x1, y1 = polar(cx, pivot_y, r_scale, a)
        x2, y2 = polar(cx, pivot_y, r_scale + 10, a)
        o.append(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{col}" stroke-width="1.6"/>')
        x, y = polar(cx, pivot_y, r_scale + 21, a)
        sign = "+" if db > 0 else ("-" if db < 0 else "")
        o.append(text(x, y + 4, f"{sign}{lab}", 11, col, 700, "middle", 0, "Inter, sans-serif"))
    for db in (-15, -8, -6, -4):
        a = vu_deflection(db)
        x1, y1 = polar(cx, pivot_y, r_scale, a)
        x2, y2 = polar(cx, pivot_y, r_scale + 6, a)
        o.append(f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="#1A1A1A" stroke-width="1"/>')
    # Percentage sub-scale.
    for pct in (20, 40, 60, 80, 100):
        a = -46 + 92 * (pct / 100) * 0.708
        x, y = polar(cx, pivot_y, r_scale - 14, a)
        o.append(text(x, y + 3, str(pct), 7, "#3A3A3A", 600, "middle", 0, "Inter, sans-serif"))
    o.append(text(18, 22, "VU", 15, "#1A1A1A", 800, "start", 0, "Inter, sans-serif"))
    o.append(text(fw - 18, 22, "VU", 15, "#C62F25", 800, "end", 0, "Inter, sans-serif"))
    o.append(text(cx, fh - 30, "SEVENTHWELL", 8, "#5A4A2A", 700, "middle", 2.4, "Inter, sans-serif"))
    o.append(text(cx, fh - 18, "MODEL 600  ·  BROADCAST VU", 6.5, "#6A5A3A", 600, "middle", 1.6, "Inter, sans-serif"))
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{fw}" height="{fh}" viewBox="0 0 {fw} {fh}">'
            + "".join(o) + "</svg>")


def render(svg_path, png_path, w, h, scale):
    subprocess.run([CHROME, "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", f"--force-device-scale-factor={scale}",
                    f"--window-size={w},{h}", f"--screenshot={png_path}", f"file://{svg_path}"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def main():
    os.makedirs(TEX, exist_ok=True)
    with open(os.path.join(TEX, "layout.json"), "w") as f:
        json.dump(LAYOUT, f, indent=2)
    with open(os.path.join(TEX, "panel_decal.svg"), "w", encoding="utf-8") as f:
        f.write(panel_svg())
    with open(os.path.join(TEX, "vu_face.svg"), "w", encoding="utf-8") as f:
        f.write(vu_face_svg())
    render(os.path.join(TEX, "panel_decal.svg"), os.path.join(TEX, "panel_decal.png"), W, H, 3)
    vu = LAYOUT["vu"]
    render(os.path.join(TEX, "vu_face.svg"), os.path.join(TEX, "vu_face.png"), vu["face_w"], vu["face_h"], 4)
    print("textures written to", TEX)


if __name__ == "__main__":
    main()
