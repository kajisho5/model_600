# MODEL 600 — アイコン / DMG 設計（Step 1・改訂 2：Apple HIG 準拠）

一次情報：Apple Human Interface Guidelines「App icons」
https://developer.apple.com/design/human-interface-guidelines/app-icons （2026-06-08 改訂版を 2026-10-16 に取得）

## 1. 対象と現状

| 対象 | 現状 | 作るもの |
|------|------|----------|
| `model600.plugin`（Finder 上の表示） | アイコン指定なし | アイコン（下記 4 の方法で組み込み） |
| 配布 `.dmg` のボリュームアイコン | なし | 同じ図柄 |
| `.dmg` を開いたときの窓 | 背景なし | 660×400 の背景＋「プラグイン → plugins フォルダ」配置 |
| Windows `model600.dll` | なし | `.ico`＋バージョン情報（任意） |

アプリ（`.app`）は存在しない（OBS プラグインのため）。

## 2. HIG の要件と、この案での対応

| HIG（macOS のアプリアイコン） | 対応 |
|------|------|
| 1024×1024 の**正方形・角丸なし**のレイヤーを渡す。角丸のマスクはシステムが掛ける（"Providing layers with pre-defined masking negatively impacts specular highlight effects"） | 全レイヤーを 1024 正方形・全面（full-bleed）で作成。角丸は `*_masked` プレビューでのみ再現 |
| 背景レイヤー＋前景レイヤーで構成し、Icon Composer で組む。前景はベクター（SVG/PDF） | `layers/<案>/` に background / fg1… を SVG で分離 |
| 背景は単色かグラデーション | 2 色の縦グラデーションのみ |
| ハイライト・影・ベベル・ブラー・グローを自前で描かない | 一切なし（第 1 版のネジ・影・グロー・ヘアラインを削除） |
| 単一の概念を最小の形で。極端に細い線・鋭い角を避ける | 形は 3〜4 個、線幅は 60 px 以上、端は丸 |
| 塗りの重なり＋不透明度差で奥行き | 半透明の円・数字・トラックで奥行き |
| 文字はブランド/体験に必須な場合のみ | A・C は文字なし。B は製品名「600」のみ（ブランドとして） |
| UI 部品の複製を避ける | GUI のつまみそのものではなく、図案化した形 |
| default / dark / clear / tinted を考慮し、核となる要素は全外観で同じ | default・dark・tinted（グレー近似）を `preview/appearances.png` で確認 |
| 色空間 sRGB / Display P3 | sRGB |

## 3. 案（改訂 2）

| 案 | 図柄 | 長所 | 短所 |
|----|------|------|------|
| A `icon_a_dial` | 270° のレベル弧（アンバー）＋つまみの円＋指針 | 「音量を整える」が一目で伝わる。16 px でも形が残る | 速度計・計測アプリと似る可能性 |
| B `icon_b_600` | アンバー地に製品名「600」（太い幾何学数字、最後の 0 は半透明） | 製品名がそのまま記憶に残る。他の音響ソフトと区別しやすい | 文字中心のため、機能は伝わらない |
| C `icon_c_mix` | 半透明の円の前に高さの違う 3 本のレベルバー | 「自動ミックス」を表す | 携帯電話の電波強度アイコンと似ており誤認のおそれ |

プレビュー：`preview/appearances.png`（行＝案、列＝default / dark / tinted）、`preview/sizes_light.png`（256〜16 px）。

## 4. 実装方法（承認後）

1. 採用案のレイヤー SVG を Icon Composer（Xcode 26 同梱、Mac で実行）に読み込み、背景・Liquid Glass・外観変種を設定して `.icon` を書き出す。
2. ビルドへの組み込み（どちらにするかは CI で検証してから決める）
   - 第一候補：`actool` で `.icon` をコンパイルし、`Assets.car` と `CFBundleIconName` をバンドルに入れる（Xcode 26 の CI ランナーで実行）
   - 代替：平坦化した 1024 px PNG から `iconutil` で `.icns` を作り、`CFBundleIconFile` に指定
3. CI で、`.plugin` の中にアイコンがあり Info.plist が指していることを確認する。

## 5. DMG 窓（`dmg_window`）

HIG にディスクイメージの窓の規定はない。macOS の慣例（左に本体、右にインストール先フォルダへのエイリアス、矢印、短い説明）に沿う。

**制約（事実）**：.dmg は `kajisho5/runway` の mac-notarize が `hdiutil create -srcfolder` で作っている。背景・アイコン配置・ボリュームアイコン・plugins へのエイリアスを入れるには runway 側の機能追加が必要。

## 6. 未確認

- `actool` が Makefile ビルド（Xcode プロジェクトなし）から `.icon` を `CFBundle`（.plugin）向けにコンパイルできるか。
- macOS 26 で、Finder が `.plugin` バンドルのアイコンを表示するか。また旧形式（`.icns`）をどう表示するか。
- 正確な角丸形状（プレビューは半径 22.37% の近似）。Apple Design Resources のテンプレートで最終確認する。
