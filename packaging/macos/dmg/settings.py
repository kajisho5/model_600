# =============================================================================
#  MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
#  packaging/macos/dmg/settings.py  -  dmgbuild layout of the release .dmg
#
#    dmgbuild -s packaging/macos/dmg/settings.py -D stage=<folder> "<volume>" <out.dmg>
#
#  <folder> holds exactly what goes into the image (signed by kajisho5/runway):
#    MODEL 600 Installer.app, model600.plugin, README.txt (README optional)
#  Design: docs/design/dmg/DMG_DESIGN.md (plan 1). Item positions match the
#  background (background.png + background@2x.png, combined by dmgbuild into
#  one HiDPI TIFF). dmgbuild executes this file; it does not set __file__,
#  so the folder is taken from the code object's file name.
# =============================================================================
import inspect
import os

_here = os.path.dirname(os.path.abspath(inspect.currentframe().f_code.co_filename))
_repo = os.path.normpath(os.path.join(_here, "..", "..", ".."))
_stage = defines.get("stage")  # noqa: F821  (provided by dmgbuild)
if not _stage or not os.path.isdir(_stage):
    raise ValueError("pass the staged folder: -D stage=<folder>")

_installer = "MODEL 600 Installer.app"
_layout = {
    _installer: (170, 168),
    "model600.plugin": (436, 168),
    "README.txt": (572, 168),
}
_names = sorted(n for n in os.listdir(_stage) if not n.startswith("."))
_missing = [n for n in (_installer, "model600.plugin") if n not in _names]
_unexpected = [n for n in _names if n not in _layout]
if _missing or _unexpected:
    # The background is drawn for exactly these items; anything else would sit
    # outside the visible window or on top of the artwork.
    raise ValueError(f"stage mismatch: missing {_missing}, unexpected {_unexpected}")

volume_name = "MODEL 600"     # Window title; same whatever name the caller passes.
files = [os.path.join(_stage, n) for n in _names]
icon_locations = {n: _layout[n] for n in _names}
# No hide_extensions: SetFile -a E stores FinderInfo on the .app bundle
# itself, which strict signature verification rejects ("detritus").

format = "ULFO"           # LZFSE; macOS 10.11+ (the plug-in needs 12+).
filesystem = "HFS+"
background = os.path.join(_here, "background.png")
badge_icon = os.path.join(_repo, "resources", "macos", "model600.icns")

# Content is 660 x 400 pt; the extra height absorbs the title bar (dmgbuild's
# own default is the built-in 640 x 240 background in a 640 x 280 window).
window_rect = ((200, 120), (660, 428))
default_view = "icon-view"
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
arrange_by = None
icon_size = 96
text_size = 13
label_pos = "bottom"
show_item_info = False
show_icon_preview = False
