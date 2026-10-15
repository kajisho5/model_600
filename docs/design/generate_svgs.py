#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  GUI design (Step 2): SVG generator.
#
#  Every screen is composed from the same component functions, so a meter or a
#  key looks identical on every screen. Tokens mirror DESIGN_SPEC.md section 2.
#
#    python3 docs/design/generate_svgs.py      -> docs/design/svg/*.svg
# =============================================================================
import math
import os
from xml.sax.saxutils import escape

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "svg")

# ---- Tokens -----------------------------------------------------------------
C = {
    "chassis": "#0E1012", "faceplate": "#191C20", "panel": "#121417",
    "raised": "#23272C", "raised_active": "#2E333A", "line": "#2C3137",
    "line_strong": "#3A4047", "t1": "#E7E9EC", "t2": "#9AA1A9", "t3": "#646B73",
    "accent": "#F0A33A", "green": "#46C17B", "yellow": "#E2BF4E", "red": "#E5534B",
    "off": "#1E2226", "gr": "#4C9BE8", "ok": "#46C17B", "warn": "#E3A83A",
    "error": "#E5534B", "info": "#4C9BE8", "led_off": "#3A4047",
}
FONT = "Inter, 'Noto Sans JP', 'Noto Sans CJK JP', sans-serif"


def svg_doc(w, h, body, bg=None):
    bgr = f'<rect width="{w}" height="{h}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" '
            f'viewBox="0 0 {w} {h}" font-family="{FONT}">'
            f'<defs><filter id="shadow" x="-10%" y="-10%" width="120%" height="130%">'
            f'<feDropShadow dx="0" dy="8" stdDeviation="12" flood-color="#000" flood-opacity="0.55"/></filter></defs>'
            f'{bgr}{body}</svg>')


def rect(x, y, w, h, fill, rx=0, stroke=None, sw=1, extra=""):
    s = f' stroke="{stroke}" stroke-width="{sw}"' if stroke else ""
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"{s} {extra}/>'


def line(x1, y1, x2, y2, color, w=1):
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{w}"/>'


def text(x, y, s, size=12, fill=None, weight=400, anchor="start", ls=0, tnum=False, family=None):
    fill = fill or C["t1"]
    st = "font-variant-numeric:tabular-nums;" if tnum else ""
    fam = f' font-family="{family}"' if family else ""
    return (f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" font-weight="{weight}" '
            f'text-anchor="{anchor}" letter-spacing="{ls}" style="{st}"{fam}>{escape(s)}</text>')


def label(x, y, s, anchor="start"):
    return text(x, y, s.upper(), 9.5, C["t2"], 600, anchor, ls=1.2)


def led(cx, cy, color=None, r=3):
    col = color or C["led_off"]
    glow = f'<circle cx="{cx}" cy="{cy}" r="{r + 2.5}" fill="{col}" opacity="0.18"/>' if color else ""
    return glow + f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{col}"/>'


# ---- Icons (line art only, no emoji) ------------------------------------------
def icon_pin(x, y, color):
    # Thumbtack: head bar, body, needle.
    return (f'<g fill="none" stroke="{color}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">'
            f'<path d="M{x+4.5} {y+1.5} L{x+9.5} {y+1.5} M{x+5.5} {y+1.5} L{x+5} {y+6.5} L{x+3} {y+8.5} L{x+11} {y+8.5} '
            f'L{x+9} {y+6.5} L{x+8.5} {y+1.5} M{x+7} {y+8.5} L{x+7} {y+12.5}"/></g>')


def icon_collapse(x, y, color, expanded=True):
    if expanded:  # Bar with chevron up: "fold".
        return (f'<g fill="none" stroke="{color}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">'
                f'<path d="M{x+2} {y+11.5} L{x+12} {y+11.5} M{x+4} {y+7} L{x+7} {y+4} L{x+10} {y+7}"/></g>')
    return (f'<g fill="none" stroke="{color}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">'
            f'<path d="M{x+2} {y+2.5} L{x+12} {y+2.5} M{x+4} {y+7} L{x+7} {y+10} L{x+10} {y+7}"/></g>')


def icon_reset(x, y, color):
    cx, cy, r = x + 7, y + 7.5, 4.6
    a0, a1 = math.radians(-60), math.radians(220)
    sx, sy = cx + r * math.cos(a0), cy + r * math.sin(a0)
    ex, ey = cx + r * math.cos(a1), cy + r * math.sin(a1)
    return (f'<g fill="none" stroke="{color}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">'
            f'<path d="M{sx:.2f} {sy:.2f} A{r} {r} 0 1 1 {ex:.2f} {ey:.2f}"/>'
            f'<path d="M{sx-3.2:.2f} {sy-0.6:.2f} L{sx:.2f} {sy:.2f} L{sx+0.4:.2f} {sy-3.2:.2f}"/></g>')


# ---- Components ---------------------------------------------------------------
def key(x, y, w, h, label_text, state="default", led_color=None, led_on=False, size=12, sub=None):
    """state: default | active | danger | primary | ghost"""
    fill = {"default": C["raised"], "active": C["raised_active"], "danger": "#2C1D1F",
            "primary": C["accent"], "ghost": "none"}[state]
    stroke = {"default": C["line"], "active": C["line_strong"], "danger": "#4A2A2D",
              "primary": C["accent"], "ghost": C["line"]}[state]
    tcol = "#1A1206" if state == "primary" else C["t1"]
    out = rect(x + 0.5, y + 0.5, w - 1, h - 1, fill, 4, stroke)
    tx = x + w / 2
    if led_color is not None:
        out += led(x + 16, y + h / 2, led_color if led_on else None)
        tx = x + w / 2 + 8
    weight = 600 if state == "primary" else 500
    if sub:
        out += text(tx, y + h / 2 + 1, label_text, size, tcol, weight, "middle")
        out += text(tx, y + h / 2 + 13, sub, 9, C["t3"], 400, "middle")
    else:
        out += text(tx, y + h / 2 + size * 0.36, label_text, size, tcol, weight, "middle")
    return out


