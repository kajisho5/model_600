# GUI サンプル（全画面）

承認済みの GUI v2「実機ハードウェア」デザイン（`docs/design/v2/DESIGN_V2.md`）を実装した Qt コードを、そのままオフスクリーン描画した画像です。

- **数値**：メーターの数値は、各ユニットの DSP エンジンに実際に音声を流して得た値です。
- **ピーク**：dBFS で表示し、色の境目は OBS と同じ -20 と -9 です。
- **パネル・部品**：パネル背景、つまみのコマ、トグル、ボタン、ランプは Blender で書き出した画像（`resources/hw/`）です。
- **表示部**：7セグ表示、LED バー、デジタルメーターはアプリが描いています。

再生成コマンド：

```bash
cmake -S . -B build -DMODEL600_BUILD_GUI_SAMPLES=ON
cmake --build build --target m600_render_gui_samples
QT_QPA_PLATFORM=offscreen QT_SCALE_FACTOR=2 ./build/m600_render_gui_samples docs/gui-samples
```

| # | 画面 | 内容 |
|---|---|---|
| 01 | ![](01_console_unit01_ja.png) | [01] VOCAL MASTER スタジオコンソール（日本語） |
| 02 | ![](02_console_unit01_en.png) | 同・英語表示 |
| 03 | ![](03_console_unit01_states.png) | メーター切替を GR にした表示、BGM 挿し間違いの警告 |
| 04 | ![](04_console_unit02_collab.png) | [02] COLLAB RIDER：相手の声のピークと自動補正量、信号の流れ図、RIDING／YOU TALK ランプ |
| 05 | ![](05_console_unit03_game.png) | [03] GAME IMPACT：ゲーム音のピーク、LIMITING LED バー、LIMIT／SCOOPING ランプ |
| 06 | ![](06_console_unit04_bgm.png) | [04] BGM POCKET：BGM のピーク、POCKET LED バー、歌枠セーフ／離席時スウェルのトグル |
| 07 | ![](07_console_director_ja.png) | [05] STREAM DIRECTOR：マスターのピーク dBFS・短期／統合 LUFS、4ch のピーク、位相、YouTube 目標ランプ、シーン切替 |
| 08 | ![](08_console_director_en.png) | 同・英語表示（パネルの刻印は共通。説明表示と右クリックメニューが切り替わる） |
| 09 | ![](09_mini_unit01.png) | コンパクト表示（[01]） |
| 10 | ![](10_mini_director.png) | コンパクト表示（[05]） |
| 11 | ![](11_calibration_measuring.png) | 10秒自動測定「INPUT CALIBRATOR」：測定中（読み上げカード、入力ピーク、経過秒数、進み具合） |
| 12 | ![](12_calibration_success.png) | 測定完了（設定した入力ゲイン、インターフェースの GAIN つまみの目安） |
| 13 | ![](13_calibration_clip_warning.png) | 入力の歪みを検出（赤ランプ、つまみを下げる案内） |
| 14 | ![](14_calibration_no_voice.png) | 声を検出できず（アンバーランプ、確認事項） |
| 15 | ![](15_obs_filter_panel_approx.png) | OBS のフィルタ設定画面（**近似の再現**。実物は OBS が描画） |

## 注意

- 絵文字は使っていません。
- **右クリックメニュー**：言語、常に手前に表示、コンパクト表示、初期化、自動測定、アップデート、ライセンス解除をまとめています。
- **最下段**：動作状況と警告を表示します。
- 15 番だけは本プラグインの描画ではなく、OBS 側の画面を Qt で再現した参考画像です。
