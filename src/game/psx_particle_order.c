/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Original sprite insertion and OT traversal follow the pinned
 * Tekken3Remake effects research; see NOTICE.md. */
#include "psx_particle_order.h"
int t3_psx_particle_order(const t3_psx_particle_draw_t* draw,unsigned count,uint8_t* order) {
    if(!draw || !order || count>T3_PSX_EFFECT_CAPACITY)return -1;
    for(unsigned i=0;i<count;++i)
        if(draw[i].quad.visible && (draw[i].quad.bucket<0 || draw[i].quad.bucket>1023))return -1;
    unsigned n=0;
    for(unsigned i=0;i<count;++i) {
        if(!draw[i].quad.visible)continue;
        unsigned at=n;
        /* Insertion at the head of a PS1 OT bucket reverses submission order. */
        while(at && draw[order[at-1]].quad.bucket<=draw[i].quad.bucket) {
            order[at]=order[at-1];--at;
        }
        order[at]=(uint8_t)i;++n;
    }
    return (int)n;
}