def icon_key(x, y, s, icon, active=False):
    fill = C["raised_active"] if active else C["raised"]
    stroke = C["accent"] if active else C["line"]
    col = C["accent"] if active else C["t2"]
    return rect(x + 0.5, y + 0.5, s - 1, s - 1, fill, 4, stroke) + icon(x + (s - 14) / 2, y + (s - 14) / 2, col)


def segmented(x, y, w, h, options, active, size=11):
    n = len(options)
    out = rect(x + 0.5, y + 0.5, w - 1, h - 1, C["panel"], 4, C["line"])
    sw = w / n
    for i, opt in enumerate(options):
        sx = x + i * sw
        if i == active:
            out += rect(sx + 2, y + 2, sw - 4, h - 4, C["raised_active"], 3)
            out += rect(sx + 8, y + h - 4, sw - 16, 2, C["accent"], 1)
        elif i > 0 and i != active + 1:
            out += line(sx, y + 7, sx, y + h - 7, C["line"])
        col = C["t1"] if i == active else C["t2"]
        out += text(sx + sw / 2, y + h / 2 + size * 0.36 - (1 if i == active else 0), opt, size, col,
                    600 if i == active else 500, "middle")
    return out


def lang_toggle(x, y, h, lang="JA"):
    return segmented(x, y, 64, h, ["JA", "EN"], 0 if lang == "JA" else 1, 10)


# Piecewise-linear meter law (dB -> 0..1 from the bottom). The top 12 dB get
# ~38% of the height, where broadcast levels actually live. Shared with Qt.
METER_LAW = [(-60, 0.0), (-48, 0.08), (-36, 0.20), (-24, 0.37), (-18, 0.48),
             (-12, 0.62), (-6, 0.80), (0, 1.0)]


def meter_pos(db):
    if db <= -60:
        return 0.0
    if db >= 0:
        return 1.0
    for (d0, p0), (d1, p1) in zip(METER_LAW, METER_LAW[1:]):
        if d0 <= db <= d1:
            return p0 + (db - d0) / (d1 - d0) * (p1 - p0)
    return 0.0


def meter_db(pos):
    """Inverse of meter_pos."""
    for (d0, p0), (d1, p1) in zip(METER_LAW, METER_LAW[1:]):
        if p0 <= pos <= p1:
            return d0 + (pos - p0) / (p1 - p0) * (d1 - d0)
    return 0.0


def zone_color(db):
    if db > -3:
        return C["red"]
    if db > -12:
        return C["yellow"]
    return C["green"]


def vmeter(x, y, w, h, peak_db, rms_db, hold_db):
    out = rect(x, y, w, h, C["panel"], 2, C["line"])
    pitch = 3
    n = int((h - 4) / pitch)
    for i in range(n):
        frac = (i + 0.5) / n
        # Invert the meter law to find the dB this segment represents.
        db = meter_db(frac)
        sy = y + h - 2 - (i + 1) * pitch
        lit_peak = frac <= meter_pos(peak_db)
        lit_rms = frac <= meter_pos(rms_db)
        col = C["off"]
        if lit_peak:
            col = zone_color(db)
        out_col = col
        opacity = "1" if lit_rms else ("0.55" if lit_peak else "1")
        out += f'<rect x="{x + 2}" y="{sy + 1}" width="{w - 4}" height="{pitch - 1}" fill="{out_col}" opacity="{opacity}"/>'
    hy = y + h - 2 - meter_pos(hold_db) * (h - 4)
    out += rect(x + 2, hy - 1, w - 4, 2, C["t1"])
    return out


def grmeter(x, y, w, h, gr_db, top_label=None):
    out = rect(x, y, w, h, C["panel"], 2, C["line"])
    frac = max(0.0, min(1.0, -gr_db / 24.0))
    fh = frac * (h - 4)
    pitch = 3
    n = int((h - 4) / pitch)
    for i in range(n):
        sy = y + 2 + i * pitch
        lit = (i + 1) * pitch <= fh + 0.01
        out += f'<rect x="{x + 2}" y="{sy}" width="{w - 4}" height="{pitch - 1}" fill="{C["gr"] if lit else C["off"]}"/>'
    return out


def meter_scale(cx, y, h, anchor="middle"):
    out = ""
    for db in (0, -3, -6, -9, -12, -18, -24, -36, -48, -60):
        py = y + h - 2 - meter_pos(db) * (h - 4)
        out += text(cx, py + 3, f"{db}" if db else "0", 9, C["t3"], 400, anchor, tnum=True)
    return out


def gr_scale(cx, y, h):
    out = ""
    for db in (0, -3, -6, -12, -24):
        py = y + 2 + (-db / 24.0) * (h - 4)
        out += text(cx, py + 3, f"{db}", 9, C["t3"], 400, "middle", tnum=True)
    return out


