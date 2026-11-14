/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's training dummy (practice.ovl FUN_800B87C8, FUN_800B8904, FUN_800B8AEC), adapted from Tekken3Remake
 * remake/core/game/practice_mode.gd (dummy_input, _standing_ok, _tech_roll) and modes.md#practice; see NOTICE.md. */
#ifndef T3_PRACTICE_DUMMY_H
#define T3_PRACTICE_DUMMY_H
#include <stdint.h>

/* TRAINING DUMMY settings (S+0x7D). CONTROLLER (8) gives the dummy the second pad and is not scripted. */
enum {
    T3_DUMMY_STAND,T3_DUMMY_CROUCH,T3_DUMMY_STAND_GUARD,T3_DUMMY_CROUCH_GUARD,T3_DUMMY_GUARD_ALL,
    T3_DUMMY_AUTO_GUARD,T3_DUMMY_UKEMI_MAE,T3_DUMMY_UKEMI_OKU,T3_DUMMY_CONTROLLER,T3_DUMMY_SETTINGS
};
/* The scripted word is an original pad word, already relative to the facing: 0x1000 up, 0x4000 down, 0x8000 back. */
enum { T3_DUMMY_UP=0x1000,T3_DUMMY_DOWN=0x4000,T3_DUMMY_BACK=0x8000 };
typedef struct {
    /* The player (the attacker the dummy reacts to). */
    uint16_t attack,pose_frame,active_last;
    /* The dummy: its stance move lies on the ground (state & 0x204), is in a reaction with an air window and a
     * tech-roll branch (transition 0x1C), has a move waiting (move_row), and the frame counter's low bit. */
    uint8_t lying,in_reaction,has_air_window,has_tech_roll,move_pending,frame_odd,doctor_b;
} t3_practice_dummy_facts_t;
typedef struct { uint16_t word; uint8_t human_guard; } t3_practice_dummy_out_t;
t3_practice_dummy_out_t t3_practice_dummy_input(const t3_practice_dummy_facts_t* f,unsigned setting);
#endif
