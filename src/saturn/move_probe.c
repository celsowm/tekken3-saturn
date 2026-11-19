#include "move_probe.h"
#ifndef T3_FRAME_COUNTER_BIAS
#define T3_FRAME_COUNTER_BIAS 0u
#endif
#ifndef T3_START_SEED
#define T3_START_SEED 1u
#endif
#include "combat_events.h"
#include "saturn/time.h"
static uint16_t g_start_physical[2];
#if defined(T3_LOCAL_DIVMOT_ENABLED)
#include "t3_local_divmot.h"
#if defined(T3_POSE_TRACE)
/* Read by a debugger after each simulation tick, not disk I/O/VDP1.
 * Epoch odd = write in progress; even and unchanged = stable pair. */
volatile uint32_t g_t3_pose_trace_epoch;
volatile uint8_t g_t3_pose_trace[2][T3_PSX_TRACE_BYTES] __attribute__((section(".wram_l")));
static uint32_t g_t3_pose_trace_tick;
#endif
#include "divmot.h"
#include "move_vm.h"
#include "attack_records.h"
#include "psx_move_events.h"
#include "psx_motion.h"
#include "psx_pose.h"
#include "psx_ik.h"
#include "psx_contact.h"
#include "psx_shadow_combat.h"
#include "psx_root.h"
#include "psx_match.h"
#include "sh2_divide.h"
#include "t3_generated_character.h"
#if defined(T3_FIGHTER_PACKS)
#include "fighter_pack.h"
#define ACTOR_PARTS(i) (t3_fighter_pack(i)->parts)
#else
#define ACTOR_PARTS(i) t3_private_kmd_parts
#endif

#include "placement.h"

static t3_divmot_t g_bank __attribute__((section(".wram_l")));
static t3_divmot_t g_common;
#if defined(T3_SECOND_CHARACTER)
/* P2's own character bank (MOTION.BIN version 2). */
static t3_divmot_t g_second;
#endif
#if defined(T3_FIGHTER_PACKS)
static uint8_t g_shared_bank;
static uint8_t g_body_shapes[2][48];
#endif
#if defined(T3_BRANCH_INPUT_FILTER)
#include "branch_input_filter.h"
#if defined(T3_SECOND_CHARACTER)
/* Paul and Law need 1,680 words (branch_filter_words in the probe); a bank that
 * needs more makes build_input_filter fail, so the table cannot overrun. */
#if defined(T3_FIGHTER_PACKS)
enum { BRANCH_FILTER_WORDS=T3_ROSTER_FILTER_WORDS+
    (T3_LOCAL_COMMON_BRANCH_BYTES/12u+31u)/32u*T3_BRANCH_INPUT_CLASSES };
#else
enum { BRANCH_FILTER_WORDS=2112 };
#endif
#else
enum { BRANCH_FILTER_WORDS=2112 };
#endif
static uint32_t g_branch_filter_words[BRANCH_FILTER_WORDS] T3_HOT_WRAM;
static t3_branch_input_filter_t g_branch_filters[4] __attribute__((section(".wram_l")));
volatile uint32_t g_t3_branch_filter_ready,g_t3_branch_filter_words;
static int build_input_filter(unsigned slot,const uint8_t* rows,size_t bytes) {
    const unsigned needed=(unsigned)((bytes/12u+31u)/32u)*T3_BRANCH_INPUT_CLASSES;
    if(needed>BRANCH_FILTER_WORDS-g_t3_branch_filter_words)return 0;
    if(!t3_branch_input_filter_init(&g_branch_filters[slot],rows,bytes,
            t3_local_sequences,T3_LOCAL_SEQUENCE_COUNT,
            g_branch_filter_words+g_t3_branch_filter_words,
            BRANCH_FILTER_WORDS-g_t3_branch_filter_words))return 0;
    g_t3_branch_filter_words+=needed;
    g_t3_branch_filter_ready|=1u<<slot;
    return 1;
}
#endif
#if !defined(T3_ENABLE_PSX_OWNED_MATCH)
static t3_motion_cache_t g_pose_cache[2],g_root_cache[2];
#endif
#if defined(T3_PSX_CAMERA)
#include "t3_local_camera.h"
#include "saturn/math2d.h"
/* The generated tables with the build's own trig, atan (ground) and square
 * root (body) tables. */
static t3_psx_camera_tables_t g_camera_tables;
static t3_psx_trig_t g_camera_trig __attribute__((section(".wram_l")));
static t3_psx_fight_camera_config_t g_camera_config;
#endif
#if defined(T3_ENABLE_PSX_OWNED_MATCH) && defined(T3_ENABLE_PSX_COMBAT) && \
    defined(T3_LOCAL_AI_TABLE_BYTES) && T3_LOCAL_AI_TABLE_BYTES > 0
#define T3_CPU_AI 1
#include "psx_ai_game.h"
/* The CPU opponent; its fighters, difficulty and level apply at reset. High
 * WRAM: the AI reads its state every tick, and Low WRAM costs four times as
 * much per access. */
static t3_psx_ai_game_t g_ai;
/* AiUpdate cost on the Slave, in FRT ticks (prescaler /128: 3,728 per 60 Hz frame). */
volatile uint32_t g_t3_ai_calls,g_t3_ai_ticks,g_t3_ai_max_ticks,g_t3_ai_reads,g_t3_ai_conditions;
#if defined(T3_AI_RING)
/* W2.1 of docs/POST_VRAM_FRAME_RATE_PLAN.md (P2): the eight slowest AiUpdate
 * calls of the run plus a slow-call histogram, sampled from the same FRT
 * bracket. The median update is cheap; the question is what the rare ~0.85
 * field call is doing, which a last-N ring cannot answer after 8,000 updates. */
typedef struct {
    uint32_t spent,tick,reads,conditions,misses;
    uint16_t fighter,slot,window,picks,held,pressed;
} t3_ai_ring_event_t;
volatile t3_ai_ring_event_t g_t3_ai_ring[8];
volatile uint32_t g_t3_ai_ring_events,g_t3_ai_ring_over_half,g_t3_ai_ring_over_field;
static uint32_t g_ai_ring_reads[2],g_ai_ring_conditions[2],g_ai_ring_misses[2];
#endif
/* Hash of the pads of the first 2,000 updates: an AI speed-up must leave it unchanged. */
volatile uint32_t g_t3_ai_pad_hash;
static uint16_t g_ai_started;
#if defined(T3_CAPTURE_INPUT_TAPE)
/* tick, held, pressed of each change of the CPU's input (Slave-written). */
volatile uint32_t g_t3_cpu_tape_count,g_t3_cpu_tape_overflow;
volatile uint16_t g_t3_cpu_tape[640][3] __attribute__((section(".wram_l")));
static void t3_cpu_tape_step(uint32_t tick,t3_input_t in) {
    const uint32_t n=g_t3_cpu_tape_count;
    if(n && g_t3_cpu_tape[n-1u][1]==in.held && g_t3_cpu_tape[n-1u][2]==in.pressed)return;
    if(n>=640u || tick>0xFFFFu){g_t3_cpu_tape_overflow=1;return;}
    g_t3_cpu_tape[n][0]=(uint16_t)tick;g_t3_cpu_tape[n][1]=in.held;g_t3_cpu_tape[n][2]=in.pressed;
    g_t3_cpu_tape_count=n+1u;
}
#endif
#if defined(T3_AI_TRACE)
/* W2.2: every bus read of every AiUpdate lands in a rolling ring; when an
 * update beats the worst-FRT record, the reads of that update are copied out.
 * The save state then shows exactly which addresses the ~0.85-field scan hit. */
#define T3_AI_TRACE_ENTRIES 1024u
volatile uint32_t g_t3_ai_trace_ring[T3_AI_TRACE_ENTRIES*2];
volatile uint32_t g_t3_ai_trace_worst[T3_AI_TRACE_ENTRIES*2];
volatile uint32_t g_t3_ai_trace_pos,g_t3_ai_trace_worst_reads,g_t3_ai_trace_worst_spent;
volatile uint32_t g_t3_ai_trace_worst_fighter,g_t3_ai_trace_worst_tick;
static uint32_t g_ai_trace_start[2];
static void ai_trace(t3_psx_ai_game_t* ai,uint32_t address,unsigned size,uint32_t value) {
    (void)ai;(void)value;
    const uint32_t slot=g_t3_ai_trace_pos&(T3_AI_TRACE_ENTRIES-1u);
    g_t3_ai_trace_ring[slot*2u]=address;
    g_t3_ai_trace_ring[slot*2u+1u]=size;
    ++g_t3_ai_trace_pos;
}
#endif
static void ai_clock(t3_psx_ai_game_t* ai,unsigned fighter,int after) {
    (void)fighter;
    if(!after){
        g_ai_started=sat_time_frc();
#if defined(T3_AI_TRACE)
        g_ai_trace_start[fighter<2u?fighter:0u]=g_t3_ai_trace_pos;
#endif
        return;
    }
    const uint32_t spent=(uint16_t)(sat_time_frc()-g_ai_started);
#if defined(T3_AI_TRACE)
    if(spent>g_t3_ai_trace_worst_spent){
        const unsigned f=fighter<2u?fighter:0u;
        const uint32_t start=g_ai_trace_start[f];
        uint32_t count=g_t3_ai_trace_pos-start;
        if(count>T3_AI_TRACE_ENTRIES)count=T3_AI_TRACE_ENTRIES;
        for(uint32_t i=0;i<count;++i){
            const uint32_t slot=(start+i)&(T3_AI_TRACE_ENTRIES-1u);
            g_t3_ai_trace_worst[i*2u]=g_t3_ai_trace_ring[slot*2u];
            g_t3_ai_trace_worst[i*2u+1u]=g_t3_ai_trace_ring[slot*2u+1u];
        }
        g_t3_ai_trace_worst_reads=count;
        g_t3_ai_trace_worst_spent=spent;
        g_t3_ai_trace_worst_fighter=f;
        g_t3_ai_trace_worst_tick=ai->match?(uint32_t)ai->match->ticks:0u;
    }
#endif
    if(g_t3_ai_calls<2000u)g_t3_ai_pad_hash=g_t3_ai_pad_hash*31u+((uint32_t)ai->held<<16|ai->pressed);
    ++g_t3_ai_calls;g_t3_ai_ticks+=spent;
    g_t3_ai_reads=ai->reads;g_t3_ai_conditions=ai->conditions;
    if(spent>g_t3_ai_max_ticks)g_t3_ai_max_ticks=spent;
#if defined(T3_AI_RING)
    {
        const unsigned f=fighter<2u?fighter:0u;
        const uint32_t reads=ai->reads,conditions=ai->conditions,misses=ai->misses;
        const uint32_t tick=ai->match?(uint32_t)ai->match->ticks:0u;
        unsigned victim=0u;
        ++g_t3_ai_ring_events;
        if(spent>=1864u)++g_t3_ai_ring_over_half;
        if(spent>=3728u)++g_t3_ai_ring_over_field;
        for(unsigned i=1u;i<8u;++i)
            if(g_t3_ai_ring[i].spent<g_t3_ai_ring[victim].spent)victim=i;
        if(spent>g_t3_ai_ring[victim].spent){
            g_t3_ai_ring[victim].spent=spent;
            g_t3_ai_ring[victim].tick=tick;
            g_t3_ai_ring[victim].reads=reads-g_ai_ring_reads[f];
            g_t3_ai_ring[victim].conditions=conditions-g_ai_ring_conditions[f];
            g_t3_ai_ring[victim].misses=misses-g_ai_ring_misses[f];
            g_t3_ai_ring[victim].fighter=(uint16_t)f;
            g_t3_ai_ring[victim].slot=(uint16_t)(uint8_t)ai->slot[f];
            g_t3_ai_ring[victim].window=(uint16_t)ai->window_count;
            g_t3_ai_ring[victim].picks=(uint16_t)ai->pick_index[f];
            g_t3_ai_ring[victim].held=ai->held;
            g_t3_ai_ring[victim].pressed=ai->pressed;
        }
        g_ai_ring_reads[f]=reads;g_ai_ring_conditions[f]=conditions;g_ai_ring_misses[f]=misses;
    }
#endif
}
static uint8_t g_cpu_mask=2u,g_cpu_level=4u;
static uint16_t g_cpu_difficulty=1u;
#endif
static uint8_t g_practice_clock;
/* Tekken Ball (docs/TEKKEN_BALL_PLAN.md): the ball, its data (scripts/extract_ball_data.py's C source, when the build
 * has it) and the copy the renderer reads. The state is not part of the match, so the versus program is unchanged. */
#include "ball_game.h"
#include "psx_effect_fire.h"
#if defined(T3_BALL_PROGRAM)
/* Only BALL.BIN carries the ball: the fight program has no room for its code and state (docs/TEKKEN_BALL_PLAN.md). */
extern const uint8_t t3_ball_data_blob[] __attribute__((weak));
extern const uint32_t t3_ball_data_blob_size __attribute__((weak));
static t3_ball_game_t g_ball;
static t3_ball_view_t g_ball_view;
static uint8_t g_ball_on,g_ball_option;
#else
enum { g_ball_on=0 };
#endif
/* FUN_800519B4: the fight's maximum health by mode (docs/research/code/resident-functions.md). Tekken Ball is mode 7, which takes the
 * switch's default, 130 (the PS1 probe reads 130 with the Ball's fighters); the other modes keep 140, practice 200. */