def readout(x, y, w, h, lab, value, unit, vcol=None, big=False):
    """Label left + unit right on the top row, value below (never overflows)."""
    out = rect(x + 0.5, y + 0.5, w - 1, h - 1, C["panel"], 4, C["line"])
    out += label(x + 10, y + 17, lab)
    vs = 22 if big else 16
    unit_span = (f'<tspan dx="3" font-size="9.5" fill="{C["t2"]}" font-weight="500">{escape(unit)}</tspan>'
                 if unit else "")
    out += (f'<text x="{x + 10}" y="{y + h - 11}" font-size="{vs}" fill="{vcol or C["t1"]}" font-weight="500" '
            f'style="font-variant-numeric:tabular-nums;">{escape(value)}{unit_span}</text>')
    return out


def value_box(x, y, w, h, value, unit="dB"):
    return (rect(x + 0.5, y + 0.5, w - 1, h - 1, C["panel"], 3, C["line"]) +
            text(x + w / 2, y + h / 2 + 4, f"{value} {unit}", 11, C["t1"], 500, "middle", tnum=True))


FADER_MARKS = [(12, "+12"), (6, "+6"), (0, "0"), (-6, "-6"), (-12, "-12"), (-24, "-24"), (-48, "-48")]


def fader_pos(db):
    """0..1 from the bottom, +12 top, -48 bottom; finer around 0 dB."""
    db = max(-48.0, min(12.0, db))
    if db >= -12:
        return 0.35 + (db + 12) / 24.0 * 0.65
    return (db + 48) / 36.0 * 0.35


def vfader(cx, y, h, value_db, label_side="left"):
    out = rect(cx - 2, y, 4, h, C["panel"], 2, C["line"])
    for db, txt in FADER_MARKS:
        py = y + h - fader_pos(db) * h
        out += line(cx - 12, py, cx - 6, py, C["t3"]) + line(cx + 6, py, cx + 12, py, C["t3"])
        out += text(cx - 16, py + 3, txt, 9, C["t3"], 400, "end", tnum=True)
    py = y + h - fader_pos(value_db) * h
    out += rect(cx - 15.5, py - 7.5, 31, 15, "#2B3036", 3, C["line_strong"])
    out += line(cx - 11, py, cx + 11, py, C["t1"], 1.5)
    return out


def hfader(x, y, w, value_db, lo=-24, hi=24):
    out = rect(x, y - 2, w, 4, C["panel"], 2, C["line"])
    for db in (-24, -12, 0, 12, 24):
        px = x + (db - lo) / (hi - lo) * w
        out += line(px, y + 7, px, y + 11, C["t3"])
        out += text(px, y + 22, f"+{db}" if db > 0 else f"{db}", 9, C["t3"], 400, "middle", tnum=True)
    px = x + (value_db - lo) / (hi - lo) * w
    out += rect(px - 7.5, y - 9.5, 15, 19, "#2B3036", 3, C["line_strong"])
    out += line(px, y - 5, px, y + 5, C["t1"], 1.5)
    return out


def hmeter(x, y, w, h, peak_db, rms_db, hold_db, scale=True):
    out = rect(x, y, w, h, C["panel"], 2, C["line"])
    pitch = 4
    n = int((w - 4) / pitch)
    for i in range(n):
        frac = (i + 0.5) / n
        db = meter_db(frac)
        sx = x + 2 + i * pitch
        lit_peak = frac <= meter_pos(peak_db)
        lit_rms = frac <= meter_pos(rms_db)
        col = zone_color(db) if lit_peak else C["off"]
        op = "1" if (lit_rms or not lit_peak) else "0.55"
        out += f'<rect x="{sx}" y="{y + 2}" width="{pitch - 1}" height="{h - 4}" fill="{col}" opacity="{op}"/>'
    hx = x + 2 + meter_pos(hold_db) * (w - 4)
    out += rect(hx - 1, y + 2, 2, h - 4, C["t1"])
    if scale:
        for db in (-60, -48, -36, -24, -18, -12, -6, -3, 0):
            px = x + 2 + meter_pos(db) * (w - 4)
            out += line(px, y + h + 2, px, y + h + 5, C["t3"])
            out += text(px, y + h + 15, f"{db}", 9, C["t3"], 400, "middle", tnum=True)
    return out


def screw(cx, cy):
    return (f'<circle cx="{cx}" cy="{cy}" r="5" fill="#1F2327" stroke="#33383E" stroke-width="1"/>'
            + line(cx - 3, cy - 0.8, cx + 3, cy + 0.8, "#0B0C0E", 1.2))


def rack(w, h, ear=18):
    out = rect(0, 0, w, h, C["chassis"])
    out += rect(ear, 0, w - 2 * ear, h, C["faceplate"])
    out += line(ear, 0.5, w - ear, 0.5, "#24282D")  # top highlight
    for cx in (ear / 2, w - ear / 2) if ear else ():
        if h > 200:
            out += screw(cx, 26) + screw(cx, h - 26)
        else:
            out += screw(cx, h / 2)
    return out


def wordmark(x, y, compact=False):
    if compact:
        return text(x, y + 4, "MODEL 600", 12.5, C["t1"], 700, ls=1)
    return (text(x, y, "SEVENTHWELL", 9, C["t3"], 600, ls=2.6) +
            text(x, y + 19, "MODEL 600", 16, C["t1"], 700, ls=1))


def unit_badge(x, y, num, name):
    return (rect(x + 0.5, y + 0.5, 25, 21, "none", 3, C["accent"]) +
            text(x + 13, y + 15, num, 11, C["accent"], 600, "middle", tnum=True) +
            text(x + 36, y + 15.5, name, 12, C["t1"], 600, ls=1.3))


