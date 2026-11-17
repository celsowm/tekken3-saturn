/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Ball's backdrop placement (volley.ovl FUN_800B5F0C) as ported in the pinned Tekken3Remake
 * (tools/research/ball_sim.py background_draw, docs/research/code/stages.md). Changes: only the scroll and the strip's top
 * are computed here (the original draws 15 x 8 sprites from them); the Saturn side scrolls one bitmap by them. */
#ifndef T3_BALL_BACKDROP_H
#define T3_BALL_BACKDROP_H
#include <stdint.h>
#include "psx_camera.h"

enum { BALL_STRIP_WIDTH=1024, BALL_STRIP_HEIGHT=256, BALL_TILE=32, BALL_STRIP_ROWS=8 };

typedef struct {
    int32_t yaw_ref;       /* 0x800B6B3C: the camera yaw the panorama was (re)started with */
    int32_t last_kind;     /* 0x800B6940 */
} t3_ball_backdrop_t;

typedef struct {
    int32_t scroll;        /* the strip's x at the first column, 0..1023 */
    int32_t first_shift;   /* the screen's left edge shows strip x = scroll + first_shift (the original starts 2, 10 or 16 tiles in) */
    int32_t top;           /* the strip's top edge on the original's screen, in original pixels (it spans 256 rows) */
} t3_ball_backdrop_out_t;

/* One frame: the camera's yaw and position, the view matrix and the round flow's state. `kind` is 0x800AFF88 (1 skips the
 * panorama, other non-zero values start 16 tiles in during the result). Returns 0 on a missing trig table. */
int t3_ball_backdrop_step(t3_ball_backdrop_t* state,const t3_psx_camera_view_t* camera,
                          const t3_psx_camera_matrix_t* view,const t3_psx_trig_t* trig,int32_t flow,int32_t kind,
                          t3_ball_backdrop_out_t* out);
#endif
