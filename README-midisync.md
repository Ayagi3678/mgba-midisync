# mgba-midisync

mGBA libretro core with a MIDI clock bridge for **FMS** (Lo-Bit Club, GBA) and **LSDj** (Game Boy)
on Linux handhelds (tested on TrimUI Brick + Knulli, with a Dirtywave M8 over USB).

FMS "GBA to GBA" sync sends one byte per NORMAL8 link transfer: `02` start, `01` clock (24 PPQN), `03` stop.
This core maps them to and from raw MIDI on `/dev/snd/midiC*D0`:

| Direction | FMS Sync | MIDI → link | link → MIDI |
|---|---|---|---|
| M8 leads | In  | `F8`→`01`, `FA`/`FB`→`02`, `FC`→`03` | — |
| FMS leads | Out | — | `01`→`F8`, `02`→`FA`, `03`→`FC` |

### LSDj (Game Boy)

Same wire protocol as Arduinoboy:

| Direction | LSDj SYNC | |
|---|---|---|
| MIDI leads | `MIDI` | between `FA` and `FC`, each `F8` becomes one 8-bit transfer on LSDj's external clock. Press START on LSDj first; it starts on the next clock. |
| LSDj leads | `LSDJ` | each byte LSDj sends is one `F8` (the first one is preceded by `FA`); when bytes stop for a few ticks, `FC` is sent. |

`MI.OUT` / `KEYBD` modes are not supported. On Knulli, pick the **mgba** core for LSDj (Game Boy games often default to another core).

### Code layout (`src/platform/libretro/`)
- `midi-host.c/.h` – shared, console-agnostic: rawmidi device, `.cfg`, log, timed MIDI output thread,
  clock tempo estimate, offset → early ticks + delay, real-time slicing, emulated ↔ real time mapping
- `sync-fms.c/.h` – FMS protocol on the GBA link port (`GBASIODriver`)
- `sync-lsdj.c/.h` – LSDj protocol on the Game Boy link port (`GBSIODriver`)
- `gba-audio-rate.c` – GBA audio to a fixed 65536 Hz with exact integer ratios (x2, 1, /2, /4)
- `libretro.c` – core options, auto-detection, attaching the driver; outputs GBA audio at a fixed
  65536 Hz instead of calling `SET_SYSTEM_AV_INFO` (RetroArch on mali-fbdev dies re-creating the EGL
  surface when FMS changes SOUNDBIAS)
- `glibc-compat.c` – keeps the .so loadable on glibc < 2.38

Protocol-specific behaviour stays in the `sync-*` drivers: e.g. FMS counts a burst of ticks as one,
so early ticks are spread out one per incoming clock.

## Settings

Defaults were tuned on a TrimUI Brick (Knulli, default RetroArch audio latency) with a Dirtywave M8 over USB; other devices will need different values.

In RetroArch: **Quick Menu → Core Options → MIDI Sync (FMS)**

| Option | Default | |
|---|---|---|
| MIDI Sync (Restart) | Auto (FMS / LSDj only) | `Auto` enables the bridge only when the ROM title or file name contains "FMS" (GBA) or "LSDJ" (Game Boy); every other game runs as plain mGBA. `Always` / `Disabled` force it. |
| Offset (follow MIDI) | 95 ms | The game follows MIDI clock: `+` plays earlier, `-` later. Cancels the emulator's audio latency. Applies from the next MIDI start. The first beat after start can't be pulled earlier. |
| Clock Out Delay (lead MIDI) | 55 ms | The game leads: delay before each MIDI clock is sent. Set it to the audio latency so external gear lines up from the first beat. |
| Real-Time Pacing | On | Runs emulation in real-time slices instead of whole frames, so emulated time equals real time (RetroArch's 60 Hz vsync runs games ~0.5% fast, so they would drift behind MIDI clock). Audio stays continuous; the screen shows the last complete frame. |

Advanced (optional) `/userdata/system/configs/mgba-midisync.cfg`, re-read about once a second:
```
device=/dev/snd/midiC1D0   # default: first non-card-0 rawmidi device
clock_div=1                # one tick to the game per N incoming F8
lead_ticks=0               # -24..24 extra / withheld ticks at start
in=1
out=1
log=1                      # /userdata/system/logs/mgba-midisync.log
```

## Build (aarch64, e.g. TrimUI Brick)
```
cmake -B build-a64 -DCMAKE_TOOLCHAIN_FILE=<aarch64 toolchain> -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS=-mcpu=cortex-a53 -DHAVE_STRLCPY=0 -DBUILD_LIBRETRO=ON -DSKIP_LIBRARY=ON \
  -DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF \
  -DUSE_FFMPEG=OFF -DUSE_LUA=OFF -DUSE_ZLIB=OFF -DUSE_PNG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF \
  -DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_EDITLINE=OFF -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF \
  -DENABLE_SCRIPTING=OFF -DUSE_JSON_C=OFF
cmake --build build-a64 --target mgba_libretro
```

## Install on Knulli
```
scp mgba_libretro.so root@<brick>:/userdata/cores/
mount -o bind /userdata/cores/mgba_libretro.so /usr/lib/libretro/mgba_libretro.so
```
Persist via `/userdata/system/custom.sh` (runs with `start` at boot).

Based on mGBA (https://github.com/mgba-emu/mgba), MPL-2.0.
