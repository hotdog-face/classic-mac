cmake_minimum_required(VERSION 3.9)
project(Ants C)
set(CMAKE_C_STANDARD 90)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -fomit-frame-pointer -DNDEBUG")

# Step 1: flat code resource (entry = _start at offset 0), like the
# Retro68 SystemExtension sample.
add_executable(AntsCode src/ants.c)
set_target_properties(AntsCode PROPERTIES
    OUTPUT_NAME Ants.flt
    LINK_FLAGS -Wl,--mac-flat)

if(REZ_INCLUDE_PATH)
    set(REZ_INCLUDE_FLAG -I${REZ_INCLUDE_PATH})
endif()

# Step 2: Rez wraps the flat code as the 'ADgm' resource; module file is
# type ADgm / creator ADrk so After Dark's control panel lists it.
add_custom_command(
    OUTPUT Ants.bin Ants.dsk
    COMMAND ${REZ} ${REZ_INCLUDE_FLAG}
            ${CMAKE_CURRENT_SOURCE_DIR}/Ants.r
            -o Ants.bin --cc Ants.dsk
            -t ADgm -c ADrk
    DEPENDS AntsCode ${CMAKE_CURRENT_SOURCE_DIR}/Ants.r)

add_custom_target(Ants_MODULE ALL DEPENDS Ants.dsk)
