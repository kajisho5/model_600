#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 GUI v2 ("hardware") - 2D layers for the Blender panel renders.
#
#  One layout per unit drives both the printed panel legends (SVG -> PNG decal)
#  and the 3D part placement in Blender (tex/layout.json), so every scale lines
#  up with its knob. Also draws the meter dial faces and the 7-segment windows.
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

W = 1100                          # Logical panel width (px). Height is per unit.
INK = "#E9E3D3"                   # Off-white panel print.
INK_DIM = "#A9A497"
AMBER = "#F0A33A"
FONT = "Inter, 'Noto Sans CJK JP', sans-serif"
FACE_W, FACE_H = 284, 148         # Design size of every dial face (scaled in 3D).

# Unit identity colours (enamel number plates). Amber stays the brand colour.
UNIT_COLORS = {"01": "#F29A1E", "02": "#2BA6BC", "03": "#E2472C", "04": "#57B65F", "05": "#D6AE55"}

# Knob sweep: 7 o'clock (-135 deg) .. 5 o'clock (+135 deg); 0 deg = 12 o'clock.
SWEEP = 135.0

# Clearances (logical px) enforced by check_layout(): printed text keeps this far
# from any physical part, from other text, and parts keep this far from each other.
GAP_TEXT_PART = 10
GAP_TEXT_TEXT = 6
GAP_PART_PART = 12
SAFE_X = (52, W - 52)             # Inside the rack ears.

# Printed scale geometry, relative to the part edge.
BIG_TICK0, BIG_TICK_MAJOR, BIG_TICK_MINOR, BIG_NUM = 12, 21, 17, 35
STEP_TICK0, STEP_TICK1, STEP_NAME_SIDE, STEP_NAME_TOP = 9, 15, 22, 30
TRIM_TICK0, TRIM_TICK_MAJOR, TRIM_TICK_MINOR, TRIM_NUM = 7, 13, 11, 22


# ---- Layout -------------------------------------------------------------------------
def common(number, name, tagline, h=480, links=None, pro=False):
    return {
        "number": number, "name": name, "tagline": tagline, "color": UNIT_COLORS[number], "pro": pro,
        "panel": {"w": W, "h": h, "ear": 40},
        "screws": [[20, 64], [20, h - 64], [1080, 64], [1080, h - 64]],
        "slots": [[20, 64], [20, h - 64], [1080, 64], [1080, h - 64]],
        "jewel": {"cx": 1002, "cy": 104, "label": "POWER"},
        "links": links or {"x": 806, "y": 186, "dx": 38, "lit": [True] * 5},
        "vus": [], "knobs_big": [], "knobs_step": [], "knobs_trim": [], "toggles": [], "buttons": [],
        "lamps": [], "ladders": [], "displays": [], "flow": None, "target": None,
    }


def big(id_, cx, cy, r, label, unit, lo, hi, marks, tick, value, label_y=None):
    return {"id": id_, "cx": cx, "cy": cy, "r": r, "label": label, "unit": unit, "min": lo, "max": hi,
            "marks": marks, "tick": tick, "value": value, "label_y": label_y}


def output_trim():
    return big("output", 940, 312, 54, "OUTPUT TRIM", "dB", -24, 24, [-24, -18, -12, -6, 0, 6, 12, 18, 24], 3, 0.0)


def vu(cx, cy, w, h, face, needle, caption):
    """Analogue VU meter (kept for reference; the current design uses digi())."""
    k = (w - 36) / FACE_W
    return {"cx": cx, "cy": cy, "w": w, "h": h, "face_w": round(FACE_W * k), "face_h": round(FACE_H * k),
            "face": face, "needle": needle, "caption": caption}


DIGI_BEZEL = 14                    # Bezel border around a digital meter window (px).


def digi(cx, cy, w, h, caption, kind, value, bar, label, unit, aux=None, target=None):
    """Digital meter window: what it measures, a big 7-segment number and a segment bar.

    kind: "level" (-36..0, green/amber/red), "ride" (-12..+12, centre zero), "phase" (-1..+1, centre zero).
    """
    return {"cx": cx, "cy": cy, "w": w, "h": h, "caption": caption,
            "digi": {"kind": kind, "value": value, "bar": bar, "label": label, "unit": unit,
                     "aux": aux, "target": target,
                     "win_w": w - 2 * DIGI_BEZEL, "win_h": h - 2 * DIGI_BEZEL}}


def unit01():
    u = common("01", "VOCAL MASTER", "BROADCAST LEVELING AMPLIFIER")
    u["vus"] = [digi(550, 172, 320, 184, "OUTPUT LEVEL", "level", "-13.6", -13.6, "SHORT-TERM", "LUFS",
                     aux="PEAK  -1.2 dBTP")]
    u["knobs_big"] = [big("input", 166, 312, 54, "INPUT GAIN", "dB", -18, 24, [-18, -12, -6, 0, 6, 12, 18, 24], 3, 3.5),
                      output_trim()]
    u["knobs_step"] = [
        {"id": "character", "cx": 346, "cy": 372, "r": 28, "label": "CHARACTER",
         "positions": ["SOFT", "BCAST", "LOUD"], "spread": 55, "value": 1},
        {"id": "space", "cx": 784, "cy": 372, "r": 28, "label": "SPACE FX",
         "positions": ["DRY", "KARAOKE", "BATH", "HALL", "CAVE", "RADIO"], "spread": 120, "value": 0},
    ]
    u["toggles"] = [
        {"id": "meter", "cx": 550, "cy": 318, "label_l": "OUT", "label_r": "GR", "title": "METER", "horizontal": True, "on": False},
        {"id": "mono", "cx": 846, "cy": 104, "label_up": "AUTO", "label_dn": "OFF", "title": "MONO", "on": True},
        {"id": "latency", "cx": 924, "cy": 104, "label_up": "1.5ms", "label_dn": "0ms", "title": "LOOK-AHEAD", "on": True},
    ]
    u["buttons"] = [
        {"id": "cal", "cx": 433, "cy": 396, "w": 54, "h": 34, "label": "CAL 10s", "color": "amber", "lit": False},
        {"id": "ab", "cx": 511, "cy": 396, "w": 54, "h": 34, "label": "A/B", "color": "amber", "lit": True},
        {"id": "bleep", "cx": 589, "cy": 396, "w": 54, "h": 34, "label": "BLEEP", "color": "red", "lit": False},
        {"id": "cough", "cx": 667, "cy": 396, "w": 54, "h": 34, "label": "COUGH", "color": "amber", "lit": False},
    ]
    return u


