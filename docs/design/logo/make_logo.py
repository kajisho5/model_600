#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  logo concepts (Step 2, black first).
#
#  The symbol is the approved app-icon idea (concept A "dial": a 270-degree
#  level arc around a solid control disc with a pointer), redrawn as a
#  one-colour mark. The concepts differ in the wordmark and the lockup.
#  Letterforms are built from geometric strokes on a 100-unit cap height (no
#  font, no <text>), so the files render the same everywhere, GitHub included.
#
#  Output (docs/design/logo/svg/):
#    symbol.svg                       one-colour dial symbol (256 x 256)
#    <concept>_wordmark.svg           wordmark only
#    <concept>_lockup.svg             symbol + wordmark, horizontal
#  Concepts: a_dialzero (knob zero), b_badge, c_engraved
# =============================================================================
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "svg")
INK = "#111111"


# --------------------------------------------------------------------------- helpers
def pol(cx, cy, r, deg):
    """Point on a circle; 0 deg = up, clockwise."""
    t = math.radians(deg - 90)
    return cx + r * math.cos(t), cy + r * math.sin(t)


def arc(cx, cy, r, a0, a1):
    x0, y0 = pol(cx, cy, r, a0)
    x1, y1 = pol(cx, cy, r, a1)
    large = 1 if (a1 - a0) % 360 > 180 else 0
    return f"M{x0:.2f},{y0:.2f} A{r},{r} 0 {large} 1 {x1:.2f},{y1:.2f}"


def doc(inner, w, h):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.0f}" height="{h:.0f}" '
            f'viewBox="0 0 {w:.2f} {h:.2f}">{inner}</svg>\n')


# --------------------------------------------------------------------------- symbol
def dial(cx, cy, size, ink=INK, knock="#FFFFFF"):
    """One-colour dial: level arc (track omitted), solid disc, pointer as a knockout.
    Proportions follow the app icon (arc r 318 / disc r 196 / stroke 76 on 1024)."""
    s = size / 1024.0
    r_arc, w_arc, r_disc = 318 * s, 76 * s, 196 * s
    a = f'<path d="{arc(cx, cy, r_arc, -135, 135)}" fill="none" stroke="{ink}" stroke-width="{w_arc:.2f}" stroke-linecap="round"/>'
    d = f'<circle cx="{cx:.2f}" cy="{cy:.2f}" r="{r_disc:.2f}" fill="{ink}"/>'
    px, py = pol(cx, cy, 118 * s, 52)
    qx, qy = pol(cx, cy, 36 * s, 52)
    p = (f'<line x1="{qx:.2f}" y1="{qy:.2f}" x2="{px:.2f}" y2="{py:.2f}" stroke="{knock}" '
         f'stroke-width="{60 * s:.2f}" stroke-linecap="round"/>')
    return a + d + p


def dial_extent(size):
    """Visible half-extent of the dial around its centre (arc outer radius)."""
    s = size / 1024.0
    return (318 + 38) * s


# --------------------------------------------------------------------------- letters
class Pen:
    """Stroke glyphs on a cap height of 100 (y 0 = cap line, 100 = baseline)."""

    def __init__(self, w, cap="round", join="round", widths=None):
        self.w, self.h = w, w / 2.0
        self.cap, self.join = cap, join
        self.W = {"M": 100, "O": 84, "D": 84, "E": 68, "L": 62, "6": 66, "0": 72, " ": 40}
        if widths:
            self.W.update(widths)

    def glyph(self, ch):
        w, h = self.w, self.h
        W = self.W[ch]
        if ch == " ":
            return "", W
        if ch == "M":
            d = f"M{h},{100 - (h if self.cap == 'round' else 0)} V{h} L{W / 2},{72} L{W - h},{h} V{100 - (h if self.cap == 'round' else 0)}"
        elif ch in "O0":
            rx = (W - w) / 2
            return (f'<rect x="{h}" y="{h}" width="{W - w}" height="{100 - w}" rx="{rx:.2f}" '
                    f'fill="none" stroke="{INK}" stroke-width="{w}"/>'), W
        elif ch == "D":
            r = (100 - w) / 2
            d = (f"M{h},{h} H{W - h - r:.2f} A{r:.2f},{r:.2f} 0 0 1 {W - h - r:.2f},{100 - h} H{h} Z")
        elif ch == "E":
            d = f"M{W - (h if self.cap == 'round' else 0)},{h} H{h} V{100 - h} H{W - (h if self.cap == 'round' else 0)} M{h},50 H{W - 10}"
        elif ch == "L":
            d = f"M{h},{(h if self.cap == 'round' else 0)} V{100 - h} H{W - (h if self.cap == 'round' else 0)}"
        elif ch == "6":
            # Geometric 6: a round bowl about two thirds of the cap height and a
            # long straight stem tangent to its left side, rising to the top.
            # Solved on a 66-wide circle, then stretched horizontally to the
            # glyph width (tangency survives the stretch); the stroke itself is
            # never scaled, so the weight stays even at every size.
            R = 33.0                              # Outer bowl radius.
            r = R - h
            cx, cy = R, 100 - R
            tx, ty = cx + 0.30 * R, h             # Top end of the stem.
            dx, dy = tx - cx, ty - cy
            dist = math.hypot(dx, dy)
            base, off = math.atan2(dy, dx), math.acos(r / dist)
            pts = [(cx + r * math.cos(base + sgn * off), cy + r * math.sin(base + sgn * off)) for sgn in (-1, 1)]
            px, py = min(pts)                     # Left-hand tangent point.
            sx = W / (2 * R)
            g = (f'<ellipse cx="{cx * sx:.2f}" cy="{cy:.2f}" rx="{r * sx:.2f}" ry="{r:.2f}" fill="none" stroke="{INK}" stroke-width="{w}"/>'
                 f'<path d="M{px * sx:.2f},{py:.2f} L{tx * sx:.2f},{ty:.2f}" fill="none" stroke="{INK}" stroke-width="{w}" '
                 f'stroke-linecap="{self.cap}"/>')
            return g, W
        else:
            raise KeyError(ch)
        return (f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="{w}" '
                f'stroke-linecap="{self.cap}" stroke-linejoin="{self.join}"/>'), W

    def word(self, text, x, y, scale, track):
        """Draws text at (x, y) = top-left of cap height; returns (svg, width)."""
        parts, cx = [], 0.0
        for i, ch in enumerate(text):
            g, adv = self.glyph(ch)
            if g:
                parts.append(f'<g transform="translate({x + cx * scale:.2f},{y:.2f}) scale({scale})">{g}</g>')
            cx += adv + (track if i < len(text) - 1 and ch != " " and text[i + 1] != " " else 0)
        return "".join(parts), cx * scale