static int32_t fight_max_health_q16(void) {
    if(g_practice_clock)return 200*65536;
    if(g_ball_on)return 130*65536;
    return 140*65536;
}
/* COMBO TRAINING's combo tables: scripts/extract_practice_combos.py's C source, when the build has it. */
extern const uint8_t t3_practice_combo_blob[] __attribute__((weak));
extern const uint32_t t3_practice_combo_blob_size __attribute__((weak));
static t3_practice_state_t g_practice_state;
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
static t3_psx_match_t g_owned_match T3_HOT_WRAM;
#if defined(T3_PSX_CAMERA) && defined(T3_LOCAL_EFFECT_TABLE_BYTES) && T3_LOCAL_EFFECT_TABLE_BYTES > 0
static t3_psx_effect_pool_t g_effect_pool T3_HOT_WRAM;
#define T3_OWNED_EFFECT_POOL 1
#endif
/* Reset assigns the entire object before match initialization. */
static t3_psx_match_config_t g_owned_config T3_HOT_WRAM;
#if T3_LOCAL_POSE_CACHE_ENTRIES == 0 && !defined(T3_REFERENCE_TRACE_HIGH)
/* Shared immutable motion samples; no live matrices or actor state. The
 * local pose cache holds its own samples. */
static t3_motion_sample_cache_t g_sample_cache T3_HOT_WRAM;
#endif
#if T3_WORLD_POSE_CACHE_ENTRIES > 0
static t3_psx_world_pose_cache_t g_world_pose_cache T3_HOT_WRAM;
static t3_psx_world_pose_entry_t g_world_pose_entries[T3_WORLD_POSE_CACHE_ENTRIES]
    T3_HOT_WRAM;
volatile uint32_t g_t3_world_cache_hits,g_t3_world_cache_misses;
#endif
#if T3_LOCAL_POSE_CACHE_ENTRIES > 0
static t3_psx_local_pose_cache_t g_local_pose_cache T3_HOT_WRAM;
static t3_psx_local_pose_entry_t g_local_pose_entries[T3_LOCAL_POSE_CACHE_ENTRIES]
    T3_HOT_WRAM;
#endif
#if defined(T3_ENABLE_PSX_COMBAT)
#if defined(T3_FIGHTER_PACKS)
/* The fighters' worst pair needs this Low WRAM for its banks. */
static t3_psx_replay_storage_t g_replay_history;
#else
static t3_psx_replay_storage_t g_replay_history __attribute__((section(".wram_l")));
#endif
#endif
#define VM(i) (g_owned_match.fighter[(i)].vm)
#else
static t3_move_vm_t g_preview_vms[2];
#define VM(i) (g_preview_vms[(i)])
#endif
static uint8_t g_active, g_round;
#if defined(T3_PARTICLE_IMAGE_SNAPSHOT)
#if !defined(T3_OWNED_EFFECT_POOL)
#error "Particle image snapshot requires the owned effect pool"
#endif
#if defined(T3_ORIGINAL_FIGHTER_OT)
t3_particle_image_t g_t3_particle_image __attribute__((section(".wram_l")));
#else
t3_particle_image_t g_t3_particle_image T3_HOT_WRAM;
#endif
volatile uint32_t g_t3_particle_images,g_t3_particle_max_sprites;
#if defined(T3_POWER_PROBE)
volatile uint32_t g_t3_power_probe[55][2];
#endif
#if defined(T3_FLIPBOOK_PROBE)
volatile uint32_t g_t3_flip_diag[24];
#endif
#if defined(T3_RAND_PROBE)
volatile uint32_t g_t3_rand_probe[56];
#endif
#if defined(T3_PARTICLE_PREPARE)
const t3_psx_trig_t* t3_move_probe_particle_trig(void) {return &g_camera_trig;}
#endif
sat_result_t t3_move_probe_capture_particles(void) {
    if(!g_active || !g_owned_match.camera.started)return SAT_ERR_INVALID_ARG;
    if(!t3_psx_particle_snapshot(&g_t3_particle_image.sprites,&g_effect_pool))return SAT_ERR_VERIFY_FAILED;
    g_t3_particle_image.camera=g_owned_match.camera.camera.view;
    g_t3_particle_image.frame_counter=g_owned_match.frame_counter;
#if defined(T3_FLIPBOOK_BURST)
    for(unsigned i=0;i<T3_PSX_FLIPBOOK_RECORDS;++i)g_t3_particle_image.flipbook[i]=g_effect_pool.flipbook.shown[i];
#endif
    if(g_t3_particle_image.sprites.count>g_t3_particle_max_sprites)
        g_t3_particle_max_sprites=g_t3_particle_image.sprites.count;
    ++g_t3_particle_images;
    return SAT_OK;
}
#endif

static unsigned g_start_slot;
static uint8_t g_segments[2];
static uint8_t g_move_event_status[2],g_move_extra_kind[2],g_move_extra_damage[2];
static uint16_t g_move_event_fired[2];
static uint32_t g_decode_errors[2];
static uint8_t g_pose_status[2],g_pose_frames[2];
static int16_t g_root_pose[2][3];
static uint32_t g_fk_mask[2],g_fk_parts[2],g_ik_mask[2];
static uint8_t g_hurt_ready[2];
static int32_t g_hurt_head_y[2];
#if defined(T3_LOCAL_KMD_BIND)
static t3_psx_joint_t g_world_pose[2][T3_PSX_DRAW_PARTS] T3_HOT_WRAM;
#endif
static int16_t g_fk_root_xx[2];
#if defined(T3_SLAVE_SIMULATION) && defined(T3_ENABLE_PSX_OWNED_MATCH)
#include <saturn/parallel.h>
sat_result_t t3_move_probe_simulation_sync(int full) {
    const struct {const void* address;uint32_t size;} query[]={
        {&g_owned_match,sizeof(g_owned_match)},
#if defined(T3_OWNED_EFFECT_POOL)
        {&g_effect_pool,sizeof(g_effect_pool)},
#endif
#if defined(T3_CPU_AI)
        {&g_ai,sizeof(g_ai)},
#endif
        {&g_round,sizeof(g_round)},
        {&g_practice_state,sizeof(g_practice_state)},
        {g_segments,sizeof(g_segments)},
        {g_move_event_status,sizeof(g_move_event_status)},
        {g_move_extra_kind,sizeof(g_move_extra_kind)},
        {g_move_extra_damage,sizeof(g_move_extra_damage)},
        {g_move_event_fired,sizeof(g_move_event_fired)},
        {g_decode_errors,sizeof(g_decode_errors)},
#if T3_LOCAL_POSE_CACHE_ENTRIES > 0
        {&g_local_pose_cache,sizeof(g_local_pose_cache)},
#endif
#if T3_WORLD_POSE_CACHE_ENTRIES > 0
        {&g_world_pose_cache,sizeof(g_world_pose_cache)},
        {(const void*)&g_t3_world_cache_hits,sizeof(g_t3_world_cache_hits)},
        {(const void*)&g_t3_world_cache_misses,sizeof(g_t3_world_cache_misses)},
#endif
    };
    for(unsigned i=0;i<sizeof(query)/sizeof(*query);++i) {
        const sat_result_t st=sat_parallel_cache_sync_range(query[i].address,query[i].size);
        if(st!=SAT_OK)return st;
    }
    if(full) {
        const struct {const void* address;uint32_t size;} reset[]={
            {&g_bank,sizeof(g_bank)},{&g_common,sizeof(g_common)},
#if defined(T3_SECOND_CHARACTER)
            {&g_second,sizeof(g_second)},
#endif
            {&g_owned_config,sizeof(g_owned_config)},
            {&g_practice_state,sizeof(g_practice_state)},
            {&g_active,sizeof(g_active)},{&g_start_slot,sizeof(g_start_slot)},
#if T3_LOCAL_POSE_CACHE_ENTRIES == 0 && !defined(T3_REFERENCE_TRACE_HIGH)
            {&g_sample_cache,sizeof(g_sample_cache)},
#elif T3_LOCAL_POSE_CACHE_ENTRIES > 0
            {g_local_pose_entries,sizeof(g_local_pose_entries)},
#endif
#if T3_WORLD_POSE_CACHE_ENTRIES > 0
            {g_world_pose_entries,sizeof(g_world_pose_entries)},
#endif
#if defined(T3_BRANCH_INPUT_FILTER)
            {g_branch_filters,sizeof(g_branch_filters)},
            {g_branch_filter_words,sizeof(g_branch_filter_words)},
#endif
#if defined(T3_ENABLE_PSX_COMBAT)
            {&g_replay_history,sizeof(g_replay_history)},
#endif
#if defined(T3_PSX_CAMERA)
            {&g_camera_tables,sizeof(g_camera_tables)},{&g_camera_trig,sizeof(g_camera_trig)},
            {&g_camera_config,sizeof(g_camera_config)},
#endif
        };
        for(unsigned i=0;i<sizeof(reset)/sizeof(*reset);++i) {
            const sat_result_t st=sat_parallel_cache_sync_range(reset[i].address,reset[i].size);
            if(st!=SAT_OK)return st;
        }
    }
    return SAT_OK;
}
#endif
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
/* Debugger-written external WORLD placement; never inferred from the
 * prototype's Q8 fighter coordinates. Writer: odd epoch, update roots
 * and valid_mask, even epoch. Reader accepts only unchanged even epoch.
 * SH2 structs and fields use target-native BIG endian. */
volatile uint32_t g_t3_shadow_root_epoch;
volatile uint32_t g_t3_shadow_root_valid_mask;
volatile t3_psx_root_t g_t3_shadow_external_roots[2];
static t3_psx_shadow_state_t g_shadow;
static t3_psx_shadow_result_t g_shadow_result[2];
static uint8_t g_shadow_valid;
static uint32_t g_shadow_last_injected_epoch;
#endif

#if defined(T3_REFERENCE_REPLAY)
/* Diagnostic field visiting/hash work is outside the live timing gate. Keep
 * its code bounded so the normal stock-memory/stack assertions still apply. */
#pragma GCC push_options
#pragma GCC optimize ("Os")
#include "reference_replay.inc"
#pragma GCC pop_options
#endif
sat_result_t t3_move_probe_init(void) {
#if defined(T3_FIGHTER_PACKS)
    /* Each fighter's parts, radii and scale differ; the sphere joints do not. */
    for(unsigned i=0;i<2u;++i) {
        __builtin_memcpy(g_body_shapes[i],t3_local_body_blob+384u,32u);
        for(unsigned k=0;k<8u;++k) {
            g_body_shapes[i][32u+2u*k]=(uint8_t)t3_fighter_pack(i)->radii[k];
            g_body_shapes[i][33u+2u*k]=(uint8_t)((uint16_t)t3_fighter_pack(i)->radii[k]>>8);
        }
    }
    if (!t3_divmot_open(&g_bank,t3_fighter_pack(0)->bank,t3_fighter_pack(0)->bank_bytes))
        return SAT_ERR_INVALID_ARG;
    g_shared_bank=(uint8_t)t3_fighter_packs_share_bank();
    if (!g_shared_bank && !t3_divmot_open(&g_second,t3_fighter_pack(1)->bank,t3_fighter_pack(1)->bank_bytes))
        return SAT_ERR_INVALID_ARG;
#else
    if (!t3_divmot_open(&g_bank,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES))
        return SAT_ERR_INVALID_ARG;
#if defined(T3_SECOND_CHARACTER)
    if (!t3_divmot_open(&g_second,t3_local_second_bank,T3_LOCAL_SECOND_BANK_BYTES))
        return SAT_ERR_INVALID_ARG;
#endif
#endif
#if defined(T3_SECOND_CHARACTER)
#if T3_LOCAL_COMMON_BRANCH_BYTES > 0
#if defined(T3_FIGHTER_PACKS)
    if (!g_shared_bank)
#endif
    if (!t3_divmot_set_common_branches(&g_second,t3_local_common_branches,
                                       T3_LOCAL_COMMON_BRANCH_BYTES))
        return SAT_ERR_INVALID_ARG;
#endif
#endif
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
    if (!t3_divmot_open(&g_common,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES))
        return SAT_ERR_INVALID_ARG;
#endif
#if T3_LOCAL_COMMON_BRANCH_BYTES > 0
    if (!t3_divmot_set_common_branches(&g_bank,t3_local_common_branches,
                                       T3_LOCAL_COMMON_BRANCH_BYTES))
        return SAT_ERR_INVALID_ARG;
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
    if (!t3_divmot_set_common_branches(&g_common,t3_local_common_branches,
                                       T3_LOCAL_COMMON_BRANCH_BYTES))
        return SAT_ERR_INVALID_ARG;
#endif
#endif
#if defined(T3_BRANCH_INPUT_FILTER)
    g_t3_branch_filter_ready=g_t3_branch_filter_words=0;
    if(!build_input_filter(0,g_bank.data+g_bank.sections[0],g_bank.branch_count*12u))
        return SAT_ERR_CAPACITY;
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
    if(!build_input_filter(1,g_common.data+g_common.sections[0],g_common.branch_count*12u))
        return SAT_ERR_CAPACITY;
#endif
    const t3_branch_input_filter_t* common_filter=NULL;
#if T3_LOCAL_COMMON_BRANCH_BYTES > 0
    if(!build_input_filter(2,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES))
        return SAT_ERR_CAPACITY;
    common_filter=&g_branch_filters[2];
#endif
    if(!t3_divmot_set_input_filters(&g_bank,&g_branch_filters[0],common_filter))
        return SAT_ERR_INVALID_ARG;
#if defined(T3_SECOND_CHARACTER)
#if defined(T3_FIGHTER_PACKS)
    if(!g_shared_bank)
#endif
    {
    if(!build_input_filter(3,g_second.data+g_second.sections[0],g_second.branch_count*12u))
        return SAT_ERR_CAPACITY;
    if(!t3_divmot_set_input_filters(&g_second,&g_branch_filters[3],common_filter))
        return SAT_ERR_INVALID_ARG;
    }
#endif
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
    if(!t3_divmot_set_input_filters(&g_common,&g_branch_filters[1],common_filter))
        return SAT_ERR_INVALID_ARG;
#endif
#endif
    /* Slot 3 is the ordinary Tekken standing stance. Fallback is useful for
     * synthetic tests, but must not be mistaken for authentic startup. */
    g_start_slot=3u;
    if (t3_divmot_slot(&g_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
          &g_common,
#else
          NULL,
#endif
          g_start_slot,&VM(0).move)!=1) g_start_slot=0u;
    g_active=1;
#if defined(T3_ENABLE_PSX_COMBAT)
    /* Low WRAM is NOLOAD; only the first initialization clears storage.
     * Later Start resets invalidate the ring through its monotonic epoch. */
    __builtin_memset(&g_replay_history,0,sizeof(g_replay_history));
#endif
    t3_move_probe_reset(1u);
    return g_active?SAT_OK:SAT_ERR_INVALID_ARG;
}