def unit02():
    # Story: you talk -> the guest is dipped; otherwise the guest is ridden to the target.
    u = common("02", "COLLAB RIDER", "GUEST VOICE AUTO-RIDER",
               links={"x": 908, "y": 186, "dx": 30, "lit": [True] * 5})
    u["vus"] = [digi(500, 160, 220, 152, "GUEST LEVEL", "level", "-16.8", -16.8, "SHORT-TERM", "LUFS",
                     aux="TARGET -14.5", target=-14.5),
                digi(736, 160, 220, 152, "AUTO RIDE", "ride", "-3.5", -3.5, "CUT  /  BOOST", "dB")]
    u["knobs_big"] = [
        big("target", 166, 312, 54, "TARGET LOUDNESS", "LUFS", -24, -8, [-24, -20, -16, -12, -8], 2, -14.5),
        big("duck", 372, 324, 42, "DUCK WHEN I TALK", "dB", -12, 0, [-12, -9, -6, -3, 0], 1.5, -3.5, label_y=426),
        output_trim(),
    ]
    u["flow"] = {"y": 316, "nodes": [["GUEST", 498], ["RIDE", 592], ["DUCK", 686], ["OUT", 774]]}
    u["lamps"] = [
        {"cx": 592, "cy": 374, "color": "amber", "lit": False, "label": "RIDING"},
        {"cx": 686, "cy": 374, "color": "green", "lit": True, "label": "YOU TALK"},
    ]
    return u


def unit03():
    u = common("03", "GAME IMPACT", "GAME AUDIO IMPACT LIMITER")
    u["vus"] = [digi(550, 172, 320, 184, "GAME OUTPUT", "level", "-9.8", -9.8, "SHORT-TERM", "LUFS",
                     aux="PEAK  -0.6 dBTP")]
    u["knobs_big"] = [
        big("ceiling", 166, 312, 54, "IMPACT CEILING", "dB", -12, 0, [-12, -9, -6, -3, 0], 1.5, -6.0),
        big("scoop", 366, 324, 42, "VOICE SCOOP", "dB", -12, 0, [-12, -9, -6, -3, 0], 1.5, -5.0, label_y=426),
        output_trim(),
    ]
    u["ladders"] = [{"cx": 600, "cy": 340, "n": 16, "lit": 6, "label": "LIMITING  dB",
                     "scale": [["0", 0], ["-3", 4], ["-6", 8], ["-9", 12], ["-12", 15]],
                     "colors": [[0.0, "amber"], [0.5, "red"]]}]
    u["lamps"] = [
        {"cx": 540, "cy": 404, "color": "red", "lit": True, "label": "LIMIT"},
        {"cx": 660, "cy": 404, "color": "green", "lit": True, "label": "SCOOPING"},
    ]
    return u


def unit04():
    u = common("04", "BGM POCKET", "MUSIC BED AUTO-DUCKER")
    u["vus"] = [digi(550, 172, 320, 184, "BGM LEVEL", "level", "-24.5", -24.5, "SHORT-TERM", "LUFS",
                     aux="PEAK  -12.1 dBTP")]
    u["knobs_big"] = [
        big("pocket", 166, 312, 54, "POCKET DEPTH", "dB", -12, 0, [-12, -9, -6, -3, 0], 1.5, -8.5),
        output_trim(),
    ]
    u["toggles"] = [
        {"id": "safe", "cx": 318, "cy": 330, "label_up": "ON", "label_dn": "OFF", "title": "SING SAFE", "on": False},
        {"id": "swell", "cx": 398, "cy": 330, "label_up": "ON", "label_dn": "OFF", "title": "AFK SWELL", "on": True},
    ]
    u["ladders"] = [{"cx": 600, "cy": 340, "n": 16, "lit": 11, "label": "POCKET  dB",
                     "scale": [["0", 0], ["-3", 4], ["-6", 8], ["-9", 12], ["-12", 15]],
                     "colors": [[0.0, "green"], [0.6, "amber"]]}]
    u["lamps"] = [
        {"cx": 520, "cy": 404, "color": "amber", "lit": True, "label": "VOICE DUCK"},
        {"cx": 600, "cy": 404, "color": "green", "lit": False, "label": "AFK SWELL"},
        {"cx": 680, "cy": 404, "color": "amber", "lit": False, "label": "COUGH LIFT"},
    ]
    return u


