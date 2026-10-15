#!/usr/bin/env python3
# =============================================================================
#  MODEL 600  -  Japanese UI font subset builder.
#
#  Extracts Noto Sans CJK JP (Regular / Bold) from the system TTC and keeps only
#  what the plugin UI can display: ASCII, Latin-1, general / CJK punctuation,
#  kana, full-width forms, JIS X 0208 level-1 kanji, plus every character that
#  actually appears in src/. Output goes to resources/fonts/ (OFL 1.1).
#
#    python3 tools/fonts/make_font_subset.py [--src-ttc-dir /usr/share/fonts/opentype/noto]
# =============================================================================
import argparse
import os
import sys

from fontTools.subset import Options, Subsetter
from fontTools.ttLib import TTCollection

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def jis_level1_kanji():
    chars = set()
    for hi in range(0xB0, 0xD0):          # JIS X 0208 rows 16-47 (level 1)
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(bytes([hi, lo]).decode("euc_jp"))
            except UnicodeDecodeError:
                pass
    return chars


def source_chars():
    chars = set()
    # UI-facing sources only (the voice dictionary's KO/ZH words never render).
    ui_files = ["custom-ui-dialog.cpp", "custom-ui-dialog.hpp", "audio-filter.cpp", "plugin-main.cpp"]
    for path in (os.path.join(ROOT, "src", n) for n in ui_files):
        with open(path, encoding="utf-8", errors="ignore") as f:
            chars.update(f.read())
    return chars


def wanted_codepoints():
    cps = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
    cps |= set(range(0x2000, 0x2070))      # General punctuation
    cps |= set(range(0x2190, 0x2200))      # Arrows (used by a few labels)
    cps |= set(range(0x2212, 0x2213))      # Minus sign
    cps |= set(range(0x25A0, 0x2600))      # Geometric shapes (status dots)
    cps |= set(range(0x3000, 0x3040))      # CJK symbols & punctuation
    cps |= set(range(0x3040, 0x3100))      # Hiragana + Katakana
    cps |= set(range(0xFF00, 0xFFF0))      # Full-width forms
    cps |= {ord(c) for c in jis_level1_kanji()}
    cps |= {ord(c) for c in source_chars() if ord(c) >= 0x80}
    return cps


def jp_face(ttc_path):
    coll = TTCollection(ttc_path)
    for font in coll.fonts:
        names = {r.toUnicode() for r in font["name"].names if r.nameID == 1}
        if any("JP" in n for n in names):
            return font
    raise SystemExit(f"No JP face in {ttc_path}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src-ttc-dir", default="/usr/share/fonts/opentype/noto")
    ap.add_argument("--out", default=os.path.join(ROOT, "resources", "fonts"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    cps = wanted_codepoints()
    for weight in ("Regular", "Bold"):
        src = os.path.join(args.src_ttc_dir, f"NotoSansCJK-{weight}.ttc")
        font = jp_face(src)
        opts = Options()
        opts.layout_features = ["kern", "palt", "halt", "liga", "locl", "ccmp", "vert", "fwid", "hwid"]
        opts.name_IDs = ["*"]
        opts.name_languages = ["*"]
        opts.notdef_outline = True
        opts.hinting = False
        sub = Subsetter(opts)
        sub.populate(unicodes=cps)
        sub.subset(font)
        out = os.path.join(args.out, f"NotoSansJP-Subset-{weight}.otf")
        font.save(out)
        print(f"{out}: {os.path.getsize(out) / 1024:.0f} KB, {len(font.getGlyphOrder())} glyphs")
    return 0


if __name__ == "__main__":
    sys.exit(main())
