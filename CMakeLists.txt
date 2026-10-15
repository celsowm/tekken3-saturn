cmake_minimum_required(VERSION 3.24)
project(tekken3_saturn VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Platform-free deterministic core. Only the frontend touches LibSaturn.
add_library(tekken3_fight STATIC src/game/fight.c src/game/input_history.c src/game/divmot.c src/game/move_vm.c)
target_include_directories(tekken3_fight PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/game>")
target_compile_options(tekken3_fight PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    # A freestanding GCC 13 SH-2 toolchain has no hosted system stdint.h.
    # Without -ffreestanding, GCC's own stdint.h attempts include_next.
    target_compile_options(tekken3_fight PRIVATE -m2 -mb -ffreestanding)
    find_package(LibSaturn 0.1 CONFIG REQUIRED)

    # LibSaturn is an installed runtime dependency. Its uninstalled source
    # tree supplies the offline original-humanoid/SKA1 generator only.
    set(LIBSATURN_SOURCE_DIR "" CACHE PATH "LibSaturn checkout for host asset tooling")
    if(NOT EXISTS "${LIBSATURN_SOURCE_DIR}/tools/generate_character_examples.py")
        message(FATAL_ERROR "Set -DLIBSATURN_SOURCE_DIR=/path/to/libsaturn "
            "for the original skeletal asset compiler.")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_t3_gen "${CMAKE_BINARY_DIR}/generated/characters")
    file(GLOB _t3_pipeline CONFIGURE_DEPENDS
        "${LIBSATURN_SOURCE_DIR}/tools/model_pipeline/*.py")
    # Explicit opt-in, private author-owned local data only. Neither normal
    # builds nor CI extract a game image or include Namco model bytes.
    set(T3_LOCAL_KMD "" CACHE FILEPATH
        "Optional absolute path to a locally extracted Tekken 3 KMD (private bind-pose preview)")
    if(T3_LOCAL_KMD)
        get_filename_component(T3_LOCAL_KMD "${T3_LOCAL_KMD}" ABSOLUTE)
        if(NOT EXISTS "${T3_LOCAL_KMD}")
            message(FATAL_ERROR "T3_LOCAL_KMD file not found: ${T3_LOCAL_KMD}")
        endif()
        add_custom_command(
            OUTPUT "${_t3_gen}/t3_generated_character.c"
                "${_t3_gen}/t3_generated_character.h"
                "${_t3_gen}/manifest.json"
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/build_local_kmd_assets.py"
                --kmd "${T3_LOCAL_KMD}"
                --libsaturn-source "${LIBSATURN_SOURCE_DIR}"
                --output "${_t3_gen}"
            DEPENDS "${T3_LOCAL_KMD}"
                "${CMAKE_SOURCE_DIR}/scripts/build_local_kmd_assets.py"
                "${CMAKE_SOURCE_DIR}/scripts/kmd_to_ska.py"
                ${_t3_pipeline}
            VERBATIM COMMENT "Compile local Tekken 3 KMD into PRIVATE LibSaturn bind-pose archive"
        )
    else()
        add_custom_command(
            OUTPUT "${_t3_gen}/t3_generated_character.c"
                "${_t3_gen}/t3_generated_character.h"
                "${_t3_gen}/manifest.json"
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/build_fighter_assets.py"
                --libsaturn-source "${LIBSATURN_SOURCE_DIR}" --output "${_t3_gen}"
            DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_fighter_assets.py"
                "${LIBSATURN_SOURCE_DIR}/tools/generate_character_examples.py"
                ${_t3_pipeline}
            VERBATIM COMMENT "Generate original low-poly 24-joint SKA1 fighter"
        )
    endif()
    add_executable(tekken3_saturn src/saturn/main.c
        src/saturn/characters.c src/saturn/move_probe.c
        "${_t3_gen}/t3_generated_character.c")
    target_include_directories(tekken3_saturn PRIVATE
        "${_t3_gen}" "${CMAKE_SOURCE_DIR}/src/saturn")
    # Deliberate local-only diagnostic: embeds copyrighted authored bank data
    # ONLY when a developer passes a local filename. Public CI never sets it.
    set(T3_LOCAL_DIVMOT "" CACHE FILEPATH
        "Private compact character DIVMOT bank (.t3logic), never for distribution")
    set(T3_LOCAL_COMMON_DIVMOT "" CACHE FILEPATH
        "Private compact divmot99 bank (optional)")
    if(T3_LOCAL_DIVMOT)
        get_filename_component(T3_LOCAL_DIVMOT "${T3_LOCAL_DIVMOT}" ABSOLUTE)
        if(NOT EXISTS "${T3_LOCAL_DIVMOT}")
            message(FATAL_ERROR "Local DIVMOT bank does not exist: ${T3_LOCAL_DIVMOT}")
        endif()
        set(_t3_private_banks "${T3_LOCAL_DIVMOT}")
        set(_t3_private_common_args "")
        if(T3_LOCAL_COMMON_DIVMOT)
            get_filename_component(T3_LOCAL_COMMON_DIVMOT
                "${T3_LOCAL_COMMON_DIVMOT}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_COMMON_DIVMOT}")
                message(FATAL_ERROR "Local common bank not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_COMMON_DIVMOT}")
            list(APPEND _t3_private_common_args --common "${T3_LOCAL_COMMON_DIVMOT}")
        endif()
        set(_t3_logic_gen "${CMAKE_BINARY_DIR}/generated/private_logic")
        add_custom_command(
            OUTPUT "${_t3_logic_gen}/t3_local_divmot.c"
                "${_t3_logic_gen}/t3_local_divmot.h"
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/build_local_divmot_assets.py"
                --bank "${T3_LOCAL_DIVMOT}"
                ${_t3_private_common_args}
                --output "${_t3_logic_gen}"
            DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_local_divmot_assets.py"
                "${CMAKE_SOURCE_DIR}/scripts/divmot_to_logic.py"
                ${_t3_private_banks}
            VERBATIM COMMENT "Embed PRIVATE original DIVMOT move logic (never distribute)"
        )
        target_sources(tekken3_saturn PRIVATE
            "${_t3_logic_gen}/t3_local_divmot.c")
        target_include_directories(tekken3_saturn PRIVATE "${_t3_logic_gen}")
        target_compile_definitions(tekken3_saturn PRIVATE T3_LOCAL_DIVMOT_ENABLED=1)
    endif()
    target_link_libraries(tekken3_saturn PRIVATE tekken3_fight LibSaturn::Saturn)
    target_compile_options(tekken3_saturn PRIVATE -Wall -Wextra -O2)
    libsaturn_configure_executable(tekken3_saturn)
    libsaturn_add_binary(tekken3_saturn MAX_BYTES 983040)
    libsaturn_add_disc(tekken3_saturn NAME tekken3_saturn
        IP_TEMPLATE_KIND native ALL)
else()
    include(CTest)
    if(BUILD_TESTING)
        add_executable(test_fight tests/test_fight.c)
        target_link_libraries(test_fight PRIVATE tekken3_fight)
        add_test(NAME deterministic_fight COMMAND test_fight)
        add_executable(test_divmot tests/test_divmot.c)
        target_link_libraries(test_divmot PRIVATE tekken3_fight)
        add_test(NAME divmot_commands_and_combat COMMAND test_divmot)
    endif()
endif()
