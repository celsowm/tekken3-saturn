#ifndef T3_SATURN_CHARACTERS_H
#define T3_SATURN_CHARACTERS_H

#include "fight.h"
#include "saturn/core.h"
#include "saturn/scene.h"
#include "t3_generated_character.h"
#if defined(T3_MIXED_OT_DIAGNOSTIC)
#include "psx_particle_prepare.h"
sat_result_t t3_characters_mixed_ot(const sat_scene_t* scene,
    const t3_psx_particle_draw_t* particles,unsigned count);
#endif

/* 48 floor faces, 36+ fighter body geometry; generous but bounded. */
#define T3_SCENE_CAPACITY (T3_FACE_COUNT * 2u + 128u)
/* With the projected queue, full scene records are needed only for faces
 * the compiled path cannot draw (a corner past the near plane or the
 * off-screen bound, uncompiled materials). The measured peak is 225 per frame, in KO close-ups; a face
 * beyond the bound is dropped and counted in skipped_faces. The Slave
 * batch asks for at most T3_ACTOR_COLD_FACES of them. */
#ifndef T3_COLD_FACES
#define T3_COLD_FACES 640u
#endif
#define T3_ACTOR_COLD_FACES 320u

typedef enum {
    T3_CHARACTER_LOAD_MODEL_READY,
    T3_CHARACTER_LOAD_PLAYER1_READY,
    T3_CHARACTER_LOAD_PLAYER2_READY
} t3_character_load_stage_t;
typedef void (*t3_character_load_progress_fn)(void* context, t3_character_load_stage_t stage);

/* Optional callback after model validation and each actor's complete setup.
 * No render frame is open; the caller may present loading progress here. */
sat_result_t t3_characters_init(t3_character_load_progress_fn progress, void* context);
sat_result_t t3_characters_reset(void);
sat_result_t t3_characters_tick(const t3_match_t* game);
sat_result_t t3_characters_render(sat_scene_t* scene, const t3_match_t* game);
#if defined(T3_FORCE_PROGRAM)
#include "psx_pose.h"
/* After sat_shutdown/sat_init, reload both packs into the same actor buffers. */
sat_result_t t3_characters_force_reload(void);
sat_result_t t3_characters_force_bind_enemy(unsigned actor,unsigned key);
unsigned t3_characters_force_model(unsigned actor);
/* Diagnostic only: compare the last skin output to the serial solver. */
sat_result_t t3_characters_force_check_skin(uint32_t* checked);
/* Tekken Force owns its match, so the frontend publishes each actor's
 * original joint pose every tick (as the move probe does for its fixture)
 * and the camera origin the renderer rebases it to. Rebasing keeps the joint
 * origins inside the original-unit bounds of the skin bridge across a whole
 * level, whose camera travels to ~32,768 units. */
void t3_characters_set_force_pose(unsigned actor,
    const t3_psx_joint_t world[T3_PSX_DRAW_PARTS], uint32_t parts_mask, int shown);
void t3_characters_set_force_origin(int32_t x, int32_t z);
/* Diagnostics: 1 when the actor's original pose was bridged for the last
 * image, 0 otherwise. */
int t3_characters_force_pose_ready(unsigned actor);
#endif
#if defined(T3_EFFECT_RING)
/* W3.2 of docs/POST_VRAM_FRAME_RATE_PLAN.md: the last image's mixed-OT
 * counts (live fighter faces, sorted entries) and the FRT split of its
 * phases (coverage test, OT sort, compose). */
extern volatile uint32_t g_t3_mixed_ot_faces_last,g_t3_mixed_ot_entries_last;
extern volatile uint32_t g_t3_mixed_ot_frt[3];
extern volatile uint32_t g_t3_mixed_ot_cover_particles_frt,g_t3_mixed_ot_compose_faces_frt;
extern volatile uint32_t g_t3_mixed_ot_considered_last,g_t3_mixed_ot_depth_last;
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
extern volatile uint32_t g_t3_original_projected[2];
#endif
#endif
/* Fused preparation normally splits the actors with sat_parallel_for. With a
 * start function, render calls it just before actor 1 is needed; it must
 * start a task whose worker calls t3_characters_prepare_worker() first (that
 * task may go on with unrelated work), and render prepares actor 0 meanwhile.
 * NULL restores sat_parallel_for. */
typedef sat_result_t (*t3_characters_worker_start_t)(void);
void t3_characters_set_worker_start(t3_characters_worker_start_t start);
void t3_characters_prepare_worker(void);
/* Percent of actor 1's vertices the Master skins in the next preparation
 * (0 to 100; T3_MASTER_SKIN_PERCENT until changed). The vertices are the same
 * whichever CPU skins them. */
void t3_characters_set_master_skin_percent(unsigned percent);
/* FRT durations are owned by the actor CPU and published after the fused
 * worker join. Dispatch includes both actors and must not be added to them. */
typedef struct {
    uint16_t skin[2], placement[2], geometry[2];
    uint16_t projection[2], face_prep[2];
    uint16_t setup, dispatch, merge;
} t3_character_phase_profile_t;
const t3_character_phase_profile_t* t3_characters_phase_profile(void);
uint8_t t3_characters_skin_preview_applied(unsigned fighter);
uint32_t t3_characters_texture_vram_bytes(void);
uint32_t t3_characters_skin_work_ms(void);

#endif
