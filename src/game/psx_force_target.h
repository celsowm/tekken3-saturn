/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Force's target choice (force.ovl FUN_800B598C) as ported in the pinned
 * Tekken3Remake (tools/research/force_sim.py choose_target, docs/research/code/modes.md). Changes: the
 * fighters' relative angles and the pair distances are inputs (the match core computes them). The
 * original saves and restores only six of the player's angle fields around its angle calls, so the
 * opponent-side copies get the player's own (game-bugs.md #47): the caller reproduces that. */
#ifndef T3_PSX_FORCE_TARGET_H
#define T3_PSX_FORCE_TARGET_H
#include <stdint.h>

typedef struct {
    int32_t wait;         /* frames before the automatic choice may change again (0x800B6A20) */
    uint32_t manual;      /* the hidden manual switch (0x800B6A14) */
    uint32_t manual_side; /* 0: first candidate, 1: second */
    int32_t manual_button;/* the button mask that swaps (0x800B6A18) */
} t3_force_target_t;

typedef struct {
    uint8_t hidden;       /* +0xC2: released or blinking out */
    uint8_t index;        /* +0x1E: the fighter slot */
    uint32_t close;       /* its +0x1278 is 1..399 */
    int16_t quadrant,rel; /* its facing quadrant and relative angle to the player (+0x32, +0x30 after
                           * FighterRelativeAngles(player, candidate)) */
} t3_force_candidate_t;

typedef struct {
    uint8_t player_index;
    int32_t pair[5];      /* the distance table at 0x8009EA08, one word per 0x10 bytes (FUN_80043C88) */
    int32_t quadrant_order[4]; /* 0x800B0F04 */
    int16_t pad_pressed;  /* the human side's newly pressed buttons */
} t3_force_target_inputs_t;

/* Chooses between candidates a and b: returns 0 for a, 1 for b; steps `s` (the wait, the manual side). */
int t3_force_choose_target(t3_force_target_t* s,const t3_force_target_inputs_t* in,
                           const t3_force_candidate_t* a,const t3_force_candidate_t* b);
#endif
