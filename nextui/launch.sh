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
# the M8's USB input). While the game runs, keep the sound on the handheld's
# speaker / headphones, also when the M8 is plugged in during play, and put
# NextUI's routing back afterwards. To hear the game through the M8 instead,
# create an empty file named "usb-audio" in this pak's folder.
ASOUNDRC="$USERDATA_PATH/.asoundrc"
ASOUNDRC_SAVED="$USERDATA_PATH/.asoundrc.mgba-midisync"
WATCHER=""

keep_speaker() { # move NextUI's USB routing aside whenever it shows up
	while :; do
		if [ -f "$ASOUNDRC" ] && grep -q "type hw" "$ASOUNDRC"; then
			mv -f "$ASOUNDRC" "$ASOUNDRC_SAVED"
		fi
		sleep 0.2 2>/dev/null || sleep 1
	done
}

restore_routing() {
	[ -n "$WATCHER" ] && kill "$WATCHER" 2>/dev/null
	[ -f "$ASOUNDRC_SAVED" ] || return
	# only if that USB card is still there (NextUI deletes the file on unplug)
	card=$(sed -n 's/^ *card \([0-9][0-9]*\).*/\1/p' "$ASOUNDRC_SAVED" | head -n 1)
	if [ ! -f "$ASOUNDRC" ] && [ -n "$card" ] && [ -e "/proc/asound/card$card/usbid" ]; then
		mv -f "$ASOUNDRC_SAVED" "$ASOUNDRC"
	else
		rm -f "$ASOUNDRC_SAVED"
	fi
}

trap restore_routing EXIT
trap 'exit 143' TERM INT HUP
if [ ! -f "$CORES_PATH/usb-audio" ]; then
	keep_speaker &
	WATCHER=$!
	sleep 0.3 2>/dev/null || sleep 1   # let it move an existing routing before the game opens audio
fi

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
