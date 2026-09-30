# mgba-midisync

mGBA libretro core with a MIDI clock bridge for **FMS** (Lo-Bit Club) on Linux handhelds
(tested on TrimUI Brick + Knulli, with a Dirtywave M8 over USB).

FMS "GBA to GBA" sync sends one byte per NORMAL8 link transfer: `02` start, `01` clock (24 PPQN), `03` stop.
This core maps them to and from raw MIDI on `/dev/snd/midiC*D0`:

| Direction | FMS Sync | MIDI → link | link → MIDI |
|---|---|---|---|
| M8 leads | In  | `F8`→`01`, `FA`/`FB`→`02`, `FC`→`03` | — |
| FMS leads | Out | — | `01`→`F8`, `02`→`FA`, `03`→`FC` |

Changes (all in `src/platform/libretro/`):
- `midisync.c/.h` – the link-port driver, timed MIDI output thread, real-time frame pacing
- `libretro.c` – attaches the driver; resamples GBA audio to a fixed 65536 Hz instead of calling
  `SET_SYSTEM_AV_INFO` (RetroArch on mali-fbdev dies re-creating the EGL surface when FMS changes SOUNDBIAS)
- `glibc-compat.c` – keeps the .so loadable on glibc < 2.38

## Config: `/userdata/system/configs/mgba-midisync.cfg`
```
device=/dev/snd/midiC1D0   # default: first non-card-0 rawmidi device
offset_ms=0                # fine trim: + FMS earlier, - later; re-read ~1/s, applies at next start
clock_div=1                # one 01 per N incoming F8
lead_ticks=0               # -24..24: + runs FMS N clocks ahead, - holds its start back N clocks
pace=1                     # keep emulation at real time by occasionally skipping a frame
out_delay_ms=20            # MIDI out sent on a real-time schedule this long after generation
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
