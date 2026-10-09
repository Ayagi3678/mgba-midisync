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
# the M8's USB input). Keep it on the handheld's speaker / headphones while the
# game runs and put NextUI's routing back afterwards. To hear the game through
# the M8 instead, create an empty file named "usb-audio" in this pak's folder.
ASOUNDRC="$USERDATA_PATH/.asoundrc"
ASOUNDRC_SAVED="$USERDATA_PATH/.asoundrc.mgba-midisync"
if [ ! -f "$CORES_PATH/usb-audio" ] && [ -f "$ASOUNDRC" ] && grep -q "type hw" "$ASOUNDRC"; then
	mv -f "$ASOUNDRC" "$ASOUNDRC_SAVED"
fi

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1

# Restore NextUI's USB routing unless it was rewritten meanwhile (device replugged)
if [ -f "$ASOUNDRC_SAVED" ]; then
	if [ -f "$ASOUNDRC" ]; then
		rm -f "$ASOUNDRC_SAVED"
	else
		mv -f "$ASOUNDRC_SAVED" "$ASOUNDRC"
	fi
fi