static uint8_t g_carry[3];
void t3_move_probe_set_carry(uint8_t wins0,uint8_t wins1,uint8_t played) {
    g_carry[0]=wins0;g_carry[1]=wins1;g_carry[2]=played;
}

void t3_move_probe_reset(unsigned round) {
    unsigned i;
    if (!g_active) return;
    g_round=(uint8_t)round;
    t3_practice_data_reset(&g_practice_state.data);
    g_practice_state.markers=(t3_practice_markers_t){{0},{0},0};
    t3_practice_keys_reset(&g_practice_state.keys);
    /* A new match can load other fighters' part tables into the same buffers;
     * the once-per-table topology check must see them (psx_pose.h). */
    t3_psx_pose_topology_reset();
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    g_owned_config=(t3_psx_match_config_t){0};
    g_owned_config.bank=&g_bank;g_owned_config.common=&g_common;
#if T3_LOCAL_POSE_CACHE_ENTRIES == 0 && !defined(T3_REFERENCE_TRACE_HIGH)
    g_owned_config.sample_cache=&g_sample_cache;
#endif
#if T3_WORLD_POSE_CACHE_ENTRIES > 0
    t3_psx_world_pose_cache_init(&g_world_pose_cache,g_world_pose_entries,T3_WORLD_POSE_CACHE_ENTRIES);
    g_owned_config.world_pose_cache=&g_world_pose_cache;
    g_t3_world_cache_hits=g_t3_world_cache_misses=0;
#endif
#if T3_LOCAL_POSE_CACHE_ENTRIES > 0
    t3_psx_local_pose_cache_init(&g_local_pose_cache,g_local_pose_entries,T3_LOCAL_POSE_CACHE_ENTRIES);
    g_owned_config.local_pose_cache=&g_local_pose_cache;
#endif
    if(!t3_psx_ground_tables_bind((t3_psx_trig_t){t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},
            t3_local_ground_blob,T3_LOCAL_GROUND_BYTES,&g_owned_config.ground_tables)){g_active=0;return;}
    g_owned_config.spline=(t3_motion_tables_t){t3_local_spline_blob,T3_LOCAL_SPLINE_BYTES};
    g_owned_config.sequences=t3_local_sequences;g_owned_config.sequence_count=T3_LOCAL_SEQUENCE_COUNT;
    g_owned_config.enable_body=1;
#if defined(T3_ENABLE_PSX_COMBAT)
    g_owned_config.enable_combat=1;
    g_owned_config.all_attacks=1; /* docs/FIGHT_ORACLE.md */
#if defined(T3_FIGHTER_PACKS)
    g_owned_config.character[0]=(uint8_t)t3_fighter_pack(0)->id;
    g_owned_config.character[1]=(uint8_t)t3_fighter_pack(1)->id;
    if(g_shared_bank) {
        g_bank.linked=&g_bank; /* the merged move index of a mirror match */
    } else {
        g_owned_config.actor_bank[1]=&g_second;
        g_bank.linked=&g_second;g_second.linked=&g_bank;
        if(T3_LOCAL_COMBAT_ATTACK_BYTES==T3_BUILTIN_ATTACK_BYTES)
            g_second.builtin_attacks=t3_local_combat_attacks;
    }
#elif defined(T3_SECOND_CHARACTER)
    /* P2 plays the second character; the merged move index links the two. */
    g_owned_config.actor_bank[1]=&g_second;
    g_owned_config.character[1]=T3_SECOND_CHARACTER_ID;
    g_bank.linked=&g_second;g_second.linked=&g_bank;
    if(T3_LOCAL_COMBAT_ATTACK_BYTES==T3_BUILTIN_ATTACK_BYTES)
        g_second.builtin_attacks=t3_local_combat_attacks;
#else
    g_bank.linked=&g_bank; /* the merged move index of a mirror match */
#endif
    if(T3_LOCAL_COMBAT_ATTACK_BYTES==T3_BUILTIN_ATTACK_BYTES)
        g_bank.builtin_attacks=g_common.builtin_attacks=t3_local_combat_attacks;
    g_owned_config.enable_rounds=2;
#if !defined(T3_REFERENCE_REPLAY)
    g_owned_config.start_intro=1;g_owned_config.start_seed=T3_START_SEED;
    g_owned_config.start_wins[0]=g_carry[0];g_owned_config.start_wins[1]=g_carry[1];
    g_owned_config.start_played=g_carry[2];
    g_owned_config.start_physical[0]=g_start_physical[0];
    g_owned_config.start_physical[1]=g_start_physical[1];
#endif
    g_owned_config.replay_storage=&g_replay_history;
#if defined(T3_OWNED_EFFECT_POOL)
    g_owned_config.effects=&g_effect_pool;
    g_owned_config.effect_tables=t3_local_effect_tables;
    g_owned_config.effect_table_bytes=T3_LOCAL_EFFECT_TABLE_BYTES;
#endif
#if defined(T3_LOCAL_SOUND_TABLE_BYTES) && T3_LOCAL_SOUND_TABLE_BYTES > 0
    g_owned_config.sound_tables=(t3_psx_sound_tables_t){t3_local_sound_tables,T3_LOCAL_SOUND_TABLE_BYTES};
#if defined(T3_FIGHTER_PACKS)
    /* Character records: the voice set is not always the id (Bryan's is 13). */
    g_owned_config.sound_voice_set[0]=(uint8_t)t3_fighter_pack(0)->voice_set;
    g_owned_config.sound_voice_set[1]=(uint8_t)t3_fighter_pack(1)->voice_set;
#else
    /* The present private pair is Paul/Law; both voice sets equal base ids. */
    g_owned_config.sound_voice_set[0]=g_owned_config.character[0];
    g_owned_config.sound_voice_set[1]=g_owned_config.character[1];
#endif
    g_owned_config.sound_sink=t3_combat_events_sound;
    t3_combat_events_connect_sound();
#endif
#if defined(T3_FIGHTER_PACKS) && defined(T3_PSX_HAND_CHANNELS)
    {
        static const uint8_t faces[][2]=T3_ROSTER_FACE_SHAPES;
        for(unsigned i=0;i<2u;++i) {
            const unsigned id=t3_fighter_pack(i)->id;
            g_owned_config.face_attack[i]=id<T3_ROSTER_COUNT?faces[id][0]:0u;
            g_owned_config.face_hit[i]=id<T3_ROSTER_COUNT?faces[id][1]:0u;
        }
    }
#endif
    g_owned_config.collapse_on_ko=1; /* measured USA VS regional policy */
    g_owned_config.reactions=t3_local_combat_reactions;
    g_owned_config.reactions_size=T3_LOCAL_COMBAT_REACTION_BYTES;
    g_owned_config.close_reactions=t3_local_combat_close;
    g_owned_config.close_reactions_size=T3_LOCAL_COMBAT_CLOSE_BYTES;
    g_owned_config.throw_victim_slots=t3_local_combat_throws;
    g_owned_config.throw_victim_slots_size=T3_LOCAL_COMBAT_THROW_BYTES;
#if defined(T3_LOCAL_REVERSAL_BYTES)
    g_owned_config.reversal_lists=t3_local_reversal_lists;g_owned_config.reversal_lists_size=T3_LOCAL_REVERSAL_BYTES;
#endif
#endif
#if defined(T3_FIGHTER_PACKS)
    g_owned_config.parts[0]=t3_fighter_pack(0)->parts;g_owned_config.parts[1]=t3_fighter_pack(1)->parts;
    g_owned_config.actor_body_shapes[0]=g_body_shapes[0];g_owned_config.actor_body_shapes[1]=g_body_shapes[1];
    g_owned_config.actor_scale_percent[0]=t3_fighter_pack(0)->scale_percent;
    g_owned_config.actor_scale_percent[1]=t3_fighter_pack(1)->scale_percent;
#else
    g_owned_config.parts[0]=t3_private_actor_parts[0];g_owned_config.parts[1]=t3_private_actor_parts[1];
#endif
    g_owned_config.ik=(t3_psx_ik_tables_t){{t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},t3_local_ik_blob,T3_LOCAL_IK_BYTES};
    g_owned_config.body_tables=t3_local_body_blob;g_owned_config.body_tables_size=T3_LOCAL_BODY_BYTES;
#if defined(T3_LOCAL_RSQRT_BYTES) && T3_LOCAL_RSQRT_BYTES > 0
    g_owned_config.rsqrt=(t3_psx_rsqrt_t){t3_local_rsqrt_blob,T3_LOCAL_RSQRT_BYTES};
#endif
    g_owned_config.scale_percent=T3_KMD_SCALE_PERCENT;g_owned_config.max_health_q16=fight_max_health_q16(); /* FUN_800519B4 */
    /* Measured USA Paul mirror round placement; render conversion is separate. */
    g_owned_config.spawn[0]=(t3_psx_spawn_t){{-1000,0,-4},{-1000,-1027,-4},16384u,16384u};
    g_owned_config.spawn[1]=(t3_psx_spawn_t){{1000,0,4},{1000,-1027,4},49151u,49151u};
    if(g_ball_on) {
        /* Tekken Ball's court (FUN_800B0C90): the players start at x = -/+0x1400 facing each other, on its z = 0 line */
        g_owned_config.ball_court=1;
        g_owned_config.spawn[0]=(t3_psx_spawn_t){{-BALL_COURT_HALF_X,0,0},{-BALL_COURT_HALF_X,-1027,0},16384u,16384u};
        g_owned_config.spawn[1]=(t3_psx_spawn_t){{BALL_COURT_HALF_X,0,0},{BALL_COURT_HALF_X,-1027,0},49151u,49151u};
    }
#if defined(T3_PSX_CAMERA)
    g_camera_trig=(t3_psx_trig_t){t3_local_trig_blob,T3_LOCAL_TRIG_BYTES};
    g_camera_tables=t3_local_camera_tables;g_camera_tables.trig=&g_camera_trig;
    g_camera_tables.atan=t3_local_ground_blob;
    g_camera_tables.sqrt=t3_local_body_blob;g_camera_tables.sqrt_size=384u;
#if defined(T3_FIGHTER_PACKS)
    g_camera_config=(t3_psx_fight_camera_config_t){
        .tables=&g_camera_tables,
        .bank_choices=t3_fighter_pack(0)->choices,
        .bank_choice_count=t3_fighter_pack(0)->choice_count,
        .bank_streams=&t3_fighter_pack(0)->streams,
        .second_bank_choices=t3_fighter_pack(1)->choices,
        .second_bank_choice_count=t3_fighter_pack(1)->choice_count,
        .second_bank_streams=&t3_fighter_pack(1)->streams,
        .spline=&g_owned_config.spline};
#else
    g_camera_config=(t3_psx_fight_camera_config_t){
        .tables=&g_camera_tables,
        .bank_choices=t3_local_camera_bank_choices,
        .bank_choice_count=T3_LOCAL_CAMERA_BANK_CHOICES,
        .bank_streams=&t3_local_camera_bank_streams,
        .spline=&g_owned_config.spline};
#endif
#if defined(T3_LOCAL_SECOND_CAMERA_BANK_CHOICES)
    g_camera_config.second_bank_choices=t3_local_second_camera_bank_choices;
    g_camera_config.second_bank_choice_count=T3_LOCAL_SECOND_CAMERA_BANK_CHOICES;
    g_camera_config.second_bank_streams=&t3_local_second_camera_bank_streams;
#endif
    g_owned_config.camera=&g_camera_config;
#endif
#if defined(T3_CPU_AI)
    if(g_cpu_mask) {
        if(!t3_psx_ai_game_init(&g_ai,t3_local_ai_tables,T3_LOCAL_AI_TABLE_BYTES,g_cpu_mask,
                                g_cpu_difficulty,g_cpu_level)){g_active=0u;return;}
#if defined(T3_FIGHTER_PACKS)
        t3_psx_ai_game_place_banks(&g_ai,g_bank.bank_type,g_shared_bank?g_bank.bank_type:g_second.bank_type,g_shared_bank);
#elif defined(T3_SECOND_CHARACTER)
        t3_psx_ai_game_place_banks(&g_ai,g_bank.bank_type,g_second.bank_type,0);
#else
        t3_psx_ai_game_place_banks(&g_ai,g_bank.bank_type,g_bank.bank_type,1);
#endif
        g_ai.observe=ai_clock;
#if defined(T3_AI_TRACE)
        g_ai.trace=ai_trace;
#endif
        g_owned_config.ai=&g_ai;
    }
#endif
#if defined(T3_REFERENCE_REPLAY)
    if(!reference_prepare_config()){g_active=0u;g_reference_error=1;return;}
#endif
    if(!t3_psx_match_init(&g_owned_match,&g_owned_config)){g_active=0u;return;}
#if defined(T3_BALL_PROGRAM)
    if(g_ball_on) {
        /* FUN_800B0C90: the ball type's constants and the ball at the start of every round */
        if(!&t3_ball_data_blob_size || !t3_ball_game_init(&g_ball,t3_ball_data_blob,t3_ball_data_blob_size,g_ball_option,0u)) {
            g_active=0u;return;
        }
        g_ball.world.first_round=(int32_t)round==0?1:0;
        t3_ball_game_view(&g_ball,&g_ball_view);
#if defined(T3_CPU_AI)
        g_ai.ball_pos=g_ball.ball.pos;   /* the CPU plays the ball (ball_ai.c) */
#endif
    }
#endif
#if defined(T3_REFERENCE_REPLAY)
#if !defined(T3_REFERENCE_START_INTRO) || !T3_REFERENCE_START_INTRO
    if(!reference_seed_state(T3_REFERENCE_STEP_X,T3_REFERENCE_STEP_Z,
            T3_REFERENCE_PREVIOUS,T3_REFERENCE_RAMP)) {g_active=0u;g_reference_error=2;return;}
#endif
#endif
#endif
    for (i=0;i<2u;++i) {
#if !defined(T3_ENABLE_PSX_OWNED_MATCH)
        g_pose_cache[i]=(t3_motion_cache_t){0};
        g_root_cache[i]=(t3_motion_cache_t){0};
#endif
        g_segments[i]=0u;
        g_move_event_status[i]=0u;
        g_move_extra_kind[i]=0u;
        g_move_extra_damage[i]=0u;
        g_move_event_fired[i]=0u;
        g_decode_errors[i]=0u;
        g_pose_status[i]=0u;
        g_pose_frames[i]=0u;
        g_fk_mask[i]=0u;
        g_fk_parts[i]=0u;
        g_hurt_ready[i]=0u;
        g_hurt_head_y[i]=0;
        g_ik_mask[i]=0u;
        g_fk_root_xx[i]=0;
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
        g_shadow_result[i].status=T3_SHADOW_INACTIVE;
        g_shadow_result[i].new_contact=0u;
        g_shadow_result[i].zone=-1;
        g_shadow_result[i].segment=-1;
#endif
        g_root_pose[i][0]=g_root_pose[i][1]=g_root_pose[i][2]=0;
#if !defined(T3_ENABLE_PSX_OWNED_MATCH)
#if defined(T3_ENABLE_PSX_MOVE_LIFECYCLE)
        if (!t3_move_vm_start_phased(&VM(i),&g_bank,
#else
        if (!t3_move_vm_start(&VM(i),&g_bank,
#endif
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
                   &g_common,
#else
                   NULL,
#endif
                   g_start_slot)) g_active=0u;
#endif
    }
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    t3_psx_shadow_reset(&g_shadow);
    g_shadow_valid=0u;
    g_t3_shadow_root_valid_mask=0u; /* debug writer must supply fresh data */
    g_shadow_last_injected_epoch=g_t3_shadow_root_epoch;
#endif
}

#if defined(T3_ENABLE_PSX_OWNED_MATCH)
/* This snapshot has no authoritative consumers. Preserve collision slot
 * order in the core and map to skin order only for the submitted image. */
static void publish_owned_pose(unsigned i) {
    const t3_psx_fighter_t* f=&g_owned_match.fighter[i];
    t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES];
    g_pose_status[i]=1;g_pose_frames[i]=f->pose_frames;
    g_fk_mask[i]=f->fk_mask;g_fk_parts[i]=f->parts_mask;
    g_ik_mask[i]=f->ik_mask;g_fk_root_xx[i]=f->fk_root_xx;
    for(unsigned axis=0;axis<3;++axis)g_root_pose[i][axis]=f->pose[axis];
    for(unsigned part=0;part<T3_PSX_DRAW_PARTS;++part)
        g_world_pose[i][part]=f->world[ACTOR_PARTS(i)[part].slot];
    /* Draw costume attachments at their KMD rest rotation relative to their
     * animated parent. The original adds spring dynamics to this rotation
     * (Tekken3Remake docs/research/formats/animation.md; see NOTICE.md). */
#if defined(T3_FIGHTER_PACKS)
    const int16_t (*rest)[3]=t3_fighter_pack(i)->attachment_rest;
#endif
    for(unsigned part=T3_PSX_ROTATION_SLOTS;part<T3_PSX_DRAW_PARTS;++part) {
        const t3_psx_part_t* p=&ACTOR_PARTS(i)[part];
        const uint32_t bit=t3_psx_bit(part);
        if(p->parent<0 || !(g_fk_parts[i]&t3_psx_bit((unsigned)p->parent)) || (g_fk_parts[i]&bit))continue;
        const t3_psx_joint_t* parent=&g_world_pose[i][p->parent];
        const int32_t delta[3]={p->offset.x,p->offset.y,p->offset.z};
        t3_psx_joint_t* joint=&g_world_pose[i][part];
        __builtin_memset(joint,0,sizeof *joint);
        joint->rotation=parent->rotation;
#if defined(T3_FIGHTER_PACKS)
        if(rest) {
            const int16_t* angles=rest[part-T3_PSX_ROTATION_SLOTS];
            t3_psx_matrix_t local;
            if((angles[0] || angles[1] || angles[2]) &&
               t3_psx_euler_matrix(angles[0],angles[1],angles[2],&g_owned_config.ik.trig,&local))
                t3_psx_mul_rot(&parent->rotation,&local,&joint->rotation);
        }
#endif
        joint->origin.x=parent->origin.x+t3_psx_dot12(parent->rotation.m[0],delta);
        joint->origin.y=parent->origin.y+t3_psx_dot12(parent->rotation.m[1],delta);
        joint->origin.z=parent->origin.z+t3_psx_dot12(parent->rotation.m[2],delta);
        g_fk_parts[i]|=bit;
    }
    g_hurt_ready[i]=(uint8_t)t3_psx_hurt_from_parts(f->world,f->parts_mask,0,zones);
    g_hurt_head_y[i]=g_hurt_ready[i]?zones[0].center.y:0;
}
#endif

void t3_move_probe_tick(const t3_match_t* match) {
    unsigned i;
    if (!g_active || !match) return;
    if (g_round!=match->round) t3_move_probe_reset(match->round);
#if !defined(T3_ENABLE_PSX_COMBAT)
    if (match->round_done || match->match_done || !g_active) return;
#endif
    for (i=0;i<2u;++i) {
        t3_attack_sample_t sampled;
        int decoded;
        {
            unsigned before=VM(i).clock.pose_frame;
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
            before=g_owned_match.fighter[i].previous_pose_frame;
            int transitioned=g_owned_match.fighter[i].started;
#elif defined(T3_ENABLE_PSX_MOVE_LIFECYCLE)
            /* Visual lifecycle preview. The enclosing match still owns
             * provisional HP/physics, so never fabricate original hit facts. */
            const t3_move_start_facts_t no_hit={0u,1u,0,0};
            int transitioned=t3_move_vm_begin(&VM(i),no_hit);
#else
            int transitioned=t3_move_vm_tick_ex(
                &VM(i),&match->input_histories[i],
                NULL,t3_local_sequences,T3_LOCAL_SEQUENCE_COUNT);
#endif
            t3_psx_move_events_t events;
#if defined(T3_ENABLE_PSX_COMBAT)
            int rc=1;events=g_owned_match.fighter[i].events;
            (void)transitioned;(void)before;
#else
            int rc=transitioned<0?-1:
                t3_psx_move_events_sample(VM(i).active_bank,
                    &VM(i).move,transitioned==1?0u:before,
                    VM(i).clock.pose_frame,&events);
#endif
            g_move_event_status[i]=rc==1?1u:(rc==0?2u:3u);
            g_move_extra_kind[i]=rc==1?events.extra_kind:0u;
            g_move_extra_damage[i]=rc==1?events.extra_damage:0u;
            g_move_event_fired[i]=rc==1?events.fired:0u;
            if(rc<0)++g_decode_errors[i];
        }
        decoded=t3_attack_sample(VM(i).active_bank,&VM(i).move,
                                 VM(i).clock.pose_frame,0,&sampled);
        g_segments[i]=decoded==1?sampled.segment_count:0u;
        if(decoded<0) ++g_decode_errors[i];
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
#if defined(T3_POSE_TRACE)
        /* Explicit oracle captures retain every intermediate tick's pose. */
        publish_owned_pose(i);
#endif
#else
        {
            const t3_motion_stream_t* motion;
            int opened=t3_motion_open_cached(&g_pose_cache[i],VM(i).active_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
                &g_common,
#else
                NULL,
#endif
                VM(i).move.animation_offset,&motion);
            g_pose_status[i]=0u;
            g_pose_frames[i]=0u;
            g_fk_mask[i]=0u;
            g_fk_parts[i]=0u;
            g_hurt_ready[i]=0u;
            g_hurt_head_y[i]=0;
            g_ik_mask[i]=0u;
            g_fk_root_xx[i]=0;
            if(opened==1) {
                int16_t pose[T3_PSX_POSE_CHANNELS];
                int16_t root[3];
                int full, simple;
#if T3_LOCAL_SPLINE_BYTES > 0
                const t3_motion_tables_t tables={
                    t3_local_spline_blob,T3_LOCAL_SPLINE_BYTES};
                const t3_motion_tables_t* weights=&tables;
#else
                const t3_motion_tables_t* weights=NULL;
#endif
                g_pose_frames[i]=motion->frames;
                full=t3_motion_sample(motion,
                    (unsigned)VM(i).clock.pose_frame-(VM(i).phased?1u:0u),weights,pose);
                if(full==1) {
#if defined(T3_ENABLE_PSX_MOVE_LIFECYCLE)
                    const t3_motion_stream_t* root_motion;
                    if(t3_motion_open_cached(&g_root_cache[i],VM(i).root_bank,VM(i).common,
                            VM(i).root_move.animation_offset,&root_motion)!=1 ||
                       t3_motion_sample_root(root_motion,
                            (unsigned)VM(i).clock.root_frame-1u,weights,root)!=1) {
                        g_pose_status[i]=4u;++g_decode_errors[i];continue;
                    }
                    /* A held transition can keep the previous root stream
                     * while the new move drives the limbs. */
                    pose[0]=root[0];pose[1]=root[1];pose[2]=root[2];
#endif
                    g_pose_status[i]=1u; /* all 49 channels available */
                    g_root_pose[i][0]=pose[0];
                    g_root_pose[i][1]=pose[1];
                    g_root_pose[i][2]=pose[2];
#if T3_LOCAL_TRIG_BYTES > 0
                    {
                        const t3_psx_trig_t trig={
                            t3_local_trig_blob,T3_LOCAL_TRIG_BYTES};
                        t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS];
                        uint32_t valid=0u;
                        if(t3_psx_pose_fk(pose,&trig,slots,&valid)) {
                            g_fk_mask[i]=valid;
                            g_fk_root_xx[i]=slots[0].m[0][0];
#if T3_LOCAL_IK_BYTES > 0
                            {
                                const t3_psx_ik_tables_t ik_tables={
                                    {t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},
                                    t3_local_ik_blob,T3_LOCAL_IK_BYTES};
                                t3_psx_matrix_t complete[T3_PSX_ROTATION_SLOTS];
                                uint32_t complete_mask=0u;
                                if(t3_psx_pose_ik(pose,&ik_tables,slots,valid,
                                                  complete,&complete_mask)==1) {
                                    unsigned k;
                                    for(k=0u;k<T3_PSX_ROTATION_SLOTS;++k)
                                        slots[k]=complete[k];
                                    g_ik_mask[i]=complete_mask & ~valid;
                                    valid=complete_mask;
                                }
                            }
#endif
#if defined(T3_LOCAL_KMD_BIND)
                            {
                                t3_psx_joint_t world[T3_PSX_DRAW_PARTS];
                                t3_psx_joint_t root_frame={0};
                                uint32_t partmask=0u;
                                /* Preview poses are model-local. Owned mode
                                 * uses the authoritative original arena frame;
                                 * renderer centering is a separate transform. */
                                t3_vec3i_t zero={0};
                                uint16_t facing=0x8000u,heading=0x8000u;
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
                                zero=g_owned_match.fighter[i].ground.anchor;
                                facing=g_owned_match.fighter[i].ground.facing;
                                heading=g_owned_match.fighter[i].ground.heading;
#endif
                                int root_ok=t3_psx_pose_world_root(pose,
#if defined(T3_FIGHTER_PACKS)
                                    t3_fighter_pack(i)->scale_percent,zero,
#else
                                    T3_KMD_SCALE_PERCENT,zero,
#endif
                                    facing,heading,&trig,&root_frame);
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
                                root_frame.origin=g_owned_match.fighter[i].ground.root;
#endif
                                if(root_ok && t3_psx_compose_parts(ACTOR_PARTS(i),
                                    slots,valid,&root_frame,world,&partmask)) {
                                    g_fk_parts[i]=partmask;
                                    if((partmask & 0x3FFFFu)==0x3FFFFu) {
                                        t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES];
                                        __builtin_memcpy(g_world_pose[i],world,
                                                         sizeof(world));
                                        /* Basis follows this mode's pose:
                                         * model-local preview or owned world.
                                         * Contact is not activated here. */
                                        if(t3_psx_parts_to_slots(ACTOR_PARTS(i),world,partmask,world,&partmask) &&
                                           t3_psx_hurt_from_parts(world,partmask,
                                                                  0,zones)) {
                                            g_hurt_ready[i]=1u;
                                            g_hurt_head_y[i]=zones[0].center.y;
                                        }
                                    }
                                }
                            }
#endif
                        }
                    }
#endif
                } else {
                    simple=t3_motion_sample_root(motion,
                        (unsigned)VM(i).clock.pose_frame-(VM(i).phased?1u:0u),
                                                  weights,root);
                    if(simple==1) {
                        g_pose_status[i]=2u; /* only scalar XYZ root available */
                        g_root_pose[i][0]=root[0];
                        g_root_pose[i][1]=root[1];
                        g_root_pose[i][2]=root[2];
                    } else {
                        g_pose_status[i]=(full<0 || simple<0)?4u:3u;
                        /* 3 = missing original interpolation tables,
                         * 4 = corrupt or unsupported authored stream. */
                    }
                }
            } else if(opened<0) {
                g_pose_status[i]=4u;
            }
        }
#endif
    }
