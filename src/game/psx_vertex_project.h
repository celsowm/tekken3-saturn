#ifndef T3_PSX_VERTEX_PROJECT_H
#define T3_PSX_VERTEX_PROJECT_H
#include "psx_vertex_exchange.h"
#include "psx_camera.h"
/* Project the block's own s16 vertices with the supplied original-format
 * GTE matrix, then apply its projected seam exchange. Positions and block
 * inputs must not overlap state; failed validation leaves state unchanged.
 * Screen offsets are (192,240). Does not build the joint/view matrix. */
int t3_psx_vertex_project_exchange(t3_psx_vertex_exchange_t* state,
    const t3_psx_vertex_block_t* block,const t3_psx_camera_matrix_t* matrix,
    const int16_t (*positions)[3]);
#endif