def link_row(x, y, states):
    out = text(x, y + 3.5, "LINK", 8.5, C["t3"], 600, ls=1.4)
    px = x + 34
    for i, on in enumerate(states):
        out += led(px, y, C["green"] if on else None, 2.6)
        out += text(px + 6, y + 3.5, f"0{i + 1}", 9, C["t2"] if on else C["t3"], 500, tnum=True)
        px += 30
    return out


def header(w, num, name, ear=18, pinned=False, lang="JA", update=None, links=(1, 1, 1, 1, 1)):
    left = ear + 16
    out = wordmark(left, 22)
    out += line(left + 104, 14, left + 104, 44, C["line"])
    out += unit_badge(left + 120, 11, num, name)
    out += link_row(left + 120, 45, links)
    rx = w - ear - 16
    out += icon_key(rx - 28, 14, 28, icon_reset)
    out += lang_toggle(rx - 28 - 8 - 64, 14, 28, lang)
    out += icon_key(rx - 28 - 8 - 64 - 8 - 28, 14, 28, lambda a, b, c: icon_collapse(a, b, c, True))
    out += icon_key(rx - 28 - 8 - 64 - 8 - 28 - 8 - 28, 14, 28, icon_pin, active=pinned)
    if update:
        uw = 104
        ux = rx - uw
        out += rect(ux + 0.5, 45.5, uw - 1, 13, "#2A2114", 6.5, C["accent"])
        out += led(ux + 10, 52, C["accent"], 2.2)
        out += text(ux + uw / 2 + 5, 55.5, f"UPDATE {update}", 8.5, C["accent"], 600, "middle", ls=0.8)
    out += line(ear, 62.5, w - ear, 62.5, C["line"])
    return out


def status_line(w, h, dot, msg, right, ear=18):
    y = h - 30
    out = line(ear, y + 0.5, w - ear, y + 0.5, C["line"])
    out += led(ear + 20, y + 15, dot, 3)
    out += text(ear + 32, y + 19, msg, 10.5, C["t2"], 500)
    out += text(w - ear - 16, y + 19, right, 10, C["t3"], 500, "end", tnum=True)
    return out


# ---- Screens -------------------------------------------------------------------
def screen_unit01():
    W, H, E = 560, 560, 18
    o = rack(W, H, E)
    o += header(W, "01", "VOCAL MASTER", E)
    L = E + 16
    # Meter block.
    o += label(L, 88, "OUTPUT")
    o += value_box(L, 96, 40, 20, "-2.2", "")
    o += value_box(L + 66, 96, 36, 20, "-2.5", "")
    my, mh = 124, 368
    o += vmeter(L, my, 22, mh, -6.0, -13.5, -2.2)
    o += meter_scale(L + 44, my, mh)
    o += grmeter(L + 66, my, 14, mh * 0.42, -2.5)
    o += gr_scale(L + 92, my, mh * 0.42)
    o += text(L + 11, my + mh + 16, "PK", 9, C["t3"], 600, "middle", ls=1)
    o += text(L + 73, my + mh * 0.42 + 16, "GR", 9, C["t3"], 600, "middle", ls=1)
    # Controls column.
    X, Wc = L + 112, W - E - 16 - (L + 112)
    bw = (Wc - 3 * 8) / 4
    vals = [("TRUE PEAK", "-2.2", "dBTP"), ("SHORT-TERM", "-12.9", "LUFS"),
            ("INTEGRATED", "-13.2", "LUFS"), ("INPUT GAIN", "+3.5", "dB")]
    for i, (lab_, v, u) in enumerate(vals):
        o += readout(X + i * (bw + 8), 80, bw, 56, lab_, v, u)
    o += label(X, 162, "CHARACTER")
    o += segmented(X, 170, Wc, 30, ["SOFT / ASMR", "BROADCAST", "FPS / LOUD"], 1)
    o += label(X, 226, "SPACE FX")
    o += segmented(X, 234, Wc, 30, ["DRY", "KARAOKE", "BATH", "HALL", "CAVE", "RADIO"], 0, 10.5)
    o += label(X, 290, "CALIBRATION / MONITOR")
    hw = (Wc - 8) / 2
    o += key(X, 298, hw, 34, "10秒 自動測定", sub=None)
    o += key(X + hw + 8, 298, hw, 34, "A/B 試聴", led_color=C["accent"], led_on=False)
    o += label(X, 358, "EMERGENCY")
    o += key(X, 366, hw, 40, "ピー音  1 kHz", "danger", led_color=C["red"], led_on=False)
    o += key(X + hw + 8, 366, hw, 40, "咳ミュート", led_color=C["accent"], led_on=False)
    o += label(X, 432, "OUTPUT TRIM")
    o += hfader(X + 6, 458, Wc - 92, 0.0)
    o += value_box(X + Wc - 72, 447, 72, 22, "0.0")
    o += status_line(W, H, C["ok"], "動作中 · 先読み 1.5 ms · Summing Guard 待機",
                     "48 kHz · 2 ch · v1.0.0", E)
    return svg_doc(W, H, o)


