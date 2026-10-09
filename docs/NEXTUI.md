# mgba-midisync on NextUI

FMS and LSDj synced to a USB MIDI device (e.g. a Dirtywave M8) on [NextUI](https://github.com/LoveRetro/NextUI).
Same core as the Knulli version, built against NextUI's toolchain (the stock TrimUI system has glibc 2.28)
and packaged as two emulator paks: **FMS.pak** and **LSDJ.pak**.

Status: tested on a TrimUI Brick. The `tg5050` (Smart Pro S) and `h700` (Anbernic) paks hold the
same core but haven't been tried yet. Reports are welcome.

## Install
1. Download `mgba-midisync-<version>-nextui.zip` from
   [Releases](https://github.com/Ayagi3678/mgba-midisync/releases) and unzip it **onto the root of the
   SD card**. It only adds `Emus/<platform>/FMS.pak`, `Emus/<platform>/LSDJ.pak` and two empty ROM
   folders. (macOS: unzip it elsewhere and copy the folders with **Merge**, not Replace, so your other
   ROMs and paks stay.)
2. Put your ROMs in the new folders: FMS in `Roms/FMS (FMS)/`, LSDj in `Roms/LSDj (LSDJ)/`.

FMS and LSDj then show up as their own entries in NextUI's menu. Plug in the M8 and start the game.

The `(FMS)` / `(LSDJ)` tag at the end of the folder name is what makes NextUI use these paks; the part
before it can be renamed. Saves go to `Saves/FMS/` and `Saves/LSDJ/`, separate from the stock GBA / GB
paks: to keep an existing LSDj save, copy its `.sav` there (same file name as the ROM).

No ROMs are included. You need your own copies of FMS and LSDj.

## Use
| | follow MIDI clock | lead MIDI clock |
|---|---|---|
| **FMS** | Sync `In`, `GBA to GBA` | Sync `Out`, `GBA to GBA` |
| **LSDj** | SYNC `MIDI`, press START and wait | SYNC `LSDJ` |

Timing settings: in-game **MENU → Options → Emulator → MIDI Sync**.
- **Offset** (game follows): `+` plays the game earlier.
- **Clock Out Delay** (game leads): delays the clock sent to the M8.

The defaults (95 ms / 55 ms) were tuned on Knulli. NextUI's audio path is different, so you'll
probably want other values. Save them with **Save Changes → Save for Console**.

The paks start with **CPU Speed: Performance** (Options → Frontend). With a CPU that slows down in the
menu, the audio is distorted and the tempo drops for a few seconds after you leave it.

## Log and settings
- MIDI log: `.userdata/<platform>/mgba-midisync/mgba-midisync.log`. The first lines show the MIDI device
  that was picked (`opened /dev/snd/midiC1D0`).
- Optional settings: `mgba-midisync.cfg` in the same folder, e.g. `device=/dev/snd/midiC1D0`, `pace_log=1`.
- NextUI's own log for the game: `.userdata/<platform>/logs/FMS.txt` / `LSDJ.txt`.

## Known issues
- **Where the sound goes.** NextUI sends all audio to a USB audio device as soon as one is plugged
  in, and the M8 is one, so the game would go silent on the handheld. The paks keep the game's sound
  on the handheld's speaker / headphones, whether the M8 is plugged in before the game starts or
  during play. NextUI's own routing (menu, other paks) is left alone. Sending the game's sound into
  the M8's USB input isn't supported yet.
- **Volume while the M8 is connected.** The volume buttons change the M8's volume, not the
  speaker's; the speaker stays at the level it had before the M8 was plugged in.
- **No sound in any game after plugging / unplugging the M8 a few times** (stock paks too): NextUI's
  audio routing got out of step. Restart the handheld.
- **Speaker Volume** (a core option on Knulli) is hidden here: NextUI manages the volume itself.
