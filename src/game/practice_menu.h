/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's pause menu state (practice.ovl FUN_800B49C4, FUN_800B4BA8 and the item handlers), adapted from
 * Tekken3Remake remake/core/game/practice_mode.gd (_cursor_step, _cycle, _toggle, _mode_select_confirm) and
 * modes.md#practice; see NOTICE.md. MODE SELECT and the three pages. */
#ifndef T3_PRACTICE_MENU_H
#define T3_PRACTICE_MENU_H
#include <stdint.h>
#include "practice_dummy.h"

enum { T3_PM_UP=1,T3_PM_DOWN=2,T3_PM_LEFT=4,T3_PM_RIGHT=8,T3_PM_OK=16,T3_PM_START=32 };
/* The sub-modes (S+0x77) and the items. */
enum { T3_PM_SUB_FREE,T3_PM_SUB_VS_CPU,T3_PM_SUB_COMBO,T3_PM_SUBS };
/* The page items (the original's item ids, FUN_800B4E4C): OK/CANCEL first, COMMAND LIST second, RETURN TO MODE SELECT last. */
enum { T3_PM_ROW_OK_CANCEL,T3_PM_ROW_COMMAND_LIST,T3_PM_ROW_DUMMY,T3_PM_ROW_COUNTER,T3_PM_ROW_CPU_DIFFICULTY,T3_PM_ROW_CPU_LEVEL,
       T3_PM_ROW_ATTACK_DATA,T3_PM_ROW_FREEZE_SIGNAL,T3_PM_ROW_REPLAY,T3_PM_ROW_COMBO_PLAYER,
       T3_PM_ROW_COMBO_TYPE,T3_PM_ROW_RETURN };
/* MODE SELECT's rows: FREE, VS CPU and COMBO TRAINING (the sub-modes), then PLAYER SELECT and RESET. */
enum { T3_PM_SELECT_PLAYER=3,T3_PM_SELECT_RESET=4,T3_PM_SELECT_ROWS=5 };
enum { T3_PM_NONE=0,T3_PM_CHARACTER_SELECT,T3_PM_LEAVE };
enum { T3_PM_MAX_ROWS=10 };   /* the USA pages have 8 rows */
typedef struct {
    uint8_t open,sub;
    uint8_t mode_select;                                      /* S+0x75: MODE SELECT shows (the first page of a session) */
    uint8_t select_cursor;                                    /* S+0x77 on MODE SELECT: 0..4 */
    uint8_t cursors[T3_PM_SUBS];                              /* S+0x78..0x7A: each page's cursor */
    uint8_t changed;                                          /* S+0xA7: 1 until a value is changed (the first row reads CANCEL, then OK) */
    uint8_t dummy,counter_attacks,attack_data,key_display,freeze_signal;   /* S+0x7D, 0x9E, 0x9D, 0xA0, 0x83 */
    uint8_t command_open;                                     /* S+0x9F: the command list shows */
    uint16_t command_cursor;                                  /* its first line */
    int16_t command_offset;                                   /* the scroll's pixels still to ease out (64 a line) */
    uint16_t command_cursor2;                                 /* the dummy's list (CONTROLLER), moved by pad 2 */
    int16_t command_offset2;
    uint8_t command_two;                                      /* set by the caller: the dummy's list shows too */
    unsigned pressed2;                                        /* set by the caller: pad 2's edges (T3_PM_*) for the second list */
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
/* The items of the current page (the sub-mode's), top to bottom. */
/* The cursor of what shows: MODE SELECT's or the current page's. */
unsigned t3_practice_menu_cursor(const t3_practice_menu_t* m);
unsigned t3_practice_menu_rows(const t3_practice_menu_t* m,uint8_t rows[T3_PM_MAX_ROWS]);
const char* t3_practice_dummy_name(unsigned setting);
const char* t3_practice_cpu_level_name(unsigned level);
const char* t3_practice_replay_name(unsigned setting);
#endif
