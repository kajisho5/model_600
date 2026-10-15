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
  `voice-command-multilingual.hpp`
- **Layer 2 (portable + Qt)** — `license-manager.hpp`, `update-checker.hpp`,
  `os-conflict-shield.hpp`, `custom-ui-dialog.{hpp,cpp}`
- **Layer 3 (OBS adapters)** — `audio-filter.cpp`, `plugin-main.cpp`,
  `voice-command-win.cpp`, `voice-command-mac.mm`

The Layer 1/2 code carries no OBS dependency, so it recompiles unchanged for the
planned Discord VC edition, a VST3 plugin and an iOS companion.

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

### Requirements
OBS Studio 30+, Qt 6, a C++20 compiler (MSVC `/utf-8 /W4`, Apple Clang
Universal2, or GCC/Clang on Linux), CMake 3.21+.

## Editions
`-DMODEL600_PRO_EDITION=ON|OFF` physically includes or excludes the Pro code
(UNIT 05 and the voice director) at compile time. Online activation is handled
by the Cloudflare Worker in `tools/cloudflare_worker_auth.js`; the vendor mints
keys with `tools/generate_keys.py`. The offline signed token is byte-for-byte
identical between the Worker and `license-manager.hpp`, so activation survives a
network outage.

## 既知の制限 / Known limitations

- 音声コマンド受理時の完了チャイム（モニター専用、-24 dB）は未実装です。OBSフィルタの出力は配信にも乗るため、モニターだけに鳴らすには別のモニター専用ソースが必要になります。
- ライセンス・保護・OS設定修復まわりの保留事項は [`docs/DEFERRED_NOTES.md`](docs/DEFERRED_NOTES.md) にまとめています。
