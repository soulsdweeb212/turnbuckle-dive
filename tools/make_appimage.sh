#!/bin/sh
# Packs a built turnbuckle-dive into an AppImage, the one-file Linux program of a release (docs/CI.md, "Releases").
#
#   tools/make_appimage.sh BUILD_DIR OUTPUT
#
# BUILD_DIR is a build folder that holds turnbuckle-dive, f3d.o2r (LibUltraShip's shaders, no game data) and the game
# pad list gamecontrollerdb.txt with its licence (lib/SDL_GameControllerDB); OUTPUT is
# the AppImage to write. linuxdeploy copies in the libraries the program needs that a Linux system may not have
# (libzip, tinyxml2, spdlog, fmt), leaves out those every system has its own of (the C library, OpenGL), and packs the
# folder. It is downloaded once into tools/.cache at a fixed version. The AppImage holds no game data: the program
# writes the game's archive from the player's ROM on its first launch.
set -eu
build="$1"
out="$2"
root="$(cd "$(dirname "$0")/.." && pwd)"
cache="$root/tools/.cache"
version="1-alpha-20250213-2"
linuxdeploy="$cache/linuxdeploy-$version-x86_64.AppImage"
mkdir -p "$cache"
if [ ! -x "$linuxdeploy" ]; then
    curl -fsSL -o "$linuxdeploy.part" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/$version/linuxdeploy-x86_64.AppImage"
    chmod +x "$linuxdeploy.part"
    mv "$linuxdeploy.part" "$linuxdeploy"
fi

appdir="$(mktemp -d)/AppDir"
mkdir -p "$appdir/usr/bin"
cp "$build/turnbuckle-dive" "$appdir/usr/bin/"
strip --strip-debug "$appdir/usr/bin/turnbuckle-dive"
cp "$build/f3d.o2r" "$build/gamecontrollerdb.txt" "$build/gamecontrollerdb.LICENSE.txt" "$appdir/usr/bin/"

# Run linuxdeploy without FUSE (CI runners and containers have none): it unpacks itself instead.
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 LDAI_OUTPUT="$out" "$linuxdeploy" --appdir "$appdir" \
    --executable "$appdir/usr/bin/turnbuckle-dive" \
    --desktop-file "$root/packaging/turnbuckle-dive.desktop" \
    --icon-file "$root/packaging/turnbuckle-dive.png" \
    --custom-apprun "$root/packaging/AppRun" \
    --output appimage
rm -rf "$(dirname "$appdir")"
echo "Wrote $out"
