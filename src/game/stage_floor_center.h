/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic. See NOTICE.md. */
#ifndef T3_STAGE_FLOOR_CENTER_H
#define T3_STAGE_FLOOR_CENTER_H
#include "psx_camera.h"
#include "psx_body.h"
/* Returns 1 when the original floor is drawable, 0 when its pitch suppresses
 * it, and -1 for unsupported arithmetic or missing private lookup tables. */
int t3_stage_floor_center(const t3_psx_camera_view_t* view,int32_t midpoint_x,int32_t midpoint_z,
    const t3_psx_trig_t* trig,const t3_psx_body_tables_t* body,const uint8_t before_sqrt[640],int32_t out[2]);
#endif
