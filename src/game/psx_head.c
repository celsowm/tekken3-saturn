/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of HeadLookAt and _head_angles in pinned Tekken3Remake
 * remake/core/fighter/fighter_animation.gd. Changes: caller-owned state,
 * bounded normal Paul camera path, explicit arithmetic and atomic outputs.
 * Gon eye tracking and non-fight camera modes are outside this path. */
#include "psx_head.h"
#include <saturn/int_math.h>
static int16_t s16(int32_t x) {
    uint16_t u=(uint16_t)x;return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);
}
static int32_t abs32(int32_t x){return x<0?-x:x;}
static int32_t trig(const t3_psx_body_tables_t* t,int angle,unsigned cosine) {
    unsigned off=2u*((unsigned)angle&4095u)+(cosine?2048u:0u);
    return s16(t->ground.trig.data[off]|((uint16_t)t->ground.trig.data[off+1]<<8));
}
static int angle(const t3_psx_body_tables_t* t,int32_t z,int32_t x,int32_t* out) {
    uint16_t a;if(!t3_psx_atan2(&t->ground,z,x,&a))return 0;*out=s16(a);return 1;
}
static int sqrt_pair(const t3_psx_body_tables_t* t,int32_t x,int32_t y,int32_t* out) {
    uint32_t n=(uint32_t)((int64_t)x*x+(int64_t)y*y),v;
    if(!t3_psx_body_sqrt(t,n,&v))return 0;
    *out=(int32_t)v;return 1;
}
static int head_angles(const t3_psx_body_tables_t* t,const t3_psx_matrix_t* m,int16_t out[4]) {
    int32_t a28,a24,a26,n,idx,s,c,y48,y38,z38,x38;
    /* Applying unit axes gives matrix columns exactly, with no IR saturation. */
    int32_t x0=m->m[0][0],x1=m->m[1][0],x2=m->m[2][0];
    int32_t z0=m->m[0][2],z1=m->m[1][2],z2=m->m[2][2];
    if(!angle(t,x1,x2,&a28))return 0;
    n=-a28;idx=n/16;s=trig(t,idx,0);c=trig(t,idx,1);
    y48=(int32_t)(((int64_t)x1*c-(int64_t)x2*s)/4096);
    y38=(int32_t)(((int64_t)z1*c-(int64_t)z2*s)/4096);
    z38=(int32_t)(((int64_t)z1*s+(int64_t)z2*c)/4096);
    if(!angle(t,s16(x0),s16(y48),&a24))return 0;
    a24=s16(a24-16384);n=-a24;idx=n/16;
    s=trig(t,idx,0);c=trig(t,idx,1);
    x38=(int32_t)(((int64_t)z0*c-(int64_t)y38*s)/4096);
    if(!angle(t,s16(z38),s16(x38),&a26))return 0;
    out[0]=s16(a26-16384);out[1]=s16(a24);out[2]=s16(a28);out[3]=0;
    return 1;
}
int t3_psx_head_look(t3_psx_head_t* state,const t3_psx_body_tables_t* t,
                     const t3_psx_joint_t own[T3_PSX_DRAW_PARTS],
                     const t3_psx_joint_t opponent[T3_PSX_DRAW_PARTS],
                     const t3_move_record_t* move,int frame,uint8_t disabled,
                     t3_psx_matrix_t* head_local) {
    t3_psx_head_t n;int active,from;int32_t target[3],local[3],v,pitch,yaw,tilt,limit;
    t3_psx_matrix_t rot,base,result;uint8_t was;int refresh=1;
    if(!state || !t || !own || !opponent || !move || !head_local ||
       !t->ground.trig.data || t->ground.trig.size!=T3_PSX_TRIG_BYTES)return 0;
    if(disabled)return 1;
    n=*state;was=n.active;active=(move->flags&0x40000000u)!=0;
    from=move->active_last;
    if(from<(int)move->length-17)from=(int)move->length-17;
    if(move->active_first && frame<from)active=0;
    n.active=(uint8_t)active;
    if(!active){*state=n;return 1;}
    {
        const int32_t a[3]={opponent[1].origin.x,opponent[1].origin.y,opponent[1].origin.z};
        const int32_t b[3]={opponent[2].origin.x,opponent[2].origin.y,opponent[2].origin.z};
        const int32_t h[3]={own[2].origin.x,own[2].origin.y,own[2].origin.z};
        for(unsigned i=0;i<3;++i)
            target[i]=s16(s16(a[i]+sat_floor_shift_i64((int64_t)b[i]-a[i],1))-h[i]);
    }
    for(unsigned i=0;i<3;++i) {
        int64_t sum=0;
        for(unsigned j=0;j<3;++j)sum+=(int64_t)own[1].rotation.m[j][i]*target[j];
        local[i]=sat_floor_shift_i64(sum,12);
    }
    if(!sqrt_pair(t,local[1],local[2],&v) || !angle(t,v,s16(local[0]),&pitch))return 0;
    pitch=s16(-pitch);if(pitch>0x11C7)pitch=0x11C7;if(pitch< -0x2AAA)pitch=-0x2AAA;
    if(!angle(t,s16(local[1]),s16(local[2]),&yaw) ||
       !sqrt_pair(t,own[1].rotation.m[0][0],own[1].rotation.m[2][0],&v) ||
       !angle(t,v,own[1].rotation.m[1][0],&tilt))return 0;
    if(tilt+0x4000<0xAAB)limit=0x38E3;
    else if(tilt+0x4000<0x38E3)limit=0x38E3-(tilt+0x3556)*0x238E/0x2E39;
    else limit=0x1555;
    if(yaw>limit)yaw=limit;
    if(yaw< -limit)yaw=-limit;
    if(was==(unsigned)active && abs32(local[1])<400 && abs32(local[2])<300) {
        n.hold=(uint8_t)(n.hold+1);refresh=n.hold>=60;if(refresh)n.hold=0;
    } else n.hold=0;
    if(refresh || n.snap){n.target[0]=0;n.target[1]=(int16_t)pitch;n.target[2]=(int16_t)yaw;}
    if(n.snap) {
        for(unsigned k=0;k<4;++k)n.current[k]=n.target[k];
        n.snap=0;
    } else if(was!=(unsigned)active) {
        if(!head_angles(t,head_local,n.current))return 0;
    } else {
        for(unsigned k=1;k<3;++k) {
            int32_t e=(int32_t)n.target[k]-n.current[k],step=e-e/2;
            if(step>0x71C)step=0x71C;
            if(step< -0x71C)step=-0x71C;
            n.current[k]=s16(n.current[k]+step);
        }
        n.current[0]=n.target[0];
    }
    if(!t3_psx_euler_matrix(n.current[0],n.current[1],n.current[2],&t->ground.trig,&rot) ||
       !t3_psx_euler_matrix(0x4000,0,0x4000,&t->ground.trig,&base) ||
       !t3_psx_mul_rot(&base,&rot,&result))return 0;
    *state=n;*head_local=result;return 1;
}
