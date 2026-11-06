#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  logo concepts taken from the product GUI.
#
#  The GUI v2 faceplate already carries the brand: SEVENTHWELL in amber,
#  tracked, over MODEL 600 in Inter Bold (off-white), next to an amber unit
#  badge; amber seven-segment readouts with the unlit segments still visible;
#  a charcoal anodised panel. These concepts lift that system into a logo.
#  Colours and fonts are the GUI's own (src/ui-theme.hpp, resources/fonts).
#
#  Text is converted to outlines with fontTools (Inter, SIL OFL 1.1, which
#  allows use in logos), so the SVGs render the same everywhere.
#    python3 -m pip install fonttools   (any recent version)
#
#  Output (docs/design/logo/svg/): d_nameplate_*, e_segment_*, f_faceplate_*
#    *_dark.svg   on the panel colour (as in the product)
#    *_light.svg  on white (README light mode, documents)
# =============================================================================
import math
import os

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "svg")
FONTS = os.path.normpath(os.path.join(HERE, "..", "..", "..", "resources", "fonts"))

# GUI tokens (src/ui-theme.hpp).
CHASSIS = "#0E1012"
FACE = "#191C20"
RAISED = "#23272C"
T1 = "#E7E9EC"
T3 = "#646B73"
AMBER = "#F0A33A"
AMBER_ON_LIGHT = "#A8620F"   # Small amber text on white: 4.6:1 (AMBER is 2.1:1).
INK = "#14171B"
SEG_OFF_DARK = "#2E2416"     # Unlit segment behind amber glass.
SEG_OFF_LIGHT = "#EADCC6"

_fonts = {}


def font(weight):
    if weight not in _fonts:
        _fonts[weight] = TTFont(os.path.join(FONTS, f"Inter-{weight}.otf"))
    return _fonts[weight]


def text(s, x, y, size, weight="Bold", track=0.0, fill=T1):
    """Outlined text. (x, y) = left end of the baseline. track is in em/1000.
    Returns (svg, advance width)."""
    f = font(weight)
    gs, cmap = f.getGlyphSet(), f.getBestCmap()
    upm = f["head"].unitsPerEm
    sc = size / upm
    pen = SVGPathPen(gs)
    cx = 0.0
    for i, ch in enumerate(s):
        g = cmap[ord(ch)]
        tp = TransformPen(pen, (sc, 0, 0, -sc, x + cx, y))
        gs[g].draw(tp)
        cx += gs[g].width * sc
        if i < len(s) - 1:
            cx += track / 1000.0 * size
    return f'<path d="{pen.getCommands()}" fill="{fill}"/>', cx


def cap_height(size, weight="Bold"):
    f = font(weight)
    return f["OS/2"].sCapHeight / f["head"].unitsPerEm * size


def doc(inner, w, h, bg=None):
    b = f'<rect width="{w:.2f}" height="{h:.2f}" rx="{min(w, h) * 0.06:.2f}" fill="{bg}"/>' if bg else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.0f}" height="{h:.0f}" '
            f'viewBox="0 0 {w:.2f} {h:.2f}">{b}{inner}</svg>\n')


# --------------------------------------------------------------------------- seven segment
SEGS = {"0": "abcdef", "6": "acdefg", "8": "abcdefg"}


def seven_seg(ch, x, y, h, on, off, skew=8.0):
    """A seven-segment digit like the GUI readouts: slanted, bevelled segments,
    the unlit ones still drawn. (x, y) = top-left, h = digit height."""
    w = h * 0.52
    t = h * 0.13                       # Segment thickness.
    g = t * 0.18                       # Gap between segments.
    k = math.tan(math.radians(skew))

    def P(px, py):                     # Slant around the baseline.
        return f"{x + px + (h - py) * k:.2f},{y + py:.2f}"

    def hseg(yc):
        x0, x1 = t / 2 + g, w - t / 2 - g
        return " ".join([P(x0, yc), P(x0 + t / 2, yc - t / 2), P(x1 - t / 2, yc - t / 2), P(x1, yc),
                         P(x1 - t / 2, yc + t / 2), P(x0 + t / 2, yc + t / 2)])

    def vseg(xc, y0, y1):
        y0, y1 = y0 + g, y1 - g
        return " ".join([P(xc, y0), P(xc + t / 2, y0 + t / 2), P(xc + t / 2, y1 - t / 2), P(xc, y1),
                         P(xc - t / 2, y1 - t / 2), P(xc - t / 2, y0 + t / 2)])

    mid = h / 2
    shapes = {
        "a": hseg(t / 2), "g": hseg(mid), "d": hseg(h - t / 2),
        "f": vseg(t / 2, t / 2, mid), "b": vseg(w - t / 2, t / 2, mid),
        "e": vseg(t / 2, mid, h - t / 2), "c": vseg(w - t / 2, mid, h - t / 2),
    }
    lit = SEGS[ch]
    out = []
    for s, pts in shapes.items():
        out.append(f'<polygon points="{pts}" fill="{on if s in lit else off}"/>')
    return "".join(out), w + h * k


