#include "psx_trace.h"

static void w16(uint8_t* p,uint16_t n) {
    p[0]=(uint8_t)n;
    p[1]=(uint8_t)(n>>8u);
}
static void w32(uint8_t* p,uint32_t n) {
    w16(p,(uint16_t)n);
    w16(p+2,(uint16_t)(n>>16u));
}
static int in_range(int32_t n) {
    return n>=-T3_PSX_UNIT_LIMIT && n<=T3_PSX_UNIT_LIMIT;
}
int t3_psx_trace_encode(uint32_t tick, unsigned fighter,
                        uint16_t move_slot, uint16_t pose_frame,
                        const t3_psx_joint_t parts[T3_PSX_DRAW_PARTS],
                        uint32_t valid_mask,
                        uint8_t* output,size_t capacity) {
    uint8_t record[T3_PSX_TRACE_BYTES]={0};
    unsigned i,j,k;
    if(!output || capacity<T3_PSX_TRACE_BYTES || fighter>1u ||
       (valid_mask & ~T3_PSX_TRACE_BODY_MASK) || (valid_mask && !parts))
        return 0;
    /* Validate EVERY populated part before changing the output buffer. */
    for(i=0;i<T3_PSX_TRACE_JOINTS;++i) {
        if(!(valid_mask & ((uint32_t)1u<<i)))continue;
        if(!in_range(parts[i].origin.x) ||
           !in_range(parts[i].origin.y) ||
           !in_range(parts[i].origin.z))return 0;
    }
    record[0]='T';record[1]='3';record[2]='P';record[3]='X';
    w16(record+4u,T3_PSX_TRACE_VERSION);
    w16(record+6u,T3_PSX_TRACE_JOINTS);
    w32(record+8u,tick);
    w16(record+12u,move_slot);
    w16(record+14u,pose_frame);
    w16(record+16u,(uint16_t)fighter);
    w32(record+20u,valid_mask);
    for(i=0;i<T3_PSX_TRACE_JOINTS;++i) {
        uint8_t* p=record+T3_PSX_TRACE_HEADER+i*T3_PSX_TRACE_JOINT_BYTES;
        if(!(valid_mask & ((uint32_t)1u<<i)))continue;
        w32(p,(uint32_t)parts[i].origin.x);
        w32(p+4u,(uint32_t)parts[i].origin.y);
        w32(p+8u,(uint32_t)parts[i].origin.z);
        for(j=0;j<3u;++j)
            for(k=0;k<3u;++k)
                w16(p+12u+(j*3u+k)*2u,
                    (uint16_t)parts[i].rotation.m[j][k]);
    }
    for(i=0;i<T3_PSX_TRACE_BYTES;++i)output[i]=record[i];
    return 1;
}
