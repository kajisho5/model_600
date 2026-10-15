#!/usr/bin/env bash
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  tools/ci/build_macos_release.sh - macOS release build for signing.
#
#  Produces dist-mac/model600.plugin (Pro edition, Universal2, macOS 12+),
#  unsigned apart from the linker's ad-hoc signature. kajisho5/runway's
#  mac-notarize re-signs it with Developer ID (--force, hardened runtime,
#  timestamp), notarizes and staples the .dmg. No signing settings here.
#
#    bash tools/ci/build_macos_release.sh
#
#  OBS_SDK_DIR   where the OBS 31.1.1 SDK is built/reused
#                (default: $RUNNER_TEMP/obs-sdk, or .cache/obs-sdk locally)
# =============================================================================
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk="${OBS_SDK_DIR:-${RUNNER_TEMP:-$root/.cache}/obs-sdk}"
build="$root/build-release"
dist="$root/dist-mac"

[ "$(uname -s)" = "Darwin" ] || { echo "error: macOS only" >&2; exit 1; }
xcodebuild -version | head -1

# 1. OBS SDK (libobs + obs-frontend-api), skipped when already present.
if [ ! -d "$sdk/sdk" ]; then
    cmake -DOUT="$sdk" -P "$root/tools/ci/obs_sdk.cmake"
fi

# 2. Plugin (Pro). libobs ships as sdk/Frameworks/libobs.framework.
cmake -S "$root" -B "$build" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
    "-DCMAKE_PREFIX_PATH=$sdk/sdk;$sdk/deps;$sdk/qt6;$sdk/sdk/Frameworks" \
    -DMODEL600_PRO_EDITION=ON
cmake --build "$build" --config Release --parallel

# 3. Install layout = what OBS loads from ~/Library/Application Support/obs-studio/plugins.
rm -rf "$dist"
cmake --install "$build" --config Release --prefix "$dist" --component model600

bin="$dist/model600.plugin/Contents/MacOS/model600"
lipo -info "$bin"
lipo "$bin" -verify_arch arm64 x86_64
test -f "$dist/model600.plugin/Contents/Info.plist"
test -f "$dist/model600.plugin/Contents/Resources/ggml-base-q5_1.bin"
test -f "$dist/model600.plugin/Contents/Resources/THIRD_PARTY_NOTICES.md"
echo "ready: $dist/model600.plugin"