def unit05():
    h = 660
    u = common("05", "STREAM DIRECTOR", "MASTER LOUDNESS DIRECTOR", h=h, pro=True,
               links={"x": 908, "y": 176, "dx": 30, "lit": [True] * 5})
    u["displays"] = [
        {"id": "m", "cx": 496, "cy": 96, "w": 112, "h": 56, "text": "-13.8", "label": "MOMENTARY"},
        {"id": "s", "cx": 640, "cy": 96, "w": 112, "h": 56, "text": "-14.1", "label": "SHORT-TERM"},
        {"id": "i", "cx": 806, "cy": 96, "w": 152, "h": 56, "text": "-14.2", "label": "INTEGRATED  LUFS"},
    ]
    names = [("01", "VOCAL", "-13.9"), ("02", "COLLAB", "-17.2"), ("03", "GAME", "-15.4"), ("04", "BGM", "-26.0")]
    for i, (num, nm, val) in enumerate(names):
        cx = 178 + 248 * i
        u["vus"].append(digi(cx, 290, 216, 148, "", "level", val, float(val), "SHORT-TERM", "LUFS"))
        u["vus"][-1]["chan"] = {"number": num, "name": nm}
        u["knobs_trim"].append({"cx": cx + 72, "cy": 418, "r": 17, "label": "TRIM", "value": 0.0, "min": -12, "max": 12})
    u["target"] = {"title": "YOUTUBE TARGET  -12 \u00b11.5 LUFS", "x": 96, "y": 506}
    u["lamps"] = [
        {"cx": 120, "cy": 544, "color": "amber", "lit": False, "label": "LOW"},
        {"cx": 190, "cy": 544, "color": "green", "lit": True, "label": "IN RANGE"},
        {"cx": 260, "cy": 544, "color": "red", "lit": False, "label": "HIGH"},
    ]
    u["vus"].append(digi(420, 540, 168, 104, "PHASE", "phase", "0.62", 0.62, "CORRELATION", ""))
    u["knobs_step"] = [{"id": "scene", "cx": 604, "cy": 548, "r": 26, "label": "SCENE",
                        "positions": ["FPS", "TALK", "SING", "ASMR"], "spread": 75, "value": 1}]
    u["buttons"] = [
        {"id": "mono", "cx": 732, "cy": 538, "w": 52, "h": 32, "label": "MONO", "color": "amber", "lit": False},
        {"id": "reset", "cx": 804, "cy": 538, "w": 52, "h": 32, "label": "RESET", "color": "amber", "lit": False},
        {"id": "chapter", "cx": 876, "cy": 538, "w": 52, "h": 32, "label": "CHAPTER", "color": "amber", "lit": True},
        {"id": "report", "cx": 948, "cy": 538, "w": 52, "h": 32, "label": "REPORT", "color": "amber", "lit": False},
    ]
    return u


UNITS = {"01": unit01(), "02": unit02(), "03": unit03(), "04": unit04(), "05": unit05()}


# ---- SVG helpers ----------------------------------------------------------------------
def knob_angle(value, lo, hi):
    return -SWEEP + (value - lo) / (hi - lo) * 2 * SWEEP


def polar(cx, cy, r, deg):
    t = math.radians(deg - 90)
    return cx + r * math.cos(t), cy + r * math.sin(t)


TEXTS = []                         # (x0, y0, x1, y1, string) of every printed text.
_FONTS = {}


def text_box(x, y, s, size, weight, anchor, ls):
    from PIL import ImageFont
    fname = "Inter-Bold.otf" if weight >= 700 else ("Inter-SemiBold.otf" if weight >= 600 else "Inter-Medium.otf")
    key = (fname, size)
    if key not in _FONTS:
        _FONTS[key] = ImageFont.truetype(os.path.join(HERE, "..", "..", "..", "resources", "fonts", fname), size)
    f = _FONTS[key]
    w = f.getlength(s) + ls * max(0, len(s) - 1)
    if weight >= 800:
        w *= 1.03                       # Synthesised extra-bold is a little wider.
    x0 = x - w / 2 if anchor == "middle" else (x - w if anchor == "end" else x)
    top = y - 0.73 * size               # Cap height; the panel print is caps and digits.
    return (x0, top, x0 + w, y + 0.5)


def text(x, y, s, size, fill=INK, weight=600, anchor="middle", ls=0.0, family=FONT):
    TEXTS.append((*text_box(x, y, s, size, weight, anchor, ls), s))
    return (f'<text x="{x:.1f}" y="{y:.1f}" font-size="{size}" fill="{fill}" font-weight="{weight}" '
            f'text-anchor="{anchor}" letter-spacing="{ls}" font-family="{family}">{s}</text>')


def line(x1, y1, x2, y2, color=INK, width=1.2, cap="round", opacity=1.0):
    return (f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{color}" '
            f'stroke-width="{width}" stroke-linecap="{cap}" opacity="{opacity}"/>')


def fmt_mark(v):
    v = int(v) if float(v).is_integer() else v
    return f"+{v}" if v > 0 else str(v)


