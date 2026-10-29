#ifndef T3_PSX_PARTICLE_ORDER_H
#define T3_PSX_PARTICLE_ORDER_H
#include "psx_particle_prepare.h"
/* Returns visible item indices in PS1 OT traversal order: far buckets first,
 * newest primitive first when multiple items share a bucket. Caller supplies
 * count slots of output; returns -1 on invalid input without writing output.
 * This orders particles only; actor and HUD primitives still need merging. */
int t3_psx_particle_order(const t3_psx_particle_draw_t* draw,unsigned count,uint8_t* order);
#endif