def strip(x, y, w, h, num, name, pk, rms, hold, duck, st_lufs, fader_db):
    o = rect(x + 0.5, y + 0.5, w - 1, h - 1, C["panel"], 6, C["line"])
    o += rect(x + 12.5, y + 12.5, 21, 17, "none", 3, C["accent"])
    o += text(x + 23, y + 25, num, 10, C["accent"], 600, "middle", tnum=True)
    o += text(x + 42, y + 25.5, name, 11, C["t1"], 600, ls=1.2)
    my, mh = y + 82, h - 162
    # Top readouts: peak (level meter) and duck amount (blue meter).
    o += label(x + 12, y + 50, "PK")
    o += value_box(x + 12, y + 56, 44, 20, pk, "")
    o += label(x + 64, y + 50, "DUCK")
    o += value_box(x + 64, y + 56, 44, 20, f"{duck:.1f}" if duck else "0.0", "")
    o += vmeter(x + 14, my, 16, mh, pk_val(pk), rms, hold)
    o += grmeter(x + 33, my, 8, mh, duck * 2.5)   # Duck shown on a 0..-24 dB scale.
    o += meter_scale(x + 58, my, mh)
    fx = x + w - 40
    o += vfader(fx, my, mh, fader_db)
    o += value_box(fx - 26, my + mh + 8, 52, 20, f"{fader_db:+.1f}" if fader_db else "0.0")
    o += line(x + 12, y + h - 42.5, x + w - 12, y + h - 42.5, C["line"])
    o += label(x + 12, y + h - 24, "S-LUFS")
    o += text(x + w - 12, y + h - 22, st_lufs, 14, C["t1"], 500, "end", tnum=True)
    return o


def pk_val(s):
    return float(s.replace("−", "-"))


def screen_unit05():
    W, H, E = 1120, 560, 18
    o = rack(W, H, E)
    o += header(W, "05", "STREAM DIRECTOR", E)
    L = E + 16
    sy, sh = 76, 440
    data = [("01", "VOCAL", "-2.2", -13.0, -2.2, 0.0, "-12.9", 0.0),
            ("02", "COLLAB", "-11.1", -19.0, -10.2, -2.1, "-17.3", -1.5),
            ("03", "GAME", "-14.2", -22.0, -9.4, -3.4, "-19.3", -3.0),
            ("04", "BGM", "-9.5", -15.5, -8.1, -5.8, "-13.0", -6.0)]
    sw, gap = 168, 10
    for i, d in enumerate(data):
        o += strip(L + i * (sw + gap), sy, sw, sh, *d)
    # Master section.
    mx = L + 4 * (sw + gap) + 6
    mw = W - E - 16 - mx
    o += rect(mx + 0.5, sy + 0.5, mw - 1, sh - 1, C["panel"], 6, C["line"])
    px = mx + 16
    pw = mw - 32
    o += label(px, sy + 24, "VIRTUAL MASTER SUM  ·  LUFS")
    cw = (pw - 16) / 3
    for i, (lab_, v) in enumerate([("MOMENTARY", "-12.0"), ("SHORT-TERM", "-12.0"), ("INTEGRATED", "-12.1")]):
        o += readout(px + i * (cw + 8), sy + 34, cw, 64, lab_, v, "", big=True)
    # YouTube target band.
    ty = sy + 126
    o += label(px, ty, "YOUTUBE LIVE TARGET   -12.0 ±1.5 LUFS")
    o += led(px + pw - 62, ty - 3.5, C["ok"], 3)
    o += text(px + pw, ty, "IN RANGE", 9.5, C["ok"], 600, "end", ls=1.2)
    by, bh = ty + 10, 14
    o += rect(px, by, pw, bh, C["off"], 2, C["line"])

    def lx(l):
        return px + (l + 24) / 18.0 * pw
    o += rect(lx(-13.5), by + 1, lx(-10.5) - lx(-13.5), bh - 2, "#1F3A2B")
    o += line(lx(-13.5), by, lx(-13.5), by + bh, C["ok"]) + line(lx(-10.5), by, lx(-10.5), by + bh, C["ok"])
    o += rect(lx(-24) + 1, by + 4, lx(-12.1) - lx(-24) - 1, bh - 8, "#3E7A57", 1)
    o += rect(lx(-12.1) - 1, by - 3, 2, bh + 6, C["t1"])
    for l in (-24, -18, -12, -6):
        o += text(lx(l), by + bh + 14, f"{l}", 9, C["t3"], 400, "middle", tnum=True)
    # Phase correlation.
    cy_ = by + 52
    o += label(px, cy_, "PHASE CORRELATION")
    o += text(px + pw, cy_, "+0.82", 11, C["t1"], 500, "end", tnum=True)
    cby = cy_ + 10
    o += rect(px, cby, pw, 12, C["off"], 2, C["line"])
    mid = px + pw / 2
    o += rect(mid, cby + 3, pw / 2 * 0.82, 6, "#3E7A57", 1)
    o += line(mid, cby - 2, mid, cby + 14, C["t3"])
    o += rect(mid + pw / 2 * 0.82 - 1, cby - 2, 2, 16, C["t1"])
    for v, t in ((-1, "-1"), (0, "0"), (1, "+1")):
        o += text(px + (v + 1) / 2 * pw, cby + 26, t, 9, C["t3"], 400, "middle", tnum=True)
    # Mono check + meter reset.
    ky = cby + 40
    half = (pw - 8) / 2
    o += key(px, ky, half, 32, "モノラル確認", led_color=C["accent"], led_on=False)
    o += key(px + half + 8, ky, half, 32, "メーターリセット")
    # Scene.
    o += label(px, ky + 58, "SCENE")
    o += segmented(px, ky + 66, pw, 32, ["FPS・ゲーム", "雑談", "歌枠", "ASMR"], 1, 11)
    o += key(px, ky + 112, half, 32, "チャプター記録")
    o += key(px + half + 8, ky + 112, half, 32, "配信カルテ出力")
    o += status_line(W, H, C["ok"], "5 ユニット リンク中 · Summing Guard 待機 · 先読み 1.5 ms",
                     "48 kHz · 2 ch · ライセンス · v1.0.0", E)
    return svg_doc(W, H, o)


