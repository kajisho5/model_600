#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  DMG window + installer design (Step 2).
#
#  Output (docs/design/dmg/svg/, rendered to preview/ by render.sh):
#    bg_<plan>.svg                 the background image that ships (660 x 400 pt)
#    window_<plan>_<appearance>.svg  preview: background + Finder items + labels
#    installer_icon.svg            icon of "MODEL 600 Installer.app" (plan 1)
#    alerts.svg                    plan 1 installer dialogs (ja / en)
#  Plans: plan1 = installer app + manual install, plan2 = drag with instructions.
#
#  Finder draws the item names itself in the system label colour (black in
#  Light Mode, white in Dark Mode), over our background. Every label therefore
#  sits on BAND, a grey picked so that both black and white text reach 4.5:1.
# =============================================================================
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ICON_DIR = os.path.normpath(os.path.join(HERE, "..", "icon"))
sys.path.insert(0, ICON_DIR)
sys.dont_write_bytecode = True
import make_icons as mi  # noqa: E402  (concept A artwork, squircle, arc_path)

OUT = os.path.join(HERE, "svg")
FONTS = mi.FONTS
W, H = 660, 400                     # Finder window content size (pt).
HEAD_H, BAND_B = 64, 350            # Header bottom, label band bottom.
ICON = 96                           # dmgbuild icon_size (pt).

HEAD = "#0E1012"
BAND = "#74777B"
FOOT = "#14171B"
AMBER = mi.AMBER
FOOT_TXT, FOOT_TXT2 = "#AEB4BB", "#8A9199"

LATIN = "Inter"
JP = "'Noto Sans CJK JP'"
MONO = "'DejaVu Sans Mono', monospace"

# Item centres (Finder icon_locations) per plan.
ITEMS = {
    "plan1": {"MODEL 600 Installer": ("installer", 170, 168),
              "model600.plugin": ("plugin", 436, 168),
              "README.txt": ("readme", 572, 168)},
    "plan2": {"model600.plugin": ("plugin", 150, 150),
              "README.txt": ("readme", 150, 266)},
}


# --------------------------------------------------------------------------- colour
def lum(hex_):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (int(hex_[i:i + 2], 16) for i in (1, 3, 5))
    return 0.2126 * ch(r) + 0.7152 * ch(g) + 0.0722 * ch(b)


