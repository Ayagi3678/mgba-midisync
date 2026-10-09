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
# ALSA and minarch read from $HOME. minarch gets a HOME of its own whose
# .asoundrc makes the built-in sound card the default, so the game stays on the handheld's
# speaker / headphones, also when the M8 is plugged in during play, whatever
# card numbers the devices got, and NextUI's routing is left alone for the menu.
# To hear the game through the M8 instead, create an empty file named
# "usb-audio" in this pak's folder.
builtin_card() { # number of the first sound card that isn't USB
	for dir in /proc/asound/card[0-9]*; do
		[ -d "$dir" ] || continue
		[ -e "$dir/usbid" ] && continue
		echo "${dir##*/card}"
		return
	done
}
CARD=$(builtin_card)
if [ ! -f "$CORES_PATH/usb-audio" ] && [ -n "$CARD" ]; then
	export HOME="$XDG_CONFIG_HOME/home"
	mkdir -p "$HOME"
	# Only pick the card; keep the system's own default PCM chain (format /
	# rate conversion, sharing) that the stock paks use. Opening hw:<card>
	# through a plain plug failed now and then ("Couldn't set hardware audio
	# parameters"). This ALSA only takes a card number here, not its name.
	cat > "$HOME/.asoundrc" <<ASOUND
defaults.pcm.card $CARD
defaults.ctl.card $CARD
ASOUND
fi

# Diagnostics: with an empty file named "alsa-diag" in the mgba-midisync
# folder, record the system's ALSA setup and which sound device the game holds.
DIAG="$XDG_CONFIG_HOME/alsa-diag.txt"
if [ -f "$XDG_CONFIG_HOME/alsa-diag" ]; then
	{
		echo "== cards"; cat /proc/asound/cards
		for d in /proc/asound/card[0-9]*; do echo "$d id=$(cat "$d/id" 2>/dev/null) usbid=$(cat "$d/usbid" 2>/dev/null)"; done
		echo "== pak HOME=$HOME CARD=$CARD"; cat "$HOME/.asoundrc" 2>/dev/null
		echo "== env"; env | grep -iE "alsa|audiodev|sdl_audio" 
		for f in /etc/asound.conf /etc/alsa/asound.conf "$USERDATA_PATH/.asoundrc"; do
			[ -f "$f" ] && { echo "== $f"; cat "$f"; }
		done
		for f in /usr/share/alsa/alsa.conf /etc/alsa/alsa.conf; do
			[ -f "$f" ] && { echo "== $f (defaults / default pcm)"; grep -nE "^ *defaults\.(pcm|ctl)|pcm\.(!)?default|cards\.pcm\.default|@hooks|func load|files \[" "$f"; }
		done
		ls -la /usr/share/alsa /usr/share/alsa/cards /usr/share/alsa/pcm /etc/alsa 2>&1 | head -60
		[ -f /usr/share/alsa/pcm/default.conf ] && { echo "== pcm/default.conf"; cat /usr/share/alsa/pcm/default.conf; }
		echo "== aplay"; command -v aplay && { aplay -l 2>&1; aplay -L 2>&1 | head -40; }
	} > "$DIAG" 2>&1
	( sleep 6
	  echo "== while playing: open PCM streams" >> "$DIAG"
	  for f in /proc/asound/card*/pcm*p/sub*/hw_params; do echo "$f:"; cat "$f"; done >> "$DIAG" 2>&1 ) &
fi

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
