/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's pause menu state (practice.ovl FUN_800B49C4, FUN_800B4BA8 and the item handlers), adapted from
 * Tekken3Remake remake/core/game/practice_mode.gd (_cursor_step, _cycle, _toggle, _mode_select_confirm) and
 * modes.md#practice; see NOTICE.md. FREE and VS CPU are here; COMBO TRAINING, REPLAY SETTINGS, FREEZE SIGNAL
 * and COMMAND LIST are not. */
#ifndef T3_PRACTICE_MENU_H
#define T3_PRACTICE_MENU_H
#include <stdint.h>
#include "practice_dummy.h"

enum { T3_PM_UP=1,T3_PM_DOWN=2,T3_PM_LEFT=4,T3_PM_RIGHT=8,T3_PM_OK=16,T3_PM_START=32 };
/* The sub-modes (S+0x77) and the items. */
enum { T3_PM_SUB_FREE,T3_PM_SUB_VS_CPU,T3_PM_SUB_COMBO,T3_PM_SUBS };
enum { T3_PM_ROW_MODE,T3_PM_ROW_DUMMY,T3_PM_ROW_COUNTER,T3_PM_ROW_CPU_DIFFICULTY,T3_PM_ROW_CPU_LEVEL,
       T3_PM_ROW_ATTACK_DATA,T3_PM_ROW_FREEZE_SIGNAL,T3_PM_ROW_REPLAY,T3_PM_ROW_COMMAND_LIST,T3_PM_ROW_KEY_DISPLAY,T3_PM_ROW_COMBO_PLAYER,T3_PM_ROW_COMBO_TYPE,T3_PM_ROW_SELECT,T3_PM_ROW_EXIT };
enum { T3_PM_NONE=0,T3_PM_CHARACTER_SELECT,T3_PM_LEAVE };
enum { T3_PM_MAX_ROWS=10 };
typedef struct {
    uint8_t open,cursor,sub;
    uint8_t dummy,counter_attacks,attack_data,key_display,freeze_signal;   /* S+0x7D, 0x9E, 0x9D, 0xA0, 0x83 */
    uint8_t command_open;                                     /* S+0x9F: the command list shows */
    uint16_t command_cursor;                                  /* its first line */
    uint16_t command_count[2];                                /* set by the caller: the lines of each player's character */
    uint8_t replay_setting;                                   /* S+0x81: T3_REPLAY_OFF..MANUAL (MANUAL is not on COMBO TRAINING) */
    uint8_t recorder;                                         /* S+0x87 in FREE: set by the chord held as the menu closes */
    uint8_t combo_player,combo_type;                          /* S+0x7B, 0x7C (a combo of the player's character) */
    uint8_t combo_count[2];                                   /* set by the caller: the combos of each player's character */
    uint8_t cpu_difficulty,cpu_level;                         /* S+0x7E (0 EASY..2 HARD), 0x7F (0 STAGE 1..9 FINAL) */
} t3_practice_menu_t;
void t3_practice_menu_reset(t3_practice_menu_t* m);      /* FUN_800B2D74's defaults */
/* One frame of the player's pad edges (T3_PM_*). Returns T3_PM_NONE, or the way out. */
unsigned t3_practice_menu_step(t3_practice_menu_t* m,unsigned pressed);
/* The items of the current sub-mode, top to bottom. */
unsigned t3_practice_menu_rows(const t3_practice_menu_t* m,uint8_t rows[T3_PM_MAX_ROWS]);
const char* t3_practice_dummy_name(unsigned setting);
const char* t3_practice_cpu_level_name(unsigned level);
const char* t3_practice_replay_name(unsigned setting);
#endif
