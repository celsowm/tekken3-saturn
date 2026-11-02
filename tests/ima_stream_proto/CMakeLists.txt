# Experimental IMA stream prototype: a standalone LibSaturn program, independent of the Tekken 3 build.
#   cmake -S tests/ima_stream_proto -B build/imaproto -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$LIBSATURN_PREFIX/share/libsaturn/toolchains/sh2eb-elf.cmake \
#     -DCMAKE_PREFIX_PATH=$LIBSATURN_PREFIX -DIMA_DATA_DIR=assets/imported/ima-proto/paul
# IMA_DATA_DIR holds ima.bin and ima_meta.h from scripts/encode_ima_stream.py (private data).
cmake_minimum_required(VERSION 3.24)
project(ima_stream_proto C)
find_package(LibSaturn 0.1 CONFIG REQUIRED)
set(IMA_DATA_DIR "" CACHE PATH "Directory with ima.bin and ima_meta.h from scripts/encode_ima_stream.py")
foreach(_f ima.bin ima_meta.h)
    if(NOT EXISTS "${IMA_DATA_DIR}/${_f}")
        message(FATAL_ERROR "Set -DIMA_DATA_DIR to the encoder output; ${_f} is missing")
    endif()
endforeach()
get_filename_component(_data "${IMA_DATA_DIR}" ABSOLUTE)
add_executable(ima_stream_proto main.c)
target_include_directories(ima_stream_proto PRIVATE "${_data}")
target_compile_options(ima_stream_proto PRIVATE -Wall -Wextra -Os)
target_link_libraries(ima_stream_proto PRIVATE LibSaturn::Saturn)
libsaturn_configure_executable(ima_stream_proto)
libsaturn_add_binary(ima_stream_proto MAX_BYTES 983040)
# the track is only on the disc: the program image holds no sample
set(_cd "${CMAKE_BINARY_DIR}/cd")
add_custom_command(OUTPUT "${_cd}/IMA.BIN"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_cd}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${_data}/ima.bin" "${_cd}/IMA.BIN"
    DEPENDS "${_data}/ima.bin" VERBATIM)
libsaturn_add_disc(ima_stream_proto NAME ima_stream_proto IP_TEMPLATE_KIND native
    ROOT_FILES "${_cd}/IMA.BIN" DEPENDS "${_cd}/IMA.BIN" ALL)
