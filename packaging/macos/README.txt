MODEL 600 // BROADCAST SUITE  -  SEVENTHWELL
macOS インストール方法 / Installation

必要なもの / Requirements
  - macOS 12 以降（Apple シリコン / Intel）
  - OBS Studio 31 以降

インストール（日本語）
  かんたんな方法
  1. 「MODEL 600 Installer」をダブルクリックします。
     （初回は「インターネットからダウンロードされたアプリ」の確認が出ます。［開く］を選びます）
  2. ［インストール］を押します。OBS Studio が起動中の場合は、終了してから［再試行］を押します。
  3. 「インストールしました」と出たら完了です。［OBS を開く］で OBS Studio が起動します。

  手動で入れる場合
  1. OBS Studio を終了します。
  2. Finder で「移動」メニュー → 「フォルダへ移動…」（⇧⌘G）を開き、次を入力します。
       ~/Library/Application Support/obs-studio/plugins
     フォルダが無い場合は作成してください。
  3. この中の「model600.plugin」を、上のフォルダへドラッグしてコピーします。
     （古い model600.plugin がある場合は置き換えます）

  使い始める
  OBS Studio を起動し、音声ソースの「フィルタ」から
  「MODEL 600 - [01] VOCAL MASTER」などを追加します。

Installation (English)
  Easy way
  1. Double-click "MODEL 600 Installer".
     (The first time, macOS asks whether to open an app downloaded from the
     internet. Choose Open.)
  2. Click Install. If OBS Studio is open, quit it and click Try Again.
  3. When "MODEL 600 is installed" appears, you are done. Open OBS starts
     OBS Studio.

  Manual install
  1. Quit OBS Studio.
  2. In Finder choose Go > Go to Folder... (Shift-Command-G) and enter
       ~/Library/Application Support/obs-studio/plugins
     Create the folder if it does not exist.
  3. Drag "model600.plugin" from this disk image into that folder
     (replace an older copy if asked).

  Getting started
  Start OBS Studio and add "MODEL 600 - [01] VOCAL MASTER" (and the other
  units as needed) from an audio source's Filters.

アンインストール / Uninstall
  上のフォルダから model600.plugin を削除します。
  Delete model600.plugin from the folder above.

音声ディレクター（Pro）/ Voice director (Pro)
  音声認識はプラグインに内蔵されたエンジンで Mac の中だけで行います。
  マイクの許可ダイアログは出ず、音声が外部に送られることはありません。
  Speech recognition runs on this Mac with an engine built into the plug-in.
  No microphone permission is requested and no audio leaves the computer.

ライセンス / Licences
  model600.plugin/Contents/Resources/THIRD_PARTY_NOTICES.md