def mini(W, num, name, pk, rms, hold, readouts):
    H, E = 120, 18
    o = rack(W, H, E)
    L = E + 16
    o += wordmark(L, 24, compact=True)
    o += line(L + 96, 14, L + 96, 34, C["line"])
    o += rect(L + 108.5, 13.5, 21, 17, "none", 3, C["accent"])
    o += text(L + 119, 26, num, 10, C["accent"], 600, "middle", tnum=True)
    o += text(L + 138, 26.5, name, 11, C["t1"], 600, ls=1.2)
    rx = W - E - 16
    o += icon_key(rx - 24, 10, 24, icon_reset)
    o += lang_toggle(rx - 24 - 6 - 64, 10, 24)
    o += icon_key(rx - 24 - 6 - 64 - 6 - 24, 10, 24, lambda a, b, c: icon_collapse(a, b, c, False), active=True)
    o += icon_key(rx - 24 - 6 - 64 - 6 - 24 - 6 - 24, 10, 24, icon_pin)
    o += line(E, 44.5, W - E, 44.5, C["line"])
    mw = W - E - 16 - L - 30 - 66 * len(readouts)
    o += hmeter(L, 58, mw, 14, pk, rms, hold)
    rx0 = L + mw + 30
    for i, (lab_, v) in enumerate(readouts):
        x = rx0 + i * 66
        o += label(x, 64, lab_)
        o += text(x, 86, v, 15, C["t1"], 500, tnum=True)
    return svg_doc(W, H, o)


def screen_minis():
    a = mini(560, "01", "VOCAL MASTER", -6.0, -13.5, -2.2, [("TP", "-2.2"), ("S", "-12.9"), ("GR", "-2.5")])
    b = mini(1120, "05", "STREAM DIRECTOR", -8.0, -14.0, -4.9,
             [("M", "-12.0"), ("S", "-12.0"), ("I", "-12.1"), ("TP", "-4.9")])
    W, H = 1160, 330
    body = (text(20, 30, "MINI  —  [01] 560 × 120", 11, C["t3"], 600, ls=1) +
            f'<g transform="translate(20,44)">{strip_svg(a)}</g>' +
            text(20, 196, "MINI  —  [05] 1120 × 120", 11, C["t3"], 600, ls=1) +
            f'<g transform="translate(20,210)">{strip_svg(b)}</g>')
    return svg_doc(W, H, body, bg="#0A0B0C")


def strip_svg(doc):
    # Embed one generated <svg> inside another (keeps the component code single-sourced).
    inner = doc.split(">", 1)[1].rsplit("</svg>", 1)[0]
    return inner


def knob(cx, cy, r, angle_deg, ok_from=None, ok_to=None, arrow=None):
    """Gain knob line art. 0 deg = 12 o'clock, clockwise positive."""
    o = ""
    def pt(a, rr):
        t = math.radians(a - 90)
        return cx + rr * math.cos(t), cy + rr * math.sin(t)
    # Range arc (7 o'clock .. 5 o'clock = -150 .. +150 deg).
    s = pt(-150, r + 7); e = pt(150, r + 7)
    o += f'<path d="M{s[0]:.1f} {s[1]:.1f} A{r+7} {r+7} 0 1 1 {e[0]:.1f} {e[1]:.1f}" fill="none" stroke="{C["line_strong"]}" stroke-width="2"/>'
    if ok_from is not None:
        a = pt(ok_from, r + 7); b = pt(ok_to, r + 7)
        o += f'<path d="M{a[0]:.1f} {a[1]:.1f} A{r+7} {r+7} 0 0 1 {b[0]:.1f} {b[1]:.1f}" fill="none" stroke="{C["accent"]}" stroke-width="3" stroke-linecap="round"/>'
    for hour in range(12):
        if hour in (6,):
            continue
        a = hour * 30
        if a > 180:
            a -= 360
        if abs(a) > 150:
            continue
        p1 = pt(a, r + 11); p2 = pt(a, r + 15)
        o += line(p1[0], p1[1], p2[0], p2[1], C["t3"])
    o += f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#2B3036" stroke="{C["line_strong"]}" stroke-width="1"/>'
    o += f'<circle cx="{cx}" cy="{cy}" r="{r-5}" fill="#23272C"/>'
    p = pt(angle_deg, r - 3)
    o += line(cx, cy, p[0], p[1], C["t1"], 2)
    if arrow == "ccw":
        a = pt(-10, r + 24); b = pt(-50, r + 24)
        o += (f'<path d="M{a[0]:.1f} {a[1]:.1f} A{r+24} {r+24} 0 0 0 {b[0]:.1f} {b[1]:.1f}" fill="none" '
              f'stroke="{C["error"]}" stroke-width="2" stroke-linecap="round"/>')
        o += f'<circle cx="{b[0]:.1f}" cy="{b[1]:.1f}" r="3" fill="{C["error"]}"/>'
    return o


