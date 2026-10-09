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
	# With the M8 plugged in, opening the built-in card directly (hw / plughw /
	# default) fails ("Couldn't set hardware audio parameters"); only the
	# system's own mixing chain (softvol -> dmix, "Playback" in the TrimUI
	# /etc/asound.conf) works. So point the default PCM at that chain when the
	# system has it, and otherwise just pick the card (this ALSA only takes a
	# card number there, not its name).
	# The default PCM still resolved to the M8 even with this .asoundrc (the
	# system's own .asoundrc gets read regardless of HOME), so SDL is also told
	# to open that chain by name (minarch only changes AUDIODEV when the
	# .asoundrc it watches changes, and this one doesn't).
	if grep -qs '^pcm\.Playback[[:space:]]' /etc/asound.conf; then
		export AUDIODEV=Playback
		KEEP_SPEAKER=1
		cat > "$HOME/.asoundrc" <<ASOUND
pcm.!default {
	type plug
	slave.pcm "Playback"
}
ctl.!default {
	type hw
	card $CARD
}
ASOUND
	else
		cat > "$HOME/.asoundrc" <<ASOUND
defaults.pcm.card $CARD
defaults.ctl.card $CARD
ASOUND
	fi
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
			[ -f "$f" ] && { echo "== $f (top)"; sed -n '1,60p' "$f"; }
			[ -f "$f" ] && { echo "== $f (defaults / default pcm)"; grep -nE "^ *defaults\.(pcm|ctl)|pcm\.(!)?default|cards\.pcm\.default|@hooks|func load|files \[" "$f"; }
		done
		ls -la /usr/share/alsa /usr/share/alsa/cards /usr/share/alsa/pcm /etc/alsa 2>&1 | head -60
		[ -f /usr/share/alsa/pcm/default.conf ] && { echo "== pcm/default.conf"; cat /usr/share/alsa/pcm/default.conf; }
		echo "== shared memory / semaphores (dmix)"; ipcs 2>&1
		echo "== fuser"; for d in /dev/snd/pcmC*p; do echo "$d: $(fuser "$d" 2>&1)"; done
		echo "== playback tests (0.5 s of silence each, same format as the game)"
		if command -v aplay >/dev/null; then
			for pcm in default Playback PlaybackDmix "plughw:$CARD,0" "hw:$CARD,0" sysdefault; do
				out=$(dd if=/dev/zero bs=65536 count=1 2>/dev/null | aplay -q -D "$pcm" -f S16_LE -r 32768 -c 2 - 2>&1)
				echo "[$pcm] exit=$? $out"
			done
			echo "-- with the system HOME ($USERDATA_PATH)"
			out=$(dd if=/dev/zero bs=65536 count=1 2>/dev/null | HOME="$USERDATA_PATH" aplay -q -D default -f S16_LE -r 32768 -c 2 - 2>&1)
			echo "[default, system HOME] exit=$? $out"
		fi
		echo "== aplay"; command -v aplay && { aplay -l 2>&1; aplay -L 2>&1 | head -40; }
	} > "$DIAG" 2>&1
	( sleep 6
	  echo "== while playing: open PCM streams" >> "$DIAG"
	  for f in /proc/asound/card*/pcm*p/sub*/hw_params; do echo "$f:"; cat "$f"; done >> "$DIAG" 2>&1
	  { echo "== speaker mute: $(cat /sys/class/speaker/mute 2>&1)"
	    amixer -c "$CARD" sget 'DAC volume'; amixer -c "$CARD" sget 'digital volume'; } >> "$DIAG" 2>&1 ) &
fi

# minarch mutes the speaker while it starts up and unmutes it when it sets the
# volume, but while NextUI routes sound to a USB device (the M8) it sets the
# volume on that device only, so the speaker stays muted (and the DAC volume can
# be left at 0). Keep the speaker on while the game runs and the M8 is plugged
# in. The volume buttons then still change the M8's volume, not the speaker's.
if [ -n "$KEEP_SPEAKER" ]; then
	(
		LAUNCHER=$$
		while kill -0 "$LAUNCHER" 2>/dev/null; do
			if grep -qs 'type hw' "$USERDATA_PATH/.asoundrc"; then
				[ "$(cat /sys/class/speaker/mute 2>/dev/null)" = 1 ] && echo 0 > /sys/class/speaker/mute
				amixer -c "$CARD" sget 'DAC volume' 2>/dev/null | grep -q 'Front Left: 0 ' &&
					amixer -q -c "$CARD" sset 'DAC volume' 160
			fi
			sleep 1
		done
	) &
fi

minarch.elf "$CORES_PATH/${EMU_EXE}_libretro.so" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1
