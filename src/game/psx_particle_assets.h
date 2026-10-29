#ifndef T3_PSX_PARTICLE_ASSETS_H
#define T3_PSX_PARTICLE_ASSETS_H
#include "psx_particle_projection.h"
#include <stddef.h>
#include <stdint.h>

/* A view into the private T3FX001 file. The caller owns its storage and must
 * keep it unchanged while the view is in use. No host-endian casts are used. */
typedef struct { const uint8_t* data; } t3_psx_particle_assets_t;
int t3_psx_particle_assets_open(t3_psx_particle_assets_t* out,const void* data,size_t size);
/* Pattern 0 is the normal spark, 1 the glow/power sprite. */
const uint8_t* t3_psx_particle_pattern(const t3_psx_particle_assets_t* assets,unsigned pattern);
/* Copies original PS1 colors, including STP and visible index zero. */
int t3_psx_particle_palette(const t3_psx_particle_assets_t* assets,uint16_t clut,uint16_t out[16]);
/* Model IDs 1..5 match the effect pool; converts little-endian SVECTORs. */
int t3_psx_particle_model(const t3_psx_particle_assets_t* assets,unsigned model,t3_psx_particle_model_t* out);
#endif
