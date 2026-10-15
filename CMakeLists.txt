cmake_minimum_required(VERSION 3.24)
project(tekken3_saturn VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Platform-free deterministic core. Only the frontend touches LibSaturn.
add_library(tekken3_fight STATIC src/game/fight.c src/game/input_history.c src/game/divmot.c src/game/move_vm.c src/game/branch_rules.c src/game/attack_records.c src/game/psx_collision.c src/game/psx_motion.c src/game/psx_pose.c src/game/psx_ik.c src/game/psx_contact.c src/game/psx_root.c src/game/psx_trace.c)
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
        src/saturn/characters.c src/saturn/move_probe.c src/saturn/psx_skin_bridge.c
        "${_t3_gen}/t3_generated_character.c")
    target_include_directories(tekken3_saturn PRIVATE
        "${_t3_gen}" "${CMAKE_SOURCE_DIR}/src/saturn")
    # Deliberate local-only diagnostic: embeds copyrighted authored bank data
    # ONLY when a developer passes a local filename. Public CI never sets it.
    set(T3_LOCAL_DIVMOT "" CACHE FILEPATH
        "Private compact character DIVMOT bank (.t3logic), never for distribution")
    set(T3_LOCAL_COMMON_DIVMOT "" CACHE FILEPATH
        "Private compact divmot99 bank (optional)")
    set(T3_LOCAL_PSX_SEQUENCES "" CACHE FILEPATH
        "Private US PS1 original 104-command sequence manifest")
    if(T3_LOCAL_PSX_SEQUENCES AND NOT T3_LOCAL_DIVMOT)
        message(FATAL_ERROR "T3_LOCAL_PSX_SEQUENCES requires T3_LOCAL_DIVMOT")
    endif()
    set(T3_LOCAL_PSX_SPLINE_TABLES "" CACHE FILEPATH
        "Private 0x660-byte normalized original PlayStation spline lookup tables")
    if(T3_LOCAL_PSX_SPLINE_TABLES AND NOT T3_LOCAL_DIVMOT)
        message(FATAL_ERROR "T3_LOCAL_PSX_SPLINE_TABLES requires T3_LOCAL_DIVMOT")
    endif()
    set(T3_LOCAL_PSX_TRIG_TABLE "" CACHE FILEPATH
        "Private original PS1 0x2800-byte sine/cosine lookup window")
    if(T3_LOCAL_PSX_TRIG_TABLE AND NOT T3_LOCAL_DIVMOT)
        message(FATAL_ERROR "T3_LOCAL_PSX_TRIG_TABLE requires T3_LOCAL_DIVMOT")
    endif()
    set(T3_LOCAL_PSX_IK_TABLES "" CACHE FILEPATH
        "Private original 0x1F2-byte normalized PS1 IK constants")
    if(T3_LOCAL_PSX_IK_TABLES AND NOT T3_LOCAL_PSX_TRIG_TABLE)
        message(FATAL_ERROR "T3_LOCAL_PSX_IK_TABLES requires T3_LOCAL_PSX_TRIG_TABLE")
    endif()
    option(T3_ENABLE_PSX_SKIN_PREVIEW
        "Private visual preview of fully decoded original PS1 pose matrices" OFF)
    if(T3_ENABLE_PSX_SKIN_PREVIEW)
        if(NOT T3_LOCAL_KMD OR NOT T3_LOCAL_DIVMOT OR
           NOT T3_LOCAL_PSX_TRIG_TABLE OR NOT T3_LOCAL_PSX_IK_TABLES)
            message(FATAL_ERROR "PS1 skin preview requires PRIVATE KMD, "
                "DIVMOT, trig and IK tables; no generated fallback allowed")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE
            T3_ENABLE_PSX_SKIN_PREVIEW=1)
    endif()

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
        set(_t3_sequence_args "")
        if(T3_LOCAL_PSX_SEQUENCES)
            get_filename_component(T3_LOCAL_PSX_SEQUENCES
                "${T3_LOCAL_PSX_SEQUENCES}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_SEQUENCES}")
                message(FATAL_ERROR "Local original command sequence manifest not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_SEQUENCES}")
            list(APPEND _t3_sequence_args
                --sequences "${T3_LOCAL_PSX_SEQUENCES}")
        endif()
        set(_t3_spline_args "")
        if(T3_LOCAL_PSX_SPLINE_TABLES)
            get_filename_component(T3_LOCAL_PSX_SPLINE_TABLES
                "${T3_LOCAL_PSX_SPLINE_TABLES}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_SPLINE_TABLES}")
                message(FATAL_ERROR "Private PS1 spline lookup blob not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_SPLINE_TABLES}")
            list(APPEND _t3_spline_args
                --spline-tables "${T3_LOCAL_PSX_SPLINE_TABLES}")
        endif()
        set(_t3_trig_args "")
        if(T3_LOCAL_PSX_TRIG_TABLE)
            get_filename_component(T3_LOCAL_PSX_TRIG_TABLE
                "${T3_LOCAL_PSX_TRIG_TABLE}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_TRIG_TABLE}")
                message(FATAL_ERROR "Private PS1 sine table window not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_TRIG_TABLE}")
            list(APPEND _t3_trig_args --trig-table "${T3_LOCAL_PSX_TRIG_TABLE}")
        endif()
        set(_t3_ik_args "")
        if(T3_LOCAL_PSX_IK_TABLES)
            get_filename_component(T3_LOCAL_PSX_IK_TABLES
                "${T3_LOCAL_PSX_IK_TABLES}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_IK_TABLES}")
                message(FATAL_ERROR "Private PS1 IK constants not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_IK_TABLES}")
            list(APPEND _t3_ik_args --ik-tables "${T3_LOCAL_PSX_IK_TABLES}")
        endif()
        set(_t3_logic_gen "${CMAKE_BINARY_DIR}/generated/private_logic")
        add_custom_command(
            OUTPUT "${_t3_logic_gen}/t3_local_divmot.c"
                "${_t3_logic_gen}/t3_local_divmot.h"
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/build_local_divmot_assets.py"
                --bank "${T3_LOCAL_DIVMOT}"
                ${_t3_private_common_args}
                ${_t3_sequence_args}
                ${_t3_spline_args}
                ${_t3_trig_args}
                ${_t3_ik_args}
                --output "${_t3_logic_gen}"
            DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_local_divmot_assets.py"
                "${CMAKE_SOURCE_DIR}/scripts/divmot_to_logic.py"
                "${CMAKE_SOURCE_DIR}/scripts/extract_psx_sequences.py"
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
        add_executable(test_branch_rules tests/test_branch_rules.c)
        target_link_libraries(test_branch_rules PRIVATE tekken3_fight)
        target_compile_options(test_branch_rules PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_branch_rules_and_sequences COMMAND test_branch_rules)
        add_executable(test_attack_records tests/test_attack_records.c)
        target_link_libraries(test_attack_records PRIVATE tekken3_fight)
        target_compile_options(test_attack_records PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_baked_attack_segments COMMAND test_attack_records)
        add_executable(test_psx_collision tests/test_psx_collision.c)
        target_link_libraries(test_psx_collision PRIVATE tekken3_fight)
        target_compile_options(test_psx_collision PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_world_segments_and_hurt_zones COMMAND test_psx_collision)
        add_executable(test_psx_motion tests/test_psx_motion.c)
        target_link_libraries(test_psx_motion PRIVATE tekken3_fight)
        target_compile_options(test_psx_motion PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_motion_streams_and_spline_tables COMMAND test_psx_motion)
        add_executable(test_psx_pose tests/test_psx_pose.c)
        target_link_libraries(test_psx_pose PRIVATE tekken3_fight)
        target_compile_options(test_psx_pose PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_euler_fk_partial_joints COMMAND test_psx_pose)
        add_executable(test_psx_ik tests/test_psx_ik.c)
        target_link_libraries(test_psx_ik PRIVATE tekken3_fight)
        target_compile_options(test_psx_ik PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_four_limb_ik_completion COMMAND test_psx_ik)
        add_executable(test_psx_contact tests/test_psx_contact.c)
        target_link_libraries(test_psx_contact PRIVATE tekken3_fight)
        target_compile_options(test_psx_contact PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_animated_sweep_and_14_hurt_zones COMMAND test_psx_contact)
        add_executable(test_psx_root tests/test_psx_root.c)
        target_link_libraries(test_psx_root PRIVATE tekken3_fight)
        target_compile_options(test_psx_root PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_root_motion_facing_and_scale COMMAND test_psx_root)
        add_executable(test_psx_trace tests/test_psx_trace.c)
        target_link_libraries(test_psx_trace PRIVATE tekken3_fight)
        target_compile_options(test_psx_trace PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_pose_oracle_trace_encoding COMMAND test_psx_trace)
    endif()
endif()
