# MODEL 600 — 配布 dmg の窓とインストーラー（Step 1 設計 / Step 2 デザイン）

状態：**案 1 で実装済み**（2026-10-16、「おまかせ」で案 1・runway 変更・dmg 名 `MODEL600-<バージョン>.dmg` に決定）。runway 側の変更は書き込みが拒否されたため `docs/RUNWAY_CHANGES.md` に未適用のまま残している。

## 1. 前提（事実）

| 項目 | 内容 | 根拠 |
|------|------|------|
| OBS が `.plugin` を読む場所（macOS） | `~/Library/Application Support/obs-studio/plugins/<名前>.plugin` と、OBS.app 内の PlugIns だけ（環境変数 `OBS_PLUGINS_PATH` を除く）。`/Library/...`（全ユーザー共通）は x86_64 の旧形式（bin/data）だけ | obs-studio 31.1.1 `frontend/widgets/OBSBasic.cpp` `AddExtraModulePaths()`、`libobs/obs-cocoa.m` `add_default_module_paths()` |
| dmg の作り方 | runway `mac-notarize.yml` が、署名済みの成果物と readme をフォルダに集めて `hdiutil create -srcfolder` で作り、dmg 自体も Developer ID で署名、公証、ステープルする。背景・アイコン位置・ボリュームアイコンの設定はない | runway `.github/workflows/mac-notarize.yml` 603〜653 行 |
| runway `mac-release`（このリポは public のため、こちらを使う） | `readme` を渡す入力がない（前回の dmg に README が入っていない原因）。`version` が空だと runway 自身のコミット ID が使われる（前回の `model_600-b4a89ad.dmg` の `b4a89ad` は runway のコミット） | runway `mac-release.yml`、`mac-notarize.yml` 105〜108 行 |
| Finder の窓の配置を CI で作る方法 | `dmgbuild`（Python, MIT）は `.DS_Store` を直接書くため、Finder の自動操作（GitHub のランナーでは不安定）が要らない。背景画像・アイコン位置・アイコンサイズ・シンボリックリンク・ボリュームアイコン（`badge_icon`）に対応 | https://dmgbuild.readthedocs.io/en/latest/settings.html |

**ここから分かること（事実からの帰結）**：ドラッグ先のフォルダはユーザーごとに場所が違う（`/Users/<名前>/...`）。そのため、`.app` 配布でよく見る「Applications へのエイリアス」と同じ形のドラッグ先は dmg に入れられない。シンボリックリンクは `~` を展開しない。

## 2. 案

| | 案 1：インストーラー＋手動（推奨） | 案 2：手順つきドラッグ |
|---|---|---|
| dmg の中身 | `MODEL 600 Installer.app`、`model600.plugin`、`README.txt` | `model600.plugin`、`README.txt` |
| 操作 | インストーラーをダブルクリック → 確認 → 完了（手動でも入れられる） | Finder で ⇧⌘G → パスを入力 → ドラッグ（背景に手順を表示） |
| 長所 | 迷わない。OBS 起動中の検出、古い版の置き換え、署名の確認ができる | 追加のプログラムがない |
| 短所 | 小さなアプリ（Swift、約 200 行）を作って保守する | 手順が 4 つ。背景の手順カードを本物のフォルダと誤解してドロップしようとする人が出る |
| runway の変更 | dmgbuild 対応＋mac-release に readme・version | 同じ |

プレビュー：`preview/window_plan1_{light,dark}.png`、`preview/window_plan2_{light,dark}.png`、`preview/alerts.png`（案 1 のダイアログ）、`preview/installer_icon.png`。

## 3. 窓のデザイン（両案共通）

- 窓の内容サイズは 660×400 pt。背景画像は 660×420 pt にして、タイトルバーの高さの違いで下が白く空かないようにする（dmgbuild の既定では、640×240 の背景に 640×280 の窓を使っている）。背景は 1x と 2x（Retina）の 2 枚で、dmgbuild が 1 つの TIFF にまとめる（`tiffutil -cathidpicheck`）。
- 上端の帯（64 pt、#0E1012）：MODEL 600 // BROADCAST SUITE と SEVENTHWELL（アンバー）。製品 GUI と同じ書体（Inter）・配色にする。
- 中央の帯（#74777B）：項目名は Finder がシステムの文字色で描く。ライトモードでは黒、ダークモードでは白（推測：macOS の既定の挙動。実機では未確認）。この灰色は黒文字 4.67:1、白文字 4.50:1 で、どちらも 4.5:1 以上になる（`make_dmg.py` で計算）。アイコンと項目名はすべてこの帯の中に置く。
- 下端の帯（#14171B）：動作環境。文字のコントラストは 8.6:1（日本語）と 5.6:1（英語）。
- アイコンサイズは 96 pt、項目名は 13 pt。拡張子を隠す設定はしない。`SetFile -a E` は `.app` の中に Finder 情報を書き込み、`codesign --verify --strict` が「detritus」として拒否するため（CI で確認、2026-10-16）。
- ボリュームアイコン：システムのディスクアイコンに案 A のダイヤルを重ねる（dmgbuild の `badge_icon`）。
- インストーラーのアイコン：案 A と同じダイヤルを明るい（アルミ）地にしたもの。プラグイン本体と並んでも区別できる。

## 4. 案 1 のインストーラー（`MODEL 600 Installer.app`）

