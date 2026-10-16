# runway に入れる変更（未適用）

対象：`kajisho5/runway`（private）。この変更が入ると、MODEL 600 の dmg に窓のレイアウト（背景・アイコンの位置・ボリュームアイコン）と README が入り、ファイル名も対象リポのコミット ID になる。
このセッションでは runway への書き込みが拒否されたので、内容だけをここに残す。

**状態**

| 項目 | 状態 |
|------|------|
| model_600 側の準備 | 済み：`packaging/macos/dmg/settings.py`、`requirements.txt`、背景画像 2 枚 |
| 確認 | `build.yml` の `macos-release` ジョブで、dmgbuild による署名なしの dmg を毎回作って確認している |
| 互換性 | 新しい入力はどれも省略できる。省略したときの動作は今と同じなので、runway を使うほかのリポには影響しない |

## 1. `.github/workflows/mac-notarize.yml`

### 1-1. 入力を追加

`on.workflow_call.inputs` に、次の入力を足す。

```yaml
      dmg_settings:
        description: "dmgbuild の設定ファイル（対象リポ内のパス）。指定すると窓のレイアウト付きの dmg を作る。空なら今まで通り hdiutil"
        type: string
        default: ""
```

`env:`（今 `RW_README: ${{ inputs.readme }}` がある所）に、次の行を足す。

```yaml
      RW_DMG_SETTINGS: ${{ inputs.dmg_settings }}
```

### 1-2. 「Package and notarize」の dmg 作成

今のコード（646〜650 行付近）：

```bash
          if [ "$RW_PACKAGE" = "dmg" ]; then
            hdiutil create -volname "$RW_NAME" -srcfolder "$RUNNER_TEMP/stage/$RW_NAME" -fs HFS+ -format UDZO -ov "$file"
            codesign --force --timestamp --sign "$RW_IDENTITY" "$file"
```

変更後：

```bash
          if [ "$RW_PACKAGE" = "dmg" ]; then
            if [ -n "$RW_DMG_SETTINGS" ]; then
              # 窓のレイアウト付き（dmgbuild は .DS_Store を直接書くので Finder の自動操作が要らない）
              settings="$GITHUB_WORKSPACE/$RW_DMG_SETTINGS"
              [ -f "$settings" ] || { echo "::error::dmg_settings が見つからない: $RW_DMG_SETTINGS"; exit 1; }
              req="$(dirname "$settings")/requirements.txt"
              python3 -m venv "$RUNNER_TEMP/dmgenv"
              if [ -f "$req" ]; then
                "$RUNNER_TEMP/dmgenv/bin/pip" install -q -r "$req"
              else
                "$RUNNER_TEMP/dmgenv/bin/pip" install -q "dmgbuild[badge_icons]==1.6.7"
              fi
              "$RUNNER_TEMP/dmgenv/bin/dmgbuild" -s "$settings" -D stage="$RUNNER_TEMP/stage/$RW_NAME" "$RW_NAME" "$file"
            else
              hdiutil create -volname "$RW_NAME" -srcfolder "$RUNNER_TEMP/stage/$RW_NAME" -fs HFS+ -format UDZO -ov "$file"
            fi
            codesign --force --timestamp --sign "$RW_IDENTITY" "$file"
```

- dmgbuild は、`stage` に集めた署名済みの成果物を `ditto` でコピーする。そのため署名は崩れない（dmgbuild 1.6.7 `core.py`）。
- 公証、ステープル、`spctl` は今のまま。

### 1-3. バージョンの既定値を対象リポのコミットにする

今は、`version` が空でタグでもないと `${GITHUB_SHA:0:7}`（runway 自身のコミット）を使う（105〜108 行）。そのため mac-release から実行すると、dmg 名が runway のコミット ID になる。

Checkout の直後に、次のステップを足す。

```yaml
      - name: Version from the source repo
        if: inputs.source_repo != '' && inputs.version == ''
        shell: bash
        run: |
          v="$(git -C "$GITHUB_WORKSPACE" rev-parse --short=7 HEAD)"
          echo "RW_VER=$v" >> "$GITHUB_ENV"
          echo "配布物: ${RW_NAME}-${v}.${RW_PACKAGE}"
```

## 2. `.github/workflows/mac-release.yml`

`on.workflow_dispatch.inputs` に、次の入力を足す。

```yaml
      readme:
        description: "dmg に同梱するテキスト（対象リポ内のパス、空なら同梱しない）"
        type: string
        default: ""
      dmg_settings:
        description: "dmgbuild の設定ファイル（対象リポ内のパス、空なら窓の設定なし）"
        type: string
        default: ""
```

`jobs.notarize.with` に、次の 2 行を足す。

```yaml
      readme: ${{ inputs.readme }}
      dmg_settings: ${{ inputs.dmg_settings }}
```

## 3. MODEL 600 の実行（変更を入れた後）

runway の Actions で mac-release を実行する。入力は次の通り。

| 入力 | 値 |
|------|------|
| repo | `kajisho5/model_600` |
| ref | `main` |
| name | `MODEL600` |
| version | `1.0.0` |
| build | `bash tools/ci/build_macos_release.sh` |
| products | `dist-mac/model600.plugin, dist-mac/MODEL 600 Installer.app` |
| readme | `packaging/macos/README.txt` |
| dmg_settings | `packaging/macos/dmg/settings.py` |

できあがるファイルは `MODEL600-1.0.0.dmg` です。
