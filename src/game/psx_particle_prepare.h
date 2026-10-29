#ifndef T3_PSX_PARTICLE_PREPARE_H
#define T3_PSX_PARTICLE_PREPARE_H
#include "psx_particle_snapshot.h"
#include "psx_particle_assets.h"

/* Still in PS1 framebuffer coordinates. Slot and both original palette
 * semantics remain available until the backend performs composition. */
typedef struct {
    t3_psx_particle_quad_t quad;
    uint16_t clut;
    uint8_t slot,pattern;
} t3_psx_particle_draw_t;
/* No allocation or framebuffer mapping. Returns 0 on unsupported model or
 * palette, invalid input, or wrong buffer; leaves output unchanged on error. */
int t3_psx_particle_prepare(const t3_psx_particle_snapshot_item_t* item,
    const t3_psx_particle_assets_t* assets,const t3_psx_particle_view_t* view,
    unsigned buffer,t3_psx_particle_draw_t* out);
#endif