def contrast(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


# --------------------------------------------------------------------------- icons
def concept_a_installer():
    """Installer variant of concept A: same dial on the aluminium (light) ground,
    so it reads as 'MODEL 600' but is not mistaken for the plug-in itself."""
    a = mi.concept_a("default")
    bg = mi.grad_bg("#EEF0F2", "#C7CCD2")
    track = a["fg1_track"].replace('stroke="#FFFFFF" stroke-opacity="0.16"', 'stroke="#14171B" stroke-opacity="0.14"')
    disc = a["fg3_disc"].replace(f'fill="{mi.ALU}"', 'fill="#2B3038"')
    return bg + track + a["fg2_level"] + disc + a["fg4_pointer"]


def shaped_inner(inner, uid, margin=100):
    body = mi.S - 2 * margin
    r = 0.2247 * body
    scale = body / mi.S
    inner = inner.replace('id="bg"', f'id="{uid}bg"').replace("url(#bg)", f"url(#{uid}bg)")
    return (f'<defs><clipPath id="{uid}"><path d="{mi.squircle(margin, margin, body, body, r)}"/></clipPath></defs>'
            f'<g clip-path="url(#{uid})"><g transform="translate({margin},{margin}) scale({scale:.5f})">{inner}</g></g>')


def place(inner_1024, cx, cy, size):
    return (f'<svg x="{cx - size / 2}" y="{cy - size / 2}" width="{size}" height="{size}" '
            f'viewBox="0 0 1024 1024">{inner_1024}</svg>')


def readme_icon():
    # Approximation of the system plain-text document icon (Finder draws the real one).
    return ('<path d="M232,96 H664 L824,256 V928 H232 Z" fill="#FFFFFF" stroke="#C9CDD2" stroke-width="16"/>'
            '<path d="M664,96 V256 H824" fill="#E6E8EB" stroke="#C9CDD2" stroke-width="16" stroke-linejoin="round"/>'
            + "".join(f'<rect x="320" y="{y}" width="{w}" height="24" rx="12" fill="#B9BEC4"/>'
                      for y, w in ((380, 420), (460, 420), (540, 360), (620, 420), (700, 300))))


def item_icon(kind, uid):
    if kind == "installer":
        return shaped_inner(concept_a_installer(), uid)
    if kind == "plugin":
        return shaped_inner("".join(mi.concept_a("default").values()), uid)
    return readme_icon()


# --------------------------------------------------------------------------- text
def t(x, y, s, size, fill, weight=400, anchor="start", family=None, ls=0):
    fam = family or (f"{LATIN}, {JP}")
    return (f'<text x="{x}" y="{y}" text-anchor="{anchor}" font-family="{fam}" font-weight="{weight}" '
            f'font-size="{size}" letter-spacing="{ls}" fill="{fill}">{s}</text>')


def css():
    return (f"<style>"
            f"@font-face {{ font-family: Inter; font-weight: 700; src: url('file://{FONTS}/Inter-Bold.otf'); }}"
            f"@font-face {{ font-family: Inter; font-weight: 600; src: url('file://{FONTS}/Inter-SemiBold.otf'); }}"
            f"@font-face {{ font-family: Inter; font-weight: 500; src: url('file://{FONTS}/Inter-Medium.otf'); }}"
            f"@font-face {{ font-family: Inter; font-weight: 400; src: url('file://{FONTS}/Inter-Regular.otf'); }}"
            f"</style>")


def frame():
    return (f'<rect width="{W}" height="{H}" fill="{FOOT}"/>'
            f'<rect width="{W}" height="{HEAD_H}" fill="{HEAD}"/>'
            f'<rect y="{HEAD_H}" width="{W}" height="{BAND_B - HEAD_H}" fill="{BAND}"/>'
            + t(32, 41, "MODEL 600", 20, "#E7E9EC", 700, ls=3)
            + t(196, 41, "// BROADCAST SUITE", 14, "#8A9199", 500, ls=2)
            + t(W - 32, 41, "SEVENTHWELL", 12, AMBER, 600, "end", ls=3)
            + t(W / 2, 372, "macOS 12 以降（Apple シリコン / Intel）・ OBS Studio 31 以降", 11.5, FOOT_TXT, 500, "middle")
            + t(W / 2, 390, "macOS 12 or later (Apple silicon / Intel) · OBS Studio 31 or later", 11, FOOT_TXT2, 400, "middle"))


# --------------------------------------------------------------------------- backgrounds
def bg_plan1():
    s = frame()
    # Primary: double-click the installer.
    s += f'<rect x="150" y="268" width="40" height="3" rx="1.5" fill="{AMBER}"/>'
    s += t(170, 296, "ダブルクリックしてインストール", 15, "#FFFFFF", 600, "middle")
    s += t(170, 318, "Double-click to install", 12.5, "#FFFFFF", 500, "middle")
    # Divider + secondary: manual install.
    s += '<rect x="330" y="92" width="1" height="236" fill="#FFFFFF" fill-opacity="0.35"/>'
    s += t(504, 100, "手動でインストールする場合 / Manual install", 12, "#FFFFFF", 600, "middle")
    s += t(504, 290, "README の手順で model600.plugin を", 12, "#FFFFFF", 500, "middle")
    s += t(504, 308, "OBS のプラグインフォルダへコピー", 12, "#FFFFFF", 500, "middle")
    s += t(504, 328, "Follow README to copy the plug-in yourself.", 11.5, "#FFFFFF", 400, "middle")
    return s


def bg_plan2():
    s = frame()
    # Arrow from the plug-in to the instruction card (the card is a picture, not a drop target).
    s += f'<path d="M222,150 H286" stroke="{AMBER}" stroke-width="4" stroke-linecap="round"/>'
    s += f'<path d="M274,138 L290,150 L274,162" fill="none" stroke="{AMBER}" stroke-width="4" stroke-linecap="round" stroke-linejoin="round"/>'
    s += f'<rect x="306" y="84" width="330" height="250" rx="12" fill="{HEAD}"/>'
    rows = [
        ("1", "Finder で ⇧⌘G（フォルダへ移動）を押す", "In Finder press ⇧⌘G (Go to Folder)"),
        ("2", "次のパスを入力して開く", "Enter this path and open it"),
        ("3", "開いたフォルダへ plugin をドラッグ", "Drag the plug-in into that folder"),
        ("4", "OBS Studio を再起動", "Restart OBS Studio"),
    ]
    y = 114
    for n, ja, en in rows:
        s += f'<circle cx="332" cy="{y - 4}" r="10" fill="{AMBER}"/>' + t(332, y, n, 12, HEAD, 700, "middle")
        s += t(350, y, ja, 12, "#E7E9EC", 500) + t(350, y + 16, en, 10.5, "#AEB4BB", 400)
        if n == "2":
            s += f'<rect x="350" y="{y + 26}" width="270" height="40" rx="6" fill="#262B31"/>'
            s += t(360, y + 42, "~/Library/Application Support/", 11, AMBER, 400, family=MONO)
            s += t(360, y + 58, "obs-studio/plugins", 11, AMBER, 400, family=MONO)
            y += 90
        else:
            y += 44
    return s


# --------------------------------------------------------------------------- previews
def window_preview(plan, appearance):
    bg = bg_plan1() if plan == "plan1" else bg_plan2()
    label = "#000000" if appearance == "light" else "#FFFFFF"
    s = bg
    for i, (name, (kind, cx, cy)) in enumerate(ITEMS[plan].items()):
        s += place(item_icon(kind, f"m{i}"), cx, cy, ICON)
        s += t(cx, cy + ICON / 2 + 18, name, 13, label, 400, "middle", family=f"{LATIN}, {JP}")
    return mi.doc(s, W, H, css())


# --------------------------------------------------------------------------- alerts
ALERTS = [
    ("confirm",
     ("MODEL 600 をインストールしますか？",
      ["このユーザーの OBS Studio の", "プラグインフォルダにインストールします。", "以前のバージョンは置き換えられます。"],
      "キャンセル", "インストール"),
     ("Install MODEL 600?",
      ["MODEL 600 will be installed in this user's", "OBS Studio plug-ins folder. An earlier", "version is replaced."],
      "Cancel", "Install")),
    ("obs_running",
     ("OBS Studio を終了してください",
      ["OBS Studio が起動しているため", "インストールできません。終了してから", "［再試行］を押してください。"],
      "キャンセル", "再試行"),
     ("Quit OBS Studio to continue",
      ["MODEL 600 can't be installed while", "OBS Studio is open. Quit OBS Studio,", "then click Try Again."],
      "Cancel", "Try Again")),
    ("done",
     ("インストールしました",
      ["OBS Studio を起動し、音声ソースの", "「フィルタ」から MODEL 600 を", "追加してください。"],
      "完了", "OBS を開く"),
     ("MODEL 600 is installed",
      ["Open OBS Studio and add MODEL 600", "from an audio source's Filters."],
      "Done", "Open OBS")),
]


def alert(x, y, title, lines, cancel, default, uid):
    w, h = 260, 236
    s = f'<g transform="translate({x},{y})">'
    s += f'<rect width="{w}" height="{h}" rx="18" fill="#F4F4F6" stroke="#000000" stroke-opacity="0.12"/>'
    s += place(shaped_inner(concept_a_installer(), uid), w / 2, 46, 64)
    s += t(w / 2, 102, title, 13, "#1D1D1F", 700, "middle")
    for i, ln in enumerate(lines):
        s += t(w / 2, 124 + i * 16, ln, 11, "#3C3C43", 400, "middle")
    bw, by = (w - 16 * 2 - 8) / 2, h - 16 - 28
    s += f'<rect x="16" y="{by}" width="{bw}" height="28" rx="14" fill="#E3E3E8"/>'
    s += t(16 + bw / 2, by + 18.5, cancel, 12, "#1D1D1F", 500, "middle")
    s += f'<rect x="{16 + bw + 8}" y="{by}" width="{bw}" height="28" rx="14" fill="#0A7AFF"/>'
    s += t(16 + bw + 8 + bw / 2, by + 18.5, default, 12, "#FFFFFF", 600, "middle")
    return s + "</g>"


def alerts_sheet():
    cw, ch, pad = 260, 236, 28
    w = 3 * cw + 4 * pad
    h = 2 * ch + 3 * pad + 24
    s = f'<rect width="{w}" height="{h}" fill="#C9CED4"/>'
    for col, (key, ja, en) in enumerate(ALERTS):
        x = pad + col * (cw + pad)
        s += t(x, 20, key, 11, "#3C3C43", 600, family=MONO)
        s += alert(x, 28, *ja, f"a{col}j")
        s += alert(x, 28 + ch + pad, *en, f"a{col}e")
    return mi.doc(s, w, h, css())


# --------------------------------------------------------------------------- main
def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    for plan, fn in (("plan1", bg_plan1), ("plan2", bg_plan2)):
        with open(os.path.join(OUT, f"bg_{plan}.svg"), "w") as f:
            f.write(mi.doc(fn(), W, H, css()))
        for app in ("light", "dark"):
            with open(os.path.join(OUT, f"window_{plan}_{app}.svg"), "w") as f:
                f.write(window_preview(plan, app))
    with open(os.path.join(OUT, "installer_icon.svg"), "w") as f:
        f.write(mi.doc(shaped_inner(concept_a_installer(), "m")))
    with open(os.path.join(OUT, "alerts.svg"), "w") as f:
        f.write(alerts_sheet())
    for name, c in (("black label", contrast(BAND, "#000000")), ("white label", contrast(BAND, "#FFFFFF")),
                    ("footer ja", contrast(FOOT, FOOT_TXT)), ("footer en", contrast(FOOT, FOOT_TXT2))):
        print(f"contrast {name}: {c:.2f}:1")


if __name__ == "__main__":
    main()