# ---- Panel print ------------------------------------------------------------------------
def panel_svg(u):
    h = u["panel"]["h"]
    o = []
    # Brand block + enamel unit plate.
    o.append(text(78, 66, "SEVENTHWELL", 11, AMBER, 700, "start", 3.2))
    o.append(text(76, 108, "MODEL 600", 40, INK, 800, "start", 1.5))
    o.append(f'<rect x="334" y="78" width="46" height="32" rx="3" fill="{u["color"]}"/>')
    o.append(f'<rect x="334.5" y="78.5" width="45" height="31" rx="3" fill="none" stroke="#000" stroke-opacity="0.35"/>')
    o.append(text(357, 101, u["number"], 19, "#16130E", 800, "middle", 0.5))
    if u["pro"]:
        o.append(f'<rect x="388" y="86" width="34" height="16" rx="2" fill="none" stroke="{INK}" stroke-width="1.2"/>')
        o.append(text(405, 98, "PRO", 9, INK, 800, "middle", 1.4))
    o.append(text(78, 132, u["name"], 11, INK_DIM, 700, "start", 2.6))
    o.append(f'<rect x="78" y="144" width="236" height="1.2" fill="{INK_DIM}" opacity="0.6"/>')
    o.append(text(78, 160, u["tagline"], 8.5, INK_DIM, 600, "start", 1.8))
    # Meter captions (and the channel names on the director).
    for v in u["vus"]:
        if v.get("chan"):
            c = v["chan"]
            x0 = v["cx"] - v["w"] / 2 + 4
            y0 = v["cy"] + v["h"] / 2 + 30
            o.append(f'<rect x="{x0:.1f}" y="{y0 - 13:.1f}" width="26" height="17" rx="2" fill="{UNIT_COLORS[c["number"]]}"/>')
            o.append(text(x0 + 13, y0, c["number"], 10, "#16130E", 800, "middle", 0.3))
            o.append(text(x0 + 34, y0, c["name"], 11, INK, 700, "start", 2.2))
        elif v["caption"]:
            o.append(text(v["cx"], v["cy"] + v["h"] / 2 + 22, v["caption"], 8.5, INK_DIM, 600, "middle", 2.2))
    # Big knobs: ticks + numbers + labels.
    for k in u["knobs_big"]:
        cx, cy, r = k["cx"], k["cy"], k["r"]
        steps = int(round((k["max"] - k["min"]) / k["tick"]))
        for i in range(steps + 1):
            v = k["min"] + i * k["tick"]
            a = knob_angle(v, k["min"], k["max"])
            major = any(abs(v - m) < 1e-6 for m in k["marks"])
            x1, y1 = polar(cx, cy, r + BIG_TICK0, a)
            x2, y2 = polar(cx, cy, r + (BIG_TICK_MAJOR if major else BIG_TICK_MINOR), a)
            o.append(line(x1, y1, x2, y2, INK, 1.6 if major else 1.0))
        for v in k["marks"]:
            a = knob_angle(v, k["min"], k["max"])
            x, y = polar(cx, cy, r + BIG_NUM, a)
            o.append(text(x, y + 4, fmt_mark(v), 10.5, INK, 600))
        ly = k["label_y"] or cy + r + 62
        o.append(text(cx, ly, k["label"], 11, INK, 700, "middle", 2.4))
        o.append(text(cx, ly + 14, k["unit"], 8.5, INK_DIM, 600, "middle", 1.5))
    # Trim knobs (director): short scale.
    for k in u["knobs_trim"]:
        cx, cy, r = k["cx"], k["cy"], k["r"]
        for i in range(9):
            v = k["min"] + i * (k["max"] - k["min"]) / 8
            a = knob_angle(v, k["min"], k["max"])
            x1, y1 = polar(cx, cy, r + TRIM_TICK0, a)
            x2, y2 = polar(cx, cy, r + (TRIM_TICK_MAJOR if i % 4 == 0 else TRIM_TICK_MINOR), a)
            o.append(line(x1, y1, x2, y2, INK, 1.3 if i % 4 == 0 else 0.9))
        for v, lab in ((k["min"], "-"), (0, "0"), (k["max"], "+")):
            a = knob_angle(v, k["min"], k["max"])
            x, y = polar(cx, cy, r + TRIM_NUM, a)
            o.append(text(x, y + 3.5, lab, 9, INK, 700))
        o.append(text(cx, cy + r + 36, k["label"], 8, INK_DIM, 600, "middle", 1.8))
    # Stepped knobs: position marks + names.
    for k in u["knobs_step"]:
        cx, cy, r = k["cx"], k["cy"], k["r"]
        n = len(k["positions"])
        for i, name in enumerate(k["positions"]):
            a = -k["spread"] + (2 * k["spread"] * i / (n - 1) if n > 1 else 0)
            x1, y1 = polar(cx, cy, r + STEP_TICK0, a)
            x2, y2 = polar(cx, cy, r + STEP_TICK1, a)
            o.append(line(x1, y1, x2, y2, INK, 1.6))
            sa = math.sin(math.radians(a))
            anchor = "end" if sa < -0.5 else ("start" if sa > 0.5 else "middle")
            x, y = polar(cx, cy, r + (STEP_NAME_SIDE if anchor != "middle" else STEP_NAME_TOP), a)
            o.append(text(x, y + 3.5, name, 8.5, INK, 700, anchor, 0.8))
        o.append(text(cx, cy + r + 44, k["label"], 10, INK, 700, "middle", 2.2))
    # Toggles.
    for t in u["toggles"]:
        cx, cy = t["cx"], t["cy"]
        if t.get("horizontal"):
            o.append(text(cx - 34, cy + 4, t["label_l"], 9, INK, 700, "end", 1.2))
            o.append(text(cx + 34, cy + 4, t["label_r"], 9, INK, 700, "start", 1.2))
            o.append(text(cx, cy + 36, t["title"], 8.5, INK_DIM, 600, "middle", 2.0))
        else:
            o.append(text(cx, cy - 32, t["label_up"], 8.5, INK, 700, "middle", 1.0))
            o.append(text(cx, cy + 38, t["label_dn"], 8.5, INK, 700, "middle", 1.0))
            o.append(text(cx, cy + 56, t["title"], 8, INK_DIM, 600, "middle", 1.6))
    # Buttons and lamps.
    for b in u["buttons"]:
        o.append(text(b["cx"], b["cy"] + b["h"] / 2 + 22, b["label"], 8.5, INK, 700, "middle", 1.2))
    for l in u["lamps"]:
        o.append(text(l["cx"], l["cy"] + 30, l["label"], 8, INK, 700, "middle", 1.4))
    # Signal-flow strip: printed boxes and arrows so the unit explains itself.
    if u["flow"]:
        f = u["flow"]
        y = f["y"]
        nodes = f["nodes"]
        for i, (name, x) in enumerate(nodes):
            wbox = 14 + 7.2 * len(name)
            o.append(f'<rect x="{x - wbox / 2:.1f}" y="{y - 11:.1f}" width="{wbox:.1f}" height="22" rx="3" '
                     f'fill="none" stroke="{INK}" stroke-width="1.2"/>')
            o.append(text(x, y + 4, name, 8.5, INK, 700, "middle", 1.2))
            if i + 1 < len(nodes):
                nx = nodes[i + 1][1] - (14 + 7.2 * len(nodes[i + 1][0])) / 2
                x0 = x + wbox / 2 + 4
                x1 = nx - 4
                o.append(line(x0, y, x1 - 5, y, INK_DIM, 1.2))
                o.append(f'<path d="M{x1 - 6:.1f} {y - 3.5:.1f} L{x1:.1f} {y:.1f} L{x1 - 6:.1f} {y + 3.5:.1f} Z" fill="{INK_DIM}"/>')
    # LED ladders: scale above, caption below.
    for L in u["ladders"]:
        pitch = 16
        x0 = L["cx"] - (L["n"] * pitch) / 2 + pitch / 2
        for lab, i in L["scale"]:
            o.append(text(x0 + i * pitch, L["cy"] - 27, lab, 8.5, INK, 700, "middle", 0.4))
        o.append(text(L["cx"], L["cy"] + 34, L["label"], 8.5, INK_DIM, 600, "middle", 2.0))
    # 7-segment windows (director).
    for d in u["displays"]:
        o.append(text(d["cx"], d["cy"] + d["h"] / 2 + 26, d["label"], 8.5, INK_DIM, 600, "middle", 2.0))
    if u["target"]:
        t = u["target"]
        o.append(text(t["x"], t["y"], t["title"], 8.5, INK_DIM, 600, "start", 1.8))
    # Power + LINK.
    j = u["jewel"]
    o.append(text(j["cx"], j["cy"] + 38, j["label"], 8, INK_DIM, 600, "middle", 1.6))
    L = u["links"]
    o.append(text(L["x"] - 22, L["y"] + 4, "LINK", 8, INK_DIM, 600, "end", 1.8))
    for i in range(5):
        o.append(text(L["x"] + i * L["dx"], L["y"] + 25, f"0{i + 1}", 8, INK, 700, "middle", 0.8))
    o.append(text(W / 2, h - 18, f"DESIGNED BY SEVENTHWELL  ·  SERIAL 600-{u['number']}001", 7.5, INK_DIM, 600, "middle", 2.0))
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{h}" viewBox="0 0 {W} {h}">'
            + "".join(o) + "</svg>")


