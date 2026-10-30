#ifndef T3_SATURN_MOVE_PROBE_H
#define T3_SATURN_MOVE_PROBE_H
#include "fight.h"
#include "psx_pose.h"
#include "psx_trace.h"
#if defined(T3_ORIGINAL_HUD) && defined(T3_ENABLE_PSX_OWNED_MATCH)
#include "psx_hud_bar.h"
#endif
#include "saturn/core.h"

/* Optional PS1 DIVMOT logic interpreter, isolated from combat gameplay.
 * Only source files emitted into the private build/ tree may be loaded. */
sat_result_t t3_move_probe_init(void);
void t3_move_probe_reset(unsigned round);
void t3_move_probe_tick(const t3_match_t* match);
/* Snapshot the final owned pose once per submitted image. Legacy preview
 * modes still advance their own pose in tick; this call leaves them alone. */
void t3_move_probe_present(void);
uint8_t t3_move_probe_active(void);
/* Original-state mode: view is presentation only, never a second simulation. */
uint8_t t3_move_probe_owned(void);
uint8_t t3_move_probe_combat(void);
sat_result_t t3_move_probe_owned_step(t3_input_t p0,t3_input_t p1,t3_match_t* view);
/* The fighters the CPU opponent drives (bit 0 P1, bit 1 P2), its difficulty
 * and level; they apply at the next reset. Builds without the private AI
 * tables have no CPU opponent (mask 0). */
void t3_move_probe_set_cpu(uint8_t mask,uint16_t difficulty,uint8_t level);
/* Original physical face/shoulder bits sampled at first-round placement. */
void t3_move_probe_set_start_buttons(uint16_t first,uint16_t second);
uint8_t t3_move_probe_cpu_mask(void);
#if defined(T3_SLAVE_SIMULATION)
/* Cache ownership only; caller has joined the previous simulation job.
 * Full transfer is required across Master reset; ordinary consumption needs
 * only the match and query counters, not the Slave-owned cache/history data. */
sat_result_t t3_move_probe_simulation_sync(int full);
#endif
#if defined(T3_PARTICLE_IMAGE_SNAPSHOT)
#include "psx_particle_snapshot.h"
#include "psx_camera.h"
#include "psx_flipbook.h"
typedef struct {
    t3_psx_particle_snapshot_t sprites;
    t3_psx_camera_view_t camera;
    uint32_t frame_counter;
#if defined(T3_FLIPBOOK_BURST)
    t3_psx_flipbook_t flipbook[T3_PSX_FLIPBOOK_RECORDS]; /* as drawn this image */
#endif
} t3_particle_image_t;
extern t3_particle_image_t g_t3_particle_image;
extern volatile uint32_t g_t3_particle_images,g_t3_particle_max_sprites;
sat_result_t t3_move_probe_capture_particles(void);
#if defined(T3_PARTICLE_PREPARE)
const t3_psx_trig_t* t3_move_probe_particle_trig(void);
#endif
#endif
int32_t t3_move_probe_world_center(unsigned axis);
#if defined(T3_PSX_CAMERA)
#include "saturn/scene3d.h"
#include "psx_camera.h"
/* The owned match's original fight camera as the Saturn camera, in the
 * skin's world (x, -y, -z around the fighters' centre, 96 fx16 per unit).
 * Sets eye, target, up, fov_y, aspect and view_proj; near/far are kept. */
sat_result_t t3_move_probe_camera(sat_camera3d_t* camera);
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
/* Snapshot before dispatching the worker and its deferred simulation. */
int t3_move_probe_camera_matrix(t3_psx_camera_matrix_t* out);
#if defined(T3_VDP1_FLOOR_PROBE)
int t3_move_probe_floor_center(int32_t out[2]);
#endif
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
int t3_move_probe_floor_camera_distance(uint32_t* out);
#endif
#endif
/* The panorama's turn after the last frame (4096 a turn). */
int32_t t3_move_probe_backdrop_turn(void);
#endif
int32_t t3_move_probe_owned_value(unsigned fighter,unsigned field);
#if defined(T3_ORIGINAL_HUD) && defined(T3_ENABLE_PSX_OWNED_MATCH)
/* Original 60 Hz HUD lengths, copied only at a committed simulation boundary. */
void t3_move_probe_hud_bars(t3_psx_hud_bar_t out[2]);
#endif
int32_t t3_move_probe_round_value(unsigned field);
/* Throw-camera handoff: 0 throw_player, 1 thrower, 2 director state,
 * 3 playing script/preset (-1 none). */
