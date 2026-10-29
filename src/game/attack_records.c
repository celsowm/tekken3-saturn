/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * The built-in descriptors (read past into the bytes after them) and the
 * attack segment count set at a move's start are adapted to C from pinned
 * Tekken3Remake remake/core/combat/attack_records.gd and _finish in
 * remake/core/fighter/move_system.gd. Changes: one bounded byte window
 * with offsets instead of the executable image; section 9 decoding is this
 * project's own. */
#include "attack_records.h"
#include <stddef.h>

/* Section 9: descriptor offset is stored as byte offset +4 in each move.
 * 0 and 0x7FF0..0x7FFB are special built-in records not stored in section 9.
 */
static uint32_t read_le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8u) |
           ((uint32_t)p[2]<<16u) | ((uint32_t)p[3]<<24u);
}
static int unpack_point(const uint8_t* p, size_t available,
                        size_t offset, t3_attack_point_t* out) {
    uint64_t v;
    if (offset > available || available-offset < 5u) return 0;
    v = (uint64_t)read_le32(p+offset) | ((uint64_t)p[offset+4u]<<32u);
    out->x = (int16_t)((v & 0x1FFFu)-0x1000);
    out->y = (int16_t)(((v>>13u)&0x1FFFu)-0x1000);
    out->z = (int16_t)(((v>>26u)&0x3FFFu)-0x2000);
    return 1;
}
static int segment(const uint8_t* p, size_t limit, size_t a, size_t b,
                   unsigned prev, t3_attack_segment_t* out) {
    if (!unpack_point(p,limit,a,&out->start) ||
        !unpack_point(p,limit,b,&out->end)) return 0;
    out->previous_start=(uint8_t)prev;
    return 1;
}

/* The bytes a descriptor reaches: 1 found, 0 built-in without the window,
 * -1 unreadable. */
static int descriptor_bytes(const t3_divmot_t* bank, uint32_t descriptor,
                            const uint8_t** out, size_t* available) {
    size_t size, rel;
    if (!descriptor || (descriptor>=0x7FF0u && descriptor<=0x7FFBu)) {
        const uint8_t* w=bank->builtin_attacks;
        if (!w) return 0;
        for (unsigned i=0;i<T3_BUILTIN_ATTACK_COUNT;++i) {
            const uint8_t* e=w+T3_BUILTIN_ATTACK_TABLE+8u*i;
            if (read_le32(e)!=descriptor) continue;
            rel=read_le32(e+4);
            if (rel>T3_BUILTIN_ATTACK_BYTES-6u) return -1;
            *out=w+rel;*available=T3_BUILTIN_ATTACK_BYTES-rel;
            return 1;
        }
        return -1;
    }
    if (descriptor < 4u || bank->sections[10] < bank->sections[9])
        return -1;
    rel=(size_t)descriptor-4u;
    size=(size_t)(bank->sections[10]-bank->sections[9]);
    if (rel>size || size-rel<6u) return -1;
    *out=bank->data+bank->sections[9]+rel;*available=size-rel;
    return 1;
}

int t3_attack_descriptor_joints(const t3_divmot_t* bank,const t3_move_record_t* move,
                                 uint8_t out[4]) {
    const uint8_t* p;size_t available;
    if(!bank || !bank->data || !move || !out)return -1;
    const int found=descriptor_bytes(bank,move->attack_descriptor,&p,&available);
    if(found!=1)return found;
    for(unsigned i=0;i<4;++i)out[i]=p[i];
    return 1;
}

int t3_attack_joint_pairs(const t3_divmot_t* bank, const t3_move_record_t* move) {
    const uint8_t* p;size_t available;int found;
    if (!bank || !bank->data || !move) return -1;
    if (!move->active_first) return 0;
    found=descriptor_bytes(bank,move->attack_descriptor,&p,&available);
    if (found<=0) return found;
    return p[2]?2:p[0]?1:0;
}