#if defined(T3_ENABLE_PSX_MOVE_LIFECYCLE) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    /* End-of-step input decisions become PENDING, never the rendered move
     * until a later begin phase satisfies their entry-frame condition. */
    for(i=0;i<2u;++i) {
        if(t3_move_vm_queue_input(&VM(i),&match->input_histories[i],NULL,
                t3_local_sequences,T3_LOCAL_SEQUENCE_COUNT)<0)++g_decode_errors[i];
    }
#endif

#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    {
        t3_psx_shadow_actor_t actors[2]={{0}};
        t3_psx_joint_t collision_joints[2][T3_PSX_DRAW_PARTS];
        t3_attack_sample_t attacks[2];
        t3_psx_root_t roots[2];
        uint32_t begin=g_t3_shadow_root_epoch, end;
        uint32_t valid;
        unsigned fighter;
        if(!(begin&1u)) {
            valid=g_t3_shadow_root_valid_mask;
            for(fighter=0u;fighter<2u;++fighter) {
                const volatile t3_psx_root_t* p=&g_t3_shadow_external_roots[fighter];
                roots[fighter].origin.x=p->origin.x;
                roots[fighter].origin.y=p->origin.y;
                roots[fighter].origin.z=p->origin.z;
                roots[fighter].sin_q12=p->sin_q12;
                roots[fighter].cos_q12=p->cos_q12;
            }
            end=g_t3_shadow_root_epoch;
            /* Reusing last tick's roots is not permitted: every sim
             * step requires a newly published, consistent PS1 frame.
             * This also fails closed if multiple sim ticks occur during
             * one VBlank without a matching fresh debugger update. */
            if(begin==end && !(end&1u) && (valid&3u)==3u &&
               end!=g_shadow_last_injected_epoch) {
                g_shadow_last_injected_epoch=end;
                for(fighter=0u;fighter<2u;++fighter) {
                    const t3_move_vm_t* vm=&VM(fighter);
                    int decoded=t3_attack_sample(vm->active_bank,&vm->move,
                                                 vm->clock.pose_frame,0,&attacks[fighter]);
                    actors[fighter].root=roots[fighter];
                    uint32_t collision_mask=0;
                    int remapped=t3_psx_parts_to_slots(ACTOR_PARTS(i),g_world_pose[fighter],
                                g_fk_parts[fighter],collision_joints[fighter],&collision_mask);
                    actors[fighter].parts=collision_joints[fighter];
                    actors[fighter].pose_available=(uint8_t)
                        (remapped && (collision_mask&T3_PSX_BODY_PART_MASK)==
                           T3_PSX_BODY_PART_MASK);
                    actors[fighter].parts_mask=collision_mask;
                    actors[fighter].move_slot=vm->slot;
                    /* t3_psx_sweep_sample expects ACTIVE-WINDOW-relative time,
                     * not the raw DIVMOT move frame. */
                    actors[fighter].attack_frame=
                        vm->clock.pose_frame>=vm->move.active_first?
                        (uint16_t)(vm->clock.pose_frame-vm->move.active_first):0u;
                    if(decoded==1 && attacks[fighter].segment_count)
                        actors[fighter].attack=&attacks[fighter];
                    else if(decoded<0)
                        actors[fighter].attack=NULL; /* no fabricated segment */
                }
                g_shadow_valid=(uint8_t)t3_psx_shadow_step(
                    &g_shadow,actors,g_shadow_result);
            } else g_shadow_valid=0u;
        } else g_shadow_valid=0u;
        if(!g_shadow_valid) {
            /* Missing or unsynchronized injected roots invalidate historical
             * endpoints. Do not reuse geometry from the last valid packet. */
            t3_psx_shadow_reset(&g_shadow);
            for(fighter=0u;fighter<2u;++fighter) {
                g_shadow_result[fighter].status=T3_SHADOW_MISSING;
                g_shadow_result[fighter].new_contact=0u;
                g_shadow_result[fighter].zone=-1;
                g_shadow_result[fighter].segment=-1;
            }
        }
    }
