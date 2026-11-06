# MODEL 600 brand files

The logo is the nameplate printed on every MODEL 600 panel. SEVENTHWELL
appears in amber, MODEL 600 in Inter Bold, and the amber unit badge beside it
carries the dial. It uses the same colours and typeface as the product GUI.

| File | Use |
|------|-----|
| `logo/model600-wordmark-black.svg` | Main logo on light backgrounds (README light mode, documents) |
| `logo/model600-wordmark-white.svg` | Main logo on dark backgrounds (README dark mode, the product, video) |
| `logo/model600-lockup-black.svg` / `-white.svg` | Logo with the rule and BROADCAST SUITE, for title cards and the download page |
| `logo/model600-badge.svg` | The amber badge with the dial on its own, for small spaces and favicons |
| `app-icon.svg` | App icon (also the macOS `.icns` and Windows `.ico`; see `docs/design/icon/`) |

All files have transparent backgrounds. The letters are outlines, so no font
is needed to display them.

## Colours

| Name | HEX | Where |
|------|-----|-------|
| Amber | `#F0A33A` | Badge, SEVENTHWELL on dark backgrounds |
| Amber (on light) | `#A8620F` | SEVENTHWELL on white (4.75:1; plain amber is only 2.1:1) |
| Panel | `#191C20` | Product faceplate, dark backgrounds |
| Text (dark bg) | `#E7E9EC` | MODEL 600 on dark |
| Ink (light bg) | `#14171B` | MODEL 600 on light, the dial inside the badge |

## Rules

- **Clear space**: leave at least the badge's width empty around the logo.
- **Minimum size**:
  - Wordmark: 24 px cap height for MODEL 600 (about 120 px wide).
  - Below that, use the badge or the app icon.
- **Do not**:
  - Recolour the wordmark.
  - Use the black version on dark backgrounds or the white version on light ones.
  - Stretch the logo.
  - Separate SEVENTHWELL from MODEL 600 in the main logo.
  - Put the logo on busy photos.

The source and the rejected concepts are in `docs/design/logo/`. To
regenerate, run `python3 make_gui_logo.py` there (it needs fontTools). Inter
is licensed under SIL OFL 1.1 (`resources/fonts/LICENSE-Inter.txt`).
