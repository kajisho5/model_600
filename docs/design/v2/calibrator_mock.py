#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 GUI v2 - "INPUT CALIBRATOR" module (10 s automatic measurement).
#
#  1. Writes tex/calib_layout.json + tex/calib_decal.(svg|png): the static print
#     of the calibrator plate (brand, title, step legends, the cream cue card).
#  2. With --mock, composes the four state samples over the Blender plate
#     (renders/calib_bg.png from render_hardware.py -- calib), using the real
#     part sprites from resources/hw/ and the same window drawing as the units:
#       renders/calib_1_measuring.png  calib_2_success.png
#       calib_3_clipping.png           calib_4_no_voice.png
#
#    python3 docs/design/v2/calibrator_mock.py            # layout + decal
#    python3 docs/design/v2/calibrator_mock.py --mock     # state samples
# =============================================================================
import base64
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import make_textures as mt  # noqa: E402  (shared drawing helpers)

TEX = os.path.join(HERE, "tex")
OUT = os.path.join(HERE, "renders")
HW = os.path.normpath(os.path.join(HERE, "..", "..", "..", "resources", "hw"))
W, H = 640, 450
CARD = "#EFE3C4"            # Cream cue card (same family as the old VU faces).
CARD_INK = "#2A241A"
CARD_DIM = "#6B5D45"

CAL = {
    "panel": {"w": W, "h": H, "ear": 0},
    "screws": [[18, 18], [622, 18], [18, 432], [622, 432]],
    "steps": {"y": 40, "xs": [458, 522, 586], "labels": ["PREPARE", "MEASURE", "RESULT"]},
    "card": {"x": 32, "y": 96, "w": 324, "h": 106},
    "meter": {"cx": 194, "cy": 284, "w": 324, "h": 128},
    "lamp": {"cx": 404, "cy": 118},
    "title": {"x": 428, "y": 124},
    "body": {"x": 384, "y": 146, "w": 224},
    "seg": {"cx": 496, "cy": 272, "w": 208, "h": 56, "label_y": 226},
    "ladder": {"cx": 496, "cy": 332, "n": 12},
    "hint": {"x": 32, "y": 392},
    "guide_knob": {"cx": 54, "cy": 388, "r": 20},
    "buttons": [{"id": "secondary", "cx": 504, "cy": 386, "w": 54, "h": 34, "color": "amber"},
                {"id": "primary", "cx": 576, "cy": 386, "w": 54, "h": 34, "color": "amber"}],
}


def decal_svg():
    o = [mt.text(32, 38, "SEVENTHWELL", 9, mt.AMBER, 700, "start", 2.6),
         mt.text(31, 64, "INPUT CALIBRATOR", 22, mt.INK, 800, "start", 1.2),
         mt.text(32, 80, "MODEL 600  ·  10 s AUTOMATIC LEVEL MATCH", 7.5, mt.INK_DIM, 600, "start", 1.6)]
    st = CAL["steps"]
    for i, (x, lab) in enumerate(zip(st["xs"], st["labels"])):
        o.append(mt.text(x, st["y"] + 24, f"{i + 1}  {lab}", 7.5, mt.INK, 700, "middle", 1.2))
    c = CAL["card"]
    # The cue card is a printed label: cream stock, thin rule, rounded corners.
    o.append(f'<rect x="{c["x"]}" y="{c["y"]}" width="{c["w"]}" height="{c["h"]}" rx="3" fill="{CARD}"/>')
    o.append(f'<rect x="{c["x"] + 4}" y="{c["y"] + 4}" width="{c["w"] - 8}" height="{c["h"] - 8}" rx="2" fill="none" '
             f'stroke="{CARD_DIM}" stroke-opacity="0.35" stroke-width="0.8"/>')
    return f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">' + "".join(o) + "</svg>"


def data_uri(path):
    with open(path, "rb") as f:
        return "data:image/png;base64," + base64.b64encode(f.read()).decode()


def sprite(name, cx, cy, w, h):
    return (f'<image href="{data_uri(os.path.join(HW, name + ".png"))}" x="{cx - w / 2:.1f}" y="{cy - h / 2:.1f}" '
            f'width="{w}" height="{h}"/>')


def knob_frame(cx, cy, r, angle):
    """One frame of the knob_mid filmstrip, scaled to radius r, via a clipped <svg>."""
    with open(os.path.join(HW, "hw.json")) as f:
        spec = json.load(f)["sprites"]["knob_mid"]
    angles = spec["angles"]
    i = min(range(len(angles)), key=lambda k: abs(angles[k] - angle))
    cols, size = spec["cols"], spec["size"]
    rows = (len(angles) + cols - 1) // cols
    side = size * r / spec["radius"]
    col, row = i % cols, i // cols
    return (f'<svg x="{cx - side / 2:.1f}" y="{cy - side / 2:.1f}" width="{side:.1f}" height="{side:.1f}" '
            f'viewBox="{col * size} {row * size} {size} {size}" overflow="hidden">'
            f'<image href="{data_uri(os.path.join(HW, "knob_mid.png"))}" width="{cols * size}" height="{rows * size}"/></svg>')


