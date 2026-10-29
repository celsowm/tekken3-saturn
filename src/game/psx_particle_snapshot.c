/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Effect sprite selection and live-list ordering follow the pinned
 * Tekken3Remake effects research; see NOTICE.md. */
#include "psx_particle_snapshot.h"

int t3_psx_particle_snapshot(t3_psx_particle_snapshot_t* out,const t3_psx_effect_pool_t* pool) {
    if(!out || !pool || pool->count>T3_PSX_EFFECT_CAPACITY) return 0;
    unsigned i=pool->head, previous=T3_PSX_EFFECT_NONE, visited=0;
    while(i!=T3_PSX_EFFECT_NONE) {
        if(i>=T3_PSX_EFFECT_CAPACITY || visited>=pool->count || pool->previous[i]!=previous) return 0;
        previous=i;i=pool->next[i];++visited;
    }
    if(visited!=pool->count || previous!=pool->tail) return 0;
    unsigned count=0;
    for(i=pool->head;i!=T3_PSX_EFFECT_NONE;i=pool->next[i]) {
        const t3_psx_spark_t* s=&pool->object[i];
        if(!(s->flags&0x40u)) continue;
        t3_psx_particle_snapshot_item_t* d=&out->item[count++];
        d->position[0]=s->x;d->position[1]=s->y;d->position[2]=s->z;
        d->clut[0]=s->clut[0];d->clut[1]=s->clut[1];
        d->slot=(uint8_t)i;d->flags=s->flags;d->model=s->model;
    }
    out->count=count;
    return 1;
}
