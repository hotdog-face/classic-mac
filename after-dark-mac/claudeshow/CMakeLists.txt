cmake_minimum_required(VERSION 3.9)
project(ClaudeShow C)
set(CMAKE_C_STANDARD 90)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -fomit-frame-pointer -DNDEBUG")

# ROTATE_TICKS override for demo builds: -DROTATE_SECONDS=15
if(ROTATE_SECONDS)
    add_compile_definitions(ROTATE_TICKS=${ROTATE_SECONDS}*60L)
endif()

add_executable(ShowCode src/show.c)
set_target_properties(ShowCode PROPERTIES
    OUTPUT_NAME Show.flt
    LINK_FLAGS -Wl,--mac-flat)

if(REZ_INCLUDE_PATH)
    set(REZ_INCLUDE_FLAG -I${REZ_INCLUDE_PATH})
endif()

add_custom_command(
    OUTPUT Show.bin Show.dsk
    COMMAND ${REZ} ${REZ_INCLUDE_FLAG}
            ${CMAKE_CURRENT_SOURCE_DIR}/Show.r
            -o Show.bin --cc Show.dsk
            -t ADgm -c ADrk
    DEPENDS ShowCode ${CMAKE_CURRENT_SOURCE_DIR}/Show.r)

add_custom_target(Show_MODULE ALL DEPENDS Show.dsk)
