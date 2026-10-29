/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Fighter model and scene conventions follow the pinned Tekken3Remake
 * research; OT arithmetic checked against original DrawSkinnedPart stores.
 * See NOTICE.md. */
#include "psx_fighter_ot.h"
uint16_t t3_psx_fighter_ot_bucket(uint16_t first,uint16_t last,uint32_t flags,int32_t nclip) {
    int32_t bias=(int32_t)(flags>>25);
    if(bias&64)bias-=128;
    if((flags&0x01000000u) && nclip<=0)bias=-bias;
    const uint32_t sum=(uint32_t)((int32_t)first+(int32_t)last-bias);
    return (uint16_t)((sum&0x7feu)>>1);
}

int t3_psx_fighter_face_ot(t3_psx_fighter_face_ot_t* out,unsigned group,
                           uint32_t word0,uint32_t word1,const uint16_t depth[128],int32_t nclip) {
    if(!out || !depth || group>3)return 0;
    t3_psx_fighter_face_ot_t face;
    face.slots[0]=(uint8_t)((word0>>2)&0x7fu);
    face.slots[1]=(uint8_t)((word0>>9)&0x7fu);
    face.slots[2]=(uint8_t)((word0>>16)&0x7fu);
    face.slots[3]=(group&1u)?(uint8_t)((word0>>25)&0x7fu):face.slots[2];
    face.group=(uint8_t)group;face.flags=(group&1u)?word1:word0;
    face.bucket=t3_psx_fighter_ot_bucket(depth[face.slots[0]],depth[face.slots[3]],face.flags,nclip);
    *out=face;return 1;
}

static uint16_t projected_bucket3(const t3_psx_projected_vertex_t* first,
    const t3_psx_projected_vertex_t* second,const t3_psx_projected_vertex_t* last,
    uint8_t flags_high,int32_t* nclip) {
    int32_t x0=(int16_t)first->xy,y0=(int16_t)(first->xy>>16);
    int32_t x1=(int16_t)second->xy,y1=(int16_t)(second->xy>>16);
    int32_t x2=(int16_t)last->xy,y2=(int16_t)(last->xy>>16);
    /* Unsigned products retain the GTE MAC0 low 32 bits without signed
     * overflow, including seam coordinates outside the normal viewport. */
    uint32_t bits=(uint32_t)(x1-x0)*(uint32_t)(y2-y0)-(uint32_t)(y1-y0)*(uint32_t)(x2-x0);
    int32_t area=bits<0x80000000u?(int32_t)bits:(int32_t)((int64_t)bits-0x100000000ll);
    if(nclip)*nclip=area;
    return t3_psx_fighter_ot_bucket((uint16_t)first->z,(uint16_t)last->z,
                                   (uint32_t)flags_high<<24,area);
}

uint16_t t3_psx_fighter_projected_bucket(const t3_psx_projected_vertex_t v[4],
    unsigned group,uint8_t flags_high,int32_t* nclip) {
    return projected_bucket3(&v[0],&v[1],&v[(group&1u)?3u:2u],flags_high,nclip);
}

uint16_t t3_psx_fighter_native_bucket(const t3_psx_projected_vertex_t v[4],
    unsigned group,uint8_t flags_high,int32_t* nclip) {
    (void)group;return projected_bucket3(&v[0],&v[1],&v[2],flags_high,nclip);
}

uint16_t t3_psx_fighter_native_indexed_bucket(const t3_psx_projected_vertex_t* v,
    const uint16_t indices[3],uint8_t flags_high,int32_t* nclip) {
    return projected_bucket3(&v[indices[0]],&v[indices[1]],&v[indices[2]],flags_high,nclip);
}