#endif
#if defined(T3_POSE_TRACE)
    {
        unsigned fighter, byte;
        ++g_t3_pose_trace_epoch; /* start write; odd */
        for(fighter=0u;fighter<2u;++fighter) {
            uint8_t encoded[T3_PSX_TRACE_BYTES];
            uint32_t mask=0u;
            const t3_psx_joint_t* joints=NULL;
#if defined(T3_LOCAL_KMD_BIND)
            if((g_fk_parts[fighter]&T3_PSX_TRACE_BODY_MASK)==
                            T3_PSX_TRACE_BODY_MASK) {
                mask=T3_PSX_TRACE_BODY_MASK;
                joints=g_world_pose[fighter];
            }
#endif
            if(!t3_psx_trace_encode(g_t3_pose_trace_tick,fighter,
                    VM(fighter).slot,VM(fighter).clock.pose_frame,
                    joints,mask,encoded,sizeof(encoded))) {
                /* Unexpected invalid part coordinates: never expose stale
                 * valid joints to the oracle harness. */
                (void)t3_psx_trace_encode(g_t3_pose_trace_tick,fighter,
                    VM(fighter).slot,VM(fighter).clock.pose_frame,
                    NULL,0u,encoded,sizeof(encoded));
            }
            for(byte=0u;byte<T3_PSX_TRACE_BYTES;++byte)
                g_t3_pose_trace[fighter][byte]=encoded[byte];
        }
        ++g_t3_pose_trace_epoch; /* release stable paired records */
        ++g_t3_pose_trace_tick;
    }
#endif
}

void t3_move_probe_present(void) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    if(!g_active)return;
    for(unsigned i=0;i<2u;++i)publish_owned_pose(i);
#if defined(T3_BALL_PROGRAM)
    if(g_ball_on)t3_ball_game_view(&g_ball,&g_ball_view);   /* the image's ball, whatever the Slave has simulated since */
#if defined(T3_BALL_TEST_POINT)
    /* Test only: every 5 seconds the ball shows a point's flash (kind 2, k = 5) for a second, to see its streaks on the probe */
    if((g_ball.ticks%300u)<60u){g_ball_view.ball.kind=2;g_ball_view.ball.timer=5;}
    else if((g_ball.ticks%300u)>=120u&&(g_ball.ticks%300u)<240u){g_ball_view.ball.kind=4;g_ball_view.ball.timer=(uint8_t)(g_ball.ticks*2u);g_ball_view.ball.pos[0]=g_ball.f[0].x+900;g_ball_view.ball.pos[1]=-900;g_ball_view.ball.pos[2]=g_ball.f[0].z;}   /* a set ball's pulse */
#endif
#endif
#endif
}
#if defined(T3_ENABLE_PSX_OWNED_MATCH) && defined(T3_BALL_PROGRAM)
/* Tekken Ball: on for the next reset, with the ball type (0 beach, 1 gum, 2 iron). */
void t3_move_probe_set_ball(uint8_t on,uint8_t option) {
    g_ball_on=on;g_ball_option=option>2u?2u:option;
}
const t3_ball_view_t* t3_move_probe_ball(void) {return g_ball_on?&g_ball_view:0;}
#if defined(T3_BALL_TEST_POINT) && defined(T3_OWNED_EFFECT_POOL)
unsigned t3_move_probe_effect_count(void) {return g_effect_pool.count;}
#endif
int t3_move_probe_camera_view(t3_psx_camera_view_t* out) {
    if(!out || !g_active || !g_owned_match.camera.started)return 0;
    *out=g_owned_match.camera.camera.view;
    return 1;
}
const t3_psx_trig_t* t3_move_probe_trig(void) {return g_active?&g_owned_config.ground_tables.trig:0;}
int t3_move_probe_ball_mesh(t3_ball_mesh_t* out) {
    return g_ball_on && &t3_ball_data_blob_size && t3_ball_mesh(t3_ball_data_blob,t3_ball_data_blob_size,g_ball_option,out);
}
#else
void t3_move_probe_set_ball(uint8_t on,uint8_t option) {(void)on;(void)option;}
const t3_ball_view_t* t3_move_probe_ball(void) {return 0;}
int t3_move_probe_camera_view(t3_psx_camera_view_t* out) {(void)out;return 0;}
const t3_psx_trig_t* t3_move_probe_trig(void) {return 0;}
int t3_move_probe_ball_mesh(t3_ball_mesh_t* out) {(void)out;return 0;}
#endif

uint8_t t3_move_probe_active(void) {return g_active;}
uint16_t t3_move_probe_slot(unsigned i) {return i<2u && g_active?VM(i).slot:0;}
uint16_t t3_move_probe_frame(unsigned i) {return i<2u && g_active?VM(i).clock.pose_frame:0;}
uint8_t t3_move_probe_phased(void) {return g_active?VM(0).phased:0u;}
int16_t t3_move_probe_pending_slot(unsigned i) {
    return i<2u && g_active && VM(i).pending_valid?(int16_t)VM(i).pending_slot:-1;
}
int16_t t3_move_probe_root_frame(unsigned i) {return i<2u && g_active?VM(i).clock.root_frame:0;}
int16_t t3_move_probe_move_age(unsigned i) {return i<2u && g_active?VM(i).clock.move_frame:0;}
int16_t t3_move_probe_freeze(unsigned i) {return i<2u && g_active?VM(i).clock.hit_freeze:0;}
int16_t t3_move_probe_hold_frames(unsigned i) {return i<2u && g_active?VM(i).clock.hold_frames:0;}
uint16_t t3_move_probe_attack_word(unsigned i) {return i<2u && g_active?VM(i).move.attack_word:0;}
uint32_t t3_move_probe_transitions(unsigned i) {return i<2u && g_active?VM(i).transitions:0;}
uint8_t t3_move_probe_segments(unsigned i) {return i<2u && g_active?g_segments[i]:0u;}
uint8_t t3_move_probe_event_status(unsigned i) {
    return i<2u && g_active?g_move_event_status[i]:0u;
}
uint8_t t3_move_probe_extra_kind(unsigned i) {
    return i<2u && g_active?g_move_extra_kind[i]:0u;
}
uint8_t t3_move_probe_extra_damage(unsigned i) {
    return i<2u && g_active?g_move_extra_damage[i]:0u;
}
uint16_t t3_move_probe_event_fired(unsigned i) {
    return i<2u && g_active?g_move_event_fired[i]:0u;
}
uint32_t t3_move_probe_decode_errors(unsigned i) {return i<2u && g_active?g_decode_errors[i]:0u;}
uint8_t t3_move_probe_pose_status(unsigned i) {return i<2u && g_active?g_pose_status[i]:0u;}
uint8_t t3_move_probe_pose_frames(unsigned i) {return i<2u && g_active?g_pose_frames[i]:0u;}
int16_t t3_move_probe_root_pose(unsigned i,unsigned axis) {
    return i<2u && axis<3u && g_active?g_root_pose[i][axis]:0;
}
uint32_t t3_move_probe_fk_mask(unsigned i) {return i<2u && g_active?g_fk_mask[i]:0u;}
uint32_t t3_move_probe_fk_parts(unsigned i) {return i<2u && g_active?g_fk_parts[i]:0u;}
int t3_move_probe_copy_world(unsigned i, t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                             uint32_t* mask) {
#if defined(T3_LOCAL_KMD_BIND)
    if(i>=2u || !g_active || !out || !mask ||
       (g_fk_parts[i]&0x3FFFFu)!=0x3FFFFu)return 0;
    __builtin_memcpy(out,g_world_pose[i],sizeof(g_world_pose[i]));
    *mask=g_fk_parts[i];
    return 1;
#else
    (void)i;(void)out;(void)mask;
    return 0;
#endif
}
uint32_t t3_move_probe_ik_mask(unsigned i) {return i<2u && g_active?g_ik_mask[i]:0u;}
uint8_t t3_move_probe_shadow_status(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    return i<2u && g_active?g_shadow_result[i].status:0u;
#else
    (void)i;return 0u;
#endif
}
int8_t t3_move_probe_shadow_zone(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    return i<2u && g_active?g_shadow_result[i].zone:-1;
#else
    (void)i;return -1;
#endif
}
uint32_t t3_move_probe_shadow_edges(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    return i<2u && g_active?g_shadow.contact_edges[i]:0u;
#else
    (void)i;return 0u;
#endif
}
uint32_t t3_move_probe_shadow_frames(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    return i<2u && g_active?g_shadow.contact_frames[i]:0u;
#else
    (void)i;return 0u;
#endif
}
uint8_t t3_move_probe_hurt_ready(unsigned i) {
    return i<2u && g_active ? g_hurt_ready[i] : 0u;
}
int32_t t3_move_probe_hurt_head_y(unsigned i) {
    return i<2u && g_active ? g_hurt_head_y[i] : 0;
}
int16_t t3_move_probe_fk_root_xx(unsigned i) {return i<2u && g_active?g_fk_root_xx[i]:0;}

