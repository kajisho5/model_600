#!/usr/bin/env bash
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  tools/ci/build_macos_release.sh - macOS release build for signing.
#
#  Produces, unsigned apart from the linker's ad-hoc signature:
#    dist-mac/model600.plugin           Pro edition, Universal2, macOS 12+
#    dist-mac/MODEL 600 Installer.app   copies the plug-in into the OBS
#                                       plug-ins folder (Swift, Universal2)
#  kajisho5/runway's mac-notarize re-signs both with Developer ID (--force,
#  hardened runtime, timestamp), notarizes and staples the .dmg. The .dmg
#  window layout is packaging/macos/dmg/settings.py. No signing settings here.
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
xcodebuild -version

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

# 4. MODEL 600 Installer.app (same version as the plug-in).
src="$root/packaging/macos/installer"
app="$dist/MODEL 600 Installer.app"
exe="$app/Contents/MacOS/MODEL 600 Installer"
version="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$dist/model600.plugin/Contents/Info.plist")"
sdk_path="$(xcrun --sdk macosx --show-sdk-path)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$app/Contents/MacOS" "$app/Contents/Resources"
for arch in arm64 x86_64; do
    xcrun swiftc -O -parse-as-library -warnings-as-errors \
        -target "$arch-apple-macos12.0" -sdk "$sdk_path" \
        "$src/Installer.swift" -o "$tmp/installer-$arch"
done
lipo -create "$tmp/installer-arm64" "$tmp/installer-x86_64" -output "$exe"
sed -e "s/@VERSION@/$version/g" -e "s/@YEAR@/$(date +%Y)/g" "$src/Info.plist.in" > "$app/Contents/Info.plist"
cp "$src/installer.icns" "$app/Contents/Resources/installer.icns"
for lang in en ja; do
    mkdir -p "$app/Contents/Resources/$lang.lproj"
    cp "$src/$lang.lproj/Localizable.strings" "$app/Contents/Resources/$lang.lproj/"
    plutil -lint "$app/Contents/Resources/$lang.lproj/Localizable.strings"
done
plutil -lint "$app/Contents/Info.plist"
lipo "$exe" -verify_arch arm64 x86_64
# swiftc's linker signs ad-hoc per arch; lipo drops it. Re-sign the bundle
# ad-hoc so it runs locally (runway replaces this with Developer ID).
codesign --force --sign - "$app"
echo "ready: $app"
