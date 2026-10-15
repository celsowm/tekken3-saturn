#ifndef T3_SATURN_CHARACTERS_H
#define T3_SATURN_CHARACTERS_H

#include "fight.h"
#include "saturn/core.h"
#include "saturn/scene.h"
#include "t3_generated_character.h"

/* 48 floor faces, 36+ fighter body geometry; generous but bounded. */
#define T3_SCENE_CAPACITY (T3_FACE_COUNT * 2u + 128u)

sat_result_t t3_characters_init(void);
sat_result_t t3_characters_reset(void);
sat_result_t t3_characters_tick(const t3_match_t* game);
sat_result_t t3_characters_render(sat_scene_t* scene, const t3_match_t* game);
uint8_t t3_characters_skin_preview_applied(unsigned fighter);

#endif
