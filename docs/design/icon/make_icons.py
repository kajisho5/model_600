#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  icon design proposals (Step 2 samples).
#  Writes 1024x1024 SVGs (macOS icon grid: 824 px body, 100 px margin,
#  continuous-corner rounded square) plus a DMG window mock, using the GUI
#  palette from docs/design/DESIGN_SPEC.md.
#    python3 docs/design/icon/make_icons.py   ->  docs/design/icon/svg/*.svg
# =============================================================================
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "svg")
FONTS = os.path.normpath(os.path.join(HERE, "..", "..", "..", "resources", "fonts"))

C = dict(chassis="#0E1012", face="#191C20", panel="#121417", raised="#23272C", line="#2C3137",
         line2="#3A4047", t1="#E7E9EC", t2="#9AA1A9", t3="#646B73", amber="#F0A33A",
         green="#46C17B", yellow="#E2BF4E", red="#E5534B", off="#1E2226")

FONT_CSS = f"""<style>
@font-face {{ font-family: Inter; font-weight: 600; src: url('file://{FONTS}/Inter-SemiBold.otf'); }}
@font-face {{ font-family: Inter; font-weight: 700; src: url('file://{FONTS}/Inter-Bold.otf'); }}
@font-face {{ font-family: Inter; font-weight: 500; src: url('file://{FONTS}/Inter-Medium.otf'); }}
@font-face {{ font-family: NotoJP; src: url('file://{FONTS}/NotoSansJP-Subset-Regular.otf'); }}
</style>"""

BODY = (100, 100, 824, 824)        # x, y, w, h of the icon body on the 1024 grid
R = 185                            # corner radius (~22.4 % of 824)


def squircle(x, y, w, h, r):
    # Continuous-corner approximation: cubic corners with 0.55 * r handles stretched.
    k = 0.62 * r
    return (f"M{x+r},{y} H{x+w-r} C{x+w-r+k},{y} {x+w},{y+r-k} {x+w},{y+r} "
            f"V{y+h-r} C{x+w},{y+h-r+k} {x+w-r+k},{y+h} {x+w-r},{y+h} "
            f"H{x+r} C{x+r-k},{y+h} {x},{y+h-r+k} {x},{y+h-r} "
            f"V{y+r} C{x},{y+r-k} {x+r-k},{y} {x+r},{y} Z")


def defs_common():
    return f"""
  <linearGradient id="plate" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#2A2E34"/><stop offset="0.5" stop-color="{C['face']}"/>
    <stop offset="1" stop-color="#121417"/>
  </linearGradient>
  <linearGradient id="rim" x1="0" y1="0" x2="0" y2="1">
    <stop offset="0" stop-color="#4A5058"/><stop offset="1" stop-color="#15181B"/>
  </linearGradient>
  <filter id="drop" x="-20%" y="-20%" width="140%" height="150%">
    <feDropShadow dx="0" dy="14" stdDeviation="18" flood-color="#000" flood-opacity="0.45"/>
  </filter>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%">
    <feGaussianBlur stdDeviation="10" result="b"/>
    <feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge>
  </filter>
  <pattern id="brush" width="1024" height="6" patternUnits="userSpaceOnUse">
    <rect width="1024" height="6" fill="none"/>
    <rect y="0" width="1024" height="1" fill="#ffffff" opacity="0.025"/>
    <rect y="3" width="1024" height="1" fill="#000000" opacity="0.05"/>
  </pattern>"""


def plate():
    x, y, w, h = BODY
    p = squircle(x, y, w, h, R)
    inner = squircle(x + 6, y + 6, w - 12, h - 12, R - 6)
    return f"""
  <g filter="url(#drop)"><path d="{p}" fill="url(#rim)"/></g>
  <path d="{inner}" fill="url(#plate)"/>
  <path d="{inner}" fill="url(#brush)"/>
  <path d="{inner}" fill="none" stroke="#ffffff" stroke-opacity="0.06" stroke-width="2"/>"""


def screws():
    out = []
    for sx, sy in ((178, 178), (846, 178), (178, 846), (846, 846)):
        out.append(f"""
  <circle cx="{sx}" cy="{sy}" r="20" fill="#2E333A" stroke="#0B0D0F" stroke-width="3"/>
  <path d="M{sx-11},{sy-6} L{sx+11},{sy+6}" stroke="#0B0D0F" stroke-width="5" stroke-linecap="round"/>""")
    return "".join(out)