int32_t t3_move_probe_camera_value(unsigned field);
uint16_t t3_move_probe_slot(unsigned fighter);
uint16_t t3_move_probe_frame(unsigned fighter);
uint8_t t3_move_probe_phased(void);
int16_t t3_move_probe_pending_slot(unsigned fighter);
int16_t t3_move_probe_root_frame(unsigned fighter);
int16_t t3_move_probe_move_age(unsigned fighter);
int16_t t3_move_probe_freeze(unsigned fighter);
int16_t t3_move_probe_hold_frames(unsigned fighter);
uint16_t t3_move_probe_attack_word(unsigned fighter);
uint32_t t3_move_probe_transitions(unsigned fighter);
uint8_t t3_move_probe_segments(unsigned fighter);
/* Private original section4 event decoder: 0 inactive; 1 decoded/no
 * event or valid event; 2 authored section not embedded; 3 corrupt. */
uint8_t t3_move_probe_event_status(unsigned fighter);
uint8_t t3_move_probe_extra_kind(unsigned fighter);
uint8_t t3_move_probe_extra_damage(unsigned fighter);
uint16_t t3_move_probe_event_fired(unsigned fighter);
uint32_t t3_move_probe_decode_errors(unsigned fighter);
/* 0 no stream; 1 full pose; 2 XYZ root only;
 * 3 missing PS1 spline tables; 4 malformed/unsupported stream. */
uint8_t t3_move_probe_pose_status(unsigned fighter);
uint8_t t3_move_probe_pose_frames(unsigned fighter);
int16_t t3_move_probe_root_pose(unsigned fighter,unsigned axis);
uint32_t t3_move_probe_fk_mask(unsigned fighter);
uint32_t t3_move_probe_fk_parts(unsigned fighter);
uint32_t t3_move_probe_ik_mask(unsigned fighter);
/* Model-local PS1 defensive geometry only; not authoritative world combat. */
uint8_t t3_move_probe_hurt_ready(unsigned fighter);
int32_t t3_move_probe_hurt_head_y(unsigned fighter);
/* Atomically hand a complete PRIVATE 18-part pose to the skin preview.
 * 0 means no reliable pose; caller keeps its normal renderer. */
int t3_move_probe_copy_world(unsigned fighter,
                             t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                             uint32_t* parts_mask);
int16_t t3_move_probe_fk_root_xx(unsigned fighter);
/* Private PS1 world-placement debug injection. Requires an explicit,
 * externally supplied root for both fighters (no Saturn Q8 fallback).
 * Write g_t3_shadow_root_epoch odd -> data + mask=3 -> even.
 * SH-2 native layout uses BIG-endian int32/int16 fields. */
uint8_t t3_move_probe_shadow_status(unsigned fighter);
int8_t t3_move_probe_shadow_zone(unsigned fighter);
uint32_t t3_move_probe_shadow_edges(unsigned fighter);
uint32_t t3_move_probe_shadow_frames(unsigned fighter);
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_DIVMOT_ENABLED) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
extern volatile uint32_t g_t3_shadow_root_epoch;
extern volatile uint32_t g_t3_shadow_root_valid_mask;
extern volatile t3_psx_root_t g_t3_shadow_external_roots[2];
#endif
/* Private debugger ABI: each record is LE and 564 bytes.
 * Read the even epoch before/after 2 records; reject changes/odd epochs.
 * Symbol exists ONLY with T3_POSE_TRACE in local DIVMOT builds. */
#if defined(T3_LOCAL_DIVMOT_ENABLED) && defined(T3_POSE_TRACE)
extern volatile uint32_t g_t3_pose_trace_epoch;
extern volatile uint8_t g_t3_pose_trace[2][T3_PSX_TRACE_BYTES];
#endif
/* Capacity/hits/misses/evictions; derived counters reset with the fight. */
void t3_move_probe_pose_cache_stats(uint32_t out[4]);
#endif