#else
sat_result_t t3_move_probe_init(void) {return SAT_OK;}
void t3_move_probe_reset(unsigned round) {(void)round;}
void t3_move_probe_set_carry(uint8_t wins0,uint8_t wins1,uint8_t played) {(void)wins0;(void)wins1;(void)played;}
void t3_move_probe_tick(const t3_match_t* match) {(void)match;}
void t3_move_probe_present(void) {}
void t3_move_probe_set_ball(uint8_t on,uint8_t option) {(void)on;(void)option;}
const t3_ball_view_t* t3_move_probe_ball(void) {return 0;}
int t3_move_probe_camera_view(t3_psx_camera_view_t* out) {(void)out;return 0;}
const t3_psx_trig_t* t3_move_probe_trig(void) {return 0;}
int t3_move_probe_ball_mesh(t3_ball_mesh_t* out) {(void)out;return 0;}
uint8_t t3_move_probe_active(void) {return 0u;}
uint16_t t3_move_probe_slot(unsigned i) {(void)i;return 0u;}
uint16_t t3_move_probe_frame(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_phased(void) {return 0u;}
int16_t t3_move_probe_pending_slot(unsigned i) {(void)i;return -1;}
int16_t t3_move_probe_root_frame(unsigned i) {(void)i;return 0;}
int16_t t3_move_probe_move_age(unsigned i) {(void)i;return 0;}
int16_t t3_move_probe_freeze(unsigned i) {(void)i;return 0;}
int16_t t3_move_probe_hold_frames(unsigned i) {(void)i;return 0;}
uint16_t t3_move_probe_attack_word(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_transitions(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_segments(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_event_status(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_extra_kind(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_extra_damage(unsigned i) {(void)i;return 0u;}
uint16_t t3_move_probe_event_fired(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_decode_errors(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_pose_status(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_pose_frames(unsigned i) {(void)i;return 0u;}
int16_t t3_move_probe_root_pose(unsigned i,unsigned axis) {(void)i;(void)axis;return 0;}
uint32_t t3_move_probe_fk_mask(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_fk_parts(unsigned i) {(void)i;return 0u;}
int t3_move_probe_copy_world(unsigned i, t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                             uint32_t* mask) {
    (void)i;(void)out;(void)mask;return 0;
}
uint32_t t3_move_probe_ik_mask(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_shadow_status(unsigned i) {(void)i;return 0u;}
int8_t t3_move_probe_shadow_zone(unsigned i) {(void)i;return -1;}
uint32_t t3_move_probe_shadow_edges(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_shadow_frames(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_hurt_ready(unsigned i) {(void)i;return 0u;}
int32_t t3_move_probe_hurt_head_y(unsigned i) {(void)i;return 0;}

int16_t t3_move_probe_fk_root_xx(unsigned i) {(void)i;return 0;}

#endif

/* Available in baseline builds too, so the main loop has one explicit switch. */
uint8_t t3_move_probe_owned(void) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    return 1u;
#else
    return 0u;
#endif
}
uint8_t t3_move_probe_combat(void) {
#if defined(T3_ENABLE_PSX_COMBAT)
    return 1;
#else
    return 0;
#endif
}
int32_t t3_move_probe_owned_value(unsigned i,unsigned field) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    if(i<2u && g_active) {
        const t3_psx_fighter_t* f=&g_owned_match.fighter[i];
        switch(field) {
        case 0:return f->health_q16;
        case 1:return f->ground.root.x;
        case 2:return f->ground.root.y;
        case 3:return f->ground.root.z;
        case 4:return f->ground.anchor.x;
        case 5:return f->ground.anchor.y;
        case 6:return f->ground.anchor.z;
        case 7:return (int32_t)f->body_contacts;
        case 8:return f->body.push.x;
        case 9:return f->body.push.y;
        case 10:return f->body.push.z;
        case 11:return f->geometry.status;
        case 12:return f->geometry.zone;
        case 13:return f->hit_gate.status;
        case 14:return (int32_t)f->contact_frames;
        case 15:return (int32_t)f->gate_frames;
        case 16:return (int32_t)f->registered_hits;
        case 17:return (int32_t)f->registered_blocks;
        case 18:return (int32_t)f->unsupported_moves;
        case 19:return f->hit.total_damage;
        case 20:return f->reaction.selected?f->reaction.move_slot:-1;
        case 21:return f->reaction.selected?f->reaction.transition:-1;
        case 22:return f->hit_done;
        case 23:return f->hit.guarded;
        case 24:return f->hit.ko_started;
        case 25:return f->ko_collapse;
        case 26:return f->strike_joint;
        case 27:case 28:case 29: {
            /* The contact point: the hitting segment's end (as the camera reads it). */
            const int seg=f->hit_gate.segment>=0 && f->hit_gate.segment<f->strike.segment_count?f->hit_gate.segment:0;
            const t3_vec3i_t* p=f->projectile_hit?&f->projectile.end:&f->strike.segments[seg].end;
            return field==27?p->x:field==28?p->y:p->z;
        }
        case 30:{
            /* Inside an attack window, from the frame before it opens. */
            const t3_move_record_t* m=&f->vm.move;
            return m->active_first>0 && m->active_last>=m->active_first &&
                   f->vm.clock.pose_frame+1>=m->active_first && f->vm.clock.pose_frame<=m->active_last;
        }
        case 31:return f->ground.throw_state;
        case 32:return f->ground.in_throw;
        case 33:return f->ground.attack_pending;
        }
    }
#else
    (void)i;(void)field;
#endif
    return 0;
}
int t3_move_probe_fighter_shown(unsigned i) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    if(i<2u && g_active)return t3_psx_match_fighter_shown(&g_owned_match,i);
#else
    (void)i;
#endif
    return 1;
}
unsigned t3_move_probe_hand_variant(unsigned i,unsigned channel) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH) && defined(T3_PSX_HAND_CHANNELS)
    if(i<2u && channel<2u && g_active)return g_owned_match.fighter[i].hands.variant[channel];
#else
    (void)i;(void)channel;
#endif
    return 0u;
}
int32_t t3_move_probe_world_center(unsigned axis) {
    return (int32_t)(((int64_t)t3_move_probe_owned_value(0,axis+1u)+
                      t3_move_probe_owned_value(1,axis+1u))/2);
}
#if defined(T3_PSX_CAMERA)
/* The PS1 picture is 368 x 480 around (192, 240) on a 4:3 screen, and its
 * view matrix scales row 1 by 0x1BD2/4096: the half tangents are 184/H
 * across and 240*4096/(0x1BD2*H) down. The projection takes its focal terms
 * from H directly; fov_y and aspect are the matching values. */
#define PSX_ROW1 0x1BD2
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
int t3_move_probe_camera_matrix(t3_psx_camera_matrix_t* out) {
    if(!out || !g_active || !g_owned_match.camera.started)return 0;
    *out=g_owned_match.camera.matrix;
    return 1;
}
#endif
sat_result_t t3_move_probe_camera(sat_camera3d_t* camera) {
    const t3_psx_fight_camera_t* c=&g_owned_match.camera;
    const t3_psx_camera_view_t* v=&c->camera.view;
    const int16_t (*r)[3]=c->matrix.m;
    const int32_t h=c->matrix.h;
    const int32_t cx=t3_move_probe_world_center(0),cz=t3_move_probe_world_center(2);
    sat_mat4_t view,projection;
    if(!camera || !g_active || !c->started)return SAT_ERR_INVALID_ARG;
    if(h<=0 || h>0x10000 || v->x-cx< -0x40000 || v->x-cx>0x40000 || v->y< -0x40000 ||
       v->y>0x40000 || v->z-cz< -0x40000 || v->z-cz>0x40000)return SAT_ERR_INVALID_ARG;
    camera->eye=(sat_vec3_t){(v->x-cx)*96,-v->y*96,-(v->z-cz)*96};
    /* Forward is row 2 and up is -row 1 (Q12, lengths 4.0 and 4 * 1.74). */
    camera->target=(sat_vec3_t){camera->eye.x+r[2][0]*64,camera->eye.y-r[2][1]*64,
                                camera->eye.z-r[2][2]*64};
    camera->up=(sat_vec3_t){-r[1][0]*16,r[1][1]*16,r[1][2]*16};
    /* The half angle (65536 per turn) doubled, in fx16 degrees. */
    camera->fov_y=(sat_fx16_t)sat_atan2_16(240*4096,PSX_ROW1*h)*720;
    camera->aspect=(sat_fx16_t)(((int64_t)184*PSX_ROW1<<16)/(240*4096));
    SAT_TRY(sat_mat4_look_at(&view,&camera->eye,&camera->target,&camera->up));
    SAT_TRY(sat_mat4_perspective(&projection,camera->fov_y,camera->aspect,camera->near_z,camera->far_z));
    projection.m[0]=(sat_fx16_t)(((int64_t)h<<16)/184);
    projection.m[5]=(sat_fx16_t)(((int64_t)PSX_ROW1*h<<16)/(240*4096));
    return sat_mat4_multiply(&camera->view_proj,&projection,&view);
}
int32_t t3_move_probe_backdrop_turn(void) {
    return g_active && g_owned_match.camera.started?g_owned_match.camera.backdrop.angle:0;
}
#endif
sat_result_t t3_move_probe_owned_step(t3_input_t p0,t3_input_t p1,t3_match_t* view) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    if(!g_active || !view)return SAT_ERR_INVALID_ARG;
    g_owned_match.flow.timer_stopped=g_practice_clock||g_ball_on;   /* practice: no round timer, on every step path; Tekken Ball: infinite time (0x800958D4) */
#if defined(T3_ENABLE_PSX_COMBAT)
    t3_psx_match_frame_t frame={.frame_counter=g_owned_match.ticks+1u+T3_FRAME_COUNTER_BIAS};
    const t3_input_t pads[2]={p0,p1};
    for(unsigned i=0;i<2;++i) {
        /* Undo the controller boundary's previous world-facing mapping.
         * InputSource maps this physical word from the newly drawn screen
         * positions, after move starts/animation can change the side. */
        if(g_owned_config.camera) {
            frame.physical_inputs=1;
            const int right=view->fighters[i].facing>0;
            if(pads[i].held&T3_IN_FORWARD)frame.physical[i]|=(uint16_t)(right?0x2000:0x8000);
            if(pads[i].held&T3_IN_BACK)frame.physical[i]|=(uint16_t)(right?0x8000:0x2000);
            if(pads[i].held&T3_IN_LEFT)frame.physical[i]|=0x1000;
            if(pads[i].held&T3_IN_RIGHT)frame.physical[i]|=0x4000;
        }
        const unsigned bits[4]={T3_IN_LP,T3_IN_RP,T3_IN_LK,T3_IN_RK};
        const unsigned original[4]={0x80,0x10,0x40,0x20};
        for(unsigned b=0;b<4;++b) {
            if(pads[i].held&bits[b])frame.physical[i]|=(uint16_t)original[b];
            if(pads[i].pressed&bits[b])frame.pressed[i]|=(uint16_t)original[b];
        }
    }
#if defined(T3_REFERENCE_REPLAY)
    if(g_owned_match.ticks>=T3_REFERENCE_TICKS)goto reference_complete;
    if(!reference_input(g_owned_match.ticks,&p0,&p1,&frame)) {
        g_reference_error=3;return SAT_ERR_INVALID_ARG;
    }
#endif
#if defined(T3_CAPTURE_INPUT_TAPE) && defined(T3_CPU_AI)
    const uint32_t cpu_tick=g_owned_match.ticks,cpu_calls=g_t3_ai_calls;
#endif
    /* Any failure is fatal to the frontend, so step the match in place. */
    if(!t3_psx_match_step_original_in_place(&g_owned_match,p0,p1,&frame)) {
#if defined(T3_REFERENCE_REPLAY)
        g_reference_error=4;
#endif
        return SAT_ERR_INVALID_ARG;
    }
#if defined(T3_BALL_PROGRAM)
    if(g_ball_on) {
        /* FUN_800B0E14: the ball after the fighters moved (the original runs it inside the frame, after MoveBranchAll) */
        t3_ball_tables_t tables;
        __builtin_memset(&tables,0,sizeof tables);
        tables.tables.ground=g_owned_config.ground_tables;
        if(!t3_ball_game_tick(&g_ball,&g_owned_match,&tables))return SAT_ERR_INVALID_ARG;
#if defined(T3_OWNED_EFFECT_POOL)
        /* a lethal charged hit sets the victim on fire (FUN_8006F248): ten flames, one marked one when the attacker is Gon */
        for(int k=0;k<g_ball.events.count;++k) {
            const t3_ball_event_t* e=&g_ball.events.e[k];
            if(e->kind==BALL_EV_BURN&&g_owned_config.effect_tables&&(unsigned)e->a<2u)
                t3_psx_effect_burn(&g_effect_pool,g_owned_config.effect_tables,(unsigned)e->a,g_owned_config.character[1u-(unsigned)e->a]==17u);
        }
#endif
    }
#endif
#if defined(T3_REFERENCE_REPLAY)
    if(!reference_observe()){g_reference_error=5;return SAT_ERR_INVALID_ARG;}
reference_complete:;
#endif
#else
    if(!t3_psx_match_step(&g_owned_match,p0,p1))return SAT_ERR_INVALID_ARG;
#endif
#if defined(T3_FLIPBOOK_PROBE)
    {
        /* Diagnostic: the last flipbook spawn's point and vector, and the burst record. */
        const t3_psx_flipbook_pool_t* book=&g_effect_pool.flipbook;
        for(unsigned k=0;k<3;++k) {
            g_t3_flip_diag[k]=(uint32_t)book->last_point[k];
            g_t3_flip_diag[3+k]=(uint32_t)(int32_t)book->last_vector[k];
        }
        for(unsigned r=0;r<3;++r) {
            g_t3_flip_diag[6+4*r]=book->record[r].active?book->record[r].frame:0u;
            for(unsigned k=0;k<3;++k)g_t3_flip_diag[7+4*r+k]=(uint32_t)book->record[r].pos[k];
        }
    }
#endif
#if defined(T3_POWER_PROBE)
    if(g_owned_match.ticks>=3495u && g_owned_match.ticks<3550u) {
        g_t3_power_probe[g_owned_match.ticks-3495u][0]=(uint32_t)(int32_t)g_owned_match.fighter[0].ground.power_timer;
        g_t3_power_probe[g_owned_match.ticks-3495u][1]=(uint32_t)(int32_t)g_owned_match.fighter[1].ground.power_timer;
    }
#endif
#if defined(T3_RAND_PROBE)
    /* Diagnostic: the shared random state after each tick 3490..3545. */
    volatile uint32_t* probe=g_t3_rand_probe;
    if(g_owned_match.ticks>=3490u && g_owned_match.ticks<3546u)probe[g_owned_match.ticks-3490u]=g_owned_match.camera.rand;
#endif
#if defined(T3_RAND_OVERRIDE_TICK)
    /* Diagnostic for the PS1 comparison: the Saturn's random state and the session's
     * differ from the first frame, so set the shared state to the value read from the
     * PS1 at the matching tick (read-only there) just before the effect spawns. */
    if(g_owned_match.ticks==T3_RAND_OVERRIDE_TICK)g_owned_match.camera.rand=T3_RAND_OVERRIDE_VALUE;
#endif
#if defined(T3_PARTICLE_SPRAY_SELFTEST)
    /* Diagnostic fixture: the scripted fights here never land the 21 damage the
     * original needs for a spray, so spawn the original's normal spray at the
     * defender's pelvis every 90 ticks. The pool, RNG and update are the real
     * ones; only the trigger is synthetic. */
    if(g_owned_match.ticks>=200u && (g_owned_match.ticks-200u)%90u==0u) {
        const t3_psx_joint_t* joint=&g_owned_match.fighter[1].world[8];
        const t3_vec3i_t point=joint->origin;
        t3_psx_effect_spray(&g_effect_pool,point,0,0x400,0);
    }
    /* A second spray at the height of the HUD bars, to exercise the HUD
     * occlusion: the screen's top rows are about 2,500 units above the
     * fighters at this camera distance. */
    if(g_owned_match.ticks>=245u && (g_owned_match.ticks-245u)%90u==0u) {
        const t3_psx_joint_t* joint=&g_owned_match.fighter[1].world[8];
        const unsigned step=((g_owned_match.ticks-245u)/90u)&3u;
        const t3_vec3i_t point={joint->origin.x,joint->origin.y-1700-(int32_t)step*500,joint->origin.z};
        t3_psx_effect_spray(&g_effect_pool,point,0,0x400,0);
    }
#endif
    view->total_ticks=g_owned_match.ticks;
#if defined(T3_CAPTURE_INPUT_TAPE) && defined(T3_CPU_AI)
    /* The CPU's P2 input of this tick, for a pad replay of the same fight. */
    t3_cpu_tape_step(cpu_tick,g_t3_ai_calls!=cpu_calls?g_ai.pad[1]:(t3_input_t){0});
#endif
#if T3_WORLD_POSE_CACHE_ENTRIES > 0
    g_t3_world_cache_hits=g_world_pose_cache.hits;
    g_t3_world_cache_misses=g_world_pose_cache.misses;
#endif
    view->hits=(uint16_t)(g_owned_match.fighter[0].registered_hits+g_owned_match.fighter[1].registered_hits);
    view->blocks=(uint16_t)(g_owned_match.fighter[0].registered_blocks+g_owned_match.fighter[1].registered_blocks);
#if defined(T3_ENABLE_PSX_COMBAT)
    const t3_psx_round_flow_t* flow=&g_owned_match.flow;
    view->timer=(uint16_t)flow->timer;
    view->round=(uint8_t)flow->played;g_round=view->round;
    view->round_done=(uint8_t)(flow->state>=T3_ROUND_RESULT);view->match_done=(uint8_t)(flow->match_result!=0);
    view->winner=flow->match_result==3?-1:flow->match_result?(int8_t)flow->match_winner:
        g_owned_match.round_actor[0].won>0?0:g_owned_match.round_actor[1].won>0?1:-1;
    view->round_pause=(uint16_t)flow->hold;
    view->wins[0]=(uint8_t)g_owned_match.round_actor[0].wins;
    view->wins[1]=(uint8_t)g_owned_match.round_actor[1].wins;
#else
    /* Query-only presentation timer. */
    view->timer=g_owned_match.ticks<T3_ROUND_TICKS?
        (uint16_t)(T3_ROUND_TICKS-g_owned_match.ticks):0u;
#endif
    for(unsigned i=0;i<2u;++i) {
        const t3_psx_fighter_t* f=&g_owned_match.fighter[i];
        t3_fighter_t* v=&view->fighters[i];
        *v=(t3_fighter_t){0};
        v->x=(int32_t)(((int64_t)f->ground.root.x-t3_move_probe_world_center(0))*3/8);
        v->z=(int32_t)(-((int64_t)f->ground.root.z-t3_move_probe_world_center(2))*3/8);
        {
            const int64_t scaled=(int64_t)f->health_q16*T3_MAX_HEALTH;
            const int32_t max=g_owned_config.max_health_q16;
            v->health=(int16_t)(max>0?t3_div64_32(scaled,max):scaled/max);
        }
        v->facing=f->ground.root.x<=g_owned_match.fighter[1u-i].ground.root.x?1:-1;
        v->guarding=f->ground.guard!=0u;v->crouching=(f->ground.state&7u)==1u;
        v->stun=f->ground.recover_frames;
        view->input_histories[i]=f->input;
    }
    return SAT_OK;
#else
    (void)p0;(void)p1;(void)view;return SAT_ERR_INVALID_ARG;
#endif
}
#if defined(T3_ORIGINAL_HUD) && defined(T3_ENABLE_PSX_OWNED_MATCH)
void t3_move_probe_hud_bars(t3_psx_hud_bar_t out[2]) {
    if(!out)return;
    out[0]=g_owned_match.hud_bars[0];out[1]=g_owned_match.hud_bars[1];
}
#endif
/* Practice (practice.ovl, modes.md#practice): hits do no damage, the fighters hold 200 health (FUN_800519B4)
 * and the round timer stands still. */
void t3_move_probe_set_practice(uint8_t on) {
    g_practice_clock=on;
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    g_owned_config.practice=on;
    g_owned_config.max_health_q16=fight_max_health_q16();
#endif
}
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
#include "practice_signal.h"
/* FREEZE SIGNAL (FUN_800B3520): whether fighter `i` could take a branch of its move now. */
int t3_move_probe_practice_can_act(unsigned i) {
    if(!g_active)return 0;
    const t3_psx_fighter_t* f=&g_owned_match.fighter[i&1u];
    t3_branch_context_t facts;
    t3_psx_match_branch_facts(&g_owned_match,i&1u,&facts);
    const t3_divmot_t* bank=t3_psx_match_bank(g_owned_match.config,i&1u);
    return t3_practice_can_act(bank,g_owned_match.config->common,&f->vm.move,(unsigned)f->vm.clock.pose_frame,
                               f->vm.clock.frame_step>0,bank->bank_type,&facts);
}
/* FUN_800B7968's two facts for fighter `i`: standing in its stance slot, and no move waiting. */
void t3_move_probe_practice_stance(unsigned i,uint8_t* in_stance,uint8_t* idle_move) {
    const t3_psx_fighter_t* f=&g_owned_match.fighter[i&1u];
    *in_stance=(uint8_t)(f->vm.slot==f->vm.move.stance_slot);
    *idle_move=(uint8_t)!f->vm.pending_valid;
}
/* The record fields practice's attack data reads (practice_data.h), for fighters 0 and 1. */
void t3_move_probe_practice_facts(t3_practice_facts_t out[2]) {
    static uint16_t previous_slot[2];static uint32_t previous_flags[2];static int16_t react_chain[2];
    for(unsigned i=0;i<2u;++i) {
        const t3_psx_fighter_t* f=&g_owned_match.fighter[i];
        t3_practice_facts_t* o=&out[i];
        *o=(t3_practice_facts_t){0};
        const int hit=f->got_hit!=0;
        o->got_hit=f->got_hit;o->hit_clean=(uint8_t)(hit && f->hit.hit_clean);
        o->counter_hit=(uint8_t)(hit && f->hit.counter);o->close_hit=(uint8_t)(hit && f->hit.close);
        o->last_extra_damage=hit?f->hit.extra_damage:0;
        o->was_hit_this_move=f->ground.was_hit_this_move;o->in_reaction=f->ground.in_reaction;
        o->throw_state=f->ground.throw_state;o->whiffed=f->whiffed;
        if(hit && f->hit.best_slot>=0) {
            o->damage=f->hit.slots[f->hit.best_slot].damage;o->base_damage=f->hit.slots[f->hit.best_slot].base_damage;
        }
        o->move_flags=f->vm.move.flags;o->pose_flags=previous_flags[i];
        o->attack=f->vm.move.attack_word;o->cur_slot=f->vm.slot;o->bank_type=f->ground.character;
        o->move_changed=(uint8_t)(f->vm.slot!=previous_slot[i]);
        react_chain[i]=t3_practice_react_chain(react_chain[i],o);o->react_chain=react_chain[i];
        previous_slot[i]=f->vm.slot;previous_flags[i]=f->vm.move.flags;
    }
}
#endif
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
#include "practice_dummy.h"
/* The training dummy's scripted input (practice_dummy.c) for fighter 1 against fighter 0's move. */
t3_input_t t3_move_probe_practice_dummy(unsigned setting) {
    t3_input_t out={0,0};
    if(!g_active)return out;
    t3_psx_fighter_t* me=&g_owned_match.fighter[0];t3_psx_fighter_t* d=&g_owned_match.fighter[1];
    me->ground.practice=d->ground.practice=1;
    me->ground.human_guard=1;me->ground.is_cpu=0;
    t3_practice_dummy_facts_t f={0};
    f.attack=me->vm.move.attack_word;f.pose_frame=(uint16_t)me->vm.clock.pose_frame;f.active_last=me->vm.move.active_last;
    f.lying=(d->vm.move.state_word&0x204u)!=0;
    f.in_reaction=d->ground.in_reaction;f.has_air_window=d->vm.move.air_start!=0;f.move_pending=d->vm.pending_valid;
    f.frame_odd=(uint8_t)(g_owned_match.frame_counter&1u);
    f.doctor_b=t3_psx_match_bank(g_owned_match.config,1)->bank_type==18u;
    {   /* transition 0x1C among the move's branch rows (FUN_800B8B94) */
        const t3_divmot_t* bank=t3_psx_match_bank(g_owned_match.config,1);
        t3_branch_record_t row;
        for(unsigned i=d->vm.move.branch_index;i<bank->branch_count && t3_divmot_branch(bank,i,&row) && row.command!=0xC000u;++i)
            if(row.flags==0x1Cu){f.has_tech_roll=1;break;}
    }
    const t3_practice_dummy_out_t r=t3_practice_dummy_input(&f,setting);
    d->ground.human_guard=r.human_guard;d->ground.is_cpu=0;
    if(r.word&0x1000u)out.held|=T3_IN_LEFT;
    if(r.word&0x4000u)out.held|=T3_IN_RIGHT;
    if(r.word&0x8000u)out.held|=T3_IN_BACK;
    if(r.word&0x10u)out.held|=T3_IN_RP;
    if(r.word&0x20u)out.held|=T3_IN_RK;   /* ○ */
    if(r.word&0x40u)out.held|=T3_IN_LK;   /* ✕ */
    if(r.word&0x80u)out.held|=T3_IN_LP;   /* □ */
    out.pressed=(uint16_t)(out.held&(T3_IN_LP|T3_IN_RP|T3_IN_LK|T3_IN_RK));
    return out;
}
#endif
void t3_move_probe_practice_counter(uint8_t dummy_counter) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    g_owned_config.practice_counter[0]=0;g_owned_config.practice_counter[1]=dummy_counter;
#else
    (void)dummy_counter;
#endif
}
void t3_move_probe_set_cpu(uint8_t mask,uint16_t difficulty,uint8_t level) {
#if defined(T3_CPU_AI)
    g_cpu_mask=mask&3u;g_cpu_difficulty=difficulty;g_cpu_level=level;
#else
    (void)mask;(void)difficulty;(void)level;
#endif
}

#include "practice_menu.h"
/* Practice's settings (applied by the CPU that runs the ticks) and its per-tick work. */
void t3_move_probe_practice_apply(const t3_practice_settings_t* settings) {
    t3_practice_state_t* st=&g_practice_state;
    const uint8_t was_open=st->menu_was_open;
    st->set=*settings;
    st->menu_was_open=settings->paused;
    if(settings->paused && !was_open)t3_practice_guide_stop(&st->guide);   /* the pause stops the guide */
    if(settings->sub==T3_PM_SUB_COMBO || (settings->sub==T3_PM_SUB_FREE && settings->recorder)) {
        /* leaving the pause (or choosing another combo) restarts the guide: ring emptied, replay rewound */
        const uint8_t key=(uint8_t)(1u|((settings->combo_player&1u)<<1)|((unsigned)settings->combo_type<<2)|((unsigned)settings->sub<<6));
        if((was_open && !settings->paused) || st->guide_key!=key) {
            st->guide_key=key;
            t3_practice_guide_stop(&st->guide);
            t3_practice_guide_reset(&st->guide);
        }
    } else st->guide_key=0;
    t3_move_probe_set_practice(settings->on);
    t3_move_probe_practice_counter((uint8_t)(settings->on && settings->sub==T3_PM_SUB_FREE && settings->counter));
    if(settings->on)
        t3_move_probe_set_cpu(settings->sub==T3_PM_SUB_VS_CPU?2u:0u,settings->sub==T3_PM_SUB_VS_CPU?settings->cpu_difficulty:1u,
                              settings->sub==T3_PM_SUB_VS_CPU?settings->cpu_level:4u);
}
const t3_practice_state_t* t3_move_probe_practice_state(void) {
    return &g_practice_state;
}
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
static const uint8_t* combo_blob(uint32_t* size) {
    *size=&t3_practice_combo_blob_size?t3_practice_combo_blob_size:0u;
    return &t3_practice_combo_blob[0]?t3_practice_combo_blob:NULL;
}
unsigned t3_move_probe_practice_combo_count(unsigned player) {
    uint32_t size;
    const uint8_t* blob=combo_blob(&size);
    if(!blob || !g_active)return 0;
    return t3_practice_combo_count(blob,size,t3_psx_match_bank(g_owned_match.config,player&1u)->bank_type);
}
/* An original pad word (relative to the facing) as the match's input. */
static t3_input_t word_to_input(uint16_t word) {
    t3_input_t out={0,0};
    if(word&0x1000u)out.held|=T3_IN_LEFT;
    if(word&0x4000u)out.held|=T3_IN_RIGHT;
    if(word&0x8000u)out.held|=T3_IN_BACK;
    if(word&0x2000u)out.held|=T3_IN_FORWARD;
    if(word&0x10u)out.held|=T3_IN_RP;
    if(word&0x20u)out.held|=T3_IN_RK;
    if(word&0x40u)out.held|=T3_IN_LK;
    if(word&0x80u)out.held|=T3_IN_LP;
    out.pressed=(uint16_t)(out.held&(T3_IN_LP|T3_IN_RP|T3_IN_LK|T3_IN_RK));
    return out;
}
static int guide_drives(unsigned fighter) {
    const t3_practice_state_t* st=&g_practice_state;
    return st->set.on && st->guide.scripted && (st->set.combo_player&1u)==fighter &&
           (st->set.sub==T3_PM_SUB_COMBO || (st->set.sub==T3_PM_SUB_FREE && st->set.recorder));
}
t3_input_t t3_move_probe_practice_p2(t3_input_t p2) {
    const t3_practice_settings_t* set=&g_practice_state.set;
    if(guide_drives(1u))return word_to_input(g_practice_state.guide.script);
    if(set->on && g_active && set->sub!=T3_PM_SUB_VS_CPU) {
        const unsigned setting=set->sub==T3_PM_SUB_COMBO?(unsigned)T3_DUMMY_STAND:set->dummy;   /* combo training: the dummy stands */
        if(setting!=T3_DUMMY_CONTROLLER || !set->human2)return t3_move_probe_practice_dummy(setting);
    }
    return p2;
}
t3_input_t t3_move_probe_practice_p1(t3_input_t p1) {
    return guide_drives(0u)?word_to_input(g_practice_state.guide.script):p1;
}
/* The guided fighter's facts for the guide's step (_guide_step reads them from its record). */
static void guide_step_now(t3_practice_state_t* st,uint16_t pad_word,uint16_t pressed_word) {
    const unsigned cp=st->set.combo_player&1u;
    const t3_psx_fighter_t* f=&g_owned_match.fighter[cp];
    const t3_psx_fighter_t* me=&g_owned_match.fighter[0];
    uint32_t size;
    const uint8_t* blob=combo_blob(&size);
    t3_practice_combo_t combo;
    const unsigned bank=t3_psx_match_bank(g_owned_match.config,cp)->bank_type;
    const int have=blob && t3_practice_combo_get(blob,size,bank,st->set.combo_type,&combo);
    t3_practice_guide_in_t in={.sub=st->set.sub,.guide_on=(uint8_t)(st->set.sub==T3_PM_SUB_COMBO || st->set.recorder),.combo_type=(int8_t)st->set.combo_type,
        .combo_known=(uint8_t)(blob && t3_practice_combo_count(blob,size,bank)!=0),.combo=have?&combo:NULL,
        .select_pressed=(pressed_word&0x100u)!=0,.select_held=(pad_word&0x100u)!=0,.held_down=(pad_word&0x4000u)!=0,
        .held_horizontal=(pad_word&0xA000u)!=0,.mapped_record_key=((pressed_word&0xF0FFu)!=0),.pad_word=pad_word,
        .side_flag=f->side_flag,
        .lying=(uint8_t)((((f->vm.pending_valid?f->vm.pending_move.state_word:f->vm.move.state_word))&0x204u)!=0),
        .turned=f->ground.facing_quadrant!=0,.move_pending=f->vm.pending_valid,
        .hold_turn=(uint8_t)(f->vm.pending_valid && f->vm.queued.transition==4u),
        .hit_clean=(uint8_t)(f->got_hit && f->hit.hit_clean),.cur_slot=f->vm.slot,.stance_slot=f->vm.move.stance_slot,
        .active_first_frame=(uint8_t)(f->vm.move.active_first!=0 && f->vm.clock.pose_frame==f->vm.move.active_first),
        .me_cur_slot=me->vm.slot,.me_stance_slot=me->vm.move.stance_slot};
    /* The guided fighter's pad is the guide's, not the player's: the recorder and Select read the player's. */
    t3_practice_guide_step(&st->guide,&in);
}
void t3_move_probe_practice_tick(uint16_t pad_word,uint16_t pressed_word) {
    t3_practice_state_t* st=&g_practice_state;
    if(!st->set.on || !g_active)return;
    if(st->replaying) {   /* a replay plays: the fight stands still; only the replay's own end is watched */
        const int mode=g_owned_match.replay.mode;
        st->replaying=(uint8_t)(mode>=1 && mode<=4);
        if(mode==T3_REPLAY_DONE){t3_psx_replay_reset(&g_owned_match.replay,g_owned_match.replay.epoch);t3_psx_match_camera_replay_end(&g_owned_match);}
        return;
    }
    {   /* a new input after 30 idle frames starts an empty ring (FUN_800B7968) */
        uint8_t stance=0,idle_move=0;
        t3_move_probe_practice_stance(0u,&stance,&idle_move);
        if(t3_practice_keys_idle(&st->keys,(pad_word&0xF0F0u)!=0,stance,idle_move))t3_practice_keys_reset(&st->keys);
    }
    if(st->set.sub==T3_PM_SUB_COMBO || (st->set.sub==T3_PM_SUB_FREE && st->set.recorder))guide_step_now(st,pad_word,pressed_word);
    else t3_practice_keys_log(&st->keys,pad_word,g_owned_match.fighter[0].side_flag!=0);
    t3_practice_facts_t facts[2];
    t3_move_probe_practice_facts(facts);
    t3_practice_data_step(&st->data,facts);
    t3_practice_markers_step(&st->markers,facts[0].attack,facts[1].hit_clean);
    if(facts[1].hit_clean) {   /* where it hit: the attack segment's end at contact (HitTest's slot point) */
        const t3_psx_fighter_t* attacker=&g_owned_match.fighter[0];
        const int8_t segment=attacker->hit_gate.segment;   /* the gate is kept on the attacker */
        /* A projectile's contact (descriptor joint 0x18 or more) tests the segment its effect object published. */
        const t3_vec3i_t* end=attacker->projectile_hit?&attacker->projectile.end:
            (segment>=0 && segment<2?&attacker->strike.segments[segment].end:NULL);
        if(end) {
            const int32_t point[3]={end->x,end->y,end->z};
            t3_practice_markers_point(&st->markers,point);
        }
    }
    if(st->set.freeze)for(unsigned i=0;i<2u;++i)st->can_act[i]=(uint8_t)t3_move_probe_practice_can_act(i);
    {   /* REPLAY SETTINGS (FUN_800B8D50): a combo's or Select's replay of the last frames, inside the running fight */
        const t3_psx_fighter_t* me=&g_owned_match.fighter[0];const t3_psx_fighter_t* dummy=&g_owned_match.fighter[1];
        const int mode=g_owned_match.replay.mode;
        st->replaying=(uint8_t)(mode>=1 && mode<=4);
        if(mode==T3_REPLAY_DONE){t3_psx_replay_reset(&g_owned_match.replay,g_owned_match.replay.epoch);t3_psx_match_camera_replay_end(&g_owned_match);}   /* the replay ends: recording again, the fight camera back */
        const t3_practice_player_t* d=&st->data.p[1];
        t3_practice_replay_in_t in={.setting=st->set.replay,.paused=st->set.paused,.replaying=st->replaying,
            .select_pressed=(pressed_word&0x100u)!=0,.combo=d->combo,.last=st->last_combo,.ended=d->ended,
            .acting=(uint8_t)(me->vm.move.active_first!=0 || (me->vm.pending_valid && me->vm.pending_move.active_first!=0) ||
                (dummy->vm.slot!=dummy->vm.move.stance_slot && (dummy->ground.was_hit_this_move || dummy->ground.in_reaction))),
            .move_length=me->vm.move.length};
        uint8_t dropped;
        const unsigned frames=t3_practice_replay_step(&st->replay,&in,&dropped);
        st->last_combo=d->combo;
        if(frames && st->set.replay!=T3_REPLAY_OFF)g_owned_match.practice_replay_back=(int16_t)(frames-2u);
    }
}
#else
t3_input_t t3_move_probe_practice_p2(t3_input_t p2) {return p2;}
t3_input_t t3_move_probe_practice_p1(t3_input_t p1) {return p1;}
unsigned t3_move_probe_practice_combo_count(unsigned player) {(void)player;return 0;}
void t3_move_probe_practice_tick(uint16_t pad_word,uint16_t pressed_word) {(void)pad_word;(void)pressed_word;}
#endif
void t3_move_probe_set_start_buttons(uint16_t first,uint16_t second) {
    g_start_physical[0]=first;g_start_physical[1]=second;
}
uint8_t t3_move_probe_cpu_mask(void) {
#if defined(T3_CPU_AI)
    return g_cpu_mask;
#else
    return 0u;
#endif
}
int32_t t3_move_probe_camera_value(unsigned field) {
#if defined(T3_ENABLE_PSX_COMBAT)
    const t3_psx_fight_camera_t* c=&g_owned_match.camera;
    if(!g_active)return 0;
    switch(field) {
    case 0:return c->camera.throw_player;
    case 1:return c->camera.thrower;
    case 2:return c->camera.state;
    case 3:return c->camera.script;
    }
#else
    (void)field;
#endif
    return 0;
}
int32_t t3_move_probe_round_value(unsigned field) {
#if defined(T3_ENABLE_PSX_COMBAT)
    switch(field) {
    case 0:return 1;
    case 1:return g_owned_match.flow.state;
    case 2:return g_owned_match.flow.counter;
    case 3:return g_owned_match.flow.hold;
    case 4:return g_owned_match.replay.mode;
    case 5:return g_owned_match.replay.record;
    case 6:return g_owned_match.replay.play;
    case 7:return g_owned_match.replay.mark;
    case 8:return g_owned_match.replay.recorded;
    case 9:return (int32_t)g_owned_match.replay.epoch;
    case 10:return g_owned_match.flow.played;
    case 11:return g_owned_match.flow.frame;
    case 12:{
        const t3_psx_round_flow_t* flow=&g_owned_match.flow;
        return flow->max_rounds<=flow->played;
    }
    case 13:return g_owned_match.flow.result;
    case 14:return g_owned_match.flow.human_mask;
    case 15:return g_owned_match.flow.replay_request;
    case 16:return (int32_t)g_owned_match.frame_counter;
    case 17:return g_owned_match.flow.max_rounds;
#if defined(T3_ORIGINAL_HUD)
    case 18:return g_owned_match.hud_replay_entry;
#endif
#if defined(T3_PSX_BALL)
    case 19:{   /* Tekken Ball: 1, the result message waits for the last replay (round_hud.gd); 2, the bars are hidden */
        const t3_psx_round_flow_t* flow=&g_owned_match.flow;
        if(!g_owned_config.ball_court)return 0;
        return (flow->ball_shown<(int32_t)g_owned_match.ball_shows-1?1:0)|(flow->hud_hidden?2:0);
    }
#endif
    }
#else
    (void)field;
#endif
    return 0;
}
void t3_move_probe_pose_cache_stats(uint32_t out[4]) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH) && T3_LOCAL_POSE_CACHE_ENTRIES > 0
    out[0]=g_local_pose_cache.capacity;out[1]=g_local_pose_cache.hits;
    out[2]=g_local_pose_cache.misses;out[3]=g_local_pose_cache.evictions;
#else
    for(unsigned i=0;i<4;++i)out[i]=0;
#endif
}