# ---- Dial faces -----------------------------------------------------------------------
def vu_deflection(vu_db):
    """Classic VU law: deflection proportional to voltage; 0 VU at ~71% of +3."""
    v = 10 ** (vu_db / 20) / 10 ** (3 / 20)
    return -46 + 92 * v


def ride_deflection(db):
    """AUTO RIDE gauge: linear, centre zero, -12..+12 dB over +-46 deg."""
    return 46 * db / 12


def phase_deflection(c):
    return 46 * c


DIAL = {"vu": vu_deflection, "ride": ride_deflection, "phase": phase_deflection}


def face_svg(kind):
    fw, fh = FACE_W, FACE_H
    cx, pivot_y = fw / 2, fh + 40          # Pivot sits below the visible face.
    r_scale = fh - 6
    o = [f'<defs><radialGradient id="g" cx="50%" cy="85%" r="80%">'
         f'<stop offset="0%" stop-color="#FFF4D6"/><stop offset="70%" stop-color="#F2DFAE"/>'
         f'<stop offset="100%" stop-color="#C9A866"/></radialGradient></defs>',
         f'<rect width="{fw}" height="{fh}" fill="url(#g)"/>']
    BLACK, RED = "#1A1A1A", "#C62F25"

    def arc(r, a0, a1, color, width):
        x0, y0 = polar(cx, pivot_y, r, a0)
        x1, y1 = polar(cx, pivot_y, r, a1)
        return (f'<path d="M{x0:.2f} {y0:.2f} A{r} {r} 0 0 1 {x1:.2f} {y1:.2f}" fill="none" '
                f'stroke="{color}" stroke-width="{width}"/>')

    def tick(a, length, color, width):
        x1, y1 = polar(cx, pivot_y, r_scale, a)
        x2, y2 = polar(cx, pivot_y, r_scale + length, a)
        return f'<line x1="{x1:.2f}" y1="{y1:.2f}" x2="{x2:.2f}" y2="{y2:.2f}" stroke="{color}" stroke-width="{width}"/>'

    def label(a, s, color, size=11):
        x, y = polar(cx, pivot_y, r_scale + 21, a)
        return text(x, y + 4, s, size, color, 700, "middle", 0, "Inter, sans-serif")

    if kind == "vu":
        o.append(arc(r_scale, vu_deflection(-20), vu_deflection(0), BLACK, 1.6))
        o.append(arc(r_scale, vu_deflection(0), vu_deflection(3), RED, 1.6))
        o.append(arc(r_scale + 4, vu_deflection(0), vu_deflection(3), RED, 6))
        for db, lab in [(-20, "20"), (-10, "10"), (-7, "7"), (-5, "5"), (-3, "3"), (-2, "2"), (-1, "1"),
                        (0, "0"), (1, "1"), (2, "2"), (3, "3")]:
            a = vu_deflection(db)
            col = RED if db >= 0 else BLACK
            o.append(tick(a, 10, col, 1.6))
            sign = "+" if db > 0 else ("-" if db < 0 else "")
            o.append(label(a, f"{sign}{lab}", col))
        for db in (-15, -8, -6, -4):
            o.append(tick(vu_deflection(db), 6, BLACK, 1))
        for pct in (20, 40, 60, 80, 100):
            a = -46 + 92 * (pct / 100) * 0.708
            x, y = polar(cx, pivot_y, r_scale - 14, a)
            o.append(text(x, y + 3, str(pct), 7, "#3A3A3A", 600, "middle", 0, "Inter, sans-serif"))
        o.append(text(18, 22, "VU", 15, BLACK, 800, "start", 0, "Inter, sans-serif"))
        o.append(text(fw - 18, 22, "VU", 15, RED, 800, "end", 0, "Inter, sans-serif"))
        sub = "MODEL 600  ·  BROADCAST VU"
    elif kind == "ride":
        # Cut (left, red) .. boost (right, black); centre zero.
        o.append(arc(r_scale, ride_deflection(-12), ride_deflection(0), RED, 1.6))
        o.append(arc(r_scale, ride_deflection(0), ride_deflection(12), BLACK, 1.6))
        o.append(arc(r_scale + 4, ride_deflection(-12), ride_deflection(0), RED, 6))
        for db in range(-12, 13, 3):
            a = ride_deflection(db)
            col = RED if db < 0 else BLACK
            o.append(tick(a, 10, col, 1.6))
            o.append(label(a, fmt_mark(db) if db else "0", col))
        for db in range(-12, 13):
            if db % 3:
                o.append(tick(ride_deflection(db), 6, RED if db < 0 else BLACK, 1))
        o.append(text(18, 22, "CUT", 12, RED, 800, "start", 0.5, "Inter, sans-serif"))
        o.append(text(fw - 18, 22, "BOOST", 12, BLACK, 800, "end", 0.5, "Inter, sans-serif"))
        o.append(text(cx, fh - 44, "dB", 10, BLACK, 700, "middle", 0, "Inter, sans-serif"))
        sub = "MODEL 600  ·  RIDE GAIN"
    else:  # phase correlation
        o.append(arc(r_scale, phase_deflection(-1), phase_deflection(0), RED, 1.6))
        o.append(arc(r_scale, phase_deflection(0), phase_deflection(1), BLACK, 1.6))
        o.append(arc(r_scale + 4, phase_deflection(-1), phase_deflection(0), RED, 6))
        for c, lab in [(-1, "-1"), (-0.5, "-.5"), (0, "0"), (0.5, "+.5"), (1, "+1")]:
            a = phase_deflection(c)
            col = RED if c < 0 else BLACK
            o.append(tick(a, 10, col, 1.6))
            o.append(label(a, lab, col, 12))
        for i in range(-10, 11):
            if i % 5:
                o.append(tick(phase_deflection(i / 10), 6, RED if i < 0 else BLACK, 1))
        o.append(text(18, 22, "OUT", 12, RED, 800, "start", 0.5, "Inter, sans-serif"))
        o.append(text(fw - 18, 22, "IN", 12, BLACK, 800, "end", 0.5, "Inter, sans-serif"))
        sub = "MODEL 600  ·  CORRELATION"
    o.append(text(cx, fh - 30, "SEVENTHWELL", 8, "#5A4A2A", 700, "middle", 2.4, "Inter, sans-serif"))
    o.append(text(cx, fh - 18, sub, 6.5, "#6A5A3A", 600, "middle", 1.6, "Inter, sans-serif"))
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{fw}" height="{fh}" viewBox="0 0 {fw} {fh}">'
            + "".join(o) + "</svg>")


