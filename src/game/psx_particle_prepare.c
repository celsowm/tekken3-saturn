/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Sprite/model conventions follow the pinned Tekken3Remake effects research;
 * adapted to caller-owned portable state. See NOTICE.md. */
#include "psx_particle_prepare.h"
int t3_psx_particle_prepare(const t3_psx_particle_snapshot_item_t* item,
    const t3_psx_particle_assets_t* assets,const t3_psx_particle_view_t* view,
    unsigned buffer,t3_psx_particle_draw_t* out) {
    t3_psx_particle_model_t model;uint16_t palette[16];t3_psx_particle_draw_t result;
    if(!item || !assets || !view || !out || buffer>1 || !(item->flags&0x40u) ||
       !t3_psx_particle_model(assets,item->model,&model) ||
       !t3_psx_particle_palette(assets,item->clut[buffer],palette)) return 0;
    if(!t3_psx_particle_project(view,item->position,&model,item->flags,&result.quad))return 0;
    result.clut=item->clut[buffer];result.slot=item->slot;result.pattern=(uint8_t)(item->model!=1);
    *out=result;return 1;
}
