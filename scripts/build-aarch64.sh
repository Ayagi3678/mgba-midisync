#!/bin/bash
# Cross-build the libretro core for aarch64 Linux handhelds (e.g. TrimUI Brick)
# and package it with the Knulli installer.
#
# Needs: cmake, gcc-aarch64-linux-gnu (Debian/Ubuntu package names).
# Output: dist/mgba-midisync-<version>-aarch64.zip (+ .sha256)
set -e
cd "$(dirname "$0")/.."
VERSION="${1:-$(git describe --tags --always 2>/dev/null || echo dev)}"
BUILD=build-aarch64

cmake -S . -B "$BUILD" \
	-DCMAKE_TOOLCHAIN_FILE="$PWD/cmake/aarch64-linux-gnu.cmake" \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS="-mcpu=cortex-a53" \
	-DHAVE_STRLCPY=0 \
	-DBUILD_LIBRETRO=ON -DSKIP_LIBRARY=ON \
	-DBUILD_QT=OFF -DBUILD_SDL=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF \
	-DUSE_FFMPEG=OFF -DUSE_LUA=OFF -DUSE_ZLIB=OFF -DUSE_PNG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF \
	-DUSE_SQLITE3=OFF -DUSE_ELF=OFF -DUSE_EDITLINE=OFF -DUSE_EPOXY=OFF -DUSE_DISCORD_RPC=OFF \
	-DUSE_FREETYPE=OFF -DENABLE_SCRIPTING=OFF -DUSE_JSON_C=OFF
cmake --build "$BUILD" --target mgba_libretro -j"$(nproc)"

# The core must stay loadable on older handheld firmware (glibc 2.35+)
NEWEST="$(aarch64-linux-gnu-objdump -T "$BUILD/mgba_libretro.so" | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -n 1)"
echo "newest glibc symbol: $NEWEST"
case "$NEWEST" in
	GLIBC_2.1[0-9]*|GLIBC_2.2[0-9]*|GLIBC_2.3[0-5]) ;;
	*) echo "error: needs $NEWEST, expected <= GLIBC_2.35"; exit 1 ;;
esac

PKG="dist/mgba-midisync-$VERSION-aarch64"
rm -rf "$PKG" "$PKG.zip"
mkdir -p "$PKG"
aarch64-linux-gnu-strip -o "$PKG/mgba_midisync_libretro.so" "$BUILD/mgba_libretro.so"
cp knulli/install.sh knulli/uninstall.sh README.md README_JA.md LICENSE "$PKG/"
chmod +x "$PKG"/*.sh
(cd dist && zip -qr "$(basename "$PKG").zip" "$(basename "$PKG")")
(cd dist && sha256sum "$(basename "$PKG").zip" > "$(basename "$PKG").zip.sha256")
echo "built $PKG.zip"
cat "$PKG.zip.sha256"
