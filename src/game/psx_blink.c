/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from pinned Tekken3Remake fighter_animation.gd's documented
 * FUN_80034354 eye timer. Changes: compact C state, shared RNG pointer;
 * texture uploads and Gon jaw commands remain presentation work. NOTICE.md. */
#include "psx_blink.h"

void t3_psx_blink_step(t3_psx_blink_t* s,uint32_t flags,uint32_t state,uint32_t* random) {
    if((flags&0x40000u) || ((state&4u) && !(flags&0x40000000u))) {
        s->timer=2;s->closed=1;
    }
    if(!s->timer) {
        if(s->closed) {
            *random=*random*0x41C64E6Du+0x3039u;
            s->timer=(int16_t)(((*random>>16)&0xFEu)+1u);
            s->closed=0;
        } else {
            s->timer=4;s->closed=1;
        }
    }
    s->timer=(int16_t)((uint16_t)s->timer-1u);
}
