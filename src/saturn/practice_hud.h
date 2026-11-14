/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's HUD text in the original font (practice.ovl FUN_800B6898, FUN_800B6F58; practice_sim.py practice_hud of the pinned
 * Tekken3Remake): the attack data, the combo counters and the guide's prompts; see NOTICE.md. */
#ifndef T3_PRACTICE_HUD_H
#define T3_PRACTICE_HUD_H
#include "move_probe.h"
#include "practice_menu.h"

typedef struct {
    const t3_practice_state_t* state;
    uint8_t sub,replay_setting,attack_data,recorder,combo_known;
} t3_practice_hud_t;
/* Draws with the original font (t3_practice_font_*); returns 0 and draws nothing when the font is not loaded. */
int t3_practice_hud_draw(const t3_practice_hud_t* hud);

/* The pause menu or the command list in the original font, on the original's panel. The command list is given line by line
 * (`command_line` fills the name and command of visible row 0..command_rows-1). Returns 0 when the font is not loaded. */
typedef struct {
    const t3_practice_menu_t* menu;
    const char* player_name[2];   /* the combo players' characters (COMBO PLAYER n-NAME) */
    unsigned command_rows,command_cursor_row;
    int (*command_line)(void* context,unsigned row,const char** name,const char** command);
    void* context;
} t3_practice_menu_view_t;
int t3_practice_menu_draw(const t3_practice_menu_view_t* view);
#endif
