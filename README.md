# MMDirectEncoder

MMD v9.32対応のDirectShow動画エンコーダーです。
v9.31以前には対応していません。

## インストール

[Release](https://github.com/hatoghx/MMDirectEncoder/releases)

## 対応環境

Windows 64bit(x64)

## 導入

1.MMDを終了した状態で、ダウンロードしたMMDirectEncoderSetup_{バージョン}.exeを実行します。コーデックの登録と動作確認が行われます。

2.MMDを起動します。

3.「AVI出力」を選択し、保存先を指定します。

4.ビデオ圧縮コーデックの一覧から「MMDirectEncoder」を選択します。

5.詳細設定から設定します。<br/>
※アンインストールは、Windowsの「設定」→「アプリ」から実行できます。

## 開発リファレンス

[MMD FFmpeg Encoder](https://github.com/opdent-cmd/mmd-ffmpeg-encoder)<br/>
[MMD2FFMPEG](https://github.com/XPRAMT/MMD2FFMPEG)<br/>
[MMDVideoRecorder](https://github.com/chris0214/MMD.H.264.Exporter)

- 本エンコーダが正常に動作しない場合は、上記リポジトリをお試しください。
- 本エンコーダは、レンダリング結果を直接取得せず、AVI出力を経由してエンコードします。

## ライセンス

[GNU General Public License v3.0](LICENSE)