# ---- 7-segment window -------------------------------------------------------------------
SEGS = {"0": "abcdef", "1": "bc", "2": "abged", "3": "abgcd", "4": "fgbc", "5": "afgcd", "6": "afgedc",
        "7": "abc", "8": "abcdefg", "9": "abcfgd", "-": "g", " ": ""}


DIGI_LIT, DIGI_GHOST, DIGI_BG, DIGI_DIM = "#FFB347", "#1A1108", "#0B0806", "#B07A35"
LED = {"green": ("#57E08A", "#0E1F14"), "amber": ("#FFB347", "#23170A"), "red": ("#FF4A3A", "#260C0A")}


def split_chars(txt):
    chars = []
    for ch in txt:
        if ch == "." and chars:
            chars[-1] = (chars[-1][0], True)
        else:
            chars.append((ch, False))
    return chars


def seg_paths(chars, x0, y0, cw, ch_h, layer, sk=0.10):
    """7-segment glyph outlines. layer "ghost" draws every segment, "lit" only the lit ones."""
    t = max(1.2, cw * 0.16)
    sw = cw * 0.68

    def poly(x, y, name):
        hw = sw - t
        hh = (ch_h - t) / 2
        e = t * 0.12
        if name in "agd":
            yy = {"a": y, "g": y + hh, "d": y + 2 * hh}[name]
            pts = [(x + t / 2 + e, yy + t / 2), (x + t + e, yy), (x + hw - e, yy), (x + hw + t / 2 - e, yy + t / 2),
                   (x + hw - e, yy + t), (x + t + e, yy + t)]
        else:
            xx = x if name in "fe" else x + hw
            yy = y if name in "fb" else y + hh
            pts = [(xx + t / 2, yy + t / 2 + e), (xx + t, yy + t + e), (xx + t, yy + hh - e),
                   (xx + t / 2, yy + hh + t / 2 - e), (xx, yy + hh - e), (xx, yy + t + e)]
        base = y + ch_h
        pts = [(px + (base - py) * sk, py) for px, py in pts]
        return "M" + " L".join(f"{px:.2f} {py:.2f}" for px, py in pts) + " Z"

    paths = []
    for i, (c, dot) in enumerate(chars):
        x = x0 + i * cw + cw * 0.12
        on = SEGS.get(c, "")
        for sg in "abcdefg":
            if layer == "ghost" or sg in on:
                paths.append(poly(x, y0, sg))
        if layer == "ghost" or dot:
            r = t * 0.55
            cx_, cy_ = x + sw + t * 0.6, y0 + ch_h - r
            paths.append(f"M{cx_ - r:.2f} {cy_:.2f} a{r:.2f} {r:.2f} 0 1 0 {2 * r:.2f} 0 "
                         f"a{r:.2f} {r:.2f} 0 1 0 {-2 * r:.2f} 0 Z")
    return " ".join(paths)


def glow_defs(sd):
    return (f'<defs><filter id="glow" x="-50%" y="-50%" width="200%" height="200%">'
            f'<feGaussianBlur stdDeviation="{sd:.2f}" result="b"/><feMerge><feMergeNode in="b"/>'
            f'<feMergeNode in="SourceGraphic"/></feMerge></filter></defs>')


