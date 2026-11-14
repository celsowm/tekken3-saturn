/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's input guide and recorder (practice.ovl FUN_800B3808's state machine, FUN_800B83A4, FUN_800B8244,
 * FUN_800B82F4, FUN_800B7FB4, FUN_800B85A0), adapted from Tekken3Remake remake/core/game/practice_mode.gd
 * (_guide_step, _guide_ready, _guide_play, load_guide, _guide_moves) and modes.md#practice; see NOTICE.md. */
#ifndef T3_PRACTICE_GUIDE_H
#define T3_PRACTICE_GUIDE_H
#include <stdint.h>
#include "practice_keys.h"

/* The combo tables (scripts/extract_practice_combos.py): "T3CB", u16 kinds, per kind u16 count and per combo
 * u16 steps, u16 moves, u32[steps], u16[moves]. */
unsigned t3_practice_combo_count(const uint8_t* blob,uint32_t size,unsigned bank_type);
typedef struct { const uint8_t* steps,*moves;uint16_t n_steps,n_moves; } t3_practice_combo_t;
int t3_practice_combo_get(const uint8_t* blob,uint32_t size,unsigned bank_type,unsigned combo,t3_practice_combo_t* out);

enum { T3_GUIDE_IDLE=0,T3_GUIDE_READY=1,T3_GUIDE_RECORD_WAIT=2,T3_GUIDE_RECORDING=3,T3_GUIDE_STAND=4,T3_GUIDE_PLAY=5,
       T3_GUIDE_REPLAY=6,T3_GUIDE_REPLAY_END=7,T3_GUIDE_MODE=8,T3_GUIDE_MOVE_END=9 };
enum { T3_GUIDE_PROMPT_NONE=1,T3_GUIDE_PROMPT_HIT=2,T3_GUIDE_PROMPT_END=3,T3_GUIDE_PROMPT_GUIDE=4 };  /* S+0xA3's values */
typedef struct {
    t3_practice_keys_t ring;           /* the key ring (S+0xB0) and its head */
    int32_t replay_pos,move_count;     /* S+0x178, S+0x180 */
    uint32_t replay_word;              /* S+0x17C */
    uint16_t marks[T3_KEY_RING_WORDS]; /* S+0x184: the move slots of the guide / of the recording */
    uint16_t script;                   /* the scripted pad word for the guided fighter (fighter +0x408) */
    uint8_t state,prompt,prompt_timer,guide_prompt,guide_length,resumed_input,guide_on;
    uint8_t scripted;                  /* input mode 2: the guided fighter reads `script` */
} t3_practice_guide_t;

/* What one frame's guide step reads (S's pad words and the fighters' records). */
typedef struct {
    uint8_t sub;                       /* 0 FREE, 1 VS CPU, 2 COMBO TRAINING */
    uint8_t guide_on;                  /* S+0x87 in FREE: Select replays / records */
    int8_t combo_type;                 /* S+0x7C */
    uint8_t combo_known;               /* the combo player's character has combos (combo_count != 0) */
    const t3_practice_combo_t* combo;  /* the chosen combo, or NULL */
    uint8_t select_pressed,select_held,held_down,held_horizontal,mapped_record_key; /* the player's pad */
    uint16_t pad_word;                 /* the combo player's physical pad word (the recorder logs it) */
    uint8_t side_flag;
    /* the guided (combo player) fighter */
    uint8_t lying,turned,move_pending,hold_turn,hit_clean;
    uint16_t cur_slot,stance_slot;
    uint8_t active_first_frame;        /* the fighter is on its move's first active frame */
    /* the player's own fighter (REPLAY_END) */
    uint16_t me_cur_slot,me_stance_slot;
} t3_practice_guide_in_t;

void t3_practice_guide_reset(t3_practice_guide_t* g);               /* the ring emptied, the replay rewound */
/* FUN_800B7FB4: the combo's steps into the ring as the guide; returns the step count (0: ring emptied). */
unsigned t3_practice_guide_load(t3_practice_guide_t* g,const t3_practice_combo_t* combo);
/* FUN_800B85A0 / FUN_800B8764: the combo's move slots into the marks. */
void t3_practice_guide_load_moves(t3_practice_guide_t* g,const t3_practice_combo_t* combo);
/* One frame of _guide_step. */
void t3_practice_guide_step(t3_practice_guide_t* g,const t3_practice_guide_in_t* in);
/* The pause opens: the guide stops and the fighters read their pads again (S+0x82 idle, input mode 1). */
void t3_practice_guide_stop(t3_practice_guide_t* g);
#endif