int t3_attack_sample(const t3_divmot_t* bank, const t3_move_record_t* move,
                     unsigned pose_frame, int choose_alternative,
                     t3_attack_sample_t* result) {
    const uint8_t* p;
    size_t base, stride, lead, record_size;
    unsigned t, kind, alt;
    int found;
    if (!result || !bank || !bank->data || !move) return -1;
    result->segment_count=0;
    result->alternative_selected=0;
    result->layout_kind=0;
    result->joint_a[0]=result->joint_a[1]=0;
    result->joint_b[0]=result->joint_b[1]=0;
    if (!move->active_first || pose_frame < move->active_first ||
        pose_frame > move->active_last) return 0;
    found=descriptor_bytes(bank,move->attack_descriptor,&p,&record_size);
    if (found<=0) return found;
    result->joint_a[0]=p[0];result->joint_b[0]=p[1];
    result->joint_a[1]=p[2];result->joint_b[1]=p[3];
    /* PS1 layout word is BIG endian, unlike the packed point stream. */
    {
        unsigned flags=(unsigned)(p[4]<<8u)|p[5];
        kind=flags&15u;
        /* These shifts are the original descriptor alternate-point packing.
         * The delta applies to POINT bytes after the six-byte descriptor,
         * not to a second descriptor header. */
        if (choose_alternative==0) alt=0;
        else if (choose_alternative==1) alt=(flags&0x7FFFu)>>4u;
        else if (choose_alternative==2)
            alt=(flags&0x8000u)?((flags&0x7FFEu)>>3u):0u;
        else return -1;
    }
    result->layout_kind=(uint8_t)kind;
    result->alternative_selected=(uint8_t)(alt!=0u);
    switch (kind) {
    case 0: stride=10;lead=0;break;
    case 1: stride=5;lead=5;break;
    case 2: stride=10;lead=10;break;
    case 3: case 4: stride=15;lead=5;break;
    case 5: stride=20;lead=0;break;
    case 6: case 7: case 9: stride=10;lead=5;break;
    case 10: case 12: case 13: case 14: stride=15;lead=0;break;
    default: return -1; /* no observed 8/11/15 layouts in research corpus */
    }
    t=pose_frame-move->active_first;
    if (t > (SIZE_MAX-6u-lead)/stride) return -1;
    base=6u+lead+(size_t)t*stride+alt;
    if (base>record_size) return -1;
    switch(kind) {
    case 0: case 10: case 12: case 13: case 14:
    case 3: case 5: case 9:
        if (!segment(p,record_size,base,base+5u,0u,&result->segments[0]))
            return -1;
        break;
    default:
        if (base<5u ||
            !segment(p,record_size,base-5u,base,1u,&result->segments[0]))
            return -1;
    }
    result->segment_count=1;
    switch(kind) {
    case 0: case 1: return 1;
    case 2:
        if (base<5u ||
            !segment(p,record_size,base-5u,base+5u,1u,&result->segments[1]))
            return -1;
        /* For layout 2, preceding segment1 has its own point at base-5
         * and segment0 must use the preceding slot at base-10. */
        if (base<10u ||
            !segment(p,record_size,base-10u,base,1u,&result->segments[0]))
            return -1;
        break;
    case 3:
        if (base<5u ||
            !segment(p,record_size,base-5u,base+10u,1u,&result->segments[1]))
            return -1;
        break;
    case 4:
        if (!segment(p,record_size,base+5u,base+10u,0u,
                     &result->segments[1])) return -1;
        break;
    case 5:
        if (!segment(p,record_size,base+10u,base+15u,0u,
                     &result->segments[1])) return -1;
        break;
    case 6:
        if (!segment(p,record_size,base+5u,base,0u,
                     &result->segments[1])) return -1;
        break;
    case 7:
        if (!segment(p,record_size,base,base+5u,0u,
                     &result->segments[1])) return -1;
        break;
    case 9: {
        /* At the first frame the seeded swept point is p+5. Afterwards
         * it is the previous pose's segment0 START (p - stride).
         * The caller must use the previous world-space endpoint when moving. */
        size_t prev;
        if (t==0u) prev=base+5u;
        else {
            if (base<stride) return -1;
            prev=base-stride;
        }
        if (!segment(p,record_size,prev,base,1u,
                     &result->segments[1])) return -1;
        break;
    }
    case 10: case 12:
        if (!segment(p,record_size,base+10u,
                     base+(kind==10?5u:0u),0u,
                     &result->segments[1])) return -1;
        break;
    case 13:
        if (!segment(p,record_size,base,base+10u,0u,
                     &result->segments[1])) return -1;
        break;
    case 14:
        if (!segment(p,record_size,base+10u,base+10u,0u,
                     &result->segments[1])) return -1;
        break;
    default: return -1;
    }
    result->segment_count=2;
    return 1;
}