def knob(cx, cy, r, angle_deg, ticks=True):
    a = math.radians(angle_deg - 90)
    px, py = cx + math.cos(a) * r * 0.78, cy + math.sin(a) * r * 0.78
    qx, qy = cx + math.cos(a) * r * 0.42, cy + math.sin(a) * r * 0.42
    knurl = []
    for i in range(60):
        t = math.radians(i * 6)
        x1, y1 = cx + math.cos(t) * (r - 2), cy + math.sin(t) * (r - 2)
        x2, y2 = cx + math.cos(t) * (r - 16), cy + math.sin(t) * (r - 16)
        knurl.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="#0E1012" stroke-opacity="0.55" stroke-width="3"/>')
    scale = []
    if ticks:
        for i in range(11):
            t = math.radians(-135 + i * 27 - 90)
            ro, ri = r + 58, r + (34 if i % 5 else 24)
            col = C['amber'] if i <= round((angle_deg + 135) / 27) else C['t3']
            scale.append(f'<line x1="{cx+math.cos(t)*ri:.1f}" y1="{cy+math.sin(t)*ri:.1f}" x2="{cx+math.cos(t)*ro:.1f}" y2="{cy+math.sin(t)*ro:.1f}" stroke="{col}" stroke-width="10" stroke-linecap="round"/>')
    return f"""
  <defs>
    <radialGradient id="cap{cx}" cx="0.38" cy="0.32" r="0.8">
      <stop offset="0" stop-color="#C9CDD2"/><stop offset="0.35" stop-color="#8A9097"/>
      <stop offset="0.75" stop-color="#4A5057"/><stop offset="1" stop-color="#2A2E33"/>
    </radialGradient>
    <radialGradient id="face{cx}" cx="0.4" cy="0.35" r="0.75">
      <stop offset="0" stop-color="#3A3F46"/><stop offset="1" stop-color="#16191C"/>
    </radialGradient>
  </defs>
  {''.join(scale)}
  <circle cx="{cx}" cy="{cy+10}" r="{r+6}" fill="#000" opacity="0.45" filter="url(#drop)"/>
  <circle cx="{cx}" cy="{cy}" r="{r}" fill="url(#cap{cx})"/>
  {''.join(knurl)}
  <circle cx="{cx}" cy="{cy}" r="{r*0.72:.1f}" fill="url(#face{cx})" stroke="#0B0D0F" stroke-width="3"/>
  <circle cx="{cx}" cy="{cy}" r="{r*0.72:.1f}" fill="none" stroke="#ffffff" stroke-opacity="0.08" stroke-width="2"/>
  <line x1="{qx:.1f}" y1="{qy:.1f}" x2="{px:.1f}" y2="{py:.1f}" stroke="{C['amber']}" stroke-width="16" stroke-linecap="round" filter="url(#glow)"/>"""


SEG = {  # 7-segment glyphs: a b c d e f g
    "6": "acdefg", "0": "abcdef", "8": "abcdefg",
}


def seg_digit(x, y, w, h, ch, on, off, th=None):
    th = th or w * 0.2
    g = th * 0.18
    segs = {
        "a": (x + th / 2 + g, y, x + w - th / 2 - g, y),
        "d": (x + th / 2 + g, y + h, x + w - th / 2 - g, y + h),
        "g": (x + th / 2 + g, y + h / 2, x + w - th / 2 - g, y + h / 2),
        "f": (x, y + th / 2 + g, x, y + h / 2 - th / 2 - g),
        "e": (x, y + h / 2 + th / 2 + g, x, y + h - th / 2 - g),
        "b": (x + w, y + th / 2 + g, x + w, y + h / 2 - th / 2 - g),
        "c": (x + w, y + h / 2 + th / 2 + g, x + w, y + h - th / 2 - g),
    }
    out = []
    for name, (x1, y1, x2, y2) in segs.items():
        col = on if name in SEG[ch] else off
        out.append(f'<line x1="{x1:.1f}" y1="{y1:.1f}" x2="{x2:.1f}" y2="{y2:.1f}" stroke="{col}" stroke-width="{th:.1f}" stroke-linecap="round"/>')
    return "".join(out)


