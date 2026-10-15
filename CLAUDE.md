# model_600 — 作業メモ（Claude 向け）

## Mac 版の配布（署名・公証）— 仕組みは用意済み。自前で作らない

Mac 向けの配布物は **Developer ID 署名 ＋ Apple の公証（notarization）** を通した `.dmg` にする。仕組みは `kajisho5/runway`（private）に用意してあり、このリポはそれを呼ぶだけ。

- `.github/workflows/release-mac.yml` → `kajisho5/runway/.github/workflows/mac-notarize.yml@main` を呼ぶ
- 流れ：`build` を実行 → `products` に当たったバンドルを内側から順に Developer ID で署名（hardened runtime ＋ timestamp）→ `.dmg` 化 → `notarytool` で公証 → ステープル → Artifacts に `model_600-<version>.dmg`
- 証明書と API キーは runway / hanger 側の Secrets にある。**このリポに証明書・鍵・`.p8`・パスワードを置かない。CMake や Xcode 側で配布用の署名設定もしない**（JUCE の ad-hoc 署名はそのままでよい。runway が `--force` で署名し直す）
- 2026/10 に runway の実機ランナーで通し確認済み（ダミー VST3 を署名 → 公証 Accepted → ステープル）

### コードを書いたら必ずやること

1. `release-mac.yml` の `build` と `products` を実際のビルド構成に合わせる
   - 今の想定は JUCE ＋ CMake、Universal（`arm64;x86_64`）、macOS 11.0 以上
   - JUCE の成果物の場所は `build/<Target>_artefacts/Release/VST3/*.vst3` と `.../AU/*.component`。Standalone も配るなら `.../Standalone/*.app` を `products` に足す
   - `products` は glob（`**` 可）。1 行でも当たらないとエラーで止まる
2. hardened runtime で動くようにする。マイク入力を使う Standalone などで entitlements が要る場合は、リポに `.entitlements` を置き、`release-mac.yml` に `entitlements: <パス>` を足す
3. dmg に同梱するインストール説明が要るなら、テキストを置いて `readme: <パス>` を足す

### 実行のしかた

- このリポの Actions → **release-mac** → Run workflow（iPhone の GitHub アプリからも可）。公証済みの dmg は実行結果の Artifacts に出る
- タグ（`v1.0.0` など）を push しても実行され、dmg 名にバージョンが入る
- 必要な Secrets（`ASC_KEY_ID` / `ASC_ISSUER_ID` / `ASC_KEY_P8_BASE64` / `APPLE_TEAM_ID` / `MATCH_GIT_TOKEN`）は runway の sync-secrets が配布済み。このリポで手動登録・変更しない
- このリポは public なので Actions の macOS ランナーは無料

### 対象外

- AAX（Pro Tools）：PACE の署名が別途必要なので、この仕組みでは扱わない
- Windows 版（VST3）のコード署名：未対応
- App Store / TestFlight：Mac 版プラグインは Developer ID 配布なので使わない
