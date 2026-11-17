/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Ball's power gauges (volley.ovl FUN_800B4E6C) as ported in the pinned Tekken3Remake
 * (tools/research/draw_sim.py ball_gauges, docs/research/code/hud.md). Changes: only the gauge state is here (what
 * the original keeps at 0x800B68E8 + 0x10 * player); the drawing is the Saturn side's. */
#ifndef T3_BALL_HUD_H
#define T3_BALL_HUD_H
#include <stdint.h>

enum { BALL_GAUGE_WIDTH=0x4C };

typedef struct {
    uint32_t last_power;    /* +0x00: the power the target was last computed for */
    int16_t target;         /* +0x04 */
    int16_t fill;           /* +0x06: the bright part, in pixels */
    int16_t trail;          /* +0x08: the lighter part that follows a drop */
} t3_ball_gauge_t;

/* One frame: the fill moves 3 pixels towards power * 76 / maximum, and the trail falls to the fill by 1/16 of
 * the gap per frame. The three segments to draw are fill, trail - fill and BALL_GAUGE_WIDTH - trail. */
void t3_ball_gauge_step(t3_ball_gauge_t* g,int32_t power,int32_t maximum);
#endif