| 項目 | 設計 |
|------|------|
| 画面 | 独自の窓は作らず、システムの警告ダイアログ（NSAlert）だけを使う（HIG：標準部品を優先する） |
| 言語 | 日本語と英語（システムの言語設定に従う） |
| 流れ | ① 確認（インストール / キャンセル）→ ② OBS Studio が起動中なら「終了してから［再試行］」→ ③ コピー → ④ 完了（OBS を開く / 完了）。失敗したら理由と「README の手動手順」を表示 |
| コピー元 | 自分と同じフォルダの `model600.plugin`。Gatekeeper が app を別の場所に移して起動した場合（App Translocation）は、マウント中のボリュームから `model600.plugin` を探す |
| 安全確認 | コピー前に、プラグインの署名が自分（インストーラー）と同じ Team ID の Developer ID であることを `SecStaticCode` で確かめる。違えば中止する |
| 置き換え | いったん一時フォルダにコピーし、`FileManager.replaceItemAt` で入れ替える。途中で失敗しても半端な状態を残さない |
| OBS 起動中の判定 | `NSRunningApplication`（バンドル ID `com.obsproject.obs-studio`）。OBS を勝手に終了させることはしない |
| 権限 | 管理者パスワードは不要（書き込み先はユーザーのフォルダだけ）。マイクや自動操作の許可も使わない |
| 署名 | `products` に追加すると、runway が Developer ID（hardened runtime）で署名し、dmg ごと公証する |
| ビルド | `swiftc` で arm64 と x86_64 を作って `lipo` でまとめる。動作環境は macOS 12 以上。`tools/ci/build_macos_release.sh` に追加する |

ダイアログの文言（プレビュー `alerts.png`。HIG：ボタンは動詞、既定のボタンは右）：

| 場面 | 日本語 | English |
|------|--------|---------|
| 確認 | MODEL 600 をインストールしますか？ ／ キャンセル・**インストール** | Install MODEL 600? ／ Cancel・**Install** |
| OBS 起動中 | OBS Studio を終了してください ／ キャンセル・**再試行** | Quit OBS Studio to continue ／ Cancel・**Try Again** |
| 完了 | インストールしました ／ 完了・**OBS を開く** | MODEL 600 is installed ／ Done・**Open OBS** |
| OBS が見つからない（警告だけで、続行はできる） | OBS Studio が見つかりません ／ キャンセル・**続ける** | OBS Studio wasn't found ／ Cancel・**Continue** |
| 失敗 | インストールできませんでした（理由）／ **閉じる** | MODEL 600 couldn't be installed (reason) ／ **Close** |

## 5. 変更するファイル（Step 3）

**このリポ（model_600）**
- `packaging/macos/installer/`：`Installer.swift`、`Info.plist.in`、`{en,ja}.lproj/Localizable.strings`、`installer.icns`
- `packaging/macos/dmg/settings.py`：dmgbuild の設定（中身・位置・背景・バッジ）。依存は `requirements.txt` でバージョンを固定
- `packaging/macos/dmg/background.png`、`background@2x.png`、`installer.icns`：このフォルダの `export_assets.py` で生成する
- `tools/ci/build_macos_release.sh`：インストーラーのビルドを追加し、`dist-mac/` に出力
- `.github/workflows/build.yml`：`macos-release` ジョブで次を確認する。確認用の dmg は Artifacts（`MODEL600-dmg-layout-unsigned`）に出る
  - インストーラーを画面なしの `--headless` で実行し、インストールと置き換えができること
  - 改ざんしたプラグインはインストールされないこと
  - dmgbuild で作った署名なしの dmg の中身（`.DS_Store`、背景、ボリュームアイコン、署名）
- `release-mac.yml` と `CLAUDE.md`：`products` と `dmg_settings` を追記

**runway（別リポ。書き込みにはあなたの許可が必要）**
- `mac-notarize.yml`：入力 `dmg_settings` を追加する。指定されたときだけ、`hdiutil create` の代わりに `dmgbuild -s <設定> -D stage=<集めたフォルダ>` で作る（バージョンは固定。指定しないときの動作は今と同じ）。dmg の署名・公証・ステープルは今のまま。
- `mac-release.yml`：入力 `readme` と `dmg_settings` を追加する。`version` が空のときは、対象リポのコミット ID を使う。

## 6. 確認事項

1. 案 1 と案 2 のどちらにするか（推奨：案 1）。
2. runway の書き込み許可。このセッションでは、runway に書き込めるように追加する操作が自動判定で拒否された。runway の変更をこちらで行うなら、許可してほしい。
3. dmg の名前。今は `model_600-<コミット>.dmg`。`MODEL600-1.0.0.dmg` のような名前にするか（runway の `name` / `version` 入力で指定できる）。

## 7. 未確認（Mac 実機が必要）

- Finder の項目名の色が、ダークモードで白になるか（上の推測）。
- Gatekeeper が dmg 内のインストーラーを別の場所に移して起動するか。署名済みの dmg では起きないという説があるが、Apple の一次情報は見つけられなかった。どちらの場合でも動くように作る。
- `.plugin` に付けたアイコン（`CFBundleIconFile`）を Finder が表示するか。表示しない場合は、システムの「プラグイン」アイコンになる。

再生成：`python3 make_dmg.py && ./render.sh && python3 export_assets.py`（このフォルダで実行。案 A の図柄は `../icon/make_icons.py` を使う）
