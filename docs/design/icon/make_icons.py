#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  icon proposals, revision 2 (Apple HIG).
#
#  Follows https://developer.apple.com/design/human-interface-guidelines/app-icons
#  (macOS): 1024x1024 SQUARE, UNMASKED, full-bleed layers (the system applies the
#  rounded corners and Liquid Glass); a solid/gradient background layer plus a
#  few filled foreground layers; no drop shadows, glows, bevels, highlights,
#  blurs or textures; no thin lines; no text unless it is the brand; no copy of
#  UI components.
#
#  Output (docs/design/icon/):
#    layers/<concept>/{background,fg1,fg2,...}.svg   Icon Composer inputs
#    svg/<concept>.svg                               flattened full-bleed square
#    svg/<concept>_<appearance>_masked.svg           preview with the system mask
#    svg/dmg_window.svg                              DMG window mock
#  Appearances previewed: default, dark, tinted (gray). The dark / tinted ones
#  here are approximations; Icon Composer produces the real variants.
# =============================================================================
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "svg")
LAYERS = os.path.join(HERE, "layers")
FONTS = os.path.normpath(os.path.join(HERE, "..", "..", "..", "resources", "fonts"))

S = 1024                           # Canvas = layer size (full bleed).
MASK_R = 0.2237 * S                # Preview only: approximate system corner radius.

AMBER = "#F0A33A"
AMBER_D = "#C9771A"
GRAPH_T, GRAPH_B = "#2B3038", "#14171B"      # Default background gradient.
DARK_T, DARK_B = "#1A1D21", "#0B0C0E"        # Dark appearance background.
ALU = "#D3D7DC"
GREEN = "#46C17B"


def grad_bg(top, bottom):
    return f"""<defs><linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
  <stop offset="0" stop-color="{top}"/><stop offset="1" stop-color="{bottom}"/></linearGradient></defs>
<rect width="{S}" height="{S}" fill="url(#bg)"/>"""


def arc_path(cx, cy, r, a0, a1):
    """Arc from angle a0 to a1 (degrees, 0 = up, clockwise)."""
    def pt(a):
        t = math.radians(a - 90)
        return cx + r * math.cos(t), cy + r * math.sin(t)
    x0, y0 = pt(a0)
    x1, y1 = pt(a1)
    large = 1 if (a1 - a0) % 360 > 180 else 0
    return f"M{x0:.1f},{y0:.1f} A{r},{r} 0 {large} 1 {x1:.1f},{y1:.1f}"


# ---------------------------------------------------------------------------
# Concept A — "Dial": a level arc around a solid control disc with an amber
# pointer. One idea (turn it up), three filled shapes, no text.
# ---------------------------------------------------------------------------
def concept_a(app):
    bg = grad_bg(*(GRAPH_T, GRAPH_B) if app != "dark" else (DARK_T, DARK_B))
    cx, cy = 512, 520
    track = f'<path d="{arc_path(cx, cy, 318, -135, 135)}" fill="none" stroke="#FFFFFF" stroke-opacity="0.16" stroke-width="76" stroke-linecap="round"/>'
    level = f'<path d="{arc_path(cx, cy, 318, -135, 52)}" fill="none" stroke="{AMBER}" stroke-width="76" stroke-linecap="round"/>'
    disc = f'<circle cx="{cx}" cy="{cy}" r="196" fill="{ALU if app != "dark" else "#9EA4AB"}"/>'
    t = math.radians(52 - 90)
    px, py = cx + 118 * math.cos(t), cy + 118 * math.sin(t)
    qx, qy = cx + 36 * math.cos(t), cy + 36 * math.sin(t)
    pointer = f'<line x1="{qx:.1f}" y1="{qy:.1f}" x2="{px:.1f}" y2="{py:.1f}" stroke="{AMBER}" stroke-width="60" stroke-linecap="round"/>'
    return {"background": bg, "fg1_track": track, "fg2_level": level, "fg3_disc": disc, "fg4_pointer": pointer}


# ---------------------------------------------------------------------------
# Concept B — "600": the model number as the brand mark, drawn from thick
# geometric shapes (text is part of the product name, the one case HIG allows).
# ---------------------------------------------------------------------------
def concept_b(app):
    if app == "dark":
        bg = grad_bg("#3A2A12", "#1E160A")
        ink, ink2 = AMBER, "#B9781F"
    else:
        bg = grad_bg("#F6B24E", AMBER_D)
        ink, ink2 = "#16191C", "#16191C"
    w = 64                                     # Stroke weight of the numerals.
    # Drawn around a local origin (x -152..674, y -302..152), scaled 0.82 and centred.
    g = 'transform="translate(298,573) scale(0.82)"'
    # "6": round bowl + one stroke rising to the upper right.
    six = (f'<g {g}><circle cx="0" cy="0" r="120" fill="none" stroke="{ink}" stroke-width="{w}"/>'
           f'<path d="M-120,0 C-120,-150 -40,-250 70,-270" fill="none" stroke="{ink}" stroke-width="{w}" stroke-linecap="round"/></g>')
    # "0" "0": stadiums on the same baseline; the second is translucent for depth.
    zero1 = f'<g {g}><rect x="214" y="-270" width="172" height="390" rx="86" fill="none" stroke="{ink}" stroke-width="{w}"/></g>'
    zero2 = f'<g {g}><rect x="470" y="-270" width="172" height="390" rx="86" fill="none" stroke="{ink2}" stroke-opacity="0.55" stroke-width="{w}"/></g>'
    return {"background": bg, "fg1_six": six, "fg2_zero": zero1, "fg3_zero_translucent": zero2}


