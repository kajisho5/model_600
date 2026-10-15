# MODEL 600 // BROADCAST SUITE

Commercial broadcast-grade auto-mixing suite for OBS Studio, by **SEVENTHWELL**.

Five cooperating units communicate over an OS shared-memory bus so that
voice-triggered mid-band ducking and a cooperative master ceiling run across
separate OBS filters (and a future standalone companion app) with no routing.

| Unit | Filter name | Purpose |
|------|-------------|---------|
| 01 | VOCAL MASTER | Mic / voice: calibration, gate, de-plosive, whisper comp, de-esser, limiter |
| 02 | COLLAB RIDER | Calls: auto-leveling + ducking under your voice |
| 03 | GAME IMPACT | Game audio: transient taming + voice scoop |
| 04 | BGM POCKET | Music: M/S spectral pocket + AFK auto-swell |
| 05 | STREAM DIRECTOR | (Pro) Master telemetry, BS.1770-4 loudness, scene macros |

## Architecture (3-layer, OBS confined to ~5%)

- **Layer 1 (portable, pure C++20)** — `shared-state.hpp`, `dsp-core.hpp`,
  `voice-command-multilingual.hpp`, `voice-segmenter.hpp`
- **Layer 2 (portable + Qt / whisper.cpp)** — `license-manager.hpp`, `update-checker.hpp`,
  `os-conflict-shield.hpp`, `custom-ui-dialog.{hpp,cpp}`, `voice-whisper.{hpp,cpp}`
- **Layer 3 (OBS adapters)** — `audio-filter.cpp`, `plugin-main.cpp`,
  `voice-command-win.cpp`

## 音声ディレクターのエンジン / Voice director engine

| OS | 認識エンジン | 備考 |
|----|------------|------|
| Windows | SAPI（OS 標準） | |
| macOS | 内蔵 whisper.cpp（`ggml-base-q5_1.bin`, 約 60 MB をバンドル同梱） | 許可ダイアログなし・完全オンデバイス |
| Linux | なし（`-DMODEL600_VOICE_WHISPER=ON` で内蔵エンジンを有効化可） | |

macOS は OS 標準の音声認識を使えないため（下の「既知の制限」参照）、音声認識を
プラグインに内蔵しています。マイクは開かず、UNIT 01 のエフェクト前の音声だけを
認識します。発話区間検出 → 16 kHz 変換 → whisper → 既存の多言語コマンド解析、の
順に処理し、OBS の音声スレッドは一切止めません。

- whisper.cpp v1.9.4 はコミット ID 固定、モデルは SHA-256 検証（`cmake/whisper.cmake`）。
- Universal2 では arm64 / x86_64 を別々にビルドして結合します（x86_64 は AVX+F16C までに制限し、2013 年の Intel Mac でも動作）。
- 回帰テスト `m600_test_whisper`（`-DMODEL600_WHISPER_TESTS=ON`）：合成音声の日本語・英語コマンドで、誤発火 0 件と認識率 70% 以上を確認します（`tests/data/voice/`）。

The Layer 1/2 code carries no OBS dependency, so it recompiles unchanged for the
planned Discord VC edition, a VST3 plugin and an iOS companion.

## GUI

全画面のサンプル画像は [`docs/gui-samples/`](docs/gui-samples/README.md) にあります。実装コードを直接描画したものです。

- **デザイン仕様**：GUI v2「実機ハードウェア」の仕様は [`docs/design/v2/DESIGN_V2.md`](docs/design/v2/DESIGN_V2.md) にあります。
- **パネルと部品**：[`docs/design/v2/render_hardware.py`](docs/design/v2/render_hardware.py)（Blender）で書き出し、[`pack_assets.py`](docs/design/v2/pack_assets.py) で `resources/hw/` にまとめています。

## Build

```bash
python3 setup_project.py --tests      # configure + build + run self-tests
python3 setup_project.py --install    # build then install into OBS
python3 setup_project.py --standard   # Standard edition (no UNIT 05 / voice)
```

Direct CMake:

```bash
cmake -S . -B build -DMODEL600_PRO_EDITION=ON -DMODEL600_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- **Windows**: `build_win.ps1 -QtDir <...> -ObsDir <...> [-Install] [-RunTests]`
- **macOS**: build, then double-click `install_mac.command` (clears Gatekeeper
  quarantine and installs the `.plugin`).

CI packages (GitHub Actions artifacts `model600-<OS>-<edition>`):

- `model600-Windows-*.zip`: `obs-plugins/64bit/model600.dll` — extract into the
  OBS install folder (e.g. `C:\Program Files\obs-studio`).
- `model600-macOS-*.zip`: `model600.plugin` (Universal2, ad-hoc signed) +
  `install_mac.command` — extract and double-click the installer.

### Requirements
OBS Studio 30+, Qt 6, a C++20 compiler (MSVC `/utf-8 /W4`, Apple Clang
Universal2, or GCC/Clang on Linux), CMake 3.21+.

Third-party components and their licences: [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## Editions
`-DMODEL600_PRO_EDITION=ON|OFF` physically includes or excludes the Pro code
(UNIT 05 and the voice director) at compile time. Online activation is handled
by the Cloudflare Worker in `tools/cloudflare_worker_auth.js`; the vendor mints
keys with `tools/generate_keys.py`. The offline signed token is byte-for-byte
identical between the Worker and `license-manager.hpp`, so activation survives a
network outage.

## 既知の制限 / Known limitations

- 音声コマンド受理時の完了チャイム（モニター専用、-24 dB）は未実装です。OBSフィルタの出力は配信にも乗るため、モニターだけに鳴らすには別のモニター専用ソースが必要になります。
- macOS で OS 標準の音声認識（Speech framework）を使わない理由：許可ダイアログの文言（`NSSpeechRecognitionUsageDescription`）はホストアプリ（OBS.app）の Info.plist から読まれますが、OBS 31.1.1 はこのキーを持っておらず、キーが無いアプリで音声認識を要求すると macOS がプロセスを終了させます。そのため内蔵の whisper.cpp を使います。
- 内蔵エンジンの認識精度は、自動テストでは合成音声（Open JTalk / eSpeak NG）でしか確認していません。実際の声での精度、韓国語・中国語・インドネシア語の精度、Intel Mac での応答時間は未確認です。
- whisper.cpp の取得とモデルのダウンロードはビルド時に行います（ネットワークが無い環境では `-DMODEL600_WHISPER_MODEL_PATH=<ggml-base-q5_1.bin>` を指定）。
- ライセンス・保護・OS設定修復まわりの保留事項は [`docs/DEFERRED_NOTES.md`](docs/DEFERRED_NOTES.md) にまとめています。
