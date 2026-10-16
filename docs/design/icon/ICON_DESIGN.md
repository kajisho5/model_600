# MODEL 600 — アイコン / DMG 設計（Step 1）

## 1. 対象と現状

| 対象 | 現状 | 作るもの |
|------|------|----------|
| `model600.plugin`（Finder 上の表示） | アイコン指定なし（汎用のバンドル表示） | `model600.icns`（16〜1024 px）を `Contents/Resources` に入れ、Info.plist に `CFBundleIconFile` を追加 |
| 配布 `.dmg` のボリュームアイコン | なし | 同じ図柄の `.VolumeIcon.icns` |
| `.dmg` を開いたときの窓 | 背景なし（ファイルが並ぶだけ） | 660×400 の背景画像＋「プラグイン → plugins フォルダ」の配置 |
| Windows `model600.dll` | アイコン・バージョン情報なし | `.ico` とバージョン情報リソース（任意。エクスプローラーのプロパティに出る） |

アプリ（`.app`）は存在しない（OBS プラグインのため）。

## 2. 方針

- GUI v2「実機ハードウェア」と同じ素材感：グラファイトの筐体、ネジ、ブラッシュ仕上げ、アクセントは SEVENTHWELL アンバー `#F0A33A` のみ。
- macOS のアイコン格子に合わせる：1024 px キャンバス、本体 824 px、角は連続曲線（半径 約 22%）。
- 16〜32 px で潰れない図柄にする（細かい文字は 64 px 以上でしか読めない前提）。
- 採用案は Blender でつまみ等を本番レンダリングし、GUI のつまみと同一の質感にする（今回の SVG は図柄の比較用）。

## 3. 案（`svg/`、プレビューは `preview/`）

| 案 | 図柄 | 長所 | 短所 |
|----|------|------|------|
| A `icon_a_knob` | 削り出しつまみ＋アンバーの指針＋目盛り＋「600」 | GUI の主役部品と同じ。高級感 | 16 px ではつまみだけになり、他の音響ソフトと似る |
| B `icon_b_segment` | 凹んだ表示窓にアンバーの 7 セグ「600」＋「MODEL」 | 製品名が小さくても読める。32 px でも「600」と分かる | 機能（音）の連想は弱い |
| C `icon_c_ladder` | つまみ＋LED レベルメーター | 「自動ミックス」が一目で伝わる | 要素が 2 つで、小サイズでは情報が多い |

`preview/sizes_light.png` / `sizes_dark.png` は 256・128・64・32・16 px の比較。

## 4. DMG 窓（`dmg_window`）

ヘッダーに製品名、中央に「model600.plugin → plugins（エイリアス）」、下に日英の操作説明。

**制約（事実）**：.dmg は `kajisho5/runway` の mac-notarize が `hdiutil create -srcfolder` で作っている。背景画像・アイコン配置・ボリュームアイコン・plugins フォルダへのエイリアスを入れるには、runway 側（mac-notarize）に機能追加が必要。このリポだけでは .dmg の見た目は変えられない。

## 5. 実装（承認後）

1. 採用案を Blender で本番レンダリング → `resources/macos/model600.icns`（`iconutil` で 16〜1024 の 10 サイズ）。
2. `CMakeLists.txt`：icns をバンドル Resources に追加、Info.plist.in に `CFBundleIconFile`。
3. CI で `.plugin` 内に icns があり Info.plist が指していることを確認。
4. DMG 背景・ボリュームアイコン：runway 側の対応可否を決めてから。
5. Windows `.ico`／バージョン情報：要否を決めてから。

## 6. 未確認

- Finder が `.plugin` バンドルに `CFBundleIconFile` のアイコンを表示するか（Mac 実機で確認が必要）。
- OBS 自体の画面にプラグインのアイコンが表示される場所は無い見込み（未確認）。