def wrap_text(x, y, lines, size, fill, weight=600, lh=1.45):
    return "".join(mt.text(x, y + i * size * lh, ln, size, fill, weight, "start", 0.2) for i, ln in enumerate(lines))


def window_png(svg, w, h, name):
    path = os.path.join(TEX, name + ".svg")
    with open(path, "w", encoding="utf-8") as f:
        f.write(svg)
    mt.render(path, os.path.join(TEX, name + ".png"), w, h, 4)
    return os.path.join(TEX, name + ".png")


STATES = {
    "1_measuring": {
        "steps": ["done", "now", "off"], "lamp": ("amber", True), "title": "測定中",
        "body": ["普段の配信と同じ声の大きさで、", "カードを読み続けてください。"],
        "card_head": "READ ALOUD  ／  読み上げ文",
        "card": ["「みなさんこんばんは、配信へようこそ。", "  今日も楽しい時間にしていきましょう。」"],
        "meter": ("PEAK", "-8.2", -8.2, -6.4, "dBFS", "VOICE  62 %"),
        "seg": ("SECONDS  /  10", "4.0"), "ladder": (5, "amber"),
        "hint": None, "guide": None,
        "buttons": [("中止", False), ("完了", None)],
    },
    "2_success": {
        "steps": ["done", "done", "now"], "lamp": ("green", True), "title": "測定が完了しました",
        "body": ["入力ゲインを自動で設定しました。", "このまま配信を始められます。"],
        "card_head": "RESULT  ／  結果",
        "card": ["基準ピーク  -7.9 dBFS  →  目標  -7.5 dBFS", "入力ゲインを +0.4 dB にしました。"],
        "meter": ("REF. PEAK", "-7.9", -7.9, -7.9, "dBFS", "TARGET -7.5"),
        "seg": ("INPUT GAIN  dB", "0.4"), "ladder": (12, "green"),
        "hint": ["オーディオインターフェースの GAIN つまみは", "1時〜2時の位置が目安です。"], "guide": 50,
        "buttons": [("再測定", False), ("完了", True)],
    },
    "3_clipping": {
        "steps": ["done", "done", "now"], "lamp": ("red", True), "title": "入力が歪んでいます",
        "body": ["インターフェースの GAIN を下げてから、", "もう一度測定してください。"],
        "card_head": "RESULT  ／  結果",
        "card": ["10.0 秒間、入力が 0 dBFS に張り付きました。", "入力ゲインは変更していません。"],
        "meter": ("CLIPPED", "10.0", 0.0, 0.0, "s", "INPUT GAIN 0.0 dB"),
        "seg": ("INPUT GAIN  dB", "0.0"), "ladder": (12, "red"),
        "hint": ["つまみを左へ回して、", "クリップランプが消える位置にします。"], "guide": 100,
        "buttons": [("閉じる", False), ("再測定", True)],
    },
    "4_no_voice": {
        "steps": ["done", "done", "now"], "lamp": ("amber", True), "title": "声を検出できませんでした",
        "body": ["マイクのミュートと、OBS の", "入力デバイスを確認してください。"],
        "card_head": "RESULT  ／  結果",
        "card": ["声として検出できたのは 0 %（20 % 以上が必要）。", "入力ゲインは変更していません。"],
        "meter": ("VOICE", "0", -58.0, -55.0, "%", "REQUIRED 20 %"),
        "seg": ("INPUT GAIN  dB", "0.0"), "ladder": (12, "amber"),
        "hint": ["マイクの物理ミュートスイッチも", "確認してください。"], "guide": None,
        "buttons": [("閉じる", False), ("再測定", True)],
    },
}


