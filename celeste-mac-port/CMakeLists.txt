# Celeste Classic -- Macintosh Classic port (Retro68)
#
# Build via ./build.sh (points cmake at the Retro68 toolchain).
# Produces Celeste.APPL (and .bin / .dsk) runnable in Mini vMac / Basilisk II
# or on real 68k hardware.
cmake_minimum_required(VERSION 3.9)
project(Celeste C CXX)

# celeste.c (vendored from ccleste) is C99; the reused p8 layer is C90-clean.
set(CMAKE_C_STANDARD 99)

# No build type was set before => -O0, which is ruinous on a 68000.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE   "-O2 -fomit-frame-pointer -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -fomit-frame-pointer -DNDEBUG")

# Fixed point: no FPU on the 68000, and software float was unplayably slow.
# ccleste's CELESTE_P8_FIXEDP swaps every game float for a 16.16 type via
# C++ operator overloading, so celeste.c compiles as C++ (no C++ runtime:
# exceptions/RTTI off, nothing from libstdc++ is used).
set_source_files_properties(src/celeste.c PROPERTIES
    LANGUAGE CXX
    COMPILE_FLAGS "-DCELESTE_P8_FIXEDP -DCELESTE_MAC_NO_CLOUDS -DCELESTE_MAC_NO_PARTICLES -DCELESTE_MAC_HOOKS -DCELESTE_TYPED_CALLS -fno-exceptions -fno-rtti")

include_directories(src src/generated)

set(SOURCES
    src/mac_main.c
    src/mac_display.c
    src/mac_input.c
    src/mac_sound.c
    src/mac_prefs.c
    src/p8.c
    src/celeste.c
    src/celeste_glue.c
    src/game.c
    src/generated/gfx_data.c
    src/generated/sprite_flags.c
    src/generated/map_data.c
    src/generated/sfx_data.c
    src/generated/music_data.c
)

# add_application is provided by the Retro68 toolchain file.
# SIZE.r: bigger memory partition -- the sfx samples live in the data segment.
add_application(Celeste ${SOURCES} SIZE.r Icon.r)

# libm for floor/sin/cos etc. (software float on the 68000; ccleste's
# CELESTE_P8_FIXEDP 16.16 path is the fallback if this is too slow).
target_link_libraries(Celeste m)

# Diagnostic build: on-screen per-phase tick costs (update/draw/present)
# for real-hardware bottleneck hunting.  Same game, extra overlay line.
add_application(CelesteDiag ${SOURCES} SIZE.r Icon.r)
target_link_libraries(CelesteDiag m)
target_compile_definitions(CelesteDiag PRIVATE PHASE_DIAG=1)

# No-music phase-diag build (~500KB -> fits a 1.44MB floppy for PCE measurement).
# music_data.c (~1MB) excluded; NO_MUSIC skips the streaming code.
add_application(CelesteDiagNM
    src/mac_main.c src/mac_display.c src/mac_input.c src/mac_sound.c src/mac_prefs.c
    src/p8.c src/celeste.c src/celeste_glue.c src/game.c
    src/generated/gfx_data.c src/generated/sprite_flags.c src/generated/map_data.c
    src/generated/sfx_data.c src/generated/music_data_stub.c
    SIZE.r Icon.r)
target_link_libraries(CelesteDiagNM m)
target_compile_definitions(CelesteDiagNM PRIVATE PHASE_DIAG=1 CELESTE_MAC_NO_MUSIC=1)
