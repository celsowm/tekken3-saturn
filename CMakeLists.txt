cmake_minimum_required(VERSION 3.24)
project(tekken3_saturn VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Platform-free deterministic core. Only the frontend touches LibSaturn.
add_library(tekken3_fight STATIC src/game/fight.c)
target_include_directories(tekken3_fight PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/game>")
target_compile_options(tekken3_fight PRIVATE -Wall -Wextra -Wpedantic)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
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
    add_executable(tekken3_saturn src/saturn/main.c
        src/saturn/characters.c "${_t3_gen}/t3_generated_character.c")
    target_include_directories(tekken3_saturn PRIVATE
        "${_t3_gen}" "${CMAKE_SOURCE_DIR}/src/saturn")
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
    endif()
endif()
