#ifndef T3_SATURN_CHARACTERS_H
#define T3_SATURN_CHARACTERS_H

#include "fight.h"
#include "saturn/core.h"
#include "saturn/scene.h"
#include "t3_generated_character.h"

/* 48 floor faces, 36+ fighter body geometry; generous but bounded. */
#define T3_SCENE_CAPACITY (T3_FACE_COUNT * 2u + 128u)
/* With the projected queue, full scene records are needed only for faces
 * the compiled path cannot draw (near-plane clipping, uncompiled
 * materials). The measured peak is 225 per frame, in KO close-ups; a face
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
/* Fused preparation normally splits the actors with sat_parallel_for. With a
 * start function, render calls it just before actor 1 is needed; it must
 * start a task whose worker calls t3_characters_prepare_worker() first (that
 * task may go on with unrelated work), and render prepares actor 0 meanwhile.
 * NULL restores sat_parallel_for. */
typedef sat_result_t (*t3_characters_worker_start_t)(void);
void t3_characters_set_worker_start(t3_characters_worker_start_t start);
void t3_characters_prepare_worker(void);
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