# ---------------------------------------------------------------------------
# Concept C — "Mix": three rounded level bars of different heights in front
# of a translucent disc — several sources balanced into one mix.
# ---------------------------------------------------------------------------
def concept_c(app):
    bg = grad_bg(*(GRAPH_T, GRAPH_B) if app != "dark" else (DARK_T, DARK_B))
    disc = '<circle cx="512" cy="512" r="330" fill="#FFFFFF" fill-opacity="0.10"/>'
    bars = []
    for x, h, col in ((334, 300, GREEN), (472, 420, GREEN), (610, 520, AMBER)):
        bars.append(f'<rect x="{x}" y="{742 - h}" width="96" height="{h}" rx="48" fill="{col}"/>')
    return {"background": bg, "fg1_disc": disc, "fg2_bars": "".join(bars)}


CONCEPTS = {"icon_a_dial": concept_a, "icon_b_600": concept_b, "icon_c_mix": concept_c}


def doc(inner, w=S, h=S, extra=""):
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">{extra}{inner}</svg>\n'


TINT = """<defs><filter id="gray"><feColorMatrix type="saturate" values="0"/></filter></defs>"""


def masked(inner, app):
    # Preview only: the system mask (rounded square). Tinted = grayscale approximation.
    body = f'<g filter="url(#gray)">{inner}</g>' if app == "tinted" else inner
    return doc(f'<defs><clipPath id="m"><rect width="{S}" height="{S}" rx="{MASK_R:.0f}"/></clipPath></defs>'
               f'<g clip-path="url(#m)">{body}</g>', extra=TINT if app == "tinted" else "")


def dmg_window():
    w, h = 660, 400
    css = f"""<style>
@font-face {{ font-family: Inter; font-weight: 700; src: url('file://{FONTS}/Inter-Bold.otf'); }}
@font-face {{ font-family: Inter; font-weight: 600; src: url('file://{FONTS}/Inter-SemiBold.otf'); }}
@font-face {{ font-family: Inter; font-weight: 500; src: url('file://{FONTS}/Inter-Medium.otf'); }}
@font-face {{ font-family: NotoJP; src: url('file://{FONTS}/NotoSansJP-Subset-Regular.otf'); }}
</style>"""
    body = f"""
  <rect width="{w}" height="{h}" fill="#191C20"/>
  <rect width="{w}" height="64" fill="#0E1012"/>
  <text x="32" y="41" font-family="Inter, NotoJP" font-weight="700" font-size="20" letter-spacing="3" fill="#E7E9EC">MODEL 600</text>
  <text x="196" y="41" font-family="Inter, NotoJP" font-weight="500" font-size="14" letter-spacing="2" fill="#646B73">// BROADCAST SUITE</text>
  <text x="{w-32}" y="41" text-anchor="end" font-family="Inter, NotoJP" font-weight="600" font-size="12" letter-spacing="3" fill="{AMBER}">SEVENTHWELL</text>
  <rect x="120" y="128" width="128" height="128" rx="28" fill="none" stroke="#3A4047" stroke-dasharray="6 6"/>
  <text x="184" y="198" text-anchor="middle" font-family="Inter, NotoJP" font-size="12" fill="#646B73">model600.plugin</text>
  <rect x="412" y="128" width="128" height="128" rx="28" fill="none" stroke="#3A4047" stroke-dasharray="6 6"/>
  <text x="476" y="198" text-anchor="middle" font-family="Inter, NotoJP" font-size="12" fill="#646B73">plugins (alias)</text>
  <path d="M276,192 H372" stroke="{AMBER}" stroke-width="4" stroke-linecap="round"/>
  <path d="M360,180 L376,192 L360,204" fill="none" stroke="{AMBER}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>
  <text x="{w/2}" y="318" text-anchor="middle" font-family="Inter, NotoJP" font-weight="500" font-size="14" fill="#9AA1A9">model600.plugin を右のフォルダへドラッグ → OBS を再起動</text>
  <text x="{w/2}" y="342" text-anchor="middle" font-family="Inter, NotoJP" font-weight="500" font-size="12" fill="#646B73">Drag model600.plugin onto the folder, then restart OBS Studio</text>"""
    return doc(body, w, h, css)


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    for name, fn in CONCEPTS.items():
        layers = fn("default")
        ldir = os.path.join(LAYERS, name)
        os.makedirs(ldir, exist_ok=True)
        for lname, content in layers.items():
            with open(os.path.join(ldir, f"{lname}.svg"), "w") as f:
                f.write(doc(content))
        flat = "".join(layers.values())
        with open(os.path.join(OUT, f"{name}.svg"), "w") as f:
            f.write(doc(flat))
        for app in ("default", "dark", "tinted"):
            src = "".join((fn("dark") if app == "dark" else layers).values())
            with open(os.path.join(OUT, f"{name}_{app}_masked.svg"), "w") as f:
                f.write(masked(src, app))
        print("wrote", name)
    with open(os.path.join(OUT, "dmg_window.svg"), "w") as f:
        f.write(dmg_window())
    print("wrote dmg_window")


if __name__ == "__main__":
    main()
