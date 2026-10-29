/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Projected KMD seam/exchange rules adapted from the pinned Tekken3Remake
 * 3dmk-models.md and verify_kmd_skin.py; original DrawSkinnedPart 0x80036F00.
 * Changes: bounded caller-owned buffers, explicit validation, portable wrap.
 * See NOTICE.md. */
#include "psx_vertex_exchange.h"
static int16_t s16(uint16_t x) {return (int16_t)(x<0x8000u?(int32_t)x:(int32_t)x-65536);}
static int16_t half_z(int16_t x) {return (int16_t)(x>=0?x/2:-((-(int32_t)x+1)/2));}
static uint32_t half_xy(uint32_t x) {return ((x>>1)|(x&0x80000000u))&0xffff7fffu;}
static int valid_local(unsigned x) {return x>=2 && !(x&1u) && x/2u<=T3_PSX_VERTEX_SLOTS;}
static int valid_global(unsigned x) {return !(x&1u) && x/2u<T3_PSX_VERTEX_SLOTS;}
int t3_psx_vertex_exchange(t3_psx_vertex_exchange_t* s,const t3_psx_vertex_block_t* b) {
    if(!s || !b || b->scratch_count>T3_PSX_VERTEX_SLOTS || b->global_count>T3_PSX_VERTEX_SLOTS ||
       b->own_count>T3_PSX_VERTEX_SLOTS || b->scratch_count+b->global_count+b->own_count>T3_PSX_VERTEX_SLOTS ||
       (b->scratch_count && !b->scratch_import) || (b->global_count && !b->global_import) ||
       (b->own_count && (!b->own_xy || !b->own_z)))return 0;
    for(size_t i=0;i<b->scratch_count;++i)if(!valid_local(b->scratch_import[i]))return 0;
    for(size_t i=0;i<b->global_count;++i)if(!valid_global(b->global_import[i]))return 0;
    size_t operations=0;
    for(unsigned group=0;group<6;++group) {
        if(b->post_count[group]>b->own_count || (b->post_count[group] && !b->post[group]))return 0;
        operations+=b->post_count[group];if(operations>b->own_count)return 0;
        for(size_t i=0;i<b->post_count[group];++i) {
            unsigned e=b->post[group][i];
            if(group==5) {if(e>3)return 0;}
            else if(e>0x1ffu || ((group==2 || group==4)?!valid_local(e&255u):!valid_global(e&255u)))return 0;
        }
    }
    size_t cursor=0;
    for(size_t i=0;i<b->scratch_count;++i) {
        unsigned src=b->scratch_import[i]/2u-1u;
        s->scratch.xy[cursor]=s->scratch.xy[src];s->scratch.z[cursor]=s->scratch.z[src];++cursor;
    }
    for(size_t i=0;i<b->global_count;++i) {
        unsigned src=b->global_import[i]/2u;
        s->scratch.xy[cursor]=s->global.xy[src];s->scratch.z[cursor]=s->global.z[src];++cursor;
    }
    const size_t own_start=cursor;
    for(size_t i=0;i<b->own_count;++i) {s->scratch.xy[cursor]=b->own_xy[i];s->scratch.z[cursor]=b->own_z[i];++cursor;}
    cursor=own_start;
    for(unsigned group=0;group<6;++group)for(size_t i=0;i<b->post_count[group];++i,++cursor) {
        unsigned e=b->post[group][i];uint32_t xy=s->scratch.xy[cursor];int16_t z=s->scratch.z[cursor];
        const int halve=group==5?e!=1:!!(e&256u);
        if(halve){xy=half_xy(xy);z=half_z(z);}
        if(group<3) {
            t3_psx_projected_slots_t* src=group==2?&s->scratch:&s->global;
            unsigned index=group==2?(e&255u)/2u-1u:(e&255u)/2u;
            xy+=src->xy[index];z=s16((uint16_t)((uint16_t)z+(uint16_t)src->z[index]));
            if(group==0){s->global.xy[index]=xy;s->global.z[index]=z;}
        } else if(group<5) {
            t3_psx_projected_slots_t* dst=group==4?&s->scratch:&s->global;
            unsigned index=group==4?(e&255u)/2u-1u:(e&255u)/2u;
            dst->xy[index]=xy;dst->z[index]=z;
        }
        s->scratch.xy[cursor]=xy;s->scratch.z[cursor]=z;
    }
    return 1;
}
