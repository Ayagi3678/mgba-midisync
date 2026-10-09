#!/bin/bash
# Build the libretro core for Linux handhelds and package it with the
# Knulli / Batocera installer.
#
#   scripts/build-aarch64.sh [version]           # aarch64 (e.g. TrimUI Brick), cross-compiled
#   ARCH=x86_64 scripts/build-aarch64.sh [version]  # x86_64 (e.g. Steam Deck, Batocera PCs)
#   ARCH=nextui scripts/build-aarch64.sh [version]  # NextUI paks (glibc 2.28 toolchain, set
#                                                   # NEXTUI_TOOLCHAIN, see cmake/aarch64-nextui.cmake)
#
# Needs: cmake, zip and, for aarch64, gcc-aarch64-linux-gnu, g++-aarch64-linux-gnu
# (Debian/Ubuntu package names).
# Output: dist/mgba-midisync-<version>-<arch>.zip (+ .sha256)
set -e
cd "$(dirname "$0")/.."
VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
ARCH="${ARCH:-aarch64}"
case "$ARCH" in
	aarch64)
		BUILD=build-aarch64
		TOOLS=aarch64-linux-gnu-
		ARCH_FLAGS=(-DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/aarch64-linux-gnu.cmake" -DCMAKE_C_FLAGS="-mcpu=cortex-a53")
		;;
	x86_64)
		BUILD=build-x86_64
		TOOLS=
		ARCH_FLAGS=()
		;;
	nextui)
		BUILD=build-nextui
		TOOLS="${NEXTUI_TOOLCHAIN:?set NEXTUI_TOOLCHAIN}/bin/aarch64-nextui-linux-gnu-"
		ARCH_FLAGS=(-DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/aarch64-nextui.cmake" -DCMAKE_C_FLAGS="-mcpu=cortex-a53")
		;;
	*) echo "unknown ARCH $ARCH (aarch64, x86_64 or nextui)"; exit 1 ;;
esac

cmake -S . -B "$BUILD" "${ARCH_FLAGS[@]}" \
	-DCMAKE_BUILD_TYPE=Release \
	-DHAVE_STRLCPY=0 \
	-DBUILD_LIBRETRO=ON -DSKIP_LIBRARY=ON \
	-DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF \
	-DUSE_FFMPEG=OFF -DUSE_LUA=OFF -DUSE_ZLIB=OFF -DUSE_PNG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF \
	-DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_EDITLINE=OFF -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF \
	-DUSE_FREETYPE=OFF -DENABLE_SCRIPTING=OFF -DUSE_JSON_C=OFF
cmake --build "$BUILD" --target mgba_libretro -j"$(nproc)"

# The core must stay loadable on older handheld firmware: glibc 2.35+ (Knulli),
# 2.28 for NextUI (stock TrimUI system)
NEWEST="$(${TOOLS}objdump -T "$BUILD/mgba_libretro.so" | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -n 1)"
echo "newest glibc symbol: $NEWEST"
if [ "$ARCH" = nextui ]; then
	case "$NEWEST" in
		GLIBC_2.1[0-9]*|GLIBC_2.2[0-8]) ;;
		*) echo "error: needs $NEWEST, expected <= GLIBC_2.28"; exit 1 ;;
	esac
else
	case "$NEWEST" in
		GLIBC_2.1[0-9]*|GLIBC_2.2[0-9]*|GLIBC_2.3[0-5]) ;;
		*) echo "error: needs $NEWEST, expected <= GLIBC_2.35"; exit 1 ;;
	esac
fi

PKG="dist/mgba-midisync-$VERSION-$ARCH"
rm -rf "$PKG" "$PKG.zip"
mkdir -p "$PKG"
if [ "$ARCH" = nextui ]; then
	# Unzip onto the SD card root: Emus/<platform>/{FMS,LSDJ}.pak and the matching Roms folders
	for platform in tg5040 tg5050 h700; do
		for tag in FMS LSDJ; do
			pak="$PKG/Emus/$platform/$tag.pak"
			mkdir -p "$pak"
			${TOOLS}strip -o "$pak/mgba_midisync_libretro.so" "$BUILD/mgba_libretro.so"
			cp nextui/launch.sh nextui/default.cfg "$pak/"
			chmod +x "$pak/launch.sh"
		done
	done
	mkdir -p "$PKG/Roms/FMS (FMS)" "$PKG/Roms/LSDj (LSDJ)"
	cp docs/NEXTUI.md README.md README_JA.md LICENSE "$PKG/"
else
	${TOOLS}strip -o "$PKG/mgba_midisync_libretro.so" "$BUILD/mgba_libretro.so"
	cp knulli/install.sh knulli/uninstall.sh README.md README_JA.md LICENSE "$PKG/"
	[ "$ARCH" = x86_64 ] && cp docs/STEAMDECK.md "$PKG/"
	chmod +x "$PKG"/*.sh
fi
(cd dist && zip -qr "$(basename "$PKG").zip" "$(basename "$PKG")")
(cd dist && sha256sum "$(basename "$PKG").zip" > "$(basename "$PKG").zip.sha256")
echo "built $PKG.zip"
cat "$PKG.zip.sha256"
