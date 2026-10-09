#!/bin/sh
# mgba-midisync for NextUI: FMS / LSDj synced to a USB MIDI device (e.g. a Dirtywave M8).
# The same script is used by FMS.pak and LSDJ.pak; the pak's folder name is its tag.

EMU_EXE=mgba_midisync
CORES_PATH=$(dirname "$0")

###############################

EMU_TAG=$(basename "$(dirname "$0")" .pak)
ROM="$1"
mkdir -p "$BIOS_PATH/$EMU_TAG"
mkdir -p "$SAVES_PATH/$EMU_TAG"
mkdir -p "$CHEATS_PATH/$EMU_TAG"
HOME="$USERDATA_PATH"
# mgba-midisync.cfg (optional) and mgba-midisync.log live here
export XDG_CONFIG_HOME="$USERDATA_PATH/mgba-midisync"
mkdir -p "$XDG_CONFIG_HOME"
cd "$HOME"

# NextUI sends all audio to a USB audio device as soon as one is plugged in, and
# the M8 is one: the game would go silent on the handheld (its sound ends up on
# the M8's USB input). NextUI does this by writing $HOME/.asoundrc, which both
# ALSA and minarch read from $HOME. Running minarch with a HOME of its own keeps
# the game on the handheld's speaker / headphones, also when the M8 is plugged
# in during play, and leaves NextUI's routing untouched for the menu. To hear
# the game through the M8 instead, create an empty file named "usb-audio" in
# this pak's folder.
if [ ! -f "$CORES_PATH/usb-audio" ]; then
	export HOME="$XDG_CONFIG_HOME/home"
	mkdir -p "$HOME"
	rm -f "$HOME/.asoundrc"
fi

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
