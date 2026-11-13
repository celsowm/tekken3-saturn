/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * The Tekken Force fight HUD as Tekken3Remake documents it (hud.md, modes.md, mode_hud_view.gd);
 * see NOTICE.md. */
#ifndef T3_FORCE_HUD_H
#define T3_FORCE_HUD_H
#include "saturn/core.h"
#include <stdint.h>

/* FHUD.BIN (extract_force_hud.py), KEYS.BIN and the name sheets of the player and of a boss in actor
 * 1 (pack section NAMES).
 * `scratch` holds the largest of them while they upload. Run again after every sat_init. */
sat_result_t t3_force_hud_init(void* scratch,unsigned bytes);

/* All coordinates are the original 384 x 480 ones; lengths in original pixels. */
/* force.ovl FUN_800B362C: enemy row 0 or 1's name, right-aligned at x 354 on its plate; `model` is
 * t3_characters_force_model's (21..24 the Force grunts, otherwise the loaded boss). */
sat_result_t t3_force_hud_enemy_name(unsigned row,unsigned model);
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

/* force.ovl's texts in print_text's fonts (FHUD.BIN), at their original positions. */
/* FUN_800B4374: LIFE UP! at x, y, flashing between colours 2 and 5 with the level clock. */
sat_result_t t3_force_hud_life_up(int x,int y,int32_t clock);
/* "+n SEC" over a defeated enemy: the pen from x, the number in font 1. */
sat_result_t t3_force_hud_seconds(int x,int y,unsigned seconds);
/* State 4: NOW LOADING... at (0xF2, 0x1A6). */
sat_result_t t3_force_hud_loading(void);
/* State 6: the bonus time left (seconds and hundredths of `frames`) over the player. */
sat_result_t t3_force_hud_bonus_left(int x,int y,int32_t frames);
/* Round state 8: STAGE CLEAR!! (font 2, colour 8) at (41, 112). */
sat_result_t t3_force_hud_stage_clear(void);

enum { T3_FORCE_TALLY_NONE=-1, T3_FORCE_TALLY_BONUS_TIME=0, T3_FORCE_TALLY_KEY=1, T3_FORCE_TALLY_FINAL=2 };
typedef struct {
    int32_t frame;          /* the tally's count: a line every 30 frames */
    uint32_t stage_bonus,clear_bonus;
    int32_t clear_time,bonus_time;   /* frames */
    int last;               /* T3_FORCE_TALLY_*: the fourth line */
} t3_force_hud_tally_t;
/* State 7: the stage clear tally. */
sat_result_t t3_force_hud_tally(const t3_force_hud_tally_t* tally);
#endif
