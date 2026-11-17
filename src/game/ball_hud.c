/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Ball's power gauges as ported in the pinned Tekken3Remake
 * (tools/research/draw_sim.py ball_gauges). See ball_hud.h for the changes. */
#include "ball_hud.h"

void t3_ball_gauge_step(t3_ball_gauge_t* g,int32_t power,int32_t maximum) {
    int32_t target=g->target,fill=g->fill,trail=g->trail;
    if(g->last_power!=(uint32_t)power) {
        g->last_power=(uint32_t)power;
        target=maximum?(int32_t)((int64_t)(int32_t)((uint32_t)power*76u)/maximum):0;
    }
    if(target>BALL_GAUGE_WIDTH)target=BALL_GAUGE_WIDTH;
    trail=fill<trail?trail-((trail-fill+15)>>4):fill;
    fill=fill<target?(fill+3<target?fill+3:target):target;
    if(trail<fill)trail=fill;
    g->target=(int16_t)target;g->fill=(int16_t)fill;g->trail=(int16_t)trail;
}
