# MODEL 600 // BROADCAST SUITE

Broadcast-grade auto-mixing for OBS Studio, by **SEVENTHWELL**.

MODEL 600 is a set of five OBS audio filters that work as one mixer. Put each
unit on the source it is made for: your mic, a call, game audio, music, and
the master. The units share one bus in OS shared memory. When you speak, music
and game audio move out of the way of your voice, and the master keeps the
whole mix under one ceiling. You set up no routing.

![The [01] VOCAL MASTER console](docs/gui-samples/02_console_unit01_en.png)

## The five units

| Unit | Filter name | Put it on | What it does |
|------|-------------|-----------|--------------|
| 01 | VOCAL MASTER | Your mic | Input calibration, gate, de-plosive, whisper compressor, de-esser, limiter |
| 02 | COLLAB RIDER | Calls (Discord and similar) | Keeps call levels even and ducks them while you talk |
| 03 | GAME IMPACT | Game audio | Tames sudden peaks and leaves room in the mid band for your voice |
| 04 | BGM POCKET | Music | Mid/side spectral pocket for your voice; brings music back up while you are away |
| 05 | STREAM DIRECTOR | Master (Pro) | Master meters and BS.1770-4 loudness, scene macros, voice commands |

## Install

### macOS (12 or later, Apple silicon and Intel)

The macOS build ships as a `.dmg` signed with a Developer ID and notarized by Apple.

1. Open the `.dmg` and double-click **MODEL 600 Installer**. The first time,
   macOS asks whether to open an app downloaded from the internet. Choose
   **Open**.
2. Click **Install**. If OBS Studio is open, quit it and click **Try Again**.
   The installer copies `model600.plugin` into
   `~/Library/Application Support/obs-studio/plugins`. That is the folder OBS
   loads third-party plug-ins from on macOS.
3. Start OBS Studio, open an audio source's **Filters**, and add
   **MODEL 600 - [01] VOCAL MASTER** (and the other units as needed).

The installer installs only a plug-in signed by the same developer as the
installer. To install by hand, copy `model600.plugin` into the folder above.
The `README.txt` in the disk image has both sets of steps.

### Windows (64-bit)

Extract `model600-Windows-<edition>.zip` into the OBS Studio install folder
(for example `C:\Program Files\obs-studio`). It contains
`obs-plugins/64bit/model600.dll`. The Windows build is not code-signed yet.

### Requirements

- OBS Studio 31 or later for the packaged macOS and Windows builds. They are
  built against the OBS 31.1.1 SDK.
- macOS 12 or later (Universal 2), or 64-bit Windows.

## Voice director (Pro)

STREAM DIRECTOR understands spoken commands in Japanese, English, Korean,
Chinese and Indonesian. A command fires only after a wake word such as
"Director" or "音響さん". Every command can be undone.

| OS | Speech engine | Notes |
|----|---------------|-------|
| Windows | SAPI (built into Windows) | |
| macOS | whisper.cpp, built into the plug-in (`ggml-base-q5_1.bin`, about 60 MB, inside the bundle) | No permission prompt; runs entirely on the Mac |
| Linux | None by default (enable the built-in engine with `-DMODEL600_VOICE_WHISPER=ON`) | |

