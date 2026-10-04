# mgba-midisync

Linuxの携帯ゲーム機で、**FMS**（Lo-Bit Club、GBA）と**LSDj**（ゲームボーイ）を、USBのMIDI機器と同期させるためのものです。
リンクケーブルもArduinoboyも要りません。

[mGBA](https://github.com/mgba-emu/mgba) のlibretroコアを改造したもので、エミュレータの通信ポートを
USBのMIDI機器（`/dev/snd/midiC*D0`）につなぎ、実機でケーブルをつないだときと同じ信号をやりとりします。
**TrimUI Brick（Knulli）**の上のUSB-Cに**Dirtywave M8**をつないだ環境で作り、確認しています。

[English README](README.md)

| | MIDIに合わせる（子） | MIDIを動かす（親） |
|---|---|---|
| **FMS** | Sync `In`、`GBA to GBA` | Sync `Out`、`GBA to GBA` |
| **LSDj** | SYNC `MIDI`、STARTを押して待機 | SYNC `LSDJ` |

ほかのゲームには影響しません。インストーラーは別のコアを置き、**Ports**に起動用の項目を2つ足すだけです。
GBAやゲームボーイの一覧からは、これまでどおりKnulli標準のコアで起動します。

PortsからFMSやLSDjを動かしている間は、CPUのガバナーを `performance`（常に最大クロック）にして、終了時に元に戻します。
`schedutil` のままだと、メニューや静かな場面でクロックが下がり、戻るまでの数秒間、音が歪んだりテンポが遅くなったりします。
ガバナーを変えたくない場合は、環境変数 `MGBA_MIDISYNC_KEEP_GOVERNOR=1` を付けてランチャーを起動してください。

## 導入（Knulli）

1. [Releases](https://github.com/Ayagi3678/mgba-midisync/releases) から `mgba-midisync-<バージョン>-aarch64.zip` をダウンロードして展開する
2. フォルダごと本体にコピーする（WinSCPなどのSFTPで、ユーザー `root`、パスワード `linux`）。例：`/userdata/system/mgba-midisync`
3. SSHで実行する
   ```
   bash /userdata/system/mgba-midisync/install.sh
   ```
   `/userdata/roms/gba` からFMS、`/userdata/roms/gb` からLSDjを、ファイル名（「fms」「lsdj」を含むもの）で探します。
   見つからないときは `install.sh --fms /パス/FMS.gba --lsdj /パス/lsdj.gb` のように指定してください。
4. MIDI機器を本体のUSBにつなぎ、**Ports**から **FMS (MIDI Sync)** か **LSDj (MIDI Sync)** を起動する

アンインストールは `bash uninstall.sh` です（セーブデータは消えません）。

### Dirtywave M8 の場合
- M8が親：M8のMIDI設定で、USBにクロックとスタート・ストップを送るようにする
- FMSやLSDjが親：M8がUSBからクロックとスタート・ストップを受けるようにする
- M8をつなぐと、Knulliが音の出力先をM8に切り替えてしまうことがあります（音が小さくなる、メニューの音量の項目が消える）。
  本体のスピーカーに固定してください。
  ```
  batocera-settings-set audio.device alsa_output._sys_devices_platform_soc_sndcodec_sound_card0.stereo-fallback
  batocera-audio set alsa_output._sys_devices_platform_soc_sndcodec_sound_card0.stereo-fallback
  ```
  （名前は `batocera-audio list` で確認できます）。
- それでも小さいときは、コアオプションの **Speaker Volume** で本体の音量（ミキサーのMaster）を上げてください。TrimUI Brickは起動時に41%まで下がります。ゲームを終了すると元の値に戻ります。

Steam Deck：[docs/STEAMDECK.md](docs/STEAMDECK.md)（x86_64版。まだ未確認で、テストしてくれる人を募集中）

## 設定

RetroArchの **Quick Menu → コアオプション → MIDI Sync (FMS / LSDj)**。
初期値は、TrimUI Brick（Knulli、RetroArchの音声遅延は初期設定のまま）とM8で合わせたものです。環境が違えば調整が必要です。

| 項目 | 初期値 | 内容 |
|---|---|---|
| MIDI Sync (Restart) | Auto | ROMのタイトルかファイル名に「FMS」（GBA）「LSDJ」（ゲームボーイ）が入っているときだけ有効。`Always` / `Disabled` で強制 |
| Offset (follow MIDI) | 95 ms | ゲームが子のとき。`+` で早く、`-` で遅く。エミュレータの音の遅れを打ち消す。次にMIDIでスタートしたときから反映 |
| Clock Out Delay (lead MIDI) | 55 ms | ゲームが親のとき。MIDIクロックをこれだけ遅らせて送り、聞こえる音と揃える |
| Speaker Volume | unchanged | ゲーム中だけ本体の音量（ミキサーのMaster）をこの値にする。終了時に元に戻す |
| Real-Time Pacing (Restart) | オン | 実際に経った時間ぶんだけエミュレータを動かし、MIDIクロックとずれないようにする（60Hzの画面に合わせると約0.5%速くなってしまうため） |
| Audio → Output Rate (Restart) | 32768 Hz | RetroArchに渡すGBAの音のレート。65536 Hzにすると高音が残る |

細かい設定（任意）は `/userdata/system/configs/mgba-midisync.cfg` に書きます。約1秒ごとに読み直されます。
（`/userdata` がない環境、たとえばSteam Deckでは `~/.config/mgba-midisync.cfg`。ログも同じ場所です。Flatpak版RetroArchでは `~/.var/app/<アプリID>/config/`）
```
device=/dev/snd/midiC1D0   # 省略時：最初のUSB MIDI機器
clock_div=1                # F8 何個でゲームに1クロック渡すか
lead_ticks=0               # -24〜24：スタート時に足す／待つクロック数
in=1                       # 0：MIDIを受け取らない
out=1                      # 0：MIDIを送らない
log=1                      # /userdata/system/logs/mgba-midisync.log
pace_log=0                 # 1: リアルタイム調整の状況を1秒ごとに記録（不具合報告用）
```

## できないこと
- ゲームが子のとき、スタート直後の一拍目だけは音の遅れのぶん遅れます（スタートの瞬間は予測できないため）。
  ゲームを親にするか、最初の1小節を空けてください。
- LSDjの `MI.OUT` と `KEYBD` モードには対応していません。
- MIDI機器は1台、Linux（ALSA rawmidi）のみ。Knulliでの動作：TrimUI Brick（作者が確認）、Anbernic RG34XX（ユーザーから報告）。Batocera系なら同じインストーラーで動くはずです。muOSなど、ほかのファームウェアにはインストーラーがまだ対応していません。
- ステートセーブは標準のmGBAコアと互換がありません（普通のセーブは使えます）。

## クレジット
- [mGBA](https://mgba.io)（endriftほか、MPL-2.0）。このプロジェクトも同じライセンスです。
- [FMS](https://lo-bit.club/fms)（Lo-Bit Club、ess）。公式とは無関係です。FMSはitch.ioで購入してください。
- [LSDj](https://www.littlesounddj.com)（Johan Kotlinski）
- LSDjの同期の形式は [Arduinoboy](https://github.com/trash80/arduinoboy)（trash80）の実装を参考にしました。コードは含んでいません。

ROMは同梱していません。
