/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Ball's single-player opponent choice (volley.ovl FUN_800B528C) as ported in the pinned
 * Tekken3Remake (tools/research/screens_sim.py ball_opponent, docs/research/code/modes.md). Changes: the
 * guest-RAM words are the fields of one state struct. */
#ifndef T3_BALL_FLOW_H
#define T3_BALL_FLOW_H
#include <stdint.h>

enum { BALL_CHARACTERS=22, BALL_GON_KEY=0x44, BALL_OPPONENT_POOL=0x53FFF };

typedef struct {
    uint32_t unlocked;                /* 0x800982D0: a bit per character */
    uint32_t lcg;                     /* 0x8009F650 (the game's second generator, x * 5 + 1) */
    uint32_t frame;                   /* 0x800AE6E0: only its low bit picks the costume */
    uint16_t met[BALL_CHARACTERS];    /* ctx + 0x44: how often each character was the opponent */
    uint8_t first_done;               /* the first game is always Gon */
} t3_ball_choice_t;

/* The opponent of the next game: the costume key (character * 4 + costume; 0x44 is Gon's) of the original
 * context word ctx + 0x70. The first game is Gon. Later games pick uniformly among the unlocked characters of
 * the pool (the original's "within 4 of the most met" filter keeps all of them: the list is sorted from the most
 * met down) and count the meeting. The first game's counter write goes through the caller's register in the
 * original (game-bugs.md #31) and is not reproduced. */
uint32_t t3_ball_opponent(t3_ball_choice_t* s);
#endif