On macOS the plug-in cannot use the system speech recognizer (see
[Known limitations](#known-limitations)), so it carries its own. It never opens
the microphone. It listens only to the signal of UNIT 01 before effects. The
pipeline is: utterance detection → resample to 16 kHz → whisper → the shared
multilingual command parser. None of this runs on the OBS audio thread.

- whisper.cpp v1.9.4 is pinned to a commit, and the model is checked against
  its SHA-256 (`cmake/whisper.cmake`).
- Universal 2 builds compile arm64 and x86_64 separately and then merge them.
  x86_64 is limited to AVX + F16C, so 2013 Intel Macs can run it.
- The regression test `m600_test_whisper` (`-DMODEL600_WHISPER_TESTS=ON`) runs
  synthesized Japanese and English commands. It requires zero false triggers
  and at least 70 % recognized (`tests/data/voice/`).

## Interface

The GUI v2 "hardware" design draws each unit as a rack panel, with 7-segment
displays, LED bars and real-time meters. Every screen is in
[`docs/gui-samples/`](docs/gui-samples/README.md). The images are rendered by
the shipping Qt code, with meter values from audio run through each unit's DSP
engine.

- Design specification: [`docs/design/v2/DESIGN_V2.md`](docs/design/v2/DESIGN_V2.md)
- Panels and parts are rendered in Blender
  ([`docs/design/v2/render_hardware.py`](docs/design/v2/render_hardware.py)).
  [`pack_assets.py`](docs/design/v2/pack_assets.py) packs them into `resources/hw/`.

## Architecture

The code is split into three layers. OBS-specific code is about 5 % of it.

- **Layer 1, portable C++20**: `shared-state.hpp`, `dsp-core.hpp`,
  `voice-command-multilingual.hpp`, `voice-segmenter.hpp`
- **Layer 2, portable with Qt / whisper.cpp**: `license-manager.hpp`,
  `update-checker.hpp`, `os-conflict-shield.hpp`, `custom-ui-dialog.{hpp,cpp}`,
  `voice-whisper.{hpp,cpp}`
- **Layer 3, OBS adapters**: `audio-filter.cpp`, `plugin-main.cpp`,
  `voice-command-win.cpp`

Layers 1 and 2 do not depend on OBS. They are meant to recompile unchanged for
the planned Discord VC edition, a VST3 plug-in and an iOS companion app.

## Build

```bash
python3 setup_project.py --tests      # configure, build and run the self-tests
python3 setup_project.py --install    # build, then install into OBS
python3 setup_project.py --standard   # Standard edition (no UNIT 05, no voice director)
```

With CMake directly:

```bash
cmake -S . -B build -DMODEL600_PRO_EDITION=ON -DMODEL600_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

- **Windows**: `build_win.ps1 -QtDir <...> -ObsDir <...> [-Install] [-RunTests]`
- **macOS (development)**: build, then double-click `install_mac.command`. It
  clears the Gatekeeper quarantine flag and installs the `.plugin`.
- **macOS (release)**: `bash tools/ci/build_macos_release.sh` builds
  `dist-mac/model600.plugin` and `dist-mac/MODEL 600 Installer.app`. Signing,
  notarization and the `.dmg` happen in a separate private pipeline
  (`kajisho5/runway`). This repository holds no signing keys. The disk image
  layout is `packaging/macos/dmg/settings.py`, and its install guide is
  `packaging/macos/README.txt`.

Build requirements: Qt 6, CMake 3.21+, and a C++20 compiler (MSVC with
`/utf-8 /W4`, Apple Clang for Universal 2, or GCC/Clang on Linux). The OBS
SDK comes from your OBS installation, or from `tools/ci/obs_sdk.cmake`
(OBS 31.1.1).

### Continuous integration

Every push builds and tests on Linux, Windows and macOS (Pro and Standard),
plus the macOS release build with Xcode 26. The workflow is
`.github/workflows/build.yml`. Its artifacts:

- `model600-Windows-<edition>.zip`: `obs-plugins/64bit/model600.dll`
- `model600-macOS-<edition>.zip`: `model600.plugin` (Universal 2, ad-hoc
  signed) and `install_mac.command`
- `MODEL600-dmg-layout-unsigned`: an unsigned preview of the release disk image

## Editions

`-DMODEL600_PRO_EDITION=ON|OFF` decides at compile time whether the Pro code
(UNIT 05 and the voice director) is in the binary at all. Online activation is
handled by the Cloudflare Worker in `tools/cloudflare_worker_auth.js`, and keys
are minted with `tools/generate_keys.py`. The Worker and `license-manager.hpp`
produce byte-for-byte identical offline tokens, so activation survives a
network outage.

## Known limitations

- **No completion chime yet.** The chime that confirms a voice command was
  meant for the monitor only, at -24 dB. A filter's output also goes to the
  stream, so playing it on the monitor alone needs a separate monitor-only
  source.
- **No system speech recognizer on macOS.** macOS reads the permission text
  (`NSSpeechRecognitionUsageDescription`) from the host app's Info.plist, here
  OBS.app. OBS 31.1.1 does not have that key, and macOS terminates an app that
  requests speech recognition without it. That is why the built-in whisper.cpp
  engine is used instead.
- **Accuracy so far comes from synthesized voices only.** Automated tests use
  Open JTalk and eSpeak NG. Accuracy with real voices, accuracy in Korean,
  Chinese and Indonesian, and response time on Intel Macs have not been
  measured.
- **Not yet run in OBS on a real Mac or Windows PC.** CI confirms that the
  plug-in loads into the real libobs on Linux, and it checks the macOS
  installer and disk image.
- **The build downloads whisper.cpp and the model.** Offline, pass
  `-DMODEL600_WHISPER_MODEL_PATH=<path to ggml-base-q5_1.bin>`.
- Open items on licensing, protection and OS setting repair are tracked in
  [`docs/DEFERRED_NOTES.md`](docs/DEFERRED_NOTES.md).

## License

MIT. See [`LICENSE`](LICENSE). Third-party components and their licences are
listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
