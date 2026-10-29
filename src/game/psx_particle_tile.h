#ifndef T3_PSX_PARTICLE_TILE_H
#define T3_PSX_PARTICLE_TILE_H
#include "psx_particle_projection.h"
#include <stdint.h>
enum { T3_PARTICLE_PROBE_MAX_TILES=80 };
/* Conservative inclusive viewport bounds for overlap rejection. This uses
 * the diagnostic particle mapping, not a validated PS1 rasterizer. */
typedef struct { int16_t left,top,right,bottom; } t3_particle_bounds_t;
int t3_particle_screen_bounds(const t3_psx_particle_quad_t* quad,t3_particle_bounds_t* bounds);
int t3_particle_bounds_intersect(const t3_particle_bounds_t* a,const t3_particle_bounds_t* b);
/* Small diagnostic surface, not a general multi-particle renderer. */
typedef struct {
    uint16_t pixels[T3_PARTICLE_PROBE_MAX_TILES][64];
    uint8_t tile_x[T3_PARTICLE_PROBE_MAX_TILES],tile_y[T3_PARTICLE_PROBE_MAX_TILES];
    unsigned count;
} t3_particle_tile_probe_t;
/* Map an axis-aligned original quad to the HUD's 368x448 visible rectangle,
 * scale its 16x16 texture by integer nearest-neighbour and clip to 320x224.
 * This sampling/viewport mapping is diagnostic and is not PS1-GPU qualified.
 * Returns 0 for malformed/oversized shapes without changing the output. */
int t3_particle_tile_probe(t3_particle_tile_probe_t* out,const t3_psx_particle_quad_t* quad,
                           const uint8_t pattern[128],const uint16_t palette[16]);
/* Accumulate one additive sprite into an initialized canvas (count=0 starts
 * empty). Rejects unsupported geometry, non-STP colors and exhausted tiles
 * without modifying the canvas. Transparent-only cells are not allocated;
 * cleared cells may be compacted when space is needed. Sampling is still
 * diagnostic. */
int t3_particle_tile_accumulate(t3_particle_tile_probe_t* out,const t3_psx_particle_quad_t* quad,
                                const uint8_t pattern[128],const uint16_t palette[16]);
typedef enum { T3_PARTICLE_TILE_UNSUPPORTED=0, T3_PARTICLE_TILE_OK=1,
               T3_PARTICLE_TILE_CAPACITY=2 } t3_particle_tile_status_t;
t3_particle_tile_status_t t3_particle_tile_accumulate_status(t3_particle_tile_probe_t* out,
    const t3_psx_particle_quad_t* quad,const uint8_t pattern[128],const uint16_t palette[16]);
/* Clear accumulated additive pixels covered by a later opaque fighter face.
 * Each mask row covers one 8-pixel cell, bit 7 first. An absent particle
 * tile is a successful no-op. The caller supplies actual opaque coverage;
 * this function neither rasterizes faces nor handles translucent fighters. */
int t3_particle_tile_occlude(t3_particle_tile_probe_t* out,unsigned tile_x,unsigned tile_y,
                             const uint8_t coverage[8]);
/* As above for a known allocated tile index; no coordinate lookup. */
int t3_particle_tile_occlude_index(t3_particle_tile_probe_t* out,unsigned tile,
                                   const uint8_t coverage[8]);
/* Test nonzero accumulated pixels against an inclusive screen rectangle.
 * The tile index must refer to an allocated tile. */
int t3_particle_tile_intersects(const t3_particle_tile_probe_t* out,unsigned tile,
                                int left,int top,int right,int bottom);
#endif
