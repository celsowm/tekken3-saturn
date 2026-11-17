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
/* Reads stage `stage`'s head and panorama, writing only VDP2 bank A. A True Ogre
 * fight shows no panorama and a spotlight floor. */
sat_result_t t3_stage_vdp2_load(unsigned stage,int true_ogre);
/* Tekken Ball's court (stage 19): the backdrop is a flat strip. The scroll, the number of dots from it to the screen's
 * left edge and the strip's top edge on the original's screen come from t3_ball_backdrop_step; set them before each prepare. */
void t3_stage_vdp2_set_strip(int32_t scroll,int32_t first_shift,int32_t top);
/* Reads the floor into bank B and sets up the layers. */
sat_result_t t3_stage_vdp2_init(void);
/* The stage's drop shadow and backdrop colours (RGB555), once loaded. */
uint16_t t3_stage_vdp2_shadow(void);
uint16_t t3_stage_vdp2_back(void);
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
#if defined(T3_BALL_TEST_POINT)
unsigned t3_stage_particle_tile_count(void);
#endif
sat_result_t t3_stage_particle_multi_add(const t3_psx_particle_quad_t* quad,const uint8_t* pattern,const uint16_t palette[16]);
/* A false result guarantees an HUD mask cannot change this cell. */
int t3_stage_particle_multi_has_cell(unsigned tile_x,unsigned tile_y);
#if defined(T3_PARTICLE_COMPOSE_CHECK) || defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
/* Every cell's position and pixels, in list order. */
uint32_t t3_stage_particle_multi_checksum(void);
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
/* The number of cells the image currently being prepared holds. */
unsigned t3_stage_particle_multi_cell_count(void);
#endif
#endif
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