# --------------------------------------------------------------------------- concepts
def d_nameplate(dark):
    """The GUI nameplate as the logo: amber SEVENTHWELL over MODEL 600, and the
    amber badge (the unit-number tile in the product) carrying the dial."""
    fg = T1 if dark else INK
    pad = 60
    s_brand, s_name = 22, 96
    brand, bw = text("SEVENTHWELL", 0, 0, s_brand, "SemiBold", track=260, fill=AMBER if dark else AMBER_ON_LIGHT)
    name, nw = text("MODEL 600", 0, 0, s_name, "Bold", track=-10, fill=fg)
    ch = cap_height(s_name)
    y_brand = pad + cap_height(s_brand, "SemiBold")
    y_name = y_brand + 22 + ch
    # Badge: square tile, height = cap height, amber, with the dial knocked out.
    bx = pad + nw + 30
    by = y_name - ch
    tile = f'<rect x="{bx:.2f}" y="{by:.2f}" width="{ch:.2f}" height="{ch:.2f}" rx="{ch * 0.12:.2f}" fill="{AMBER}"/>'
    c, r = (bx + ch / 2, by + ch / 2), ch * 0.30
    knock = FACE if dark else "#FFFFFF"
    a0, a1 = math.radians(-135 - 90), math.radians(135 - 90)
    arc = (f'<path d="M{c[0] + r * math.cos(a0):.2f},{c[1] + r * math.sin(a0):.2f} '
           f'A{r:.2f},{r:.2f} 0 1 1 {c[0] + r * math.cos(a1):.2f},{c[1] + r * math.sin(a1):.2f}" '
           f'fill="none" stroke="{knock}" stroke-width="{ch * 0.075:.2f}" stroke-linecap="round"/>')
    disc = f'<circle cx="{c[0]:.2f}" cy="{c[1]:.2f}" r="{ch * 0.17:.2f}" fill="{knock}"/>'
    ang = math.radians(52 - 90)
    ptr = (f'<line x1="{c[0] + ch * 0.03 * math.cos(ang):.2f}" y1="{c[1] + ch * 0.03 * math.sin(ang):.2f}" '
           f'x2="{c[0] + ch * 0.11 * math.cos(ang):.2f}" y2="{c[1] + ch * 0.11 * math.sin(ang):.2f}" '
           f'stroke="{AMBER}" stroke-width="{ch * 0.05:.2f}" stroke-linecap="round"/>')
    body = (f'<g transform="translate({pad},{y_brand:.2f})">{brand}</g>'
            f'<g transform="translate({pad},{y_name:.2f})">{name}</g>' + tile + arc + disc + ptr)
    # Rule + subline, as on the faceplate.
    y_rule = y_name + 30
    sub, sw = text("BROADCAST SUITE", 0, 0, 20, "SemiBold", track=240, fill=fg if dark else INK)
    body += f'<rect x="{pad}" y="{y_rule:.2f}" width="{nw * 0.55:.2f}" height="3" fill="{T3 if dark else "#B9BEC4"}"/>'
    body += f'<g transform="translate({pad},{y_rule + 22 + cap_height(20, "SemiBold"):.2f})">{sub}</g>'
    w = bx + ch + pad
    h = y_rule + 22 + cap_height(20, "SemiBold") + pad
    return doc(body, w, h, FACE if dark else "#FFFFFF")


