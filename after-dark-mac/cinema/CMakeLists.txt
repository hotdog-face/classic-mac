cmake_minimum_required(VERSION 3.9)
project(Cinema C)
set(CMAKE_C_STANDARD 90)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_C_FLAGS_RELEASE "-O2 -fomit-frame-pointer -DNDEBUG")

# demo builds: -DFILM_SECONDS=45 switches films every 45s
if(FILM_SECONDS)
    add_compile_definitions(FILM_TICKS=${FILM_SECONDS}*60L)
endif()

add_executable(CinemaCode src/cinema.c)
set_target_properties(CinemaCode PROPERTIES
    OUTPUT_NAME Cinema.flt
    LINK_FLAGS -Wl,--mac-flat)

if(REZ_INCLUDE_PATH)
    set(REZ_INCLUDE_FLAG -I${REZ_INCLUDE_PATH})
endif()

add_custom_command(
    OUTPUT Cinema.bin Cinema.dsk
    COMMAND ${REZ} ${REZ_INCLUDE_FLAG}
            ${CMAKE_CURRENT_SOURCE_DIR}/Cinema.r
            -o Cinema.bin --cc Cinema.dsk
            -t ADgm -c ADrk
    DEPENDS CinemaCode ${CMAKE_CURRENT_SOURCE_DIR}/Cinema.r)

add_custom_target(Cinema_MODULE ALL DEPENDS Cinema.dsk)
