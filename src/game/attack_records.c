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

int t3_attack_sample(const t3_divmot_t* bank, const t3_move_record_t* move,
                     unsigned pose_frame, int choose_alternative,
                     t3_attack_sample_t* result) {
    const uint8_t* p;
    size_t size, rel, base, stride, lead, record_size;
    unsigned t, kind, descriptor, alt;
    if (!result || !bank || !bank->data || !move) return -1;
    result->segment_count=0;
    result->alternative_selected=0;
    result->layout_kind=0;
    result->joint_a[0]=result->joint_a[1]=0;
    result->joint_b[0]=result->joint_b[1]=0;
    if (!move->active_first || pose_frame < move->active_first ||
        pose_frame > move->active_last) return 0;
    descriptor=move->attack_descriptor;
    if (!descriptor || (descriptor>=0x7FF0u && descriptor<=0x7FFBu))
        return 0; /* built-in table is not loaded in this milestone */
    if (descriptor < 4u || bank->sections[10] < bank->sections[9])
        return -1;
    rel=(size_t)descriptor-4u;
    size=(size_t)(bank->sections[10]-bank->sections[9]);
    if (rel>size || size-rel<6u) return -1;
    p=bank->data+bank->sections[9]+rel;
    record_size=size-rel;
    result->joint_a[0]=p[0];result->joint_b[0]=p[1];
    result->joint_a[1]=p[2];result->joint_b[1]=p[3];
    /* PS1 layout word is BIG endian, unlike the packed point stream. */
    kind=(unsigned)((p[4]<<8u)|p[5])&15u;
    alt=(unsigned)((p[4]<<8u)|p[5])&0x7FF0u;
    result->layout_kind=(uint8_t)kind;
    if (choose_alternative && alt) {
        if (alt>record_size || record_size-alt<6u) return -1;
        p+=alt;
        record_size-=alt;
        result->alternative_selected=1;
    }
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
    base=6u+lead+(size_t)t*stride;
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
    case 9:
        if (base<5u ||
            !segment(p,record_size,base-5u,base,1u,
                     &result->segments[1])) return -1;
        break;
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
