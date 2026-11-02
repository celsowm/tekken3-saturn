# Experimental C352 -> SCSP prototype: a standalone LibSaturn program, independent of the Tekken 3 build.
#   cmake -S tests/c352_scsp_proto -B build/c352proto -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$LIBSATURN_PREFIX/share/libsaturn/toolchains/sh2eb-elf.cmake \
#     -DCMAKE_PREFIX_PATH=$LIBSATURN_PREFIX -DC352_DATA_DIR=assets/imported/c352-proto/data
# C352_DATA_DIR holds bank.bin, score.bin, pages.bin and score_meta.h from scripts/c352_scsp_convert.py (private data).
cmake_minimum_required(VERSION 3.24)
project(c352_scsp_proto C)
find_package(LibSaturn 0.1 CONFIG REQUIRED)
set(C352_DATA_DIR "" CACHE PATH "Directory with bank.bin, score.bin and score_meta.h")
option(C352_GUARD "Silence a sample region before its load, so a late load is heard as silence" OFF)
set(C352_LOAD_DELAY_TICKS 0 CACHE STRING "Test only: hold every fourth sample load back this many ticks")
option(C352_CD "Read the bank and the pages from the disc (BANK.BIN, PAGES.BIN) instead of the image" OFF)
foreach(_f bank.bin score.bin pages.bin loads.bin score_meta.h)
    if(NOT EXISTS "${C352_DATA_DIR}/${_f}")
        message(FATAL_ERROR "Set -DC352_DATA_DIR to the converter output; ${_f} is missing")
    endif()
endforeach()
get_filename_component(_data "${C352_DATA_DIR}" ABSOLUTE)
add_executable(c352_scsp_proto main.c)
target_include_directories(c352_scsp_proto PRIVATE "${_data}")
target_compile_definitions(c352_scsp_proto PRIVATE C352_CD=$<BOOL:${C352_CD}> C352_GUARD=$<BOOL:${C352_GUARD}> C352_LOAD_DELAY_TICKS=${C352_LOAD_DELAY_TICKS} C352_BANK_FILE="${_data}/bank.bin" C352_SCORE_FILE="${_data}/score.bin" C352_PAGES_FILE="${_data}/pages.bin")
set_source_files_properties(main.c PROPERTIES OBJECT_DEPENDS "${_data}/bank.bin;${_data}/score.bin;${_data}/pages.bin;${_data}/loads.bin;${_data}/score_meta.h")
target_compile_options(c352_scsp_proto PRIVATE -Wall -Wextra -Os)
target_link_libraries(c352_scsp_proto PRIVATE LibSaturn::Saturn)
libsaturn_configure_executable(c352_scsp_proto)
libsaturn_add_binary(c352_scsp_proto MAX_BYTES 983040)
if(C352_CD)
    # a disc whose root holds the two data files in upper case (ISO 9660 names)
    set(_cd "${CMAKE_BINARY_DIR}/cd")
    add_custom_command(OUTPUT "${_cd}/BANK.BIN" "${_cd}/PAGES.BIN" "${_cd}/LOADS.BIN"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_cd}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_data}/bank.bin" "${_cd}/BANK.BIN"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_data}/pages.bin" "${_cd}/PAGES.BIN"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_data}/loads.bin" "${_cd}/LOADS.BIN"
        DEPENDS "${_data}/bank.bin" "${_data}/pages.bin" "${_data}/loads.bin" VERBATIM)
    libsaturn_add_disc(c352_scsp_proto NAME c352_scsp_proto IP_TEMPLATE_KIND native
        ROOT_FILES "${_cd}/BANK.BIN" "${_cd}/PAGES.BIN" "${_cd}/LOADS.BIN" DEPENDS "${_cd}/BANK.BIN" "${_cd}/PAGES.BIN" "${_cd}/LOADS.BIN" ALL)
endif()
