/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Tekken Ball's HUD mode line and power gauges as Tekken3Remake documents them (hud.md, modes.md,
 * draw_sim.py hud_mode_line, hud_coin and ball_gauges, volley.ovl FUN_800B5210 and FUN_800B4E6C); see
 * NOTICE.md. */
#ifndef T3_BALL_HUD_ART_H
#define T3_BALL_HUD_ART_H
#include "ball_hud.h"
#include "saturn/core.h"
#include <stdint.h>

/* BHUD.BIN (extract_ball_hud.py): the composed labels and the gauge's pieces. `scratch` holds the file
 * while it uploads. */
sat_result_t t3_ball_hud_art_init(void* scratch,unsigned bytes);
/* FUN_8004EE3C for mode 7 with one human (side `human`): TEKKEN BALL over the human's bar, the
 * blinking coin prompt (FUN_8004F1D0) over the other's. `fields` is the 60 Hz field count (the
 * game's frame counter); `pad2` whether a controller is in port 2. */
sat_result_t t3_ball_hud_mode_line(unsigned human,uint32_t fields,int pad2);
/* FUN_800B4E6C: both gauges (fill, trail and empty segments between the caps); `highlight` is
 * 1 << the player whose charged ball is in play (0 for none), whose gauge pulses with `fields`. */
sat_result_t t3_ball_hud_gauges(const t3_ball_gauge_t gauge[2],unsigned highlight,uint32_t fields);
#endif
