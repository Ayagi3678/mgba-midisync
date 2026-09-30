# mgba-midisync

Sync **FMS** (Lo-Bit Club, GBA) and **LSDj** (Game Boy) with USB MIDI gear on a Linux handheld —
no link cable, no Arduinoboy.

This is a fork of the [mGBA](https://github.com/mgba-emu/mgba) libretro core. It connects the emulated
link port to a USB MIDI device (`/dev/snd/midiC*D0`) and speaks the same protocol a real link-cable
setup would. Developed and tested on a **TrimUI Brick running Knulli** with a **Dirtywave M8** plugged
into the Brick's USB-C host port.

[日本語の説明はこちら (README_JA.md)](README_JA.md) · [mGBA upstream](https://github.com/mgba-emu/mgba)

| | follow MIDI clock | lead MIDI clock |
|---|---|---|
| **FMS** | Sync `In`, `GBA to GBA` | Sync `Out`, `GBA to GBA` |
| **LSDj** | SYNC `MIDI`, press START and wait | SYNC `LSDJ` |

Other games are not affected: the installer adds a separate core and two entries in **Ports**; the
GBA / Game Boy lists keep using Knulli's stock core.

## Quick start (Knulli)

1. Download `mgba-midisync-<version>-aarch64.zip` from
   [Releases](https://github.com/Ayagi3678/mgba-midisync/releases) and unzip it.
2. Copy the folder to the handheld (WinSCP / SFTP: user `root`, password `linux`), e.g. to
   `/userdata/system/mgba-midisync`.
3. Over SSH:
   ```
   bash /userdata/system/mgba-midisync/install.sh
   ```
   It finds your FMS ROM in `/userdata/roms/gba` and LSDj in `/userdata/roms/gb`
   (file names containing "fms" / "lsdj"), or pass them:
   `install.sh --fms /path/FMS.gba --lsdj /path/lsdj.gb`.
4. Plug the MIDI device into the USB host port and start **FMS (MIDI Sync)** or
   **LSDj (MIDI Sync)** from **Ports**.

Uninstall: `bash uninstall.sh` (saves are left alone).

### Dirtywave M8
- M8 leads: in the M8's MIDI settings, send clock and transport over USB.
- FMS / LSDj leads: let the M8 receive clock and transport over USB.
- Knulli may switch its audio output to the M8 once it's plugged in (volume gets low, the
  volume item disappears from the menu). Pin the output to the built-in speaker:
  ```
  batocera-settings-set audio.device alsa_output._sys_devices_platform_soc_sndcodec_sound_card0.stereo-fallback
  batocera-audio set alsa_output._sys_devices_platform_soc_sndcodec_sound_card0.stereo-fallback
  ```
  (names from `batocera-audio list`). If it's still quiet, check `amixer -c 0 sget Master`.

## Settings

RetroArch **Quick Menu → Core Options → MIDI Sync (FMS / LSDj)**. The defaults were tuned on a
TrimUI Brick (Knulli, default RetroArch audio latency) with an M8; other setups need other values.

| Option | Default | |
|---|---|---|
| MIDI Sync (Restart) | Auto | Bridge on when the ROM title or file name contains "FMS" (GBA) or "LSDJ" (Game Boy). `Always` / `Disabled` force it. |
| Offset (follow MIDI) | 95 ms | The game follows MIDI: `+` plays earlier, `-` later, to cancel the emulator's audio latency. Applies from the next MIDI start. |
| Clock Out Delay (lead MIDI) | 55 ms | The game leads: MIDI clock goes out this much later, so gear lines up with what you hear. |
| Real-Time Pacing (Restart) | On | Emulates exactly the real time that passed on each call, so the game doesn't drift against MIDI clock (60 Hz vsync would run it ~0.5% fast). |
| Audio → Output Rate (Restart) | 32768 Hz | GBA audio rate handed to RetroArch. 65536 Hz keeps more treble. |

Advanced settings (optional) in `/userdata/system/configs/mgba-midisync.cfg`, re-read about once a second:
```
device=/dev/snd/midiC1D0   # default: first rawmidi device that isn't card 0
clock_div=1                # one tick to the game per N incoming F8
lead_ticks=0               # -24..24 extra / withheld ticks at start
in=1                       # 0: ignore incoming MIDI
out=1                      # 0: don't send MIDI
log=1                      # /userdata/system/logs/mgba-midisync.log
```

## Known limits
- When the game follows MIDI, the very first beat after start is late by the audio latency
  (a start can't be predicted). Let the game lead, or leave the first bar empty.
- LSDj `MI.OUT` and `KEYBD` modes are not supported.
- One MIDI device, Linux only (ALSA rawmidi). Tested on Knulli (TrimUI Brick); other
  Batocera-based firmware should work with the same installer.
- Save states aren't compatible with the stock mGBA core (regular saves are).

## How it works

**FMS** uses 8-bit normal-mode transfers: `02` start, `01` clock (24 PPQN), `03` stop.

| FMS Sync | MIDI → link | link → MIDI |
|---|---|---|
| In (external clock) | `F8`→`01`, `FA`/`FB`→`02`, `FC`→`03` | — |
| Out (internal clock) | — | `01`→`F8`, `02`→`FA`, `03`→`FC` |

FMS counts a burst of ticks as one, so ticks sent early (for the offset) are spread out, one per incoming clock.

**LSDj** follows the Arduinoboy protocol: when following, every `F8` between `FA` and `FC` becomes one
8-bit transfer on LSDj's external clock; when leading, every byte LSDj sends is an `F8` (`FA` before the
first, `FC` after a few silent ticks). MIDI echoed back while LSDj leads is ignored.

Timing: emulation runs in real-time slices; MIDI out is sent by a thread at the real time each byte
belongs to (no per-frame clumps); GBA audio goes out at a fixed rate with exact integer ratios, because
asking RetroArch to reinitialise audio (`SET_SYSTEM_AV_INFO`) when FMS changes its output rate crashes
RetroArch on mali-fbdev.

### Code (`src/platform/libretro/`)
- `midi-host.c/.h` – console-agnostic: rawmidi device, config, log, timed MIDI output, tempo
  estimate, offset math, real-time slicing
- `sync-fms.c/.h` – FMS on the GBA link port (`GBASIODriver`)
- `sync-lsdj.c/.h` – LSDj on the Game Boy link port (`GBSIODriver`)
- `gba-audio-rate.c/.h` – fixed-rate GBA audio
- `libretro.c`, `libretro_core_options.h` – options, auto-detection, glue
- `glibc-compat.c` – keeps the .so loadable on glibc 2.35+
- `knulli/` – installer and uninstaller

## Building
```
sudo apt install cmake gcc-aarch64-linux-gnu g++-aarch64-linux-gnu zip
scripts/build-aarch64.sh            # -> dist/mgba-midisync-<version>-aarch64.zip
```
GitHub Actions builds the same package for every push; publishing a GitHub release (tag `vX.Y.Z`) builds it again and attaches the zip to the release.

## Credits
- [mGBA](https://mgba.io) by endrift and contributors (MPL-2.0) — this project keeps the same license.
- [FMS](https://lo-bit.club/fms) by Lo-Bit Club (ess). Not affiliated; buy FMS on itch.io.
- [LSDj](https://www.littlesounddj.com) by Johan Kotlinski.
- LSDj sync protocol as implemented by [Arduinoboy](https://github.com/trash80/arduinoboy) (trash80);
  no Arduinoboy code is included.

No ROMs are included.
