# Linux build

A native Linux version of the collection, packaged as an AppImage. No Wine/Proton.

It reuses the upstream code as-is: the emulation core, game modules and app logic are the Windows build's, and the
drawing, sound and controllers are the Android build's SDL2 + OpenGL ES 3 layer (`src/platform/android`), compiled
unchanged. Everything Linux-specific lives in this folder (plus `.github/workflows/linux-appimage.yml`), so the fork
can keep merging upstream without conflicts.

| File | What it does |
|---|---|
| `src/app_linux.cpp` | SDL window and event loop: windowed / full screen (F11, Alt+Enter), mouse on the launcher's links, frame pacing, `--game`, `--autotest` |
| `src/sys_linux.cpp` | Files and folders (XDG), ROM search paths, links (`xdg-open`), the update check (`curl`) |
| `src/glyphs_linux.cpp` | UI text with stb_truetype: what Android's `MDActivity.renderGlyphs` does in Java |
| `shim/jni.h`, `shim/android/log.h` | Stand-ins that let `video_gles.cpp` compile unchanged: its one JNI call (`renderGlyphs`) lands in `glyphs_linux.cpp` |
| `appimage/build.sh` | SDL2 from source, the game, then the AppImage (linuxdeploy) into `dist/` |

## Playing

Download the AppImage from the repository's **Releases**: **continuous** is always the latest build of `main`,
and pushed `v*` tags get releases of their own. Then:

```sh
chmod +x Mickey_Donald_Collection-*.AppImage
./Mickey_Donald_Collection-*.AppImage
```

Put the ROMs (zipped is fine) **next to the AppImage**, or in a `roms` folder beside it, or in
`~/.local/share/mickey-donald-collection/roms`. Settings, save states, screenshots, covers and the achievements
export go to `~/.local/share/mickey-donald-collection` (`$XDG_DATA_HOME` if set). MD+ soundtrack packs go in its
`soundtracks` folder.

You need OpenGL ES 3.0, which Mesa (Intel, AMD, nouveau) and the NVIDIA driver both provide on X11 and Wayland.
`./…AppImage --game coi` (or `qs`, `woi`) starts a game directly.

Not available on Linux (Windows-only upstream): slang shader presets (librashader's Direct3D 11 runtime), High
Frame Rate (its menu item is Windows-only), the monitor picker (the window manager places the window) and trailer
recording.

## Building

Debian / Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config libsdl2-dev libgles-dev libegl-dev fonts-dejavu-core
cmake -S linux -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux
build/linux/bin/DisneyClassics
```

The AppImage (SDL2 2.32.10 built from source, so it also needs SDL's build dependencies: see the package list in
`.github/workflows/linux-appimage.yml`):

```sh
./linux/appimage/build.sh            # -> dist/Mickey_Donald_Collection-<version>-x86_64.AppImage
USE_SYSTEM_SDL=1 ./linux/appimage/build.sh   # with the system's libsdl2-dev instead
```

GitHub Actions runs `build.sh` on every push and pull request (Ubuntu 22.04, so the AppImage runs on glibc 2.35 and
newer), starts the AppImage headless as a smoke test, and uploads it as an artifact. A push to `main` also
replaces the AppImage in the **continuous** pre-release; pushing a tag such as `v1.3-linux1` publishes a release
under that name.

## Keeping up with upstream

```sh
git remote add upstream https://github.com/Darhkfox/MickeyAndDonaldCollection.git   # once
git fetch upstream
git merge upstream/main
```

Upstream changes that could need a matching change here:

- a new source file in `android/app/src/main/cpp/CMakeLists.txt`: add it to `linux/CMakeLists.txt`;
- a new per-platform `App` method (one that `app_android.cpp` / `app_win.cpp` define): define it in `app_linux.cpp`;
- a new function in `src/app/sys.h`: implement it in `sys_linux.cpp`;
- a new JNI call in `video_gles.cpp`: the build fails in `shim/jni.h`, which then needs that call.

The CI build catches all of these as compile or link errors.
