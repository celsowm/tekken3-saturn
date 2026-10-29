/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * FloorDrawGrid visibility follows the pinned Tekken3Remake stages research
 * and original function 0x80049290; see NOTICE.md. */
#ifndef T3_STAGE_FLOOR_VISIBILITY_H
#define T3_STAGE_FLOOR_VISIBILITY_H
#include <stdint.h>

/* PS1 GTE clamps screen X/Y to signed 11 bits and depth to unsigned 16.
 * FloorDrawGrid rejects zero depth, but does not clip at H/2: the GTE has
 * already saturated its divide there. Its horizontal and vertical tests can
 * be satisfied by different vertices. Inputs are in the original 368x480
 * GPU framebuffer, before conversion to the Saturn viewport. */
static inline int t3_stage_floor_visible(const int32_t p[4][3]) {
    int horizontal=0,vertical=0;
    for(unsigned i=0;i<4;++i) {
        if(!p[i][2])return 0;
        if((uint32_t)p[i][0]<368u)horizontal=1;
        if((uint32_t)p[i][1]<480u)vertical=1;
    }
    return horizontal && vertical;
}
#endif
