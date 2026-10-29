/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Hit spark phases adapted from pinned Tekken3Remake effect_objects.gd and
 * effects.md: original 0x800700EC, 0x800704D8, 0x8007379C, 0x80073B94.
 * Changes: portable canonical state, shared RNG/table arguments, both CLUTs;
 * no pointer-bearing draw primitives. See NOTICE.md and upstream LICENSE.md. */
#include "psx_spark.h"

static int32_t sine(const t3_psx_trig_t* t,unsigned angle,unsigned cosine) {
    const unsigned at=2u*(angle&0xFFFu)+(cosine?0x800u:0u);
    return (int16_t)((uint16_t)t->data[at]|((uint16_t)t->data[at+1]<<8));
}
static uint32_t next(uint32_t* rng) {
    *rng=*rng*0x41C64E6Du+0x3039u;return (*rng>>16)&0x7FFFu;
}
static int32_t add(int32_t a,int32_t b) {return (int32_t)((uint32_t)a+(uint32_t)b);}
static void aim(t3_psx_spark_t* s,const t3_psx_trig_t* t) {
    const unsigned p=(unsigned)-(int32_t)s->pitch,y=(unsigned)-(int32_t)s->yaw;
    s->vy=s->speed*sine(t,p,0);
    const int32_t h=(s->speed*sine(t,p,1))>>12;
    s->vx=h*sine(t,y,0);s->vz=h*sine(t,y,1);
}
static void move(t3_psx_spark_t* s) {
    s->x=add(s->x,s->vx>>16);s->y=add(s->y,s->vy>>16);s->z=add(s->z,s->vz>>16);
}
int t3_psx_spark_step(t3_psx_spark_t* s,const t3_psx_trig_t* t,uint32_t* rng,unsigned buffer) {
    if(!s || !t || !t->data || t->size<T3_PSX_TRIG_BYTES || !rng || buffer>1 ||
       (s->type!=1 && s->type!=2 && s->type!=12 && s->type!=13))return 0;
    const unsigned power=s->type>=12,spray=s->type==2 || s->type==13;
    if(s->state==0) {
        s->state=1;s->flags|=0x40u;s->model=(uint8_t)(power?2:1);
        s->clut[0]=s->clut[1]=(uint16_t)(power?0x7DC7:0x7DC6);
        const uint32_t r=next(rng),u=r|(next(rng)<<16);
        s->a=0;
        if(spray) {
            s->b=(int32_t)((u>>10)&31u)+20;
            s->x=add(s->x,(int32_t)(r&255u)-127);
            s->y=add(s->y,(int32_t)((u>>2)&255u)-127);
            s->z=add(s->z,(int32_t)((u>>4)&255u)-127);
            s->pitch=(int16_t)(s->pitch+(int32_t)((u>>6)&127u)-63);
            s->yaw=(int16_t)(s->yaw+(int32_t)((u>>8)&127u)-63);
            s->speed=(int16_t)((((u>>12)&0x7FFu)*5u)>>2);
            aim(s,t);
            s->x=add(s->x,s->vx>>13);s->y=add(s->y,s->vy>>13);s->z=add(s->z,s->vz>>13);
        } else {
            s->b=(int32_t)((u>>4)&31u)+10;
            s->c=(int32_t)((u>>6)&511u);s->d=(int32_t)((u>>8)&511u)+0x680;
            s->pitch=(int16_t)(s->pitch+((int32_t)(r&0x7FFu)-0x3FF)*2/3);
            s->yaw=(int16_t)(s->yaw+((int32_t)((u>>2)&0x7FFu)-0x3FF)*2/3);
        }
    } else if(s->state!=1)return 1;
    s->a=add(s->a,1);
    if(s->a<s->b) {
        const int32_t fade=s->a*16/s->b;
        s->clut[buffer]=(uint16_t)(((16+fade)&63)|(power?0x7F40:0x7F80));
        if(!spray) {
            const int32_t w=sine(t,(unsigned)(s->a*0x3FF/s->b)&0x3FFu,1);
            s->speed=(int16_t)(s->c+((s->d-s->c)*(w*4+0x2000)>>15));
            aim(s,t);
        }
        move(s);
        if(s->y<0)return 1;
    }
    s->type=0;s->state=0;s->flags=0x80;
    return 1;
}
