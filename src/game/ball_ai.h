/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the CPU opponent's Tekken Ball hooks (volley.ovl FUN_800B4958, FUN_800B4B08, FUN_800B4B64 and FUN_800B4BE4) as
 * ported in the pinned Tekken3Remake (tools/research/ai_update.py, docs/research/code/ai.md, Mode-specific behaviour). Changes:
 * the projected line is a caller's array and the back-dash's script start is left to the AI that calls it. */
#ifndef T3_BALL_AI_H
#define T3_BALL_AI_H
#include <stdint.h>
#include "ball_rules.h"

/* The scene projected onto the line through the two players (0x800B6AF0): [0] player 1 along it, [1] player 2, [2] the ball,
 * [3] the ball's height (y), [4] its lateral offset. */
enum { BALL_LINE_WORDS=5 };
/* FUN_800B4958 with the players' roots (x, z) and the ball's position. The line's z intercept divides by the players' x difference
 * with no zero check (game-bugs.md #24): the R3000's quotient for a zero divisor is used. */
void t3_ball_ai_project(const t3_ball_tables_t* t,const int32_t ball[3],const int32_t p0[2],const int32_t p1[2],int32_t line[BALL_LINE_WORDS]);
/* FUN_800B4B08: the player's distance to the ball in the projected plane. */
int32_t t3_ball_ai_distance(const int32_t line[BALL_LINE_WORDS],int player);
/* FUN_800B4B64's test: the ball is behind the player on the line (the CPU then back-dashes). */
int t3_ball_ai_behind(const int32_t line[BALL_LINE_WORDS],int player);
/* FUN_800B4BE4: 1 when the ball is ahead (player 1: more than 2,000 units ahead; player 2: less than 2,000 ahead, game-bugs.md #3). */
int t3_ball_ai_ahead(const int32_t line[BALL_LINE_WORDS],int player);
#endif