# --------------------------------------------------------------------------- concepts
def concept_a(lockup):
    """Knob zero: monoline round strokes; the last 0 of 600 carries the knob's
    pointer, so the name itself turns up. Lockup adds the dial symbol."""
    pen = Pen(w=15)
    body, wd = pen.word("MODEL 600", 0, 0, 1.0, track=16)
    W0 = pen.W["0"]
    zx = wd - W0 / 2                      # Centre of the last zero.
    ang = math.radians(52 - 90)
    ux, uy = math.cos(ang), math.sin(ang)
    body += (f'<line x1="{zx:.2f}" y1="50" x2="{zx + 13 * ux:.2f}" y2="{50 + 13 * uy:.2f}" '
             f'stroke="{INK}" stroke-width="12" stroke-linecap="round"/>')
    if not lockup:
        return body, wd, 100
    size = 120 / (2 * 356 / 1024)
    sym = dial(60, 50, size)
    return sym + f'<g transform="translate({120 + 36},0)">{body}</g>', 120 + 36 + wd, 100


def concept_b(lockup):
    """Panel badge: small tracked MODEL over heavy 600, like a rack front plate."""
    small = Pen(w=14, cap="butt", join="miter")
    heavy = Pen(w=20, cap="butt", join="miter", widths={"6": 70, "0": 74})
    top, tw = small.word("MODEL", 0, 0, 0.34, track=30)
    big, bw = heavy.word("600", 0, 0, 1.0, track=10)
    gap = 14
    width = max(tw, bw)
    body = f'<g transform="translate({(width - tw) / 2:.2f},0)">{top}</g>'
    body += f'<g transform="translate({(width - bw) / 2:.2f},{34 + gap})">{big}</g>'
    height = 34 + gap + 100
    if not lockup:
        return body, width, height
    size = height / (2 * 356 / 1024)
    sym = dial(height / 2, height / 2, size)
    return sym + f'<g transform="translate({height + 34},0)">{body}</g>', height + 34 + width, height


def concept_c(lockup):
    """Engraved: extended square-cut letters, wide tracking, like silkscreen on a faceplate."""
    pen = Pen(w=11, cap="butt", join="miter",
              widths={"M": 128, "O": 118, "D": 112, "E": 92, "L": 86, "6": 92, "0": 96, " ": 56})
    body, wd = pen.word("MODEL 600", 0, 0, 1.0, track=30)
    if not lockup:
        return body, wd, 100
    size = 150 / (2 * 356 / 1024)
    sym = dial(75, 50, size)
    return sym + f'<g transform="translate({75 + 75 + 46},0)">{body}</g>', 75 + 75 + 46 + wd, 100


CONCEPTS = {"a_dialzero": concept_a, "b_badge": concept_b, "c_engraved": concept_c}


def framed(inner, w, h, pad):
    return doc(f'<g transform="translate({pad},{pad})">{inner}</g>', w + 2 * pad, h + 2 * pad)


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, "symbol.svg"), "w") as f:
        f.write(doc(dial(128, 128, 256 * 0.98), 256, 256))
    for name, fn in CONCEPTS.items():
        for kind, lk in (("wordmark", False), ("lockup", True)):
            inner, w, h = fn(lk)
            # Lockup symbols taller than the cap height overhang it; add room.
            over = {"a_dialzero": 10, "c_engraved": 25}.get(name, 0) if lk else 0
            pad = 40
            if over:
                inner = f'<g transform="translate(0,{over})">{inner}</g>'
                h += 2 * over
            with open(os.path.join(OUT, f"{name}_{kind}.svg"), "w") as f:
                f.write(framed(inner, w, h, pad))
        print("wrote", name)


if __name__ == "__main__":
    main()