def modal(state):
    W, H = 560, 400
    o = rect(0.5, 0.5, W - 1, H - 1, C["faceplate"], 8, C["line_strong"])
    o += text(24, 34, "入力レベル自動測定", 14, C["t1"], 600)
    steps = ["準備", "測定", "結果"]
    cur = 1 if state == "measuring" else 2
    for i, s in enumerate(steps):
        x = W - 24 - (3 - i) * 64
        col = C["accent"] if i == cur else (C["t2"] if i < cur else C["t3"])
        o += rect(x, 24, 56, 3, col if i <= cur else C["line"], 1.5)
        o += text(x, 42, f"{i + 1}  {s}", 10, col, 600 if i == cur else 500)
    o += line(0, 58.5, W, 58.5, C["line"])
    if state == "measuring":
        o += rect(24.5, 78.5, W - 49, 110, C["panel"], 6, C["line"])
        o += label(40, 100, "読み上げ文")
        o += text(40, 134, "「みなさんこんばんは、配信へようこそ。", 16, C["t1"], 500)
        o += text(40, 162, "　今日も楽しい時間にしていきましょう。」", 16, C["t1"], 500)
        o += label(24, 218, "INPUT")
        o += hmeter(72, 208, W - 96, 12, -9.0, -18.0, -7.5, scale=False)
        o += label(24, 256, "PROGRESS")
        o += rect(96, 246, W - 120 - 96, 10, C["off"], 5, C["line"])
        o += rect(96, 246, (W - 120 - 96) * 0.4, 10, C["accent"], 5)
        o += text(W - 24, 256, "04.0 / 10.0 秒", 11, C["t1"], 500, "end", tnum=True)
        o += text(24, 300, "普段の配信と同じ声の大きさで、そのまま読み続けてください。", 12, C["t2"], 500)
        o += key(W - 24 - 96, H - 24 - 34, 96, 34, "中止")
        return o
    color = {"success": C["ok"], "clip": C["error"], "novoice": C["warn"]}[state]
    title = {"success": "測定が完了しました", "clip": "入力が歪んでいます", "novoice": "声を検出できませんでした"}[state]
    o += rect(24, 78, 4, 84 if state != "success" else 118, color, 2)
    o += text(42, 98, title, 15, C["t1"], 600)
    if state == "success":
        o += text(42, 122, "入力ゲインを自動で設定しました。", 12, C["t2"], 500)
        bw = 148
        for i, (lab_, v, u) in enumerate([("INPUT GAIN", "+3.5", "dB"), ("REF. PEAK", "-9.5", "dBFS"),
                                          ("EST. LOUDNESS", "-12.4", "LUFS")]):
            o += readout(42 + i * (bw + 8), 136, bw, 60, lab_, v, u)
        o += knob(92, 270, 22, 45, ok_from=30, ok_to=60)
        o += text(138, 262, "オーディオインターフェースの GAIN つまみは", 12, C["t1"], 500)
        o += text(138, 282, "1時〜2時の位置が目安です。", 12, C["t1"], 500)
        o += key(W - 24 - 96 - 8 - 96, H - 24 - 34, 96, 34, "再測定")
        o += key(W - 24 - 96, H - 24 - 34, 96, 34, "完了", "primary")
    elif state == "clip":
        o += text(42, 122, "オーディオインターフェース本体の GAIN つまみを下げてから、", 12, C["t2"], 500)
        o += text(42, 141, "もう一度測定してください。クリップランプが点かない位置が目安です。", 12, C["t2"], 500)
        o += knob(92, 250, 22, 120, arrow="ccw")
        o += readout(156, 214, 150, 60, "CLIP DETECTED", "12", "回", vcol=C["error"])
        o += readout(316, 214, 150, 60, "INPUT GAIN", "0.0", "dB（変更なし）")
        o += key(W - 24 - 96 - 8 - 96, H - 24 - 34, 96, 34, "閉じる")
        o += key(W - 24 - 96, H - 24 - 34, 96, 34, "再測定", "primary")
    else:
        o += text(42, 122, "マイクのミュートと、OBS の入力デバイスの選択を確認してください。", 12, C["t2"], 500)
        o += text(42, 141, "入力ゲインは変更していません。", 12, C["t2"], 500)
        o += label(42, 196, "INPUT (MEASURED)")
        o += hmeter(42, 206, W - 84, 12, -58.0, -66.0, -55.0)
        o += readout(42, 250, 150, 60, "VOICE DETECTED", "6", "%", vcol=C["warn"])
        o += readout(200, 250, 150, 60, "REQUIRED", "20", "% 以上")
        o += key(W - 24 - 96 - 8 - 96, H - 24 - 34, 96, 34, "閉じる")
        o += key(W - 24 - 96, H - 24 - 34, 96, 34, "再測定", "primary")
    return o


