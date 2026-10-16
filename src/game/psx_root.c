#include "psx_root.h"
#include "fixed_shift.h"

static int32_t floor12(int64_t value) {
    return t3_floor_shift(value,12u);
}
static int32_t trunc12(int64_t value) {
    return (int32_t)(value>=0?value/4096:
                     -((-value)/4096));
}
static int16_t signed16(uint16_t n) {
    return n<0x8000u?(int16_t)n:(int16_t)((int32_t)n-65536);
}
static int16_t sin12(const t3_psx_trig_t* table,uint16_t angle,int cosine) {
    size_t p=(size_t)(((unsigned)angle>>3u)&0x1FFEu)+
             (cosine?0x800u:0u);
    return signed16((uint16_t)(table->data[p] |
                              ((uint16_t)table->data[p+1u]<<8u)));
}
static int bounded(t3_vec3i_t v) {
    return v.x>=-T3_PSX_UNIT_LIMIT && v.x<=T3_PSX_UNIT_LIMIT &&
           v.y>=-T3_PSX_UNIT_LIMIT && v.y<=T3_PSX_UNIT_LIMIT &&
           v.z>=-T3_PSX_UNIT_LIMIT && v.z<=T3_PSX_UNIT_LIMIT;
}
int t3_psx_pose_world_root(const int16_t pose[T3_PSX_POSE_CHANNELS],
                           unsigned scale_percent,t3_vec3i_t anchor,
                           uint16_t facing,uint16_t heading,
                           const t3_psx_trig_t* trig,t3_psx_joint_t* out) {
    t3_psx_joint_t result={0};
    int16_t delta[3];
    int32_t scale;
    int16_t c,s,cy,sy;
    int64_t x,y,z;
    unsigned i;
    if(!pose || !out || !trig || !trig->data ||
       trig->size!=T3_PSX_TRIG_BYTES ||
       scale_percent<1u || scale_percent>200u || !bounded(anchor))
        return 0;
    scale=(int32_t)(scale_percent*4096u/100u);
    for(i=0;i<3u;++i)
        delta[i]=signed16((uint16_t)floor12((int64_t)pose[i]*scale));
    c=sin12(trig,(uint16_t)(0x8000u-facing),1);
    s=sin12(trig,(uint16_t)(0x8000u-facing),0);
    x=(int64_t)anchor.x+
      trunc12((int64_t)delta[0]*c-(int64_t)delta[2]*s);
    y=(int64_t)anchor.y+delta[1];
    z=(int64_t)anchor.z+
      trunc12((int64_t)delta[0]*s+(int64_t)delta[2]*c);
    if(x< -T3_PSX_UNIT_LIMIT || x>T3_PSX_UNIT_LIMIT ||
       y< -T3_PSX_UNIT_LIMIT || y>T3_PSX_UNIT_LIMIT ||
       z< -T3_PSX_UNIT_LIMIT || z>T3_PSX_UNIT_LIMIT)return 0;
    result.origin.x=(int32_t)x;
    result.origin.y=(int32_t)y;
    result.origin.z=(int32_t)z;
    cy=sin12(trig,(uint16_t)(heading-0x8000u),1);
    sy=sin12(trig,(uint16_t)(heading-0x8000u),0);
    /* Original root_rotation(0,y,0) from FighterSkeleton: Ry. */
    result.rotation.m[0][0]=cy;
    result.rotation.m[0][2]=sy;
    result.rotation.m[1][1]=4096;
    result.rotation.m[2][0]=(int16_t)-sy;
    result.rotation.m[2][2]=cy;
    *out=result;
    return 1;
}
