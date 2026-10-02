# MMDirect Encoder

MMD v9.32対応のDirectShow動画エンコーダーです。
v9.31以前には対応していません。

## ダウンロード

[Release](https://github.com/hatoghx/MMDirectEncoder/releases)

## 対応環境

Windows 64bit(x64)

## インストール / 導入方法

1.ダウンロードしたZIPを任意のフォルダに解凍

2.解凍したフォルダ内のinstall.exeを実行 (コーデックが自動登録されます)

3.MMDを起動 (起動中の場合は再起動)

4.「AVI出力」を選択し、保存先を指定 (通常の動画出力方法に同じ)

5.ビデオ圧縮コーデックの一覧から「FFmpeg Video Encoder」を選択

6.詳細設定から出力方法を設定<br/>
※アンインストールは、uninstall.exeまたはuninstallerから実行できます。

## 開発リファレンス

[MMD FFmpeg Encoder](https://github.com/opdent-cmd/mmd-ffmpeg-encoder)<br/>
[MMD2FFMPEG](https://github.com/XPRAMT/MMD2FFMPEG)<br/>
[MMDVideoRecorder](https://github.com/chris0214/MMD.H.264.Exporter)

- 本エンコーダが正常に動作しない場合は、上記リポジトリをお試しください。
- 本エンコーダは、レンダリング結果を直接取得せず、AVI出力を経由してエンコードします。

## ライセンス

[GNU General Public License v3.0](LICENSE)

Copyright (c) 2026 hato
