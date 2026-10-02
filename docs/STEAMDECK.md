# Testing on a Steam Deck

The core has only been tested on a TrimUI Brick running Knulli so far. These notes are for trying the
x86_64 build on a Steam Deck (SteamOS) with a Dirtywave M8 or another USB MIDI device. Reports of what
works and what doesn't are very welcome, either in the Discord thread or in
[GitHub issues](https://github.com/Ayagi3678/mgba-midisync/issues).

## What you need
- `mgba-midisync-<version>-x86_64.zip` from [Releases](https://github.com/Ayagi3678/mgba-midisync/releases)
  (the `aarch64` zip will not load on the Deck)
- RetroArch: the Steam version, or the Flatpak (also what EmuDeck installs)
- Your own copy of FMS and/or LSDj. No ROMs are included.
- The M8 (or other USB MIDI gear) connected to the Deck's USB-C port, directly or through a hub

`install.sh` and `uninstall.sh` in the zip are for Knulli / Batocera only. Don't run them on the Deck.

## Install
Switch to Desktop Mode, unzip the release and copy `mgba_midisync_libretro.so` into RetroArch's
`cores` folder:

| RetroArch | cores folder |
|---|---|
| Steam | `~/.local/share/Steam/steamapps/common/RetroArch/cores/` |
| Flatpak / EmuDeck | `~/.var/app/org.libretro.RetroArch/config/retroarch/cores/` |

The ROM file name must contain `FMS` (GBA) or `LSDJ` (Game Boy), otherwise the sync stays off. Or set
**Core Options → MIDI Sync (Restart)** to `Always`.

## Run
1. Plug in the M8 before starting RetroArch.
2. **Load Core → mgba_midisync**, then **Load Content** → your FMS or LSDj ROM.
3. On the M8, enable sending (M8 leads) or receiving (game leads) clock and transport over USB.
4. Try both directions:

| | game follows the M8 | game leads the M8 |
|---|---|---|
| FMS | Sync `In`, `GBA to GBA`, press play on the M8 | Sync `Out`, `GBA to GBA`, press play in FMS |
| LSDj | SYNC `MIDI`, press START, then play on the M8 | SYNC `LSDJ`, press START in LSDj |

Timing settings are in **Quick Menu → Core Options → MIDI Sync (FMS / LSDj)**:
- **Offset** (game follows): `+` plays the game earlier.
- **Clock Out Delay** (game leads): delays the clock sent to the M8.

The defaults were tuned on the Brick, so the Deck will probably need different values.

## Log and settings
The log is rewritten every time the game starts:

| RetroArch | log |
|---|---|
| Steam | `~/.config/mgba-midisync.log` |
| Flatpak / EmuDeck | `~/.var/app/org.libretro.RetroArch/config/mgba-midisync.log` |

The first lines show the MIDI device that was picked, e.g. `device=(auto)` followed by
`opened /dev/snd/midiC1D0`. Optional settings go in `mgba-midisync.cfg` in the same folder:

```
device=/dev/snd/midiC1D0   # pick a MIDI port by hand (see: ls /dev/snd/midi*)
pace_log=1                 # timing stats once a second
```

## Useful things to report
- Steam or Flatpak RetroArch, and its version
- Which directions work (FMS In / Out, LSDj MIDI / LSDJ)
- How stable the tempo feels over a few minutes, and the Offset / Clock Out Delay you ended up with
- Audio crackles or tempo changes, especially after opening the RetroArch menu
- The log, or at least its first lines and any lines with `error`

## Known issues and unknowns
- **Sound comes out of the M8 instead of the Deck.** The M8 is also a USB audio device, and SteamOS may
  switch its output to it when you plug it in. Pick the Deck's speakers (or headphones) again in
  Steam's sound settings, or in Desktop Mode under Settings > Audio. The same thing happens on Knulli.
- **Flatpak sandbox:** the Flathub RetroArch has the `pulseaudio` socket and `--device=all`, which
  include the MIDI devices in `/dev/snd`, so it should be able to open the M8. If you restricted it
  with Flatseal, give those permissions back. Note that the Flatpak's `/tmp` is private. That's why the
  log is in the app's config folder.
- **Steam RetroArch:** it runs inside the Steam Linux Runtime container. That container is known to
  limit file access (e.g. to SD cards). Whether it passes `/dev/snd/midi*` through hasn't been checked
  yet. If the log shows no device or a permission error, please try the Flatpak and report it.
- **CPU clock:** on the Brick, the CPU dropping its clock in the menu caused distorted audio and a
  slower tempo for a few seconds afterwards. Knulli's launcher sets the `performance` governor to
  avoid that. It is not known yet whether the Deck shows the same thing. If it does, `pace_log=1`
  makes it visible in the log.
