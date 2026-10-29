#ifndef T3_PSX_PARTICLE_SNAPSHOT_H
#define T3_PSX_PARTICLE_SNAPSHOT_H
#include "psx_effect_pool.h"

/* Immutable sprite inputs for one displayed image. Preserve both original
 * primitive CLUTs: which buffer is presented is decided by the caller. */
typedef struct {
    int32_t position[3];
    uint16_t clut[2];
    uint8_t slot,flags,model;
} t3_psx_particle_snapshot_item_t;
typedef struct {
    t3_psx_particle_snapshot_item_t item[T3_PSX_EFFECT_CAPACITY];
    unsigned count;
} t3_psx_particle_snapshot_t;
/* Called while the effect pool is stable, before the simulation worker starts.
 * Live-list traversal order is retained; non-sprite records are excluded.
 * On malformed list or arguments returns zero without modifying output. */
int t3_psx_particle_snapshot(t3_psx_particle_snapshot_t* out,const t3_psx_effect_pool_t* pool);
#endif
