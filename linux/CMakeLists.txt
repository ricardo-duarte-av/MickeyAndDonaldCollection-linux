# Linux build: the emulation core, game modules and app logic are the Windows build's; the drawing, sound and
# controllers are the Android build's SDL2 + OpenGL ES 3 layer (src/platform/android), compiled unchanged.
# Only linux/ is Linux-specific, so the upstream sources stay as they are.
#
#   cmake -S linux -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/linux
#   build/linux/bin/DisneyClassics
#
# Needs SDL2 and OpenGL ES headers (Debian / Ubuntu: libsdl2-dev libgles-dev libegl-dev). linux/appimage/build.sh
# packages the result as an AppImage.
cmake_minimum_required(VERSION 3.16)
project(MickeyDonaldLinux C CXX)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
set(SRC ${ROOT}/src)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
find_library(GLESV2_LIBRARY NAMES GLESv2 REQUIRED)
find_path(GLES3_INCLUDE_DIR GLES3/gl3.h REQUIRED)

add_executable(DisneyClassics
    ${SRC}/core/m68k.cpp
    ${SRC}/core/z80.cpp
    ${SRC}/core/vdp.cpp
    ${SRC}/core/ym2612.cpp
    ${SRC}/core/psg.cpp
    ${SRC}/core/md.cpp
    ${SRC}/game/common.cpp
    ${SRC}/game/coi.cpp
    ${SRC}/game/qs.cpp
    ${SRC}/game/woi.cpp
    ${SRC}/app/app.cpp
    ${SRC}/app/app_achievements.cpp
    ${SRC}/app/achievements.cpp
    ${SRC}/app/app_menus.cpp
    ${SRC}/app/launcher.cpp
    ${SRC}/app/jukebox.cpp
    ${SRC}/app/session.cpp
    ${SRC}/app/soundtrack.cpp
    ${SRC}/app/settings.cpp
    ${SRC}/app/palette.cpp
    ${SRC}/app/audio_mixer.cpp
    ${SRC}/app/romfind.cpp
    ${SRC}/app/unzip.cpp
    ${SRC}/app/png.cpp
    ${SRC}/app/rewind.cpp
    ${SRC}/app/ui.cpp
    ${SRC}/app/imageload.cpp
    ${SRC}/app/updatecheck.cpp
    ${SRC}/app/achexport.cpp
    ${SRC}/platform/android/video_gles.cpp
    ${SRC}/platform/android/audio_sdl.cpp
    ${SRC}/platform/android/input_sdl.cpp
    src/app_linux.cpp
    src/sys_linux.cpp
    src/glyphs_linux.cpp
)
set_target_properties(DisneyClassics PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# linux/shim first: its jni.h / android/log.h stand in for Android's (see linux/shim/jni.h)
target_include_directories(DisneyClassics PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${SRC}
    ${ROOT}/third_party/stb
    ${GLES3_INCLUDE_DIR})
# (the emulation core needs optimising even in debug builds to reach full speed)
target_compile_options(DisneyClassics PRIVATE -O2 -Wno-unused-parameter -Wno-sign-compare)
target_link_libraries(DisneyClassics PRIVATE PkgConfig::SDL2 ${GLESV2_LIBRARY} pthread)

# Fonts next to the executable (bin/fonts): Luckiest Guy (the Theatre menu style's titles, Apache 2.0) and,
# when the build machine has it, DejaVu Sans for the UI (else the player's sans-serif, through fontconfig).
set(FONT_DIR ${CMAKE_BINARY_DIR}/bin/fonts)
add_custom_command(TARGET DisneyClassics POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${FONT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ROOT}/third_party/fonts/LuckiestGuy-Regular.ttf ${FONT_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ROOT}/third_party/fonts/LICENSE.txt ${FONT_DIR}/LuckiestGuy-LICENSE.txt)
foreach(dir /usr/share/fonts/truetype/dejavu /usr/share/fonts/TTF /usr/share/fonts/dejavu)
    if(EXISTS ${dir}/DejaVuSans.ttf AND EXISTS ${dir}/DejaVuSans-Bold.ttf)
        add_custom_command(TARGET DisneyClassics POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dir}/DejaVuSans.ttf ${dir}/DejaVuSans-Bold.ttf ${FONT_DIR})
        break()
    endif()
endforeach()
