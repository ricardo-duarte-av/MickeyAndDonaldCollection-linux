#!/usr/bin/env bash
# Builds the Linux version and packages it as an AppImage:
#   1. SDL2 2.32.10 from source (the Android build's version) into build/linux-sdl, unless USE_SYSTEM_SDL=1
#   2. the game (linux/CMakeLists.txt) into build/linux
#   3. the AppImage (linuxdeploy) into dist/
#
# Needs: cmake, ninja, a C++17 compiler, curl, unzip, OpenGL ES / EGL headers, and SDL2's build dependencies
# (see .github/workflows/linux-appimage.yml for the Debian / Ubuntu package list).
# Run from anywhere: ./linux/appimage/build.sh
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD="$ROOT/build"
JOBS="$(nproc)"
ARCH="$(uname -m)"
GEN=(); command -v ninja >/dev/null && GEN=(-G Ninja)
# (EXTRA_CMAKE_ARGS: passed to the game's configure, e.g. "-DGLES3_INCLUDE_DIR=/path" for headers elsewhere)
read -r -a EXTRA <<< "${EXTRA_CMAKE_ARGS:-}"

SDL_VER=2.32.10
SDL_SHA256=12b2dc2eb8f2836100a7916b5d394a0c82f1f7e32693f95f98305403af242f08   # (as android/fetch_sdl.ps1)
SDL_PREFIX="$BUILD/linux-sdl/install"

# ---- 1. SDL2 ----------------------------------------------------------------------------------------------
if [[ "${USE_SYSTEM_SDL:-0}" != 1 ]]; then
    if [[ ! -f "$SDL_PREFIX/lib/pkgconfig/sdl2.pc" ]]; then
        mkdir -p "$BUILD/linux-sdl"
        zip="$BUILD/linux-sdl/SDL2-$SDL_VER.zip"
        [[ -f "$zip" ]] || curl -fsSL -o "$zip" "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VER/SDL2-$SDL_VER.zip"
        echo "$SDL_SHA256  $zip" | sha256sum -c -
        rm -rf "$BUILD/linux-sdl/src" && mkdir -p "$BUILD/linux-sdl/src"
        unzip -q "$zip" -d "$BUILD/linux-sdl/src"
        # Video and audio back ends are loaded at run time (dlopen), so the AppImage works on X11 and Wayland,
        # with PipeWire, PulseAudio or ALSA, without needing any of them.
        cmake -S "$BUILD/linux-sdl/src/SDL2-$SDL_VER" -B "$BUILD/linux-sdl/obj" "${GEN[@]}" \
            -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$SDL_PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
            -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF \
            -DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_WAYLAND_LIBDECOR=ON -DSDL_KMSDRM=OFF \
            -DSDL_PIPEWIRE=ON -DSDL_PULSEAUDIO=ON -DSDL_ALSA=ON -DSDL_JACK=OFF -DSDL_SNDIO=OFF \
            -DSDL_OPENGLES=ON -DSDL_VULKAN=OFF -DSDL_RPATH=OFF
        cmake --build "$BUILD/linux-sdl/obj" -j "$JOBS"
        cmake --install "$BUILD/linux-sdl/obj"
    fi
    export PKG_CONFIG_PATH="$SDL_PREFIX/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
    export LD_LIBRARY_PATH="$SDL_PREFIX/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

# ---- 2. the game ------------------------------------------------------------------------------------------
cmake -S "$ROOT/linux" -B "$BUILD/linux" "${GEN[@]}" -DCMAKE_BUILD_TYPE=Release "${EXTRA[@]}"
cmake --build "$BUILD/linux" -j "$JOBS"

# ---- 3. the AppImage --------------------------------------------------------------------------------------
APPDIR="$BUILD/linux-appdir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps" \
         "$APPDIR/usr/share/doc/mickey-donald-collection"
cp "$BUILD/linux/bin/DisneyClassics" "$APPDIR/usr/bin/"
cp -r "$BUILD/linux/bin/fonts" "$APPDIR/usr/bin/"
cp "$ROOT/linux/appimage/mickey-donald-collection.desktop" "$APPDIR/usr/share/applications/"
cp "$ROOT/res/app_icon_preview.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/mickey-donald-collection.png"
cp "$ROOT/LICENSE" "$ROOT/README.md" "$APPDIR/usr/share/doc/mickey-donald-collection/"
[[ -f "$APPDIR/usr/bin/fonts/DejaVuSans.ttf" ]] || echo "warning: DejaVu Sans not bundled (install fonts-dejavu-core); the UI will use the system's sans-serif"

TOOLS="$BUILD/linux-tools"
mkdir -p "$TOOLS"
LINUXDEPLOY="$TOOLS/linuxdeploy-$ARCH.AppImage"
if [[ ! -x "$LINUXDEPLOY" ]]; then
    curl -fsSL -o "$LINUXDEPLOY" "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage"
    chmod +x "$LINUXDEPLOY"
fi

# Version: the app's own (kAppVersion in src/app/app.h) and the commit
APP_VER="$(sed -n 's/.*kAppVersion = "\([^"]*\)".*/\1/p' "$ROOT/src/app/app.h")"
GIT_REV="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo local)"
export VERSION="${APP_VER:-0}-$GIT_REV"
export OUTPUT="Mickey_Donald_Collection-$VERSION-$ARCH.AppImage"

mkdir -p "$ROOT/dist"
cd "$ROOT/dist"
# (--appimage-extract-and-run: works without FUSE, as on CI runners and in containers)
# The OpenGL / EGL / GLES libraries must come from the player's system: they are thin glvnd dispatchers tied to the
# system's libGLdispatch and GPU driver, and a bundled copy that doesn't match them makes every GL call fail
# ("video: shaders failed"). linuxdeploy's own exclude list misses libGLESv2.
DEPLOY=("$LINUXDEPLOY" --appimage-extract-and-run --appdir "$APPDIR"
    --desktop-file "$APPDIR/usr/share/applications/mickey-donald-collection.desktop"
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/mickey-donald-collection.png"
    --exclude-library 'libGLESv2.so*' --exclude-library 'libGLESv1_CM.so*' --exclude-library 'libEGL.so*'
    --exclude-library 'libGL.so*' --exclude-library 'libGLX.so*' --exclude-library 'libOpenGL.so*'
    --exclude-library 'libGLdispatch.so*')
"${DEPLOY[@]}"
bad="$(find "$APPDIR" -name 'libGL*.so*' -o -name 'libEGL*.so*' -o -name 'libOpenGL*.so*' -o -name 'libGLdispatch*.so*')"
if [[ -n "$bad" ]]; then
    echo "error: GL libraries were bundled (they must come from the system):" >&2
    echo "$bad" >&2
    exit 1
fi
"${DEPLOY[@]}" --output appimage
echo "AppImage: $ROOT/dist/$OUTPUT"
