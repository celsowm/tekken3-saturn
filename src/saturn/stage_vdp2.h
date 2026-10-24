#ifndef T3_STAGE_VDP2_H
#define T3_STAGE_VDP2_H
#include "saturn/scene3d.h"
sat_result_t t3_stage_vdp2_init(void);
/* `turn` is the panorama's turn (4096 a turn, t3_psx_backdrop_turn_t). */
sat_result_t t3_stage_vdp2_prepare(const sat_camera3d_t* camera,int32_t center_x,int32_t center_z,int32_t turn);
/* Call right before sat_end_frame: the mapping last prepared appears with
 * that list's sprites, written in the VBlank of its swap. */
sat_result_t t3_stage_vdp2_present(void);
/* Make what one CPU's prepare wrote visible to the other one's cache. */
sat_result_t t3_stage_vdp2_sync(void);
#endif