def svg(body, w=1024, h=1024, extra_defs=""):
    return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">
{FONT_CSS}
<defs>{defs_common()}{extra_defs}</defs>
{body}
</svg>
"""


def icon_a():
    # A: machined knob — the hardware console's signature part.
    body = plate() + screws() + knob(512, 470, 205, 62) + f"""
  <text x="512" y="842" text-anchor="middle" font-family="Inter, NotoJP" font-weight="700" font-size="74"
        letter-spacing="10" fill="{C['t1']}">600</text>"""
    return svg(body)


def icon_b():
    # B: amber 7-segment "600" in a recessed display window.
    x0, y0, dw, dh = 214, 330, 596, 300
    digits = "".join(seg_digit(x0 + 64 + i * 170, y0 + 62, 112, 176, ch, C['amber'], "#2A2418", th=26)
                     for i, ch in enumerate("600"))
    body = plate() + screws() + f"""
  <rect x="{x0}" y="{y0}" width="{dw}" height="{dh}" rx="34" fill="#070809" stroke="#000" stroke-width="4"/>
  <rect x="{x0+4}" y="{y0+4}" width="{dw-8}" height="{dh-8}" rx="30" fill="none" stroke="#ffffff" stroke-opacity="0.05" stroke-width="3"/>
  <g filter="url(#glow)">{digits}</g>
  <text x="512" y="752" text-anchor="middle" font-family="Inter, NotoJP" font-weight="600" font-size="58"
        letter-spacing="18" fill="{C['t2']}">MODEL</text>"""
    return svg(body)


def icon_c():
    # C: knob + LED level ladder — "auto-mixing" read at a glance.
    leds = []
    n = 9
    for i in range(n):
        y = 720 - i * 58
        col = C['green'] if i < 5 else C['yellow'] if i < 7 else C['red']
        lit = i < 7
        fill = col if lit else C['off']
        glow = ' filter="url(#glow)"' if lit else ""
        leds.append(f'<rect x="676" y="{y}" width="120" height="40" rx="10" fill="{fill}"{glow}/>')
    body = plate() + screws() + knob(400, 512, 168, 30, ticks=False) + f"""
  <rect x="656" y="220" width="160" height="560" rx="26" fill="#070809" stroke="#000" stroke-width="4"/>
  {''.join(leds)}"""
    return svg(body)


def dmg_window():
    # DMG Finder window background (660 x 400 pt). Volume shows the plug-in
    # on the left and an alias to the OBS plugins folder on the right.
    w, h = 660, 400
    body = f"""
  <rect width="{w}" height="{h}" fill="{C['face']}"/>
  <rect width="{w}" height="{h}" fill="url(#brush)"/>
  <rect x="0" y="0" width="{w}" height="64" fill="{C['chassis']}"/>
  <text x="32" y="41" font-family="Inter, NotoJP" font-weight="700" font-size="20" letter-spacing="3" fill="{C['t1']}">MODEL 600</text>
  <text x="196" y="41" font-family="Inter, NotoJP" font-weight="500" font-size="14" letter-spacing="2" fill="{C['t3']}">// BROADCAST SUITE</text>
  <text x="{w-32}" y="41" text-anchor="end" font-family="Inter, NotoJP" font-weight="600" font-size="12" letter-spacing="3" fill="{C['amber']}">SEVENTHWELL</text>
  <!-- icon slots (Finder draws the real icons here) -->
  <rect x="120" y="128" width="128" height="128" rx="28" fill="none" stroke="{C['line2']}" stroke-dasharray="6 6"/>
  <text x="184" y="198" text-anchor="middle" font-family="Inter, NotoJP" font-size="12" fill="{C['t3']}">model600.plugin</text>
  <rect x="412" y="128" width="128" height="128" rx="28" fill="none" stroke="{C['line2']}" stroke-dasharray="6 6"/>
  <text x="476" y="198" text-anchor="middle" font-family="Inter, NotoJP" font-size="12" fill="{C['t3']}">plugins (alias)</text>
  <path d="M276,192 H372" stroke="{C['amber']}" stroke-width="4" stroke-linecap="round"/>
  <path d="M360,180 L376,192 L360,204" fill="none" stroke="{C['amber']}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>
  <text x="{w/2}" y="318" text-anchor="middle" font-family="Inter, NotoJP" font-weight="500" font-size="14" fill="{C['t2']}">model600.plugin を右のフォルダへドラッグ → OBS を再起動</text>
  <text x="{w/2}" y="342" text-anchor="middle" font-family="Inter, NotoJP" font-weight="500" font-size="12" fill="{C['t3']}">Drag model600.plugin onto the folder, then restart OBS Studio</text>"""
    return svg(body, w, h)


def main():
    os.makedirs(OUT, exist_ok=True)
    for name, fn in (("icon_a_knob", icon_a), ("icon_b_segment", icon_b), ("icon_c_ladder", icon_c),
                     ("dmg_window", dmg_window)):
        with open(os.path.join(OUT, f"{name}.svg"), "w") as f:
            f.write(fn())
        print("wrote", name)


if __name__ == "__main__":
    main()
