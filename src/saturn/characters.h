#ifndef T3_SATURN_CHARACTERS_H
#define T3_SATURN_CHARACTERS_H

#include "fight.h"
#include "saturn/core.h"
#include "saturn/scene.h"
#include "t3_generated_character.h"

/* 48 floor faces, 36+ fighter body geometry; generous but bounded. */
#define T3_SCENE_CAPACITY (T3_FACE_COUNT * 2u + 128u)

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
uint8_t t3_characters_skin_preview_applied(unsigned fighter);
uint32_t t3_characters_texture_vram_bytes(void);
uint32_t t3_characters_skin_work_ms(void);

#endif
