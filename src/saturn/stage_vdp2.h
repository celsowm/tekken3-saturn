#ifndef T3_STAGE_VDP2_H
#define T3_STAGE_VDP2_H
#include "saturn/scene3d.h"
#include <stdint.h>
#if defined(T3_PARTICLE_POSITION_PROBE)
#include "psx_particle_projection.h"
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
#include "saturn/model3d.h"
#endif
#endif
sat_result_t t3_stage_vdp2_init(void);
#if defined(T3_VDP1_FLOOR_PROBE)
const sat_vdp1_texture_t* t3_stage_floor_texture(void);
#if defined(T3_VDP1_FLOOR_SUBDIVIDE_PROBE)
const sat_vdp1_texture_t* t3_stage_floor_quarter(unsigned index);
#endif
const uint8_t* t3_stage_floor_sqrt_before(void);
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
uint16_t t3_stage_floor_shade(uint32_t depth,uint32_t midpoint_distance);
#endif
#endif
/* `turn` is the panorama's turn (4096 a turn, t3_psx_backdrop_turn_t). */
sat_result_t t3_stage_vdp2_prepare(const sat_camera3d_t* camera,int32_t center_x,int32_t center_z,int32_t turn);
/* Call right before sat_end_frame: the mapping last prepared appears with
 * that list's sprites, written in the VBlank of its swap. */
sat_result_t t3_stage_vdp2_present(void);
#if defined(T3_PARTICLE_SWAP_PROBE)
/* Test-only 16x16 stationary sprite; NULL pattern produces a blank image. */
sat_result_t t3_stage_particle_probe_prepare(const uint8_t* pattern,const uint16_t palette[16],uint32_t frame,uint16_t clut,uint8_t pattern_id
#if defined(T3_PARTICLE_POSITION_PROBE)
    ,const t3_psx_particle_quad_t* quad
#endif
);
#endif
#if defined(T3_PARTICLE_MULTI_PROBE)
sat_result_t t3_stage_particle_multi_begin(uint32_t frame);
sat_result_t t3_stage_particle_multi_add(const t3_psx_particle_quad_t* quad,const uint8_t* pattern,const uint16_t palette[16]);
/* A false result guarantees an HUD mask cannot change this cell. */
int t3_stage_particle_multi_has_cell(unsigned tile_x,unsigned tile_y);
/* Mask pixels covered by a later opaque face in painter order. */
sat_result_t t3_stage_particle_multi_occlude(unsigned tile_x,unsigned tile_y,const uint8_t coverage[8]);
void t3_stage_particle_multi_end(void);
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
/* Rejects unsupported overlapping face geometry; caller discards the image. */
sat_result_t t3_stage_particle_multi_face(const sat_model_texture_asset_t* texture,const int16_t x[4],const int16_t y[4]);
#endif
#endif
/* Make what one CPU's prepare wrote visible to the other one's cache. */
sat_result_t t3_stage_vdp2_sync(void);
#endif
