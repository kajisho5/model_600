# model_600 — 作業メモ（Claude 向け）

## Mac 版の配布（署名・公証）— 仕組みは用意済み。自前で作らない

Mac 向けの配布物は **Developer ID 署名 ＋ Apple の公証（notarization）** を通した `.dmg` にする。仕組みは `kajisho5/runway`（private）に用意してあり、このリポはそれを呼ぶだけ。

- `.github/workflows/release-mac.yml` → `kajisho5/runway/.github/workflows/mac-notarize.yml@main` を呼ぶ
- 流れ：`build` を実行 → `products` に当たったバンドルを内側から順に Developer ID で署名（hardened runtime ＋ timestamp）→ `.dmg` 化 → `notarytool` で公証 → ステープル → Artifacts に `model_600-<version>.dmg`
- 証明書と API キーは runway / hanger 側の Secrets にある。**このリポに証明書・鍵・`.p8`・パスワードを置かない。CMake や Xcode 側で配布用の署名設定もしない**（JUCE の ad-hoc 署名はそのままでよい。runway が `--force` で署名し直す）
- 2026/10 に runway の実機ランナーで通し確認済み（ダミー VST3 を署名 → 公証 Accepted → ステープル）

### コードを書いたら必ずやること

1. `release-mac.yml` の `build` と `products` を実際のビルド構成に合わせる
   - 今の構成：OBS プラグイン（CMake ＋ Qt6 ＋ OBS 31.1.1 SDK）、Universal（`arm64;x86_64`）、macOS 12.0 以上
   - `build: bash tools/ci/build_macos_release.sh` → 成果物は `dist-mac/model600.plugin`（Pro 版。音声モデルと THIRD_PARTY_NOTICES.md を Contents/Resources に同梱）
   - ビルドを変えたら `build.yml` の `macos-release` ジョブ（同じスクリプトを Xcode 26 で実行）が通ることを確認する
   - `products` は glob（`**` 可）。1 行でも当たらないとエラーで止まる
2. hardened runtime で動くようにする。マイク入力を使う Standalone などで entitlements が要る場合は、リポに `.entitlements` を置き、`release-mac.yml` に `entitlements: <パス>` を足す
3. dmg に同梱するインストール説明が要るなら、テキストを置いて `readme: <パス>` を足す

### 実行のしかた

- **現状は runway 側から実行する**：runway の Actions → **mac-release** → Run workflow。入力は repo `kajisho5/model_600`、ref `main`（またはタグ）、build `bash tools/ci/build_macos_release.sh`、products `dist-mac/model600.plugin`。公証済みの dmg は runway 側の実行結果の Artifacts に出る
- このリポの Actions → release-mac は今は動かない（2026/10/16 に実行して「workflow was not found」）。GitHub の仕様で、private リポ（runway）の再利用ワークフローは **private リポからしか呼べない**（"Access is allowed only from private repositories" — docs.github.com「Managing GitHub Actions settings for a repository」の Access 節）。このリポは public なので呼べない。runway 側の方針を変えるまでは上の runway 実行を使う
- タグ（`v1.0.0` など）を push しても実行され、dmg 名にバージョンが入る
- 必要な Secrets（`ASC_KEY_ID` / `ASC_ISSUER_ID` / `ASC_KEY_P8_BASE64` / `APPLE_TEAM_ID` / `MATCH_GIT_TOKEN`）は runway の sync-secrets が配布済み。このリポで手動登録・変更しない
- このリポは public なので Actions の macOS ランナーは無料

### 対象外

- AAX（Pro Tools）：PACE の署名が別途必要なので、この仕組みでは扱わない
- Windows 版（VST3）のコード署名：未対応
- App Store / TestFlight：Mac 版プラグインは Developer ID 配布なので使わない
