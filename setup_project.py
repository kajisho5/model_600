#!/usr/bin/env python3
# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  setup_project.py  -  One-command project driver.
#
#  The canonical sources live in this repository (src/, tools/, CMakeLists.txt).
#  This script is the single entry point that verifies the tree, locates Qt6 and
#  libobs/OBS, then configures, builds, tests and (optionally) installs the
#  plugin for the host platform -- so a fresh clone goes from zero to a loaded
#  OBS plugin with one command.
#
#  Usage:
#    python3 setup_project.py                 # configure + build (Pro)
#    python3 setup_project.py --standard      # Standard edition (no UNIT 05)
#    python3 setup_project.py --tests         # also build + run self-tests
#    python3 setup_project.py --install       # install into OBS afterwards
#    python3 setup_project.py --verify        # just check the tree is complete
#    python3 setup_project.py --qt <dir> --obs <dir>   # explicit SDK locations
# =============================================================================
import argparse
import os
import platform
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))

EXPECTED = [
    "CMakeLists.txt",
    "version.json",
    "install_mac.command",
    "build_win.ps1",
    "tools/generate_keys.py",
    "tools/cloudflare_worker_auth.js",
    "src/shared-state.hpp",
    "src/dsp-core.hpp",
    "src/voice-command-multilingual.hpp",
    "src/license-manager.hpp",
    "src/update-checker.hpp",
    "src/os-conflict-shield.hpp",
    "src/custom-ui-dialog.hpp",
    "src/custom-ui-dialog.cpp",
    "src/voice-command-win.cpp",
    "src/voice-command-mac.mm",
    "src/audio-filter.cpp",
    "src/plugin-main.cpp",
]


def verify_tree():
    missing = [p for p in EXPECTED if not os.path.exists(os.path.join(ROOT, p))]
    if missing:
        print("MISSING FILES:")
        for m in missing:
            print("  -", m)
        return False
    print(f"Project tree OK ({len(EXPECTED)} files present).")
    return True


def have(cmd):
    return shutil.which(cmd) is not None


def find_qt(explicit):
    if explicit:
        return explicit
    for env in ("Qt6_DIR", "QT_DIR", "QTDIR"):
        if os.environ.get(env):
            return os.environ[env]
    # Common install roots per platform.
    candidates = []
    sysname = platform.system()
    if sysname == "Darwin":
        candidates += ["/opt/homebrew/opt/qt6", "/opt/homebrew/opt/qt",
                       "/usr/local/opt/qt6", "/usr/local/opt/qt"]
    elif sysname == "Linux":
        candidates += ["/usr/lib/x86_64-linux-gnu/cmake/Qt6", "/usr"]
    else:  # Windows
        for base in (r"C:\Qt", os.path.expanduser(r"~\Qt")):
            if os.path.isdir(base):
                for v in sorted(os.listdir(base), reverse=True):
                    msvc = os.path.join(base, v, "msvc2019_64")
                    if os.path.isdir(msvc):
                        candidates.append(msvc)
    for c in candidates:
        if os.path.isdir(c):
            return c
    return None


def find_obs(explicit):
    if explicit:
        return explicit
    sysname = platform.system()
    if sysname == "Windows":
        for c in (r"C:\Program Files\obs-studio",):
            if os.path.isdir(c):
                return c
    # On Linux/macOS libobs-dev / brew provides the find_package path already.
    return None


def obs_plugin_dir():
    sysname = platform.system()
    if sysname == "Darwin":
        return os.path.expanduser("~/Library/Application Support/obs-studio/plugins")
    if sysname == "Linux":
        return os.path.expanduser("~/.config/obs-studio/plugins/model600/bin/64bit")
    if sysname == "Windows":
        return r"C:\Program Files\obs-studio\obs-plugins\64bit"
    return None


def run(cmd, **kw):
    print("+", " ".join(cmd))
    return subprocess.run(cmd, check=True, **kw)


def main():
    ap = argparse.ArgumentParser(description="MODEL 600 build driver")
    ap.add_argument("--standard", action="store_true", help="build Standard edition")
    ap.add_argument("--tests", action="store_true", help="build + run self-tests")
    ap.add_argument("--install", action="store_true", help="install into OBS after build")
    ap.add_argument("--verify", action="store_true", help="only verify the tree")
    ap.add_argument("--qt", default="", help="Qt6 dir (CMAKE_PREFIX_PATH entry)")
    ap.add_argument("--obs", default="", help="OBS SDK/install dir")
    ap.add_argument("--config", default="Release", help="build config")
    ap.add_argument("--build-dir", default=os.path.join(ROOT, "build"))
    args = ap.parse_args()

    print("=" * 64)
    print(" MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL  -  setup_project")
    print("=" * 64)

    if not verify_tree():
        return 1
    if args.verify:
        return 0

    if not have("cmake"):
        print("ERROR: cmake not found on PATH.", file=sys.stderr)
        return 2

    qt = find_qt(args.qt)
    obs = find_obs(args.obs)
    prefix = os.pathsep.join([p for p in (qt, obs) if p])

    cfg = [
        "cmake", "-S", ROOT, "-B", args.build_dir,
        f"-DMODEL600_PRO_EDITION={'OFF' if args.standard else 'ON'}",
        f"-DMODEL600_BUILD_TESTS={'ON' if args.tests else 'OFF'}",
        f"-DCMAKE_BUILD_TYPE={args.config}",
    ]
    if prefix:
        cfg.append(f"-DCMAKE_PREFIX_PATH={prefix}")
    if platform.system() == "Windows":
        cfg += ["-G", "Visual Studio 17 2022", "-A", "x64"]

    try:
        run(cfg)
        run(["cmake", "--build", args.build_dir, "--config", args.config, "--parallel"])
        if args.tests:
            run(["ctest", "--test-dir", args.build_dir, "-C", args.config, "--output-on-failure"])
    except subprocess.CalledProcessError as e:
        print(f"\nBUILD FAILED (exit {e.returncode}).", file=sys.stderr)
        if not qt:
            print("Hint: Qt6 was not auto-detected. Pass --qt <path-to-qt6>.", file=sys.stderr)
        return e.returncode

    print("\nBuild succeeded.")

    if args.install:
        dest = obs_plugin_dir()
        if not dest:
            print("Install target unknown for this platform; skipping.", file=sys.stderr)
            return 0
        os.makedirs(dest, exist_ok=True)
        # Find the built artefact.
        names = ["model600.dll", "model600.so", "libmodel600.so", "model600.plugin"]
        found = None
        for base, _dirs, files in os.walk(args.build_dir):
            for n in names:
                p = os.path.join(base, n)
                if os.path.exists(p):
                    found = p
                    break
            if found:
                break
        if not found:
            print("Built artefact not found to install.", file=sys.stderr)
            return 1
        target = os.path.join(dest, os.path.basename(found))
        if os.path.isdir(found):
            if os.path.exists(target):
                shutil.rmtree(target)
            shutil.copytree(found, target)
        else:
            shutil.copy2(found, target)
        print(f"Installed: {target}")
        print("Restart OBS and add the MODEL 600 filters.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