def seg_svg(d):
    w, h = d["w"], d["h"]
    chars = split_chars(d["text"])
    cw = (w - 16) / len(chars)
    o = [glow_defs(2.2), f'<rect width="{w}" height="{h}" fill="{DIGI_BG}"/>']
    o.append(f'<path d="{seg_paths(chars, 8, 9, cw, h - 18, "ghost")}" fill="{DIGI_GHOST}"/>')
    o.append(f'<path d="{seg_paths(chars, 8, 9, cw, h - 18, "lit")}" fill="{DIGI_LIT}" filter="url(#glow)"/>')
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">' + "".join(o) + "</svg>"


def bar_zone(kind, centre):
    if kind == "level":
        return "green" if centre < -9 else ("amber" if centre < -3 else "red")
    return "red" if centre < 0 else ("amber" if kind == "ride" else "green")


def meter_svg(v):
    """Digital meter window: label + aux on top, big 7-segment value + unit, segment bar + scale."""
    m = v["digi"]
    W, H = m["win_w"], m["win_h"]
    kind = m["kind"]
    pad = max(7.0, H * 0.07)
    fs = max(6.0, H * 0.078)
    o = [glow_defs(max(1.0, H * 0.012)), f'<rect width="{W}" height="{H}" fill="{DIGI_BG}"/>',
         # Faint inner shading so the window reads as glass over a deep cavity.
         f'<defs><linearGradient id="sh" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#000" stop-opacity="0.55"/>'
         f'<stop offset="0.25" stop-color="#000" stop-opacity="0"/></linearGradient></defs>']
    family = "Inter, sans-serif"
    o.append(text(pad, pad + fs * 0.75, m["label"], fs, DIGI_DIM, 700, "start", fs * 0.14, family))
    if m["aux"]:
        o.append(text(W - pad, pad + fs * 0.75, m["aux"], fs, DIGI_DIM, 700, "end", fs * 0.14, family))
    # Big value + unit, centred as a group.
    chars = split_chars(m["value"])
    ch_h = H * 0.36
    cw = ch_h * 0.62
    ufs = H * 0.095
    unit_w = text_box(0, 0, m["unit"], ufs, 700, "start", ufs * 0.1)[2] if m["unit"] else 0
    group = len(chars) * cw + (ufs * 0.5 + unit_w if m["unit"] else 0)
    x0 = (W - group) / 2
    y0 = pad + fs + H * 0.07
    o.append(f'<path d="{seg_paths(chars, x0, y0, cw, ch_h, "ghost")}" fill="{DIGI_GHOST}"/>')
    o.append(f'<path d="{seg_paths(chars, x0, y0, cw, ch_h, "lit")}" fill="{DIGI_LIT}" filter="url(#glow)"/>')
    if m["unit"]:
        o.append(text(x0 + len(chars) * cw + ufs * 0.5, y0 + ch_h, m["unit"], ufs, DIGI_LIT, 700, "start", ufs * 0.1, family))
    # Segment bar.
    lo, hi, n = {"level": (-36.0, 0.0, 36), "ride": (-12.0, 12.0, 24), "phase": (-1.0, 1.0, 20)}[kind]
    bx0, bx1 = pad, W - pad
    by = H * 0.70
    bh = max(3.0, H * 0.085)
    pitch = (bx1 - bx0) / n
    val = max(lo, min(hi, m["bar"]))
    for i in range(n):
        a, b = lo + (hi - lo) * i / n, lo + (hi - lo) * (i + 1) / n
        c = (a + b) / 2
        if kind == "level":
            lit = c <= val
        else:                                # Centre-zero: light from 0 toward the value.
            lit = (0 <= c <= val) or (val <= c <= 0)
        on, off = LED[bar_zone(kind, c)]
        x = bx0 + i * pitch + pitch * 0.12
        o.append(f'<rect x="{x:.2f}" y="{by:.2f}" width="{pitch * 0.76:.2f}" height="{bh:.2f}" rx="{pitch * 0.12:.2f}" '
                 f'fill="{on if lit else off}"{" filter=\"url(#glow)\"" if lit else ""}/>')
    # Scale under the bar.
    marks = {"level": [(-36, "-36"), (-24, "-24"), (-18, "-18"), (-12, "-12"), (-6, "-6"), (0, "0")],
             "ride": [(-12, "-12"), (-6, "-6"), (0, "0"), (6, "+6"), (12, "+12")],
             "phase": [(-1, "-1"), (0, "0"), (1, "+1")]}[kind]
    sy = by + bh + fs * 1.25
    for mv, lab in marks:
        x = bx0 + (mv - lo) / (hi - lo) * (bx1 - bx0)
        anchor = "start" if mv == lo else ("end" if mv == hi else "middle")
        o.append(text(x, sy, lab, fs * 0.92, DIGI_DIM, 700, anchor, 0, family))
    # Target marker: a small amber triangle above the bar.
    if m["target"] is not None:
        x = bx0 + (m["target"] - lo) / (hi - lo) * (bx1 - bx0)
        ts = bh * 0.85
        o.append(f'<path d="M{x - ts * 0.7:.2f} {by - ts - 2:.2f} L{x + ts * 0.7:.2f} {by - ts - 2:.2f} L{x:.2f} {by - 2:.2f} Z" '
                 f'fill="{DIGI_LIT}" filter="url(#glow)"/>')
    o.append(f'<rect width="{W}" height="{H}" fill="url(#sh)"/>')
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">' + "".join(o) + "</svg>"


