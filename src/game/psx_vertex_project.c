/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Original per-part GTE projection and projected KMD exchange follow the
 * pinned Tekken3Remake 3dmk-models research. See NOTICE.md. */
#include "psx_vertex_project.h"
int t3_psx_vertex_project_exchange(t3_psx_vertex_exchange_t* state,
    const t3_psx_vertex_block_t* block,const t3_psx_camera_matrix_t* matrix,
    const int16_t (*positions)[3]) {
    if(!state || !block || !matrix || block->own_count>T3_PSX_VERTEX_SLOTS ||
       (block->own_count && !positions))return 0;
    uint32_t xy[T3_PSX_VERTEX_SLOTS];int16_t z[T3_PSX_VERTEX_SLOTS];
    for(size_t i=0;i<block->own_count;++i) {
        int32_t projected[3];
        t3_psx_camera_project_s16(matrix,positions[i],projected);
        xy[i]=(uint16_t)projected[0]|((uint32_t)(uint16_t)projected[1]<<16);
        z[i]=(int16_t)((uint32_t)projected[2]>>5);
    }
    t3_psx_vertex_block_t projected=*block;
    projected.own_xy=xy;projected.own_z=z;
    return t3_psx_vertex_exchange(state,&projected);
}
