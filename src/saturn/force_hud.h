/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * The Tekken Force fight HUD as Tekken3Remake documents it (hud.md, modes.md, mode_hud_view.gd);
 * see NOTICE.md. */
#ifndef T3_FORCE_HUD_H
#define T3_FORCE_HUD_H
#include "saturn/core.h"
#include <stdint.h>

/* FHUD.BIN (extract_force_hud.py), KEYS.BIN and the player's name sheet (pack section NAMES).
 * `scratch` holds the largest of them while they upload. Run again after every sat_init. */
sat_result_t t3_force_hud_init(void* scratch,unsigned bytes);

/* All coordinates are the original 384 x 480 ones; lengths in original pixels. */
/* FUN_800B3790: a bar `width` wide at x, y, filled from its right end (fill, then recent damage). */
sat_result_t t3_force_hud_bar(int x,int y,int width,int fill,int recent);
/* FUN_8004EB9C as force.ovl places it: the player's name at (14, 66) on its plate. */
sat_result_t t3_force_hud_player_name(void);
/* FUN_8004E410: the two timer digits at x 169, y 22. */
sat_result_t t3_force_hud_timer(unsigned seconds);
/* ModeCaption (FUN_8004EE3C) mode 10: SCORE at x 19, HI-SCORE at x 208, y 24, font 0, 8 digits. */
sat_result_t t3_force_hud_scores(uint32_t score,uint32_t hiscore);
/* FUN_800B4924: one key per key earned (copper, silver, gold) at x 0, 20, 40, y 420. */
sat_result_t t3_force_hud_keys(unsigned keys);
/* FUN_800B3D34: the four level segments, the marker at `mark` (original x) and its pointer;
 * `sine` is the executable's sine (4096 = 1) at the bar's phase. */
sat_result_t t3_force_hud_progress(int level,int mark,int sine);
#endif