def screen_modals():
    W, H = 1220, 920
    body = ""
    cells = [("measuring", "測定中"), ("success", "完了"), ("clip", "入力の歪み"), ("novoice", "声を検出できず")]
    for i, (st, cap) in enumerate(cells):
        x = 30 + (i % 2) * 600
        y = 50 + (i // 2) * 440
        body += text(x, y - 16, f"自動測定モーダル — {cap}", 11, C["t3"], 600, ls=0.6)
        body += f'<g transform="translate({x},{y})" filter="url(#shadow)">{modal(st)}</g>'
    return svg_doc(W, H, body, bg="#0A0B0C")


def screen_components():
    W, H = 1220, 780
    o = ""
    o += text(30, 40, "COMPONENT SHEET  —  MODEL 600 GUI", 13, C["t1"], 700, ls=1.6)
    # Palette.
    o += label(30, 76, "COLOR TOKENS")
    sw = [("faceplate", C["faceplate"]), ("panel", C["panel"]), ("raised", C["raised"]),
          ("raised.active", C["raised_active"]), ("line", C["line"]), ("text.1", C["t1"]), ("text.2", C["t2"]),
          ("text.3", C["t3"]), ("accent", C["accent"]), ("meter.green", C["green"]), ("meter.yellow", C["yellow"]),
          ("meter.red", C["red"]), ("gr", C["gr"])]
    for i, (n, col) in enumerate(sw):
        x = 30 + i * 90
        o += rect(x + 0.5, 88.5, 80, 40, col, 4, C["line_strong"])
        o += text(x, 144, n, 9.5, C["t2"], 500)
        o += text(x, 158, col, 9, C["t3"], 400, tnum=True)
    # Type scale.
    o += label(30, 196, "TYPE SCALE  (Inter / Noto Sans JP)")
    o += text(30, 228, "-12.1", 22, C["t1"], 500, tnum=True) + text(100, 228, "Display 22 Medium", 10, C["t3"])
    o += text(250, 228, "MODEL 600", 16, C["t1"], 700, ls=1) + text(352, 228, "Title 15–16 Bold", 10, C["t3"])
    o += text(500, 228, "10秒 自動測定", 12, C["t1"], 500) + text(590, 228, "Body 12 Medium", 10, C["t3"])
    o += label(720, 228, "SHORT-TERM") + text(800, 228, "Label 9.5 SemiBold", 10, C["t3"])
    o += text(940, 228, "-24", 9, C["t3"], 400, tnum=True) + text(966, 228, "Scale 9", 10, C["t3"])
    # Keys.
    o += label(30, 270, "KEYS")
    o += key(30, 280, 130, 34, "A/B 試聴", led_color=C["accent"], led_on=False)
    o += key(170, 280, 130, 34, "A/B 試聴", "active", led_color=C["accent"], led_on=True)
    o += key(310, 280, 130, 40, "ピー音  1 kHz", "danger", led_color=C["red"], led_on=True)
    o += key(450, 280, 130, 34, "再測定")
    o += key(590, 280, 100, 34, "完了", "primary")
    o += icon_key(700, 283, 28, icon_pin) + icon_key(736, 283, 28, icon_pin, True)
    o += icon_key(772, 283, 28, lambda a, b, c: icon_collapse(a, b, c, True))
    o += icon_key(808, 283, 28, icon_reset)
    o += lang_toggle(846, 283, 28, "JA")
    for x, t in ((30, "トグル 消灯"), (170, "トグル 点灯"), (310, "非常用 押下中"), (450, "通常"), (590, "主ボタン"),
                 (700, "ピン 通常 / 固定中 · 折畳 · 戻す · 言語")):
        o += text(x, 336, t, 9.5, C["t3"])
    # Segmented.
    o += label(30, 372, "SEGMENTED CONTROL")
    o += segmented(30, 380, 420, 30, ["SOFT / ASMR", "BROADCAST", "FPS / LOUD"], 1)
    o += segmented(470, 380, 420, 30, ["DRY", "KARAOKE", "BATH", "HALL", "CAVE", "RADIO"], 2, 10.5)
    # Meters & faders.
    o += label(30, 446, "METERS / FADERS")
    o += vmeter(30, 456, 22, 250, -1.5, -9.0, -0.6)
    o += meter_scale(74, 456, 250)
    o += grmeter(96, 456, 14, 105, -6.5)
    o += gr_scale(124, 456, 105)
    o += vfader(200, 456, 250, -3.0)
    o += hmeter(260, 470, 340, 14, -7.0, -15.0, -4.0)
    o += hfader(266, 540, 250, 2.5)
    o += value_box(532, 529, 68, 22, "+2.5")
    o += readout(260, 590, 120, 60, "TRUE PEAK", "-0.6", "dBTP", vcol=C["red"])
    o += readout(390, 590, 120, 60, "SHORT-TERM", "-12.9", "LUFS")
    o += text(30, 730, "目盛り則：0 dB=100% / -6=80% / -12=62% / -18=48% / -24=37% / -36=20% / -48=8% / -60=0%（区間は直線補間）",
              10, C["t3"])
    o += text(30, 746, "白線 = ピークホールド、淡色 = ピークは到達したが RMS は未到達の区間", 10, C["t3"])
    # Header states.
    o += label(640, 446, "HEADER / STATUS STATES")
    hdr = svg_doc(560, 64, rack(560, 64, 0) + header(560, "01", "VOCAL MASTER", 0, pinned=True, update="1.1.0"))
    o += f'<g transform="translate(640,456)">{strip_svg(hdr)}</g>'
    o += text(640, 536, "ピン固定中 + アップデートあり（ボタン下に表示）", 9.5, C["t3"])
    sts = [(C["ok"], "動作中 · 先読み 1.5 ms"), (C["warn"], "音楽のような信号です。BGM には 04 BGM POCKET を使用してください。"),
           (C["info"], "Link Bridge：VC Edition と連携中のため、二重処理を自動で止めています。"),
           (C["ok"], "雑談ソロを検出：天井を -1.5 dB 下げています。"),
           (C["error"], "Bluetooth が通話用の音質（16 kHz）に下がっています。")]
    for i, (col, msg) in enumerate(sts):
        y = 552 + i * 34
        o += rect(640.5, y + 0.5, 559, 27, C["faceplate"], 4, C["line"])
        o += led(656, y + 14, col, 3)
        o += text(668, y + 18, msg, 10, C["t2"], 500)
    return svg_doc(W, H, o, bg="#0A0B0C")


def main():
    os.makedirs(OUT, exist_ok=True)
    files = {
        "01_unit01_console.svg": screen_unit01(),
        "02_unit05_mixer.svg": screen_unit05(),
        "03_mini_strips.svg": screen_minis(),
        "04_calibration_modal_states.svg": screen_modals(),
        "05_component_sheet.svg": screen_components(),
    }
    for name, doc in files.items():
        with open(os.path.join(OUT, name), "w", encoding="utf-8") as f:
            f.write(doc)
        print("wrote", name)


if __name__ == "__main__":
    main()