def e_segment(dark):
    """600 as the GUI's amber seven-segment readout (unlit segments visible),
    MODEL as the readout's label, inside a dark display window."""
    pad = 60
    dh = 150                                   # Digit height.
    win_pad = 34
    label, lw = text("MODEL", 0, 0, 24, "SemiBold", track=220, fill=AMBER)
    digits, x = [], 0.0
    for ch in "600":
        g, adv = seven_seg(ch, x, 0, dh, AMBER, SEG_OFF_DARK)
        digits.append(g)
        x += adv + dh * 0.12
    dw = x - dh * 0.12
    lh = cap_height(24, "SemiBold")
    win_w = max(dw, lw) + 2 * win_pad
    win_h = win_pad + lh + 22 + dh + win_pad
    wx, wy = pad, pad
    bezel = (f'<rect x="{wx - 8}" y="{wy - 8}" width="{win_w + 16:.2f}" height="{win_h + 16:.2f}" rx="16" fill="{RAISED}"/>'
             f'<rect x="{wx}" y="{wy}" width="{win_w:.2f}" height="{win_h:.2f}" rx="10" fill="{CHASSIS}"/>')
    body = bezel
    body += f'<g transform="translate({wx + win_pad},{wy + win_pad + lh:.2f})">{label}</g>'
    body += f'<g transform="translate({wx + win_pad},{wy + win_pad + lh + 22:.2f})">{"".join(digits)}</g>'
    # Brand under the window.
    fg = T1 if dark else INK
    brand, bw = text("SEVENTHWELL", 0, 0, 20, "SemiBold", track=260, fill=T3 if dark else "#646B73")
    by = wy + win_h + 8 + 40 + cap_height(20, "SemiBold")
    body += f'<g transform="translate({wx + (win_w - bw) / 2:.2f},{by:.2f})">{brand}</g>'
    w = win_w + 2 * pad
    h = by + pad
    return doc(body, w, h, FACE if dark else "#FFFFFF")


def f_faceplate(dark):
    """A miniature rack faceplate: the nameplate engraved on a charcoal plate
    with rack ears and slots, the way the product looks in OBS."""
    pad = 50
    s_name = 84
    brand, bw = text("SEVENTHWELL", 0, 0, 18, "SemiBold", track=260, fill=AMBER)
    name, nw = text("MODEL 600", 0, 0, s_name, "Bold", track=-10, fill=T1)
    ch = cap_height(s_name)
    ear = 56
    inner_w = nw + 2 * 56
    plate_w = inner_w + 2 * ear
    plate_h = 40 + cap_height(18, "SemiBold") + 18 + ch + 44
    px, py = pad, pad
    plate = f'<rect x="{px}" y="{py}" width="{plate_w:.2f}" height="{plate_h:.2f}" rx="12" fill="{FACE}"/>'
    # Rack-ear slots (stadiums) and the seams between ears and panel.
    for ex in (px + ear / 2, px + plate_w - ear / 2):
        for ey in (py + plate_h * 0.24, py + plate_h * 0.76):
            plate += f'<rect x="{ex - 14:.2f}" y="{ey - 7:.2f}" width="28" height="14" rx="7" fill="{CHASSIS}"/>'
    plate += f'<rect x="{px + ear - 2:.2f}" y="{py}" width="3" height="{plate_h:.2f}" fill="{CHASSIS}"/>'
    plate += f'<rect x="{px + plate_w - ear - 1:.2f}" y="{py}" width="3" height="{plate_h:.2f}" fill="{CHASSIS}"/>'
    x0 = px + ear + 56
    yb = py + 40 + cap_height(18, "SemiBold")
    plate += f'<g transform="translate({x0:.2f},{yb:.2f})">{brand}</g>'
    plate += f'<g transform="translate({x0:.2f},{yb + 18 + ch:.2f})">{name}</g>'
    w, h = plate_w + 2 * pad, plate_h + 2 * pad
    return doc(plate, w, h, CHASSIS if dark else "#FFFFFF")


CONCEPTS = {"d_nameplate": d_nameplate, "e_segment": e_segment, "f_faceplate": f_faceplate}


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in CONCEPTS.items():
        for mode, dark in (("dark", True), ("light", False)):
            with open(os.path.join(OUT, f"{name}_{mode}.svg"), "w") as f:
                f.write(fn(dark))
        print("wrote", name)


if __name__ == "__main__":
    main()
