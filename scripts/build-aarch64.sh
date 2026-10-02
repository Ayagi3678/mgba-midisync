#!/bin/bash
# Build the libretro core for Linux handhelds and package it with the
# Knulli / Batocera installer.
#
#   scripts/build-aarch64.sh [version]           # aarch64 (e.g. TrimUI Brick), cross-compiled
#   ARCH=x86_64 scripts/build-aarch64.sh [version]  # x86_64 (e.g. Steam Deck, Batocera PCs)
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
	*) echo "unknown ARCH $ARCH (aarch64 or x86_64)"; exit 1 ;;
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

# The core must stay loadable on older handheld firmware (glibc 2.35+)
NEWEST="$(${TOOLS}objdump -T "$BUILD/mgba_libretro.so" | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -n 1)"
echo "newest glibc symbol: $NEWEST"
case "$NEWEST" in
	GLIBC_2.1[0-9]*|GLIBC_2.2[0-9]*|GLIBC_2.3[0-5]) ;;
	*) echo "error: needs $NEWEST, expected <= GLIBC_2.35"; exit 1 ;;
esac

PKG="dist/mgba-midisync-$VERSION-$ARCH"
rm -rf "$PKG" "$PKG.zip"
mkdir -p "$PKG"
${TOOLS}strip -o "$PKG/mgba_midisync_libretro.so" "$BUILD/mgba_libretro.so"
cp knulli/install.sh knulli/uninstall.sh README.md README_JA.md LICENSE "$PKG/"
chmod +x "$PKG"/*.sh
(cd dist && zip -qr "$(basename "$PKG").zip" "$(basename "$PKG")")
(cd dist && sha256sum "$(basename "$PKG").zip" > "$(basename "$PKG").zip.sha256")
echo "built $PKG.zip"
cat "$PKG.zip.sha256"
