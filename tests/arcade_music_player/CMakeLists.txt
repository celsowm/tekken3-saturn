# Experimental: plays a converted Arcade song from the disc with the game's music module, alone.
#   cmake -S tests/arcade_music_player -B build/arcade-music -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$LIBSATURN_PREFIX/share/libsaturn/toolchains/sh2eb-elf.cmake \
#     -DCMAKE_PREFIX_PATH=$LIBSATURN_PREFIX -DMUSIC_DATA_DIR=assets/imported/c352-proto/<run>/data
# MUSIC_DATA_DIR holds meta.bin, bank.bin, loads.bin and pages.bin from scripts/c352_scsp_convert.py (private data).
cmake_minimum_required(VERSION 3.24)
project(arcade_music_player C)
find_package(LibSaturn 0.1 CONFIG REQUIRED)
set(MUSIC_DATA_DIR "" CACHE PATH "Directory with meta.bin, bank.bin, loads.bin and pages.bin")
set(MUSIC_SLOTS 26 CACHE STRING "SCSP slots 0..N-1 used by the score (the converter's --max-slots)")
foreach(_f meta.bin bank.bin loads.bin pages.bin)
    if(NOT EXISTS "${MUSIC_DATA_DIR}/${_f}")
        message(FATAL_ERROR "Set -DMUSIC_DATA_DIR to the converter output; ${_f} is missing")
    endif()
endforeach()
get_filename_component(_data "${MUSIC_DATA_DIR}" ABSOLUTE)
get_filename_component(_src "${CMAKE_CURRENT_SOURCE_DIR}/../../src/saturn" ABSOLUTE)
add_executable(arcade_music_player main.c "${_src}/arcade_music.c")
target_include_directories(arcade_music_player PRIVATE "${_src}")
target_compile_definitions(arcade_music_player PRIVATE MUSIC_SLOTS=${MUSIC_SLOTS}u)
target_compile_options(arcade_music_player PRIVATE -Wall -Wextra -Os)
target_link_libraries(arcade_music_player PRIVATE LibSaturn::Saturn)
libsaturn_configure_executable(arcade_music_player)
libsaturn_add_binary(arcade_music_player MAX_BYTES 983040)
set(_cd "${CMAKE_BINARY_DIR}/cd")
set(_names ARCMETA ARCBANK ARCLOADS ARCPAGES)
set(_outs)
foreach(_n ${_names})
    string(TOLOWER ${_n} _l)
    string(REPLACE "arc" "" _l ${_l})
    add_custom_command(OUTPUT "${_cd}/${_n}.BIN"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_cd}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_data}/${_l}.bin" "${_cd}/${_n}.BIN"
        DEPENDS "${_data}/${_l}.bin" VERBATIM)
    list(APPEND _outs "${_cd}/${_n}.BIN")
endforeach()
libsaturn_add_disc(arcade_music_player NAME arcade_music_player IP_TEMPLATE_KIND native
    ROOT_FILES ${_outs} DEPENDS ${_outs} ALL)