# ---- Spacing check -----------------------------------------------------------------------
def parts_of(u):
    """Physical footprints seen from the front: ("circle", cx, cy, r) or ("rect", x0, y0, x1, y1)."""
    out = []

    def circ(name, cx, cy, r):
        out.append((name, ("circle", cx, cy, r)))

    def rect(name, cx, cy, w, h):
        out.append((name, ("rect", cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)))

    for k in u["knobs_big"]:
        circ(f"knob {k['id']}", k["cx"], k["cy"], k["r"] + 2)
    for k in u["knobs_step"]:
        circ(f"selector {k['id']}", k["cx"], k["cy"], k["r"] * 1.1)
    for i, k in enumerate(u["knobs_trim"]):
        circ(f"trim {i + 1}", k["cx"], k["cy"], k["r"])
    for i, v in enumerate(u["vus"]):
        rect(f"meter {v['caption'] or i + 1}", v["cx"], v["cy"], v["w"], v["h"])
    for d in u["displays"]:
        rect(f"display {d['id']}", d["cx"], d["cy"], d["w"] + 16, d["h"] + 16)
    for L in u["ladders"]:
        rect("ladder", L["cx"], L["cy"], L["n"] * 16 + 12, 30)
    for b in u["buttons"]:
        rect(f"button {b['id']}", b["cx"], b["cy"], b["w"] + 6, b["h"] + 6)
    for l in u["lamps"]:
        circ(f"lamp {l['label']}", l["cx"], l["cy"], 12)
    for t in u["toggles"]:
        circ(f"toggle {t['id']}", t["cx"], t["cy"], 17)
    L = u["links"]
    for i in range(5):
        circ(f"link {i + 1}", L["x"] + i * L["dx"], L["y"], 7)
    j = u["jewel"]
    circ("power", j["cx"], j["cy"], 16)
    for x, y in u["screws"]:
        circ("screw", x, y, 8)
    return out


def gap(a, b):
    """Clearance between two shapes (negative = overlap)."""
    if a[0] == "circle" and b[0] == "circle":
        return math.hypot(a[1] - b[1], a[2] - b[2]) - a[3] - b[3]
    if a[0] == "circle":
        a, b = b, a
    if b[0] == "circle":
        _, x0, y0, x1, y1 = a
        _, cx, cy, r = b
        dx = max(x0 - cx, 0, cx - x1)
        dy = max(y0 - cy, 0, cy - y1)
        return math.hypot(dx, dy) - r if (dx or dy) else -r
    _, ax0, ay0, ax1, ay1 = a
    _, bx0, by0, bx1, by1 = b
    dx = max(bx0 - ax1, ax0 - bx1)
    dy = max(by0 - ay1, ay0 - by1)
    if dx > 0 and dy > 0:
        return math.hypot(dx, dy)
    return max(dx, dy)


def check_layout(num, u):
    TEXTS.clear()
    panel_svg(u)
    texts = [(t[4], ("rect", *t[:4])) for t in TEXTS]
    parts = parts_of(u)
    h = u["panel"]["h"]
    issues = []
    for name, shp in texts:
        for pname, pshp in parts:
            g = gap(shp, pshp)
            if g < GAP_TEXT_PART:
                issues.append((g, f"text '{name}' <-> {pname}"))
        x0, y0, x1, y1 = shp[1:]
        if x0 < SAFE_X[0] or x1 > SAFE_X[1] or y0 < 12 or y1 > h - 8:
            issues.append((0, f"text '{name}' outside the safe area"))
    for i in range(len(texts)):
        for j in range(i + 1, len(texts)):
            g = gap(texts[i][1], texts[j][1])
            if g < GAP_TEXT_TEXT:
                issues.append((g, f"text '{texts[i][0]}' <-> text '{texts[j][0]}'"))
    for i in range(len(parts)):
        for j in range(i + 1, len(parts)):
            if parts[i][0] == "screw" or parts[j][0] == "screw":
                continue
            g = gap(parts[i][1], parts[j][1])
            if g < GAP_PART_PART:
                issues.append((g, f"{parts[i][0]} <-> {parts[j][0]}"))
    for g, msg in sorted(issues):
        print(f"  unit {num}: {g:6.1f} px  {msg}")
    return len(issues)


# ---- Output ------------------------------------------------------------------------------
def render(svg_path, png_path, w, h, scale):
    subprocess.run([CHROME, "--no-sandbox", "--disable-gpu", "--hide-scrollbars",
                    "--default-background-color=00000000", f"--force-device-scale-factor={scale}",
                    f"--window-size={w},{h}", f"--screenshot={png_path}", f"file://{svg_path}"],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def write(name, svg, w, h, scale):
    svg_path = os.path.join(TEX, name + ".svg")
    with open(svg_path, "w", encoding="utf-8") as f:
        f.write(svg)
    render(svg_path, os.path.join(TEX, name + ".png"), w, h, scale)


def main():
    import sys
    bad = sum(check_layout(n, u) for n, u in UNITS.items())
    print(f"spacing check: {bad} issue(s)")
    if "--check" in sys.argv:
        return
    os.makedirs(TEX, exist_ok=True)
    with open(os.path.join(TEX, "layout.json"), "w") as f:
        json.dump({"face": {"w": FACE_W, "h": FACE_H}, "units": UNITS}, f, indent=2)
    for kind in DIAL:
        write(f"face_{kind}", face_svg(kind), FACE_W, FACE_H, 4)
    for num, u in UNITS.items():
        write(f"unit{num}_decal", panel_svg(u), W, u["panel"]["h"], 3)
        for d in u["displays"]:
            write(f"unit{num}_seg_{d['id']}", seg_svg(d), d["w"], d["h"], 6)
        for i, v in enumerate(u["vus"]):
            if "digi" in v:
                m = v["digi"]
                write(f"unit{num}_meter_{i}", meter_svg(v), m["win_w"], m["win_h"], 6)
    # Retire the single-unit files from the first study.
    for old in ("panel_decal.svg", "panel_decal.png", "vu_face.svg", "vu_face.png"):
        p = os.path.join(TEX, old)
        if os.path.exists(p):
            os.remove(p)
    print("textures written to", TEX)


if __name__ == "__main__":
    main()
