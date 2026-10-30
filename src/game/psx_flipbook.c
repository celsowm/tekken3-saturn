/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Flipbook ring state adapted from the pinned Tekken3Remake effects research
 * (effects.md, effects_view.gd) and checked against the original. See NOTICE.md. */
#include "psx_flipbook.h"
static uint32_t isqrt(uint32_t v) {
    uint32_t r=0,bit=1u<<30;
    while(bit>v)bit>>=2;
    while(bit) {
        if(v>=r+bit){v-=r+bit;r=(r>>1)+bit;}
        else r>>=1;
        bit>>=2;
    }
    return r;
}
void t3_psx_flipbook_clear(t3_psx_flipbook_pool_t* pool) {
    for(unsigned i=0;i<T3_PSX_FLIPBOOK_RECORDS;++i)pool->record[i].active=0;
    pool->cursor=0;
}
void t3_psx_flipbook_spawn(t3_psx_flipbook_pool_t* pool,unsigned set,unsigned index,unsigned frames,
                           const int32_t point[3],const int16_t vector[3],int no_velocity) {
    t3_psx_flipbook_t* r=&pool->record[pool->cursor];
    pool->cursor=(uint8_t)(pool->cursor+1u>=T3_PSX_FLIPBOOK_RECORDS?0u:pool->cursor+1u);
    r->active=1;r->set=(uint8_t)set;r->index=(uint8_t)index;r->frames=(uint16_t)frames;r->frame=1;
    for(unsigned k=0;k<3;++k) {
        r->pos[k]=(int32_t)((uint32_t)point[k]<<7);
        pool->last_point[k]=point[k];pool->last_vector[k]=vector?vector[k]:0;
        r->vel[k]=0;
    }
    if(!no_velocity && vector) {
        const int32_t a=vector[0],b=vector[1],c=vector[2];
        const uint32_t length=isqrt((uint32_t)(a*a+b*b+c*c));
        if(length) {
            r->vel[0]=a*0x1900/(int32_t)length;
            r->vel[1]=b*0x1900/(int32_t)length;
            r->vel[2]=c*0x1900/(int32_t)length;
        }
    }
}
void t3_psx_flipbook_step(t3_psx_flipbook_pool_t* pool) {
    for(unsigned i=0;i<T3_PSX_FLIPBOOK_RECORDS;++i) {
        t3_psx_flipbook_t* r=&pool->record[i];
        if(!r->active)continue;
        ++r->frame;
        for(unsigned k=0;k<3;++k) {
            r->vel[k]-=r->vel[k]*5/64;
            r->pos[k]+=r->vel[k];
        }
        if(r->frame>r->frames)r->active=0;
    }
}
