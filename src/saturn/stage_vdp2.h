#ifndef T3_STAGE_VDP2_H
#define T3_STAGE_VDP2_H
#include "saturn/scene3d.h"
sat_result_t t3_stage_vdp2_init(void);
sat_result_t t3_stage_vdp2_prepare(const sat_camera3d_t* camera,int32_t center_x,int32_t center_z);
sat_result_t t3_stage_vdp2_present(void);
#endif