def mock(state, s, bg):
    o = [f'<image href="{data_uri(bg)}" x="0" y="0" width="{W}" height="{H}"/>']
    # Step LEDs.
    st = CAL["steps"]
    for x, sname in zip(st["xs"], s["steps"]):
        led = {"done": "led_on", "now": "led_idle", "off": "led_off"}[sname]
        o.append(sprite(led, x, st["y"], 16, 16))
    # Status lamp + title + body (printed by the app in the UI language).
    lc, lit = s["lamp"]
    o.append(sprite(f"lamp_{lc}_{'on' if lit else 'off'}", CAL["lamp"]["cx"], CAL["lamp"]["cy"], 28, 28))
    o.append(mt.text(CAL["title"]["x"], CAL["title"]["y"], s["title"], 14, mt.INK, 700, "start", 0.4))
    o.append(wrap_text(CAL["body"]["x"], CAL["body"]["y"] + 10, s["body"], 9.5, mt.INK_DIM))
    # Cue card text.
    c = CAL["card"]
    o.append(mt.text(c["x"] + 14, c["y"] + 22, s["card_head"], 8, CARD_DIM, 700, "start", 1.4))
    o.append(wrap_text(c["x"] + 14, c["y"] + 50, s["card"], 12.5 if state == "1_measuring" else 10.5, CARD_INK, 700, 1.6))
    # Meter window: same digital meter as the units (dBFS, OBS zones, hold).
    m = CAL["meter"]
    label, value, bar, hold, unit, aux = s["meter"]
    v = mt.digi(m["cx"], m["cy"], m["w"], m["h"], "", "dbfs", value, bar, label, unit, aux=aux, hold=hold)
    win = window_png(mt.meter_svg(v), v["digi"]["win_w"], v["digi"]["win_h"], f"calib_meter_{state}")
    o.append(f'<image href="{data_uri(win)}" x="{m["cx"] - v["digi"]["win_w"] / 2}" y="{m["cy"] - v["digi"]["win_h"] / 2}" '
             f'width="{v["digi"]["win_w"]}" height="{v["digi"]["win_h"]}"/>')
    # 7-segment window + its label.
    g = CAL["seg"]
    o.append(mt.text(g["cx"], g["label_y"], s["seg"][0], 8.5, mt.INK_DIM, 600, "middle", 2.0))
    shown = s["seg"][1]
    while len(mt.split_chars(shown)) < 4:   # Fixed 4-cell field, right-aligned, like the app.
        shown = " " + shown
    seg = window_png(mt.seg_svg({"w": g["w"], "h": g["h"], "text": shown}), g["w"], g["h"], f"calib_seg_{state}")
    o.append(f'<image href="{data_uri(seg)}" x="{g["cx"] - g["w"] / 2}" y="{g["cy"] - g["h"] / 2}" width="{g["w"]}" height="{g["h"]}"/>')
    # Progress ladder (12 LEDs in the bezel from the plate).
    L = CAL["ladder"]
    lit_n, color = s["ladder"]
    on, off = {"green": ("#57E08A", "#0E1F14"), "amber": ("#FFB347", "#23170A"), "red": ("#FF4A3A", "#260C0A")}[color]
    x0 = L["cx"] - L["n"] * 8 + 8
    for i in range(L["n"]):
        o.append(f'<rect x="{x0 + i * 16 - 5.5:.1f}" y="{L["cy"] - 8}" width="11" height="16" rx="1.5" '
                 f'fill="{on if i < lit_n else off}"/>')
    # Hint line; on results the interface-gain guide knob sits before it.
    hx = CAL["hint"]["x"]
    if s["guide"] is not None:
        k = CAL["guide_knob"]
        o.append(knob_frame(k["cx"], k["cy"], k["r"], -135 + 270 * s["guide"] / 100.0))
        hx += 52
    lines = s["hint"] or ["Esc で中止できます。"]
    o.append(wrap_text(hx, CAL["hint"]["y"] - 6, lines, 9.5, mt.INK_DIM))
    # Illuminated push buttons (primary lit), labels printed by the app.
    for b, lab in zip(CAL["buttons"], s["buttons"]):
        text_, primary = lab
        # primary: True = lit (default action), False = dark, None = not yet available (dark, dim label).
        o.append(sprite(f"cap_amber_{'on' if primary else 'off'}_54x34", b["cx"], b["cy"], 64, 44))
        o.append(mt.text(b["cx"], b["cy"] + 17 + 22, text_, 9, mt.INK_DIM if primary is None else mt.INK, 700, "middle", 1.0))
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">' + "".join(o) + "</svg>"
    path = os.path.join(TEX, f"calib_{state}.svg")
    with open(path, "w", encoding="utf-8") as f:
        f.write(svg)
    mt.render(path, os.path.join(OUT, f"calib_{state}.png"), W, H, 2)
    os.remove(path)
    print("wrote", os.path.join(OUT, f"calib_{state}.png"))


def main():
    os.makedirs(TEX, exist_ok=True)
    with open(os.path.join(TEX, "calib_layout.json"), "w") as f:
        json.dump(CAL, f, indent=2)
    svg_path = os.path.join(TEX, "calib_decal.svg")
    with open(svg_path, "w", encoding="utf-8") as f:
        f.write(decal_svg())
    mt.render(svg_path, os.path.join(TEX, "calib_decal.png"), W, H, 3)
    print("calibrator layout + decal written")
    if "--mock" in sys.argv:
        bg = os.path.join(OUT, "calib_bg.png")
        if not os.path.exists(bg):
            sys.exit("render the plate first: blender -b --factory-startup -P docs/design/v2/render_hardware.py -- calib")
        for state, s in STATES.items():
            mock(state, s, bg)


if __name__ == "__main__":
    main()
