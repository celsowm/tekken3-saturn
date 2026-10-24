cmake_minimum_required(VERSION 3.24)
project(tekken3_saturn VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Platform-free deterministic core. Only the frontend uses LibSaturn hardware;
# the core also consumes its standalone integer-math header.
add_library(tekken3_fight STATIC src/game/sha256.c src/game/fight.c src/game/input_history.c src/game/divmot.c src/game/move_index.c src/game/move_vm.c src/game/move_clock.c src/game/branch_rules.c src/game/attack_records.c src/game/psx_collision.c src/game/psx_motion.c src/game/psx_pose.c src/game/psx_ik.c src/game/psx_contact.c src/game/psx_root.c src/game/psx_trace.c src/game/psx_shadow_combat.c src/game/psx_hit_damage.c src/game/psx_reaction.c src/game/psx_hit_apply.c src/game/psx_hit_test_gate.c src/game/psx_move_events.c src/game/psx_combat_pipeline.c src/game/psx_state_transition.c src/game/psx_movevm_bridge.c)
target_include_directories(tekken3_fight PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src/game>")
target_compile_options(tekken3_fight PRIVATE -Wall -Wextra -Wpedantic)
target_sources(tekken3_fight PRIVATE src/game/move_lifecycle.c)
target_sources(tekken3_fight PRIVATE src/game/psx_ground.c src/game/psx_body.c src/game/psx_exact_contact.c src/game/psx_hit_transaction.c)
target_sources(tekken3_fight PRIVATE src/game/psx_match.c src/game/psx_round.c src/game/psx_round_flow.c)
target_sources(tekken3_fight PRIVATE src/game/psx_replay.c)
target_sources(tekken3_fight PRIVATE src/game/psx_blend_math.c src/game/psx_blend.c)
target_sources(tekken3_fight PRIVATE src/game/psx_head.c)
target_sources(tekken3_fight PRIVATE src/game/psx_ai.c src/game/psx_ai_game.c)
target_sources(tekken3_fight PRIVATE src/game/psx_camera.c src/game/psx_match_camera.c)
target_sources(tekken3_fight PRIVATE src/game/stage_backdrop.c)
target_sources(tekken3_fight PRIVATE src/game/tick_clock.c)
target_sources(tekken3_fight PRIVATE src/game/psx_local_pose.c)
target_sources(tekken3_fight PRIVATE src/game/branch_input_filter.c)
target_sources(tekken3_fight PRIVATE src/game/psx_world_pose.c)

# PRIVATE original fight camera tables (scripts/build_local_camera_assets.py):
# the USA executable's tables from the private import and the fighter's raw
# DIVMOT bank (BNS record; its camera sections). Never in public builds.
set(T3_LOCAL_CAMERA_BANK "" CACHE FILEPATH "Private raw DIVMOT bank (BNS record) for the original fight camera")
set(T3_LOCAL_SECOND_CAMERA_BANK "" CACHE FILEPATH "Private raw P2 DIVMOT bank for its fight camera")
option(T3_COMPACT_CAMERA_STREAMS "Keep original compact camera stream words" OFF)
option(T3_COLD_CAMERA_STREAMS "Load compact camera stream words from CAMERA.BIN to Low WRAM" OFF)
set(T3_LOCAL_CAMERA_SPLINE "${CMAKE_SOURCE_DIR}/assets/imported/tekken3-usa/pose-tables/psx_spline.bin"
    CACHE FILEPATH "Private 0x660-byte PS1 spline window for the camera streams")
function(t3_add_private_camera target)
    if(NOT EXISTS "${T3_LOCAL_CAMERA_BANK}" OR NOT EXISTS "${T3_LOCAL_CAMERA_SPLINE}")
        message(FATAL_ERROR "The original fight camera requires T3_LOCAL_CAMERA_BANK and T3_LOCAL_CAMERA_SPLINE")
    endif()
    set(_t3_second_camera_args "")
    set(_t3_camera_format_args "")
    set(_t3_camera_outputs "")
    if(T3_COLD_CAMERA_STREAMS)
        if(NOT T3_COMPACT_CAMERA_STREAMS OR NOT T3_COLD_LOW_WRAM)
            message(FATAL_ERROR "T3_COLD_CAMERA_STREAMS requires compact streams and cold Low WRAM")
        endif()
        list(APPEND _t3_camera_format_args --cold-streams)
        list(APPEND _t3_camera_outputs "${CMAKE_BINARY_DIR}/generated/private_camera/CAMERA.BIN")
        set(_t3_camera_disc_file "${CMAKE_BINARY_DIR}/generated/private_camera/CAMERA.BIN" PARENT_SCOPE)
    endif()
    if(T3_COMPACT_CAMERA_STREAMS)
        list(APPEND _t3_camera_format_args --compact-streams)
    endif()
    if(T3_LOCAL_SECOND_CAMERA_BANK)
        if(NOT EXISTS "${T3_LOCAL_SECOND_CAMERA_BANK}")
            message(FATAL_ERROR "T3_LOCAL_SECOND_CAMERA_BANK does not exist")
        endif()
        list(APPEND _t3_second_camera_args --second-bank "${T3_LOCAL_SECOND_CAMERA_BANK}")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(_gen "${CMAKE_BINARY_DIR}/generated/private_camera")
    add_custom_command(OUTPUT "${_gen}/t3_local_camera.c" "${_gen}/t3_local_camera.h"
            ${_t3_camera_outputs}
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/build_local_camera_assets.py"
            --bank "${T3_LOCAL_CAMERA_BANK}" --spline-tables "${T3_LOCAL_CAMERA_SPLINE}"
            --output "${_gen}" ${_t3_second_camera_args} ${_t3_camera_format_args}
        DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_local_camera_assets.py"
            "${CMAKE_SOURCE_DIR}/scripts/psx_camera_tables.py"
            "${CMAKE_SOURCE_DIR}/scripts/divmot_to_logic.py"
            "${T3_LOCAL_CAMERA_BANK}" "${T3_LOCAL_CAMERA_SPLINE}"
            ${T3_LOCAL_SECOND_CAMERA_BANK}
        VERBATIM COMMENT "Embed PRIVATE original fight camera tables (never distribute)")
    target_sources(${target} PRIVATE "${_gen}/t3_local_camera.c")
    target_include_directories(${target} PRIVATE "${_gen}")
endfunction()

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    # These new blend routines otherwise exceed High WRAM's stack boundary.
    # Restrict size optimization to this small component; retain the measured
    # O3 pose/skinning/owned-match path. Runtime cadence is checked separately.
    set_source_files_properties(src/game/psx_blend.c src/game/psx_blend_math.c
        PROPERTIES COMPILE_OPTIONS "-Os")
    # The CPU AI's bus glue: its hot reads are memoized, so size wins.
    # The AI itself costs about 280 of a tick's 2,000 FRC/128 in Mednafen; -Os
    # frees 17 KB of High WRAM against -O3.
    set_source_files_properties(src/game/psx_ai_game.c src/game/psx_ai.c
        PROPERTIES COMPILE_OPTIONS "-Os")
    # A freestanding GCC 13 SH-2 toolchain has no hosted system stdint.h.
    # Without -ffreestanding, GCC's own stdint.h attempts include_next.
    # The linker already collects unused sections. Keep optional original-flow
    # entry points independently collectible while the frontend uses mode 1.
    target_compile_options(tekken3_fight PRIVATE -m2 -mb -ffreestanding -ffunction-sections -fdata-sections)
    find_package(LibSaturn 0.1 CONFIG REQUIRED)
    target_include_directories(tekken3_fight PRIVATE
        "$<TARGET_PROPERTY:LibSaturn::Saturn,INTERFACE_INCLUDE_DIRECTORIES>")

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
    set(T3_LOCAL_CHARACTER_ARC "" CACHE FILEPATH "Private matching original character texture ARC")
    set(T3_LOCAL_ACTOR0_KMD "" CACHE FILEPATH "Private P1 skeleton override; render mesh still uses T3_LOCAL_KMD")
    set(T3_LOCAL_ACTOR1_KMD "" CACHE FILEPATH "Private P2 skeleton override; render mesh still uses T3_LOCAL_KMD")
    set(T3_LOCAL_ACTOR1_MESH_KMD "" CACHE FILEPATH "Private P2 render mesh of a second character")
    set(T3_LOCAL_ACTOR1_ARC "" CACHE FILEPATH "Private texture ARC matching T3_LOCAL_ACTOR1_MESH_KMD")
    if((T3_LOCAL_ACTOR0_KMD OR T3_LOCAL_ACTOR1_KMD OR T3_LOCAL_ACTOR1_MESH_KMD) AND NOT T3_LOCAL_KMD)
        message(FATAL_ERROR "T3_LOCAL_ACTOR0_KMD, T3_LOCAL_ACTOR1_KMD and T3_LOCAL_ACTOR1_MESH_KMD require T3_LOCAL_KMD")
    endif()
    if(T3_LOCAL_ACTOR1_ARC AND NOT T3_LOCAL_ACTOR1_MESH_KMD)
        message(FATAL_ERROR "T3_LOCAL_ACTOR1_ARC requires T3_LOCAL_ACTOR1_MESH_KMD")
    endif()
    if(T3_LOCAL_CHARACTER_ARC AND NOT T3_LOCAL_KMD)
        message(FATAL_ERROR "T3_LOCAL_CHARACTER_ARC requires T3_LOCAL_KMD")
    endif()
    if(T3_LOCAL_KMD)
        get_filename_component(T3_LOCAL_KMD "${T3_LOCAL_KMD}" ABSOLUTE)
        if(NOT EXISTS "${T3_LOCAL_KMD}")
            message(FATAL_ERROR "T3_LOCAL_KMD file not found: ${T3_LOCAL_KMD}")
        endif()
        set(_t3_texture_args "")
        if(T3_LOCAL_ACTOR0_KMD)
            get_filename_component(T3_LOCAL_ACTOR0_KMD "${T3_LOCAL_ACTOR0_KMD}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_ACTOR0_KMD}")
                message(FATAL_ERROR "Private P1 KMD not found")
            endif()
            list(APPEND _t3_texture_args --actor0-kmd "${T3_LOCAL_ACTOR0_KMD}")
        endif()
        if(T3_LOCAL_ACTOR1_KMD)
            get_filename_component(T3_LOCAL_ACTOR1_KMD "${T3_LOCAL_ACTOR1_KMD}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_ACTOR1_KMD}")
                message(FATAL_ERROR "Private P2 KMD not found")
            endif()
            list(APPEND _t3_texture_args --actor1-kmd "${T3_LOCAL_ACTOR1_KMD}")
        endif()
        foreach(_t3_input T3_LOCAL_ACTOR1_MESH_KMD T3_LOCAL_ACTOR1_ARC)
            if(${_t3_input})
                get_filename_component(${_t3_input} "${${_t3_input}}" ABSOLUTE)
                if(NOT EXISTS "${${_t3_input}}")
                    message(FATAL_ERROR "${_t3_input} not found")
                endif()
            endif()
        endforeach()
        if(T3_LOCAL_ACTOR1_MESH_KMD)
            list(APPEND _t3_texture_args --actor1-mesh-kmd "${T3_LOCAL_ACTOR1_MESH_KMD}")
        endif()
        if(T3_LOCAL_ACTOR1_ARC)
            list(APPEND _t3_texture_args --actor1-textures-arc "${T3_LOCAL_ACTOR1_ARC}")
        endif()
        if(T3_LOCAL_CHARACTER_ARC)
            get_filename_component(T3_LOCAL_CHARACTER_ARC "${T3_LOCAL_CHARACTER_ARC}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_CHARACTER_ARC}")
                message(FATAL_ERROR "Private character texture ARC not found")
            endif()
            list(APPEND _t3_texture_args --textures-arc "${T3_LOCAL_CHARACTER_ARC}")
        endif()
        add_custom_command(
            OUTPUT "${_t3_gen}/t3_generated_character.c"
                "${_t3_gen}/t3_generated_character.h"
                "${_t3_gen}/manifest.json"
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/build_local_kmd_assets.py"
                --kmd "${T3_LOCAL_KMD}"
                ${_t3_texture_args}
                --libsaturn-source "${LIBSATURN_SOURCE_DIR}"
                --output "${_t3_gen}"
            DEPENDS "${T3_LOCAL_KMD}"
                ${T3_LOCAL_ACTOR0_KMD}
                ${T3_LOCAL_ACTOR1_KMD}
                ${T3_LOCAL_ACTOR1_MESH_KMD}
                ${T3_LOCAL_ACTOR1_ARC}
                ${T3_LOCAL_CHARACTER_ARC}
                "${CMAKE_SOURCE_DIR}/scripts/build_local_kmd_assets.py"
                "${CMAKE_SOURCE_DIR}/scripts/kmd_to_ska.py"
                "${CMAKE_SOURCE_DIR}/scripts/compiled_render_plan.py"
                "${CMAKE_SOURCE_DIR}/scripts/psx_character_textures.py"
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
        src/saturn/characters.c src/saturn/move_probe.c src/saturn/psx_skin_bridge.c src/saturn/perf.c src/saturn/sim_profile.c
        src/saturn/disc.c src/saturn/cold_data.c src/saturn/combat_events.c
        "${_t3_gen}/t3_generated_character.c")
    option(T3_COMPILED_RENDERER "Experimental original-asset rendering path (reference remains default)" OFF)
    option(T3_NATIVE_COMMANDS "Experimental native command preparation (requires experimental SDK)" OFF)
    if(T3_NATIVE_COMMANDS)
        if(NOT T3_COMPILED_RENDERER)
            message(FATAL_ERROR "T3_NATIVE_COMMANDS requires T3_COMPILED_RENDERER")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_NATIVE_COMMANDS=1)
    endif()
    if(T3_COMPILED_RENDERER)
        if(NOT T3_LOCAL_KMD)
            message(FATAL_ERROR "T3_COMPILED_RENDERER requires a private original KMD")
        endif()
        target_sources(tekken3_saturn PRIVATE src/saturn/compiled_renderer.c)
        target_compile_definitions(tekken3_saturn PRIVATE T3_COMPILED_RENDERER=1)
    endif()
    option(T3_VERIFY_COMPILED_RENDERER "Compare compiled world vertices with reference skinning every frame" OFF)
    if(T3_VERIFY_COMPILED_RENDERER)
        if(NOT T3_COMPILED_RENDERER)
            message(FATAL_ERROR "T3_VERIFY_COMPILED_RENDERER requires T3_COMPILED_RENDERER")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_VERIFY_COMPILED_RENDERER=1)
    endif()
    option(T3_PARALLEL_GEOMETRY "Prepare the second actor using the existing Slave executor" OFF)
    option(T3_PARALLEL_SKINNING "Parallelize actor skinning before geometry prep" ${T3_PARALLEL_GEOMETRY})
    option(T3_DIRECT_FACE_SPANS "Prepare worker faces directly in the scene queue" ${T3_PARALLEL_GEOMETRY})
    if(T3_DIRECT_FACE_SPANS)
        target_compile_definitions(tekken3_saturn PRIVATE T3_DIRECT_FACE_SPANS=1)
    endif()
    option(T3_PROJECTED_FACE_QUEUE "Keep compact projected scene records in High WRAM" ON)
    set(T3_COLD_FACES "640" CACHE STRING "Capacity of Low WRAM cold scene face records")
    if(T3_COLD_FACES LESS 320 OR T3_COLD_FACES GREATER 1562)
        message(FATAL_ERROR "T3_COLD_FACES must be between 320 and 1562")
    endif()
    target_compile_definitions(tekken3_saturn PRIVATE T3_COLD_FACES=${T3_COLD_FACES})
    if(T3_NATIVE_COMMANDS AND NOT T3_PROJECTED_FACE_QUEUE)
        message(FATAL_ERROR "T3_NATIVE_COMMANDS requires T3_PROJECTED_FACE_QUEUE")
    endif()
    if(T3_DIRECT_FACE_SPANS AND (NOT T3_PROJECTED_FACE_QUEUE OR NOT T3_PARALLEL_GEOMETRY))
        message(FATAL_ERROR "T3_DIRECT_FACE_SPANS requires projected queue and parallel geometry")
    endif()
    if(T3_PROJECTED_FACE_QUEUE)
        target_compile_definitions(tekken3_saturn PRIVATE T3_PROJECTED_FACE_QUEUE=1)
    endif()
    option(T3_TRUSTED_SCENE_FLUSH "Skip the per-face residency census at flush; assets never leave after load" ON)
    if(T3_TRUSTED_SCENE_FLUSH)
        if(NOT T3_PROJECTED_FACE_QUEUE)
            message(FATAL_ERROR "T3_TRUSTED_SCENE_FLUSH requires the projected face queue")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_TRUSTED_SCENE_FLUSH=1)
    endif()
    option(T3_HIGH_GEOMETRY_SCRATCH "Place hot actor geometry in High WRAM" ON)
    option(T3_HIGH_MATERIAL_PROGRAMS "Place compiled material prefixes in High WRAM" ON)
    option(T3_HIGH_TEXTURE_DESCRIPTORS "Place native texture descriptors in High WRAM" ON)
    foreach(_bank_control MATERIAL_PROGRAMS TEXTURE_DESCRIPTORS)
        if(T3_HIGH_${_bank_control})
            target_compile_definitions(tekken3_saturn PRIVATE T3_HIGH_${_bank_control}=1)
        endif()
    endforeach()
    if(T3_HIGH_GEOMETRY_SCRATCH)
        target_compile_definitions(tekken3_saturn PRIVATE T3_HIGH_GEOMETRY_SCRATCH=1)
    endif()
    option(T3_PERF_SAMPLES "Retain a bounded ring of stage timings (diagnostic overhead)" OFF)
    option(T3_SIM_PROFILE "Record nested per-tick simulation FRT costs (diagnostic overhead)" OFF)
    option(T3_MATCH_FAILURE_TRACE "Publish the last original-match phase on a guest failure" OFF)
    if(T3_MATCH_FAILURE_TRACE)
        target_compile_definitions(tekken3_fight PRIVATE T3_MATCH_FAILURE_TRACE=1)
        target_compile_definitions(tekken3_saturn PRIVATE T3_MATCH_FAILURE_TRACE=1)
    endif()
    option(T3_POSE_TRACE "Publish paired debugger pose records every simulation tick" OFF)
    set(T3_SIM_PROFILE_CAPACITY "128" CACHE STRING "Simulation profile ticks retained individually (totals cover all)")
    if(T3_SIM_PROFILE)
        target_compile_definitions(tekken3_fight PRIVATE T3_SIM_PROFILE=1)
        target_compile_definitions(tekken3_saturn PRIVATE T3_SIM_PROFILE=1
            T3_SIM_PROFILE_CAPACITY=${T3_SIM_PROFILE_CAPACITY})
    endif()
    option(T3_CLOSE_CAMERA "Frame fighters at a readable distance with separation-based zoom" ON)
    if(T3_CLOSE_CAMERA)
        target_compile_definitions(tekken3_saturn PRIVATE T3_CLOSE_CAMERA=1)
    endif()
    set(T3_PERF_MODE "0" CACHE STRING "Diagnostic only: 0 full game, 1 no drawing, 2 one rendered actor")
    if(NOT T3_PERF_MODE MATCHES "^[012]$")
        message(FATAL_ERROR "T3_PERF_MODE must be 0, 1 or 2")
    endif()
    target_compile_definitions(tekken3_saturn PRIVATE T3_PERF_MODE=${T3_PERF_MODE})
    if(T3_PERF_SAMPLES)
        target_compile_definitions(tekken3_saturn PRIVATE T3_PERF_SAMPLES=1)
    endif()
    option(T3_VERIFY_PARALLEL_GEOMETRY "Compare every worker-prepared face with serial preparation (diagnostic overhead)" OFF)
    if(T3_VERIFY_PARALLEL_GEOMETRY AND NOT T3_PARALLEL_GEOMETRY)
        message(FATAL_ERROR "T3_VERIFY_PARALLEL_GEOMETRY requires T3_PARALLEL_GEOMETRY")
    endif()
    if(T3_PARALLEL_GEOMETRY)
        target_compile_definitions(tekken3_saturn PRIVATE T3_PARALLEL_GEOMETRY=1)
        if(T3_VERIFY_PARALLEL_GEOMETRY)
            target_compile_definitions(tekken3_saturn PRIVATE T3_VERIFY_PARALLEL_GEOMETRY=1)
        endif()
    endif()
    if(T3_PARALLEL_SKINNING)
        target_compile_definitions(tekken3_saturn PRIVATE T3_PARALLEL_SKINNING=1)
    endif()
    set(T3_RENDER_BENCH_TICK "0" CACHE STRING "Private fixed-pose render benchmark tick; 0 keeps live gameplay")
    if(NOT T3_RENDER_BENCH_TICK MATCHES "^(0|[1-9][0-9]*)$" OR T3_RENDER_BENCH_TICK GREATER 3600)
        message(FATAL_ERROR "T3_RENDER_BENCH_TICK must be 0..3600")
    endif()
    if(T3_RENDER_BENCH_TICK GREATER 0)
        target_compile_definitions(tekken3_saturn PRIVATE T3_RENDER_BENCH_TICK=${T3_RENDER_BENCH_TICK})
    endif()
    target_include_directories(tekken3_saturn PRIVATE
        "${_t3_gen}" "${CMAKE_SOURCE_DIR}/src/saturn")
    # Deliberate local-only diagnostic: embeds copyrighted authored bank data
    # ONLY when a developer passes a local filename. Public CI never sets it.
    set(T3_LOCAL_DIVMOT "" CACHE FILEPATH
        "Private compact character DIVMOT bank (.t3logic), never for distribution")
    if(T3_POSE_TRACE)
        if(NOT T3_LOCAL_DIVMOT)
            message(FATAL_ERROR "T3_POSE_TRACE requires a private T3_LOCAL_DIVMOT bank")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_POSE_TRACE=1)
    endif()
    set(T3_LOCAL_COMMON_DIVMOT "" CACHE FILEPATH
        "Private compact divmot99 bank (optional)")
    set(T3_LOCAL_PSX_SEQUENCES "" CACHE FILEPATH
        "Private US PS1 original 104-command sequence manifest")
    set(T3_LOCAL_PSX_COMMON_BRANCHES "" CACHE FILEPATH
        "Private original EXE common-branch table (12-byte rows)")
    set(T3_LOCAL_PSX_REACTIONS "" CACHE FILEPATH "Private original reaction records")
    set(T3_LOCAL_PSX_BODY_TABLES "" CACHE FILEPATH "Private sqrt/joint/Paul body-radius tables")
    set(T3_LOCAL_PSX_RSQRT_TABLE "" CACHE FILEPATH "Private 768-byte original blend coefficient window")
    set(T3_LOCAL_PSX_GROUND_TABLES "" CACHE FILEPATH "Private concatenated atan/push tables")
    set(T3_LOCAL_PSX_AI_TABLES "" CACHE FILEPATH
        "Private CPU opponent EXE tables (scripts/extract_psx_ai_tables.py)")
    if(T3_LOCAL_PSX_COMMON_BRANCHES AND NOT T3_LOCAL_DIVMOT)
        message(FATAL_ERROR "T3_LOCAL_PSX_COMMON_BRANCHES requires T3_LOCAL_DIVMOT")
    endif()
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
    option(T3_ENABLE_PSX_SHADOW_CONTACT
        "Private read-only PS1 contact probe with debugger-supplied world roots" OFF)
    if(T3_ENABLE_PSX_SHADOW_CONTACT)
        if(NOT T3_LOCAL_KMD OR NOT T3_LOCAL_DIVMOT OR
           NOT T3_LOCAL_PSX_TRIG_TABLE OR NOT T3_LOCAL_PSX_IK_TABLES)
            message(FATAL_ERROR "PS1 shadow contact requires private KMD, "
                "DIVMOT, trig and IK tables; no guessed inputs allowed")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE
            T3_ENABLE_PSX_SHADOW_CONTACT=1)
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
    option(T3_ENABLE_PSX_MOVE_LIFECYCLE "Experimental original begin/queue move clock (not combat parity)" OFF)
    if(T3_COMPILED_RENDERER AND NOT T3_ENABLE_PSX_SKIN_PREVIEW)
        message(FATAL_ERROR "T3_COMPILED_RENDERER requires T3_ENABLE_PSX_SKIN_PREVIEW")
    endif()
    option(T3_ENABLE_PSX_OWNED_MATCH "Experimental original owned movement state (damage not enabled)" OFF)
    option(T3_FRAME_ON_SUBMIT "Show completed VDP1 lists rather than automatic redraws" ON)
    set(_t3_overlap_default OFF)
    if(T3_LOCAL_KMD AND T3_ENABLE_PSX_OWNED_MATCH AND T3_FRAME_ON_SUBMIT)
        set(_t3_overlap_default ON)
    endif()
    option(T3_OVERLAP_VDP1 "Prepare the next staged image while the preceding image draws/swaps" ${_t3_overlap_default})
    if(T3_OVERLAP_VDP1)
        if(NOT T3_FRAME_ON_SUBMIT)
            message(FATAL_ERROR "T3_OVERLAP_VDP1 requires complete-list frame-on-submit presentation")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_OVERLAP_VDP1=1)
    endif()
    set(T3_WORLD_POSE_CACHE_ENTRIES "0" CACHE STRING "Experimental exact post-blend composition cache; 0 disables")
    if(NOT T3_WORLD_POSE_CACHE_ENTRIES MATCHES "^(0|4|8|16|32|64|128|256)$")
        message(FATAL_ERROR "T3_WORLD_POSE_CACHE_ENTRIES must be 0 or a power of two from 4 to 256")
    endif()
    if(T3_WORLD_POSE_CACHE_ENTRIES GREATER 0 AND NOT T3_ENABLE_PSX_OWNED_MATCH)
        message(FATAL_ERROR "World composition reuse requires the owned match")
    endif()
    target_compile_definitions(tekken3_saturn PRIVATE T3_WORLD_POSE_CACHE_ENTRIES=${T3_WORLD_POSE_CACHE_ENTRIES})
    option(T3_CPU_DIAGNOSTICS "Inspect actual cache state at boot on both SH-2 CPUs" OFF)
    option(T3_SHARE_ACTOR_RESOURCES "Experimental shared immutable textures/programs for identical original actors" OFF)
    option(T3_ASSET_TOPOLOGY "Read immutable decoded topology directly from the resident original archive" OFF)
    if(T3_ASSET_TOPOLOGY)
        if(NOT T3_LOCAL_KMD)
            message(FATAL_ERROR "Resident asset topology requires the private original asset")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_ASSET_TOPOLOGY=1)
    endif()
    if(T3_SHARE_ACTOR_RESOURCES)
        if(NOT T3_LOCAL_KMD OR NOT T3_LOCAL_CHARACTER_ARC)
            message(FATAL_ERROR "Shared actor resources require the original mesh and texture archive")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_SHARE_ACTOR_RESOURCES=1)
    endif()
    set(T3_LOCAL_PAUL_STAGE "" CACHE PATH "Private USA import containing raw/036.bin, raw/056.bin and SLUS_004.02")
    set(_t3_stage_default OFF)
    if(T3_LOCAL_PAUL_STAGE)
        set(_t3_stage_default ON)
    endif()
    option(T3_TITLE_SCREEN "Show a title screen and wait for START before the first fight" OFF)
    if(T3_TITLE_SCREEN)
        target_compile_definitions(tekken3_saturn PRIVATE T3_TITLE_SCREEN=1)
    endif()
    set(T3_LOCAL_SFX "" CACHE PATH "Private USA import (SLUS_004.02, raw/072, 073, 080; Law ARC via T3_LOCAL_ACTOR1_ARC): adds the original combat sounds and voices")
    if(T3_LOCAL_SFX)
        set(_t3_sfx_gen "${CMAKE_BINARY_DIR}/generated/private_sfx")
        add_custom_command(OUTPUT "${_t3_sfx_gen}/SFX.BIN" "${_t3_sfx_gen}/t3_local_sfx.h"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/extract_psx_sfx.py"
                --exe "${T3_LOCAL_SFX}/SLUS_004.02" --out "${_t3_sfx_gen}/tables" --generated "${_t3_sfx_gen}"
                --voice "0:0:${T3_LOCAL_SFX}/raw/072.bin:${T3_LOCAL_SFX}/raw/073.bin"
                --voice "1:1:${T3_LOCAL_SFX}/raw/080.bin:${T3_LOCAL_ACTOR1_ARC}"
            DEPENDS "${CMAKE_SOURCE_DIR}/scripts/extract_psx_sfx.py" "${T3_LOCAL_SFX}/SLUS_004.02"
                "${T3_LOCAL_SFX}/raw/072.bin" "${T3_LOCAL_SFX}/raw/073.bin" "${T3_LOCAL_SFX}/raw/080.bin"
                "${T3_LOCAL_ACTOR1_ARC}" VERBATIM)
        target_sources(tekken3_saturn PRIVATE src/saturn/sfx.c "${_t3_sfx_gen}/t3_local_sfx.h")
        target_include_directories(tekken3_saturn PRIVATE "${_t3_sfx_gen}")
        list(APPEND _t3_stage_disc_files "${_t3_sfx_gen}/SFX.BIN")
        target_compile_definitions(tekken3_saturn PRIVATE T3_SFX=1)
    endif()
    set(T3_LOCAL_HUD_EXE "" CACHE FILEPATH "Private USA SLUS_004.02 for original fight HUD sprites")
    option(T3_ORIGINAL_HUD "Draw private original fight HUD bar, timer and mark sprites" OFF)
    if(T3_ORIGINAL_HUD)
        option(T3_ORIGINAL_HUD_BARS "Draw original health bar sprites (off isolates their cost)" ON)
        if(NOT T3_LOCAL_HUD_EXE OR NOT EXISTS "${T3_LOCAL_HUD_EXE}")
            message(FATAL_ERROR "T3_ORIGINAL_HUD requires T3_LOCAL_HUD_EXE")
        endif()
        set(_t3_hud_gen "${CMAKE_BINARY_DIR}/generated/private_hud")
        add_custom_command(OUTPUT "${_t3_hud_gen}/HUD.BIN"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/extract_psx_hud.py"
                --exe "${T3_LOCAL_HUD_EXE}" --out "${_t3_hud_gen}"
            DEPENDS "${CMAKE_SOURCE_DIR}/scripts/extract_psx_hud.py"
                "${CMAKE_SOURCE_DIR}/third_party/Tekken3Remake/tools/research/system_textures.py"
                "${T3_LOCAL_HUD_EXE}"
            VERBATIM)
        target_sources(tekken3_saturn PRIVATE src/saturn/original_hud.c
            "${_t3_hud_gen}/HUD.BIN")
        list(APPEND _t3_stage_disc_files "${_t3_hud_gen}/HUD.BIN")
        target_compile_definitions(tekken3_saturn PRIVATE T3_ORIGINAL_HUD=1)
        if(T3_ORIGINAL_HUD_BARS)
            target_compile_definitions(tekken3_saturn PRIVATE T3_ORIGINAL_HUD_BARS=1)
        endif()
    endif()
    option(T3_VDP2_FLOOR "Draw the private Paul stage with NBG0 and RBG0" ${_t3_stage_default})
    if(T3_VDP2_FLOOR)
        if(NOT T3_LOCAL_PAUL_STAGE)
            message(FATAL_ERROR "T3_VDP2_FLOOR requires T3_LOCAL_PAUL_STAGE")
        endif()
        if(NOT T3_LOCAL_KMD OR NOT T3_ENABLE_PSX_OWNED_MATCH OR NOT T3_FRAME_ON_SUBMIT)
            message(FATAL_ERROR "Private Paul stage requires the original owned encounter and frame-on-submit presentation")
        endif()
        set(_t3_stage_gen "${CMAKE_BINARY_DIR}/generated/private_stage")
        add_custom_command(OUTPUT "${_t3_stage_gen}/PAUL.STG" "${_t3_stage_gen}/t3_local_stage.h"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/build_local_paul_stage.py"
                --source "${T3_LOCAL_PAUL_STAGE}" --output "${_t3_stage_gen}"
            DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_local_paul_stage.py"
                "${CMAKE_SOURCE_DIR}/scripts/panorama_raycast.py"
                "${CMAKE_SOURCE_DIR}/scripts/psx_character_textures.py"
                "${T3_LOCAL_PAUL_STAGE}/raw/036.bin" "${T3_LOCAL_PAUL_STAGE}/raw/056.bin"
                "${T3_LOCAL_PAUL_STAGE}/SLUS_004.02" VERBATIM)
        target_sources(tekken3_saturn PRIVATE "${_t3_stage_gen}/t3_local_stage.h")
        target_sources(tekken3_saturn PRIVATE src/saturn/stage_vdp2.c)
        target_include_directories(tekken3_saturn PRIVATE "${_t3_stage_gen}")
        list(APPEND _t3_stage_disc_files "${_t3_stage_gen}/PAUL.STG")
        target_compile_definitions(tekken3_saturn PRIVATE T3_VDP2_FLOOR=1)
        option(T3_STAGE_BENCH_EVERY_FRAME "Profiling only: remap the stage layers every frame" OFF)
        if(T3_STAGE_BENCH_EVERY_FRAME)
            target_compile_definitions(tekken3_saturn PRIVATE T3_STAGE_BENCH_EVERY_FRAME=1)
        endif()
    endif()
    set(_t3_fused_default OFF)
    if(T3_VDP2_FLOOR AND T3_LOCAL_CHARACTER_ARC AND T3_PARALLEL_GEOMETRY AND
       T3_PARALLEL_SKINNING AND T3_DIRECT_FACE_SPANS AND
       NOT T3_COMPILED_RENDERER AND NOT T3_NATIVE_COMMANDS AND NOT T3_VERIFY_PARALLEL_GEOMETRY)
        set(_t3_fused_default ON)
    endif()
    option(T3_FUSED_ACTOR_PREPARATION "Prepare each owned actor from skin through faces in one parallel range" ${_t3_fused_default})
    if(T3_FUSED_ACTOR_PREPARATION)
        if(NOT T3_PARALLEL_GEOMETRY OR NOT T3_PARALLEL_SKINNING OR
           NOT T3_DIRECT_FACE_SPANS OR NOT T3_ENABLE_PSX_OWNED_MATCH OR
           T3_NATIVE_COMMANDS OR T3_VERIFY_PARALLEL_GEOMETRY)
            message(FATAL_ERROR "Fused actors require owned match, parallel skin/geometry and direct spans without native commands or geometry verification")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_FUSED_ACTOR_PREPARATION=1)
    endif()
    set(_t3_skin_screen_default OFF)
    if(T3_FUSED_ACTOR_PREPARATION AND NOT T3_COMPILED_RENDERER)
        set(_t3_skin_screen_default ON)
    endif()
    option(T3_SKIN_TO_SCREEN "Skin fused actors straight to screen through camera-concatenated joints" ${_t3_skin_screen_default})
    if(T3_SKIN_TO_SCREEN)
        if(NOT T3_FUSED_ACTOR_PREPARATION)
            message(FATAL_ERROR "T3_SKIN_TO_SCREEN requires T3_FUSED_ACTOR_PREPARATION=ON")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_SKIN_TO_SCREEN=1)
    endif()

    set(_t3_branch_default OFF)
    option(T3_SLAVE_SIMULATION "Experimental serial simulation overlapped with painter flush" OFF)
    if(T3_SLAVE_SIMULATION)
        # Input tapes only read or record inputs; the Slave loop replays them (CPU capture: move_probe.c).
        if(NOT T3_FUSED_ACTOR_PREPARATION OR T3_SIM_PROFILE OR T3_PERF_SAMPLES OR T3_POSE_TRACE)
            message(FATAL_ERROR "Slave simulation requires fused actors without intrusive simulation instrumentation")
        endif()
        enable_language(ASM)
        target_sources(tekken3_saturn PRIVATE src/saturn/simulation_stack.S)
        target_compile_definitions(tekken3_saturn PRIVATE T3_SLAVE_SIMULATION=1)
    endif()
    # The Slave skins actor 1 and then runs the deferred ticks; the Master,
    # which waited for actor 1 anyway, submits its faces.
    option(T3_MASTER_WORKER_FACES "The Master submits the worker actor's faces; the worker only skins it before its ticks" ${T3_SLAVE_SIMULATION})
    if(T3_MASTER_WORKER_FACES)
        if(NOT T3_FUSED_ACTOR_PREPARATION)
            message(FATAL_ERROR "T3_MASTER_WORKER_FACES requires T3_FUSED_ACTOR_PREPARATION=ON")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_MASTER_WORKER_FACES=1)
    endif()
    if(T3_FUSED_ACTOR_PREPARATION AND T3_VDP2_FLOOR AND
       T3_LOCAL_CHARACTER_ARC AND T3_LOCAL_DIVMOT)
        set(_t3_branch_default ON)
    endif()
    option(T3_BRANCH_INPUT_FILTER "Skip branch rows whose current-input condition is provably impossible" ${_t3_branch_default})
    if(T3_BRANCH_INPUT_FILTER)
        if(NOT T3_LOCAL_DIVMOT)
            message(FATAL_ERROR "Branch input filtering requires a private DIVMOT bank")
        endif()
        target_compile_definitions(tekken3_fight PRIVATE T3_BRANCH_INPUT_FILTER=1)
        target_compile_definitions(tekken3_saturn PRIVATE T3_BRANCH_INPUT_FILTER=1)
    endif()
    option(T3_STAGE_HUD "Show completed frame stage timings in the diagnostic HUD" OFF)
    if(T3_STAGE_HUD)
        if(NOT T3_PERF_SAMPLES)
            message(FATAL_ERROR "T3_STAGE_HUD requires T3_PERF_SAMPLES=ON")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_STAGE_HUD=1)
    endif()
    option(T3_NATIVE_WAIT_PROBE "Show native VDP1 submission wait timing in the diagnostic HUD" OFF)
    if(T3_NATIVE_WAIT_PROBE)
        target_compile_definitions(tekken3_saturn PRIVATE T3_NATIVE_WAIT_PROBE=1)
        option(T3_NATIVE_WAIT_PROBE_SILENT "Collect native wait counters without drawing the diagnostic HUD" OFF)
        if(T3_NATIVE_WAIT_PROBE_SILENT)
            target_compile_definitions(tekken3_saturn PRIVATE T3_NATIVE_WAIT_PROBE_SILENT=1)
        endif()
    endif()
    option(T3_SLAVE_FRT_PROBE "Keep Slave tick, preparation and stage FRT totals in Low WRAM for a Mednafen save state" OFF)
    if(T3_SLAVE_FRT_PROBE)
        target_compile_definitions(tekken3_saturn PRIVATE T3_SLAVE_FRT_PROBE=1)
    endif()
    option(T3_NATIVE_PHASE_PROBE "Show per-actor fused preparation timings in the diagnostic HUD" OFF)
    if(T3_NATIVE_PHASE_PROBE)
        if(NOT T3_FUSED_ACTOR_PREPARATION)
            message(FATAL_ERROR "T3_NATIVE_PHASE_PROBE requires T3_FUSED_ACTOR_PREPARATION=ON")
        endif()
        if(T3_NATIVE_WAIT_PROBE OR T3_STAGE_HUD)
            message(FATAL_ERROR "T3_NATIVE_PHASE_PROBE needs its own diagnostic HUD column")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_NATIVE_PHASE_PROBE=1)
        option(T3_NATIVE_PHASE_PROBE_SILENT "Collect per-actor phase sums without drawing the diagnostic HUD" OFF)
        if(T3_NATIVE_PHASE_PROBE_SILENT)
            target_compile_definitions(tekken3_saturn PRIVATE T3_NATIVE_PHASE_PROBE_SILENT=1)
        endif()
    endif()
    option(T3_MEMORY_PROBE "Time loads, stores, MAC and divides at boot (g_t3_memory_probe)" OFF)
    if(T3_MEMORY_PROBE)
        target_sources(tekken3_saturn PRIVATE src/saturn/memory_probe.c)
        target_compile_definitions(tekken3_saturn PRIVATE T3_MEMORY_PROBE=1)
    endif()
    option(T3_DMA_COMMAND_LIST "Experiment with SCU-DMA VDP1 command-list submission" OFF)
    if(T3_DMA_COMMAND_LIST)
        target_compile_definitions(tekken3_saturn PRIVATE T3_DMA_COMMAND_LIST=1)
    endif()
    option(T3_HIGH_SORT_KEYS "Experiment with scene painter keys in High WRAM" OFF)
    if(T3_HIGH_SORT_KEYS)
        target_compile_definitions(tekken3_saturn PRIVATE T3_HIGH_SORT_KEYS=1)
    endif()
    if(T3_CPU_DIAGNOSTICS)
        target_compile_definitions(tekken3_saturn PRIVATE T3_CPU_DIAGNOSTICS=1)
    endif()
    if(T3_FRAME_ON_SUBMIT)
        target_compile_definitions(tekken3_saturn PRIVATE T3_FRAME_ON_SUBMIT=1)
    endif()
    set(_t3_pose_cache_default "0")
    if(T3_ENABLE_PSX_OWNED_MATCH)
        set(_t3_pose_cache_default "256")
    endif()
    set(T3_LOCAL_POSE_CACHE_ENTRIES "${_t3_pose_cache_default}" CACHE STRING "Bounded sample and local FK/IK cache; 0 disables")
    if(NOT T3_LOCAL_POSE_CACHE_ENTRIES MATCHES "^(0|8|32|64|128|256)$")
        message(FATAL_ERROR "T3_LOCAL_POSE_CACHE_ENTRIES must be 0, 8, 32, 64, 128 or 256")
    endif()
    if(T3_LOCAL_POSE_CACHE_ENTRIES GREATER 0 AND NOT T3_ENABLE_PSX_OWNED_MATCH)
        message(FATAL_ERROR "Local FK/IK reuse requires T3_ENABLE_PSX_OWNED_MATCH")
    endif()
    target_compile_definitions(tekken3_saturn PRIVATE
        T3_LOCAL_POSE_CACHE_ENTRIES=${T3_LOCAL_POSE_CACHE_ENTRIES})
    option(T3_ENABLE_PSX_COMBAT "Experimental bounded original damage and reactions" OFF)
    option(T3_CAPTURE_INPUT_TAPE "Record private tick-indexed simulation inputs in Low WRAM" OFF)
    set(T3_LOCAL_INPUT_TAPE "" CACHE FILEPATH "Private captured simulation inputs for a demonstration CD")
    set(T3_LOCAL_REFERENCE_REPLAY "" CACHE PATH "Private passing host/original export for an instrumented Saturn oracle")
    option(T3_REFERENCE_TRACE_HIGH "Store private reference digests in High WRAM" OFF)
    option(T3_REFERENCE_DEBUG_VALUES "Capture first reference tick's raw values in Low WRAM" OFF)
    set(T3_REFERENCE_DEBUG_TICK "0" CACHE STRING "Zero-based reference tick for raw diagnostic values")
    if(T3_LOCAL_REFERENCE_REPLAY)
        if(NOT T3_ENABLE_PSX_COMBAT OR T3_CAPTURE_INPUT_TAPE OR T3_LOCAL_INPUT_TAPE OR T3_RENDER_BENCH_TICK GREATER 0)
            message(FATAL_ERROR "Reference replay requires live original combat and its own external input fixture")
        endif()
        set(_t3_reference "${CMAKE_BINARY_DIR}/generated/private_reference/t3_reference_fixture.h")
        set(_t3_reference_views "")
        if(EXISTS "${T3_LOCAL_REFERENCE_REPLAY}/views.bin")
            set(_t3_reference_views "${T3_LOCAL_REFERENCE_REPLAY}/views.bin")
        endif()
        add_custom_command(OUTPUT "${_t3_reference}"
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/build_reference_replay.py"
                "${T3_LOCAL_REFERENCE_REPLAY}" "${_t3_reference}"
            DEPENDS "${CMAKE_SOURCE_DIR}/scripts/build_reference_replay.py"
                "${T3_LOCAL_REFERENCE_REPLAY}/manifest.json"
                "${T3_LOCAL_REFERENCE_REPLAY}/inputs.tsv" "${T3_LOCAL_REFERENCE_REPLAY}/seed.ranges"
                "${T3_LOCAL_REFERENCE_REPLAY}/seed-addresses.json" "${T3_LOCAL_REFERENCE_REPLAY}/expected.sha256"
                ${_t3_reference_views}
                "${CMAKE_SOURCE_DIR}/tools/psx_match_reference.inc"
                "${CMAKE_SOURCE_DIR}/tools/psx_ground_eval_fields.h"
                "${CMAKE_SOURCE_DIR}/tools/psx_round_flow_eval_fields.h"
            VERBATIM)
        target_sources(tekken3_saturn PRIVATE "${_t3_reference}")
        target_include_directories(tekken3_saturn PRIVATE "${CMAKE_BINARY_DIR}/generated/private_reference" "${CMAKE_SOURCE_DIR}/tools")
        target_compile_definitions(tekken3_saturn PRIVATE T3_REFERENCE_REPLAY=1)
        if(T3_REFERENCE_TRACE_HIGH)
            target_compile_definitions(tekken3_saturn PRIVATE T3_REFERENCE_TRACE_HIGH=1)
        endif()
        if(T3_REFERENCE_DEBUG_VALUES)
            target_compile_definitions(tekken3_saturn PRIVATE T3_REFERENCE_DEBUG_VALUES=1
                T3_REFERENCE_DEBUG_TICK=${T3_REFERENCE_DEBUG_TICK})
        endif()
    endif()
    if(T3_CAPTURE_INPUT_TAPE OR T3_LOCAL_INPUT_TAPE)
        if(NOT T3_ENABLE_PSX_COMBAT)
            message(FATAL_ERROR "Input fixture requires original combat mode")
        endif()
        target_sources(tekken3_saturn PRIVATE src/saturn/input_tape.c)
        if(T3_CAPTURE_INPUT_TAPE)
            target_compile_definitions(tekken3_saturn PRIVATE T3_CAPTURE_INPUT_TAPE=1)
        endif()
        if(T3_LOCAL_INPUT_TAPE)
            set(_t3_tape "${CMAKE_BINARY_DIR}/generated/private_input/t3_private_input_tape.h")
            add_custom_command(OUTPUT "${_t3_tape}"
                COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_input_tape.py"
                    "${T3_LOCAL_INPUT_TAPE}" "${_t3_tape}"
                DEPENDS "${T3_LOCAL_INPUT_TAPE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/build_input_tape.py")
            target_sources(tekken3_saturn PRIVATE "${_t3_tape}")
            target_include_directories(tekken3_saturn PRIVATE "${CMAKE_BINARY_DIR}/generated/private_input")
            target_compile_definitions(tekken3_saturn PRIVATE T3_REPLAY_INPUT_TAPE=1)
        endif()
    endif()

    if(T3_ENABLE_PSX_COMBAT)
        if(NOT T3_ENABLE_PSX_OWNED_MATCH OR NOT T3_LOCAL_PSX_REACTIONS)
            message(FATAL_ERROR "Original combat requires owned state and private reaction records")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_ENABLE_PSX_COMBAT=1)
    endif()
    option(T3_PSX_CAMERA "Original fight camera from private tables (T3_LOCAL_CAMERA_BANK)" OFF)
    if(T3_PSX_CAMERA)
        if(NOT T3_ENABLE_PSX_COMBAT)
            message(FATAL_ERROR "The original fight camera requires original combat (its round flow)")
        endif()
        t3_add_private_camera(tekken3_saturn)
        target_compile_definitions(tekken3_saturn PRIVATE T3_PSX_CAMERA=1)
        if(T3_COLD_CAMERA_STREAMS)
            target_compile_definitions(tekken3_saturn PRIVATE T3_COLD_CAMERA_STREAMS=1)
        endif()
    endif()
    if(T3_ENABLE_PSX_OWNED_MATCH)
        if(NOT T3_LOCAL_PSX_GROUND_TABLES OR NOT T3_LOCAL_PSX_BODY_TABLES OR NOT T3_ENABLE_PSX_SKIN_PREVIEW OR NOT T3_LOCAL_COMMON_DIVMOT)
            message(FATAL_ERROR "Owned original match requires private ground/body tables, shared bank and original skin path")
        endif()
        set(T3_ENABLE_PSX_MOVE_LIFECYCLE ON)
        target_compile_definitions(tekken3_saturn PRIVATE T3_ENABLE_PSX_OWNED_MATCH=1)
    endif()
    if(T3_ENABLE_PSX_MOVE_LIFECYCLE)
        if(NOT T3_LOCAL_DIVMOT)
            message(FATAL_ERROR "Original move lifecycle requires private DIVMOT")
        endif()
        target_compile_definitions(tekken3_saturn PRIVATE T3_ENABLE_PSX_MOVE_LIFECYCLE=1)
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
        set(_t3_common_branch_args "")
        if(T3_LOCAL_PSX_REACTIONS)
            get_filename_component(T3_LOCAL_PSX_REACTIONS "${T3_LOCAL_PSX_REACTIONS}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_REACTIONS}")
                message(FATAL_ERROR "Private reaction records not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_REACTIONS}")
            list(APPEND _t3_common_branch_args --combat-reactions "${T3_LOCAL_PSX_REACTIONS}")
        endif()
        if(T3_LOCAL_PSX_BODY_TABLES)
            get_filename_component(T3_LOCAL_PSX_BODY_TABLES "${T3_LOCAL_PSX_BODY_TABLES}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_BODY_TABLES}")
                message(FATAL_ERROR "Private original body tables not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_BODY_TABLES}")
            list(APPEND _t3_common_branch_args --body-tables "${T3_LOCAL_PSX_BODY_TABLES}")
        endif()
        if(T3_LOCAL_PSX_RSQRT_TABLE)
            get_filename_component(T3_LOCAL_PSX_RSQRT_TABLE "${T3_LOCAL_PSX_RSQRT_TABLE}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_RSQRT_TABLE}")
                message(FATAL_ERROR "Private original blend coefficient window missing")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_RSQRT_TABLE}")
            list(APPEND _t3_common_branch_args --rsqrt-table "${T3_LOCAL_PSX_RSQRT_TABLE}")
        endif()
        if(T3_LOCAL_PSX_AI_TABLES)
            get_filename_component(T3_LOCAL_PSX_AI_TABLES "${T3_LOCAL_PSX_AI_TABLES}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_AI_TABLES}")
                message(FATAL_ERROR "Private CPU opponent tables not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_AI_TABLES}")
            list(APPEND _t3_common_branch_args --ai-tables "${T3_LOCAL_PSX_AI_TABLES}")
        endif()
        if(T3_LOCAL_PSX_GROUND_TABLES)
            get_filename_component(T3_LOCAL_PSX_GROUND_TABLES "${T3_LOCAL_PSX_GROUND_TABLES}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_GROUND_TABLES}")
                message(FATAL_ERROR "Private original ground tables not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_GROUND_TABLES}")
            list(APPEND _t3_common_branch_args --ground-tables "${T3_LOCAL_PSX_GROUND_TABLES}")
        endif()
        if(T3_LOCAL_PSX_COMMON_BRANCHES)
            get_filename_component(T3_LOCAL_PSX_COMMON_BRANCHES "${T3_LOCAL_PSX_COMMON_BRANCHES}" ABSOLUTE)
            if(NOT EXISTS "${T3_LOCAL_PSX_COMMON_BRANCHES}")
                message(FATAL_ERROR "Private EXE common branch table not found")
            endif()
            list(APPEND _t3_private_banks "${T3_LOCAL_PSX_COMMON_BRANCHES}")
            list(APPEND _t3_common_branch_args --common-branches "${T3_LOCAL_PSX_COMMON_BRANCHES}")
        endif()
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
        option(T3_COLD_LOW_WRAM "Load DIVMOT banks into Low WRAM from MOTION.BIN and keep hot state in High WRAM" OFF)
        set(_t3_cold_args "")
        set(_t3_cold_outputs "")
        if(T3_COLD_LOW_WRAM)
            if(NOT T3_LOCAL_COMMON_DIVMOT)
                message(FATAL_ERROR "T3_COLD_LOW_WRAM requires the private common DIVMOT bank")
            endif()
            set(_t3_cold_args --cold-file)
            set(_t3_cold_outputs "${_t3_logic_gen}/MOTION.BIN")
            list(APPEND _t3_stage_disc_files "${_t3_logic_gen}/MOTION.BIN")
            target_compile_definitions(tekken3_saturn PRIVATE T3_COLD_LOW_WRAM=1)
            option(T3_VERIFY_COLD "Re-hash 4 KB of the loaded cold banks per tick; a mismatch panics" OFF)
            if(T3_VERIFY_COLD)
                target_compile_definitions(tekken3_saturn PRIVATE T3_VERIFY_COLD=1)
            endif()
            # Claim LibSaturn's runtime state (VDP1 command staging) for High
            # WRAM ahead of the SDK script's .wram_l rule; main() zeroes it.
            target_link_options(tekken3_saturn PRIVATE
                "-Wl,-T,${CMAKE_SOURCE_DIR}/src/saturn/hot_wram.ld")
            set_property(TARGET tekken3_saturn APPEND PROPERTY LINK_DEPENDS
                "${CMAKE_SOURCE_DIR}/src/saturn/hot_wram.ld")
        endif()
        # The opponent's character bank. Host evaluators compile it from
        # t3_local_second.c; a cold Saturn build loads it from MOTION.BIN
        # and gives it to P2.
        set(T3_LOCAL_SECOND_DIVMOT "" CACHE FILEPATH "Private second character DIVMOT bank (P2)")
        set(T3_SECOND_CHARACTER_ID "1" CACHE STRING "Original character id of the second bank (1: Law)")
        if(T3_LOCAL_SECOND_DIVMOT)
            list(APPEND _t3_cold_args --second-bank "${T3_LOCAL_SECOND_DIVMOT}")
            list(APPEND _t3_private_banks "${T3_LOCAL_SECOND_DIVMOT}")
            if(T3_COLD_LOW_WRAM)
                target_compile_definitions(tekken3_saturn PRIVATE T3_SECOND_CHARACTER=1
                    T3_SECOND_CHARACTER_ID=${T3_SECOND_CHARACTER_ID})
            endif()
        endif()
        add_custom_command(
            OUTPUT "${_t3_logic_gen}/t3_local_divmot.c"
                "${_t3_logic_gen}/t3_local_divmot.h"
                "${_t3_logic_gen}/t3_local_second.c"
                ${_t3_cold_outputs}
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/scripts/build_local_divmot_assets.py"
                --bank "${T3_LOCAL_DIVMOT}"
                ${_t3_private_common_args}
                ${_t3_sequence_args}
                ${_t3_common_branch_args}
                ${_t3_spline_args}
                ${_t3_trig_args}
                ${_t3_ik_args}
                ${_t3_cold_args}
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
    if(T3_COLD_CAMERA_STREAMS)
        list(APPEND _t3_stage_disc_files "${_t3_camera_disc_file}")
    endif()
    target_link_libraries(tekken3_saturn PRIVATE tekken3_fight LibSaturn::Saturn)
    target_compile_options(tekken3_saturn PRIVATE -Wall -Wextra -O2)
    libsaturn_configure_executable(tekken3_saturn)
    libsaturn_add_binary(tekken3_saturn MAX_BYTES 983040)
    libsaturn_add_disc(tekken3_saturn NAME tekken3_saturn
        IP_TEMPLATE_KIND native ROOT_FILES ${_t3_stage_disc_files} ALL)
else()
    find_path(T3_SATURN_MATH_INCLUDE_DIR saturn/int_math.h
        HINTS "${LIBSATURN_SOURCE_DIR}/include" "$ENV{LIBSATURN_PREFIX}/include"
              "${CMAKE_SOURCE_DIR}/../lib-saturn/include"
              "${CMAKE_SOURCE_DIR}/.external/libsaturn/include")
    if(NOT T3_SATURN_MATH_INCLUDE_DIR)
        message(FATAL_ERROR "Set T3_SATURN_MATH_INCLUDE_DIR to the LibSaturn include directory (standalone integer math; no target toolchain needed).")
    endif()
    target_include_directories(tekken3_fight PRIVATE "${T3_SATURN_MATH_INCLUDE_DIR}")
    # Explicit local-only replay against private generated assets. Public CI
    # leaves this empty and neither compiles nor downloads retail data.
    set(T3_PRIVATE_REPLAY_ASSET_DIR "" CACHE PATH "Configured private Saturn build used by the native match replay evaluator")
    if(T3_PRIVATE_REPLAY_ASSET_DIR)
        set(_replay_logic "${T3_PRIVATE_REPLAY_ASSET_DIR}/generated/private_logic")
        set(_replay_character "${T3_PRIVATE_REPLAY_ASSET_DIR}/generated/characters")
        if(NOT EXISTS "${_replay_logic}/t3_local_divmot.c" OR
           NOT EXISTS "${_replay_character}/t3_generated_character.c")
            message(FATAL_ERROR "Private replay requires previously generated original logic and character data")
        endif()
        set(_replay_second "")
        if(EXISTS "${_replay_logic}/t3_local_second.c")
            set(_replay_second "${_replay_logic}/t3_local_second.c")
        endif()
        add_executable(t3_match_replay_eval tools/psx_match_replay_eval.c
            "${_replay_logic}/t3_local_divmot.c" ${_replay_second} "${_replay_character}/t3_generated_character.c")
        target_include_directories(t3_match_replay_eval PRIVATE "${_replay_logic}" "${_replay_character}")
        target_link_libraries(t3_match_replay_eval PRIVATE tekken3_fight)
        target_compile_options(t3_match_replay_eval PRIVATE -Wall -Wextra -Werror)
    endif()
    include(CTest)
    if(BUILD_TESTING)
        add_executable(test_world_pose_cache tests/test_world_pose_cache.c)
        target_link_libraries(test_world_pose_cache PRIVATE tekken3_fight)
        target_compile_options(test_world_pose_cache PRIVATE -UNDEBUG -Wall -Wextra -Werror)
        add_test(NAME world_pose_cache_exact_and_bounded COMMAND test_world_pose_cache)
        add_executable(test_branch_input_filter tests/test_branch_input_filter.c)
        target_link_libraries(test_branch_input_filter PRIVATE tekken3_fight)
        target_compile_options(test_branch_input_filter PRIVATE -UNDEBUG -Wall -Wextra -Werror)
        add_test(NAME branch_input_filter_order_and_fallback COMMAND test_branch_input_filter)
        add_executable(test_compiled_renderer tests/test_compiled_renderer.c
            src/saturn/compiled_renderer.c src/saturn/psx_skin_bridge.c)
        target_include_directories(test_compiled_renderer PRIVATE src/saturn src/game
            "${T3_SATURN_MATH_INCLUDE_DIR}")
        target_compile_options(test_compiled_renderer PRIVATE -Wall -Wextra -Werror)
        add_test(NAME compiled_renderer_exact_arithmetic COMMAND test_compiled_renderer)
        add_executable(test_tick_clock tests/test_tick_clock.c)
        target_link_libraries(test_tick_clock PRIVATE tekken3_fight)
        add_test(NAME scheduling_time_accounting COMMAND test_tick_clock)
        add_executable(test_sha256 tests/test_sha256.c)
        target_link_libraries(test_sha256 PRIVATE tekken3_fight)
        add_test(NAME portable_observation_sha256 COMMAND test_sha256)
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
        add_executable(test_psx_shadow_combat tests/test_psx_shadow_combat.c)
        target_link_libraries(test_psx_shadow_combat PRIVATE tekken3_fight)
        target_compile_options(test_psx_shadow_combat PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_readonly_original_combat_contact COMMAND test_psx_shadow_combat)
        add_executable(test_psx_hit_damage tests/test_psx_hit_damage.c)
        target_link_libraries(test_psx_hit_damage PRIVATE tekken3_fight)
        target_compile_options(test_psx_hit_damage PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_readonly_hit_classification_damage COMMAND test_psx_hit_damage)
        add_executable(test_psx_reaction tests/test_psx_reaction.c)
        target_link_libraries(test_psx_reaction PRIVATE tekken3_fight)
        target_compile_options(test_psx_reaction PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_original_reaction_record_selection COMMAND test_psx_reaction)
        add_executable(test_psx_hit_apply tests/test_psx_hit_apply.c)
        target_link_libraries(test_psx_hit_apply PRIVATE tekken3_fight)
        target_compile_options(test_psx_hit_apply PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_original_hit_extra_health_apply_preview COMMAND test_psx_hit_apply)
        add_executable(test_psx_move_events tests/test_psx_move_events.c)
        target_link_libraries(test_psx_move_events PRIVATE tekken3_fight)
        target_compile_options(test_psx_move_events PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_divmot_frame_extra_damage_events COMMAND test_psx_move_events)
        add_executable(test_psx_hit_test_gate tests/test_psx_hit_test_gate.c)
        target_link_libraries(test_psx_hit_test_gate PRIVATE tekken3_fight)
        target_compile_options(test_psx_hit_test_gate PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_hit_test_gate_validates_original_contact COMMAND test_psx_hit_test_gate)
        add_executable(test_psx_combat_pipeline tests/test_psx_combat_pipeline.c)
        target_link_libraries(test_psx_combat_pipeline PRIVATE tekken3_fight)
        target_compile_options(test_psx_combat_pipeline PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_simultaneous_original_hit_test_hit_apply_pipeline
                 COMMAND test_psx_combat_pipeline)
        add_executable(test_psx_state_transition tests/test_psx_state_transition.c)
        target_link_libraries(test_psx_state_transition PRIVATE tekken3_fight)
        target_compile_options(test_psx_state_transition PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_readonly_hitstun_knockdown_recovery
                 COMMAND test_psx_state_transition)
        add_executable(test_psx_movevm_bridge tests/test_psx_movevm_bridge.c)
        target_link_libraries(test_psx_movevm_bridge PRIVATE tekken3_fight)
        target_compile_options(test_psx_movevm_bridge PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_original_movevm_post_hit_projection
                 COMMAND test_psx_movevm_bridge)
        # Independent numerical reference, imported from our existing
        # pinned research submodule. No commercial disc or fixture is used.
        add_executable(t3_motion_export tools/psx_motion_export.c)
        target_link_libraries(t3_motion_export PRIVATE tekken3_fight)
        target_compile_options(t3_motion_export PRIVATE -Wall -Wextra -Werror)
        add_executable(t3_hit_eval tools/psx_hit_eval.c)
        target_link_libraries(t3_hit_eval PRIVATE tekken3_fight)
        target_compile_options(t3_hit_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(t3_hit_transaction_eval tools/psx_hit_transaction_eval.c)
        target_link_libraries(t3_hit_transaction_eval PRIVATE tekken3_fight)
        target_compile_options(t3_hit_transaction_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(t3_hit_ram_eval tools/psx_hit_ram_eval.c)
        target_link_libraries(t3_hit_ram_eval PRIVATE tekken3_fight)
        target_compile_options(t3_hit_ram_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(t3_exact_contact_eval tools/psx_exact_contact_eval.c)
        target_link_libraries(t3_exact_contact_eval PRIVATE tekken3_fight)
        target_compile_options(t3_exact_contact_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(test_psx_exact_contact tests/test_psx_exact_contact.c)
        target_link_libraries(test_psx_exact_contact PRIVATE tekken3_fight)
        target_compile_options(test_psx_exact_contact PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_exact_collision_and_attack_placement COMMAND test_psx_exact_contact)
        add_executable(t3_contact_ram_eval tools/psx_contact_ram_eval.c)
        target_link_libraries(t3_contact_ram_eval PRIVATE tekken3_fight)
        target_compile_options(t3_contact_ram_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(t3_body_ram_eval tools/psx_body_ram_eval.c)
        target_link_libraries(t3_body_ram_eval PRIVATE tekken3_fight)
        target_compile_options(t3_body_ram_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(t3_ground_ram_eval tools/psx_ground_ram_eval.c)
        target_link_libraries(t3_ground_ram_eval PRIVATE tekken3_fight)
        target_compile_options(t3_ground_ram_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(t3_clock_eval tools/psx_clock_eval.c)
        target_link_libraries(t3_clock_eval PRIVATE tekken3_fight)
        target_compile_options(t3_clock_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(test_move_clock tests/test_move_clock.c)
        target_link_libraries(test_move_clock PRIVATE tekken3_fight)
        add_test(NAME ps1_move_clock_freeze_hold_and_reverse COMMAND test_move_clock)
        add_executable(test_psx_body tests/test_psx_body.c)
        target_link_libraries(test_psx_body PRIVATE tekken3_fight)
        target_compile_options(test_psx_body PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_grounded_body_separation_and_arena COMMAND test_psx_body)
        add_executable(test_psx_hit_transaction tests/test_psx_hit_transaction.c)
        target_link_libraries(test_psx_hit_transaction PRIVATE tekken3_fight)
        target_compile_options(test_psx_hit_transaction PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_owned_hit_transaction_and_ko_collapse COMMAND test_psx_hit_transaction)
        add_executable(test_psx_round tests/test_psx_round.c)
        add_executable(t3_round_score_eval tools/psx_round_score_eval.c)
        target_link_libraries(t3_round_score_eval PRIVATE tekken3_fight)
        target_compile_options(t3_round_score_eval PRIVATE -Wall -Wextra -Werror)
        target_link_libraries(test_psx_round PRIVATE tekken3_fight)
        add_test(NAME ps1_result_and_bounded_round_restart COMMAND test_psx_round)
        add_executable(test_psx_round_flow tests/test_psx_round_flow.c)
        target_link_libraries(test_psx_round_flow PRIVATE tekken3_fight)
        target_compile_options(test_psx_round_flow PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_original_vs_round_flow COMMAND test_psx_round_flow)
        add_executable(test_psx_replay tests/test_psx_replay.c)
        target_link_libraries(test_psx_replay PRIVATE tekken3_fight)
        target_compile_options(test_psx_replay PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_original_replay_ring_and_cursors COMMAND test_psx_replay)
        add_executable(t3_round_flow_eval tools/psx_round_flow_eval.c)
        target_link_libraries(t3_round_flow_eval PRIVATE tekken3_fight)
        target_compile_options(t3_round_flow_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(test_psx_match tests/test_psx_match.c)
        target_link_libraries(test_psx_match PRIVATE tekken3_fight)
        add_test(NAME ps1_owned_ground_match_tick_order COMMAND test_psx_match)
        add_executable(test_psx_head tests/test_psx_head.c)
        target_link_libraries(test_psx_head PRIVATE tekken3_fight)
        target_compile_options(test_psx_head PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_procedural_head_tracking COMMAND test_psx_head)
        # Golden-trace camera check; private inputs from scripts/camera_trace_cases.py.
        add_executable(test_psx_camera_trace tests/test_psx_camera_trace.c)
        target_link_libraries(test_psx_camera_trace PRIVATE tekken3_fight)
        target_compile_options(test_psx_camera_trace PRIVATE -Wall -Wextra -Werror)
        # One test per golden fight trace (scripts/camera_trace_cases.py).
        foreach(_t3_camera_trace paul_law lei_king long_match)
            add_test(NAME ps1_original_camera_trace_${_t3_camera_trace} COMMAND test_psx_camera_trace
                     ${CMAKE_SOURCE_DIR}/build/camera-trace/${_t3_camera_trace}.cases
                     ${CMAKE_SOURCE_DIR}/assets/imported/tekken3-usa/pose-tables/psx_trig.bin
                     ${CMAKE_SOURCE_DIR}/assets/imported/tekken3-usa/pose-tables/psx_atan.bin
                     ${CMAKE_SOURCE_DIR}/assets/imported/tekken3-usa/pose-tables/psx_body_sqrt.bin)
            set_tests_properties(ps1_original_camera_trace_${_t3_camera_trace} PROPERTIES SKIP_RETURN_CODE 77)
        endforeach()
        # The owned match with the original camera on private data, as on the Saturn.
        if(T3_PRIVATE_REPLAY_ASSET_DIR AND T3_LOCAL_CAMERA_BANK)
            add_executable(t3_match_camera_eval tools/psx_match_camera_eval.c
                "${_replay_logic}/t3_local_divmot.c" ${_replay_second} "${_replay_character}/t3_generated_character.c")
            target_include_directories(t3_match_camera_eval PRIVATE "${_replay_logic}" "${_replay_character}")
            t3_add_private_camera(t3_match_camera_eval)
            target_link_libraries(t3_match_camera_eval PRIVATE tekken3_fight m)
            target_compile_options(t3_match_camera_eval PRIVATE -Wall -Wextra -Werror)
            add_test(NAME ps1_owned_match_original_camera COMMAND t3_match_camera_eval)
        endif()
        add_executable(test_stage_backdrop tests/test_stage_backdrop.c)
        target_link_libraries(test_stage_backdrop PRIVATE tekken3_fight m)
        target_compile_options(test_stage_backdrop PRIVATE -Wall -Wextra -Werror)
        add_test(NAME stage_backdrop_line_mapping COMMAND test_stage_backdrop)
        # Line mapping on given cameras, for scripts/check_backdrop_mapping.py.
        add_executable(t3_stage_backdrop_eval tools/stage_backdrop_eval.c)
        target_link_libraries(t3_stage_backdrop_eval PRIVATE tekken3_fight)
        target_compile_options(t3_stage_backdrop_eval PRIVATE -Wall -Wextra -Werror)
        add_executable(t3_pose_eval tools/psx_pose_eval.c)
        target_link_libraries(t3_pose_eval PRIVATE tekken3_fight)
        target_compile_options(t3_pose_eval PRIVATE -Wall -Wextra -Werror)
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        add_test(NAME saturn_release_workload_identity
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_saturn_workload.py")
        add_test(NAME private_stage_converter
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_local_paul_stage.py")
        add_test(NAME cold_motion_file_layout
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_cold_motion_file.py")
        add_test(NAME simulation_profile_accounting
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/tests/test_simulation_profile.py")
        add_test(NAME ps1_round_flow_snapshot_audit
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audit_psx_round_flow.py"
                --runner "$<TARGET_FILE:t3_round_flow_eval>")
        add_test(NAME ps1_round_score_snapshot_audit
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_audit_psx_round_score.py"
                --runner "$<TARGET_FILE:t3_round_score_eval>")
        add_executable(test_psx_blend_math tests/test_psx_blend_math.c)
        add_executable(test_psx_blend tests/test_psx_blend.c)
        target_link_libraries(test_psx_blend PRIVATE tekken3_fight)
        target_compile_options(test_psx_blend PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_live_transition_blend_modes COMMAND test_psx_blend)
        target_link_libraries(test_psx_blend_math PRIVATE tekken3_fight)
        target_compile_options(test_psx_blend_math PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_blend_math_validation_and_aliasing COMMAND test_psx_blend_math)
        add_executable(t3_blend_math_eval tools/psx_blend_math_eval.c)
        target_link_libraries(t3_blend_math_eval PRIVATE tekken3_fight)
        target_compile_options(t3_blend_math_eval PRIVATE -Wall -Wextra -Werror)
        add_test(NAME ps1_blend_math_upstream_differential
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_diff_psx_blend_math.py"
                --runner "$<TARGET_FILE:t3_blend_math_eval>"
                --upstream "${CMAKE_SOURCE_DIR}/third_party/Tekken3Remake/tools/research/fight_sim.py")
        add_test(NAME ps1_original_hitapply_ram_adapter
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_psx_hit_ram_eval.py"
                --runner "$<TARGET_FILE:t3_hit_ram_eval>")
        add_test(NAME ps1_move_clock_upstream_differential
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_diff_move_clock.py"
                --runner "$<TARGET_FILE:t3_clock_eval>"
                --upstream "${CMAKE_SOURCE_DIR}/third_party/Tekken3Remake/tools/research/fight_sim.py")
        add_test(NAME ps1_private_divmot_pose_export
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/test_psx_motion_export.py"
                --runner "$<TARGET_FILE:t3_motion_export>")
        set(_t3_pose_upstream
            "${CMAKE_SOURCE_DIR}/third_party/Tekken3Remake/tools/research/pose.py")
        set(_t3_hit_upstream
            "${CMAKE_SOURCE_DIR}/third_party/Tekken3Remake/tools/research/fight_math.py")
        if(EXISTS "${_t3_hit_upstream}")
            add_test(NAME ps1_exact_collision_upstream_differential
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/test_diff_exact_contact.py"
                    --runner "$<TARGET_FILE:t3_exact_contact_eval>"
                    --upstream "${_t3_hit_upstream}")
            add_test(NAME ps1_hit_damage_upstream_differential
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/test_diff_psx_hit_damage.py"
                    --evaluator "$<TARGET_FILE:t3_hit_eval>"
                    --upstream "${_t3_hit_upstream}")
        endif()
        if(EXISTS "${_t3_pose_upstream}")
            find_package(Python3 REQUIRED COMPONENTS Interpreter)
            add_test(NAME ps1_fk_ik_upstream_differential
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/test_diff_psx_solver.py"
                    --runner "$<TARGET_FILE:t3_pose_eval>"
                    --upstream "${_t3_pose_upstream}")
            add_test(NAME ps1_divmot_batch_fk_ik_coverage
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_SOURCE_DIR}/tests/test_batch_psx_pose_oracle.py"
                    --exporter "$<TARGET_FILE:t3_motion_export>"
                    --solver "$<TARGET_FILE:t3_pose_eval>"
                    --upstream "${_t3_pose_upstream}")
        else()
            message(STATUS "PS1 reference comparison unavailable: "
                "git submodule update --init third_party/Tekken3Remake")
        endif()
    endif()
endif()
