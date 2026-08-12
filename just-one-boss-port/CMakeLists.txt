# Just One Boss -- Macintosh Classic port (Retro68)
#
# Build (with Retro68 installed):
#   mkdir build && cd build
#   cmake -DCMAKE_TOOLCHAIN_FILE=<Retro68>/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake ..
#   make
# Produces JustOneBoss.APPL (and .bin / .dsk) runnable in Mini vMac / Basilisk II
# or on real 68k hardware.
cmake_minimum_required(VERSION 3.9)
project(JustOneBoss C)

set(CMAKE_C_STANDARD 90)

# no build type => -O0, ruinous on a 68000 (lesson from the Celeste port)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -fomit-frame-pointer -DNDEBUG")

# No build type was set before => -O0, which is ruinous on a 68000.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -fomit-frame-pointer -DNDEBUG")

include_directories(src src/generated)

set(SOURCES
    src/mac_main.c
    src/mac_display.c
    src/mac_input.c
    src/mac_audio.c
    src/mac_prefs.c
    src/p8.c
    src/engine.c
    src/util.c
    src/entities.c
    src/game.c
    src/generated/gfx_data.c
    src/generated/sprite_flags.c
    src/generated/map_data.c
    src/generated/sfx_data.c
    src/generated/music_data.c
)

# add_application is provided by the Retro68 toolchain file.
add_application(JustOneBoss ${SOURCES} Icon.r)

# libm for floor/sin/cos (software float on the 68000; see PORTING-PLAN.md §7
# for the fixed-point plan if this proves too slow on real hardware).
target_link_libraries(JustOneBoss m)
