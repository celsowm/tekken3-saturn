/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Original effect-pool ordering, placeholder, power glows and particles
 * adapted from pinned Tekken3Remake effect_objects.gd / effects.md.
 * Changes: numeric indices, caller-owned canonical records and tables.
 * The original EffectsUpdate prefetches the next node before dispatch.
 * No PS1 pointers, primitives or audio side effects. See NOTICE.md. */
#include "psx_effect_pool.h"
static int32_t add(int32_t a,int32_t b){return (int32_t)((uint32_t)a+(uint32_t)b);}
static uint32_t next_random(uint32_t* r){*r=*r*0x41C64E6Du+0x3039u;return (*r>>16)&0x7FFFu;}
static int16_t le16(const uint8_t* p){return (int16_t)(uint16_t)(p[0]|(p[1]<<8));}
static uint32_t le32(const uint8_t* p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void finish(t3_psx_spark_t* o){o->type=o->state=0;o->flags=0x80;}
static void reset(t3_psx_effect_pool_t* p) {
    p->head=p->tail=80;p->free_head=0;p->free_tail=79;p->count=0;
    for(unsigned i=0;i<80;++i){p->next[i]=(uint8_t)(i==79?80:i+1);p->previous[i]=(uint8_t)(i?i-1:80);}
}
t3_psx_spark_t* t3_psx_effect_alloc(t3_psx_effect_pool_t* p,unsigned type) {
    unsigned i=p->free_head;if(i==80)return NULL;
    p->free_head=p->next[i];if(p->free_head==80)p->free_tail=80;else p->previous[p->free_head]=80;
    p->previous[i]=p->tail;p->next[i]=80;
    if(p->tail!=80)p->next[p->tail]=(uint8_t)i;else p->head=(uint8_t)i;
    p->tail=(uint8_t)i;++p->count;
    t3_psx_spark_t* o=&p->object[i];o->type=(uint8_t)type;o->flags=0x80;o->state=0;return o;
}
static void release(t3_psx_effect_pool_t* p,unsigned i) {
    unsigned a=p->previous[i],b=p->next[i];
    if(a==80)p->head=(uint8_t)b;else p->next[a]=(uint8_t)b;
    if(b==80)p->tail=(uint8_t)a;else p->previous[b]=(uint8_t)a;
    p->previous[i]=p->free_tail;p->next[i]=80;
    if(p->free_tail==80)p->free_head=(uint8_t)i;else p->next[p->free_tail]=(uint8_t)i;
    p->free_tail=(uint8_t)i;p->object[i].flags=0;--p->count;
}
static void glows(t3_psx_effect_pool_t* p) {
    static const uint16_t joints[8]={6,10,14,17,0x8006,0x800A,0x800E,0x8011};
    for(unsigned i=0;i<8;++i){t3_psx_spark_t* o=t3_psx_effect_alloc(p,3);if(o)o->a=joints[i];}
}
void t3_psx_effect_pool_round(t3_psx_effect_pool_t* p) {
    reset(p);p->replay_glows=p->end_cleared=0;p->glow_mask[0]=p->glow_mask[1]=0;glows(p);
}
void t3_psx_effect_spray(t3_psx_effect_pool_t* p,t3_vec3i_t point,int16_t pitch,int16_t yaw,int power) {
    for(unsigned group=0;group<2;++group)for(unsigned k=0;k<(group?6u:20u);++k) {
        t3_psx_spark_t* o=t3_psx_effect_alloc(p,group?(power?12:1):(power?13:2));
        if(!o)break;
        o->x=point.x;o->y=point.y;o->z=point.z;o->pitch=pitch;o->yaw=yaw;
    }
}
static int masks(t3_psx_effect_pool_t* p,const uint8_t* t,size_t bytes,const t3_psx_effect_actor_t f[2]) {
    for(unsigned i=0;i<2;++i) {
        p->glow_mask[i]=0;if(!f[i].power_timer)continue;
        if(!t || bytes!=440)return 0;
        unsigned sum=f[i].joints[0]+f[i].joints[1]+f[i].joints[2]+f[i].joints[3];
        if(f[i].character!=22 && sum) {
            if(f[i].pose<=f[i].active_last)for(unsigned k=0;k<4;++k) {
                unsigned j=f[i].joints[k]<26?le32(t+4*f[i].joints[k]):0;
                if(j)p->glow_mask[i]|=(1u<<(j&31u))|0x80000000u;
            }
        } else p->glow_mask[i]|=0x440;
    }
    return 1;
}
static int glow(t3_psx_effect_pool_t* p,t3_psx_spark_t* o,const t3_psx_effect_actor_t f[2],
    const uint8_t* t,size_t bytes,uint32_t* rng,unsigned buffer) {
    if(!o->state){o->state=1;o->flags|=0x60;o->model=3;o->clut[0]=o->clut[1]=0x7DC7;o->b=-1;}
    else if(o->state!=1)return 1;
    unsigned player=((unsigned)o->a&0x8000u)>>15,joint=(unsigned)o->a&0x7FFFu;
    uint32_t mask=p->glow_mask[player];
    if(!(mask&(1u<<(joint&31u)))){o->flags&=(uint8_t)~0x40u;return 1;}
    if(!t || bytes!=440 || !f[player].world || f[player].character>=21)return 0;
    o->flags|=0x40;
    unsigned block=joint==6 && f[player].character==20?19:joint;
    const t3_psx_joint_t* j=&f[player].world[block];
    t3_vec3i_t point=j->origin;
    if(joint==6 || joint==10) {
        const uint8_t* off=t+104+16*f[player].character+(joint==10?8:0);
        int32_t v[3]={le16(off),le16(off+2),le16(off+4)};
        point.x=add(point.x,t3_psx_dot12(j->rotation.m[0],v));
        point.y=add(point.y,t3_psx_dot12(j->rotation.m[1],v));
        point.z=add(point.z,t3_psx_dot12(j->rotation.m[2],v));
    }
    o->x=point.x;o->y=point.y;o->z=point.z;o->b=add(o->b,1);
    o->clut[buffer]=(uint16_t)(0x7F50|((o->b>>1)&7));
    if(mask&0x80000000u) {
        t3_psx_spark_t* s=t3_psx_effect_alloc(p,4);
        if(s){s->x=o->x;s->y=o->y;s->z=o->z;}
    }
    uint32_t r=next_random(rng);
    int32_t limit=((80-(int32_t)p->live)*0xC8000/80000)*4;
    if((int32_t)r<limit) {
        t3_psx_spark_t* s=t3_psx_effect_alloc(p,5);
        if(s){s->x=o->x;s->y=o->y;s->z=o->z;}
    }
    return 1;
}
static void rising(t3_psx_spark_t* o,uint32_t* rng,unsigned buffer) {
    unsigned ember=o->type==5;
    if(!o->state) {
        o->state=1;o->flags|=(uint8_t)(ember?0x40:0x60);o->model=(uint8_t)(ember?5:4);
        o->clut[0]=o->clut[1]=0x7DC7;o->vy=0;
        uint32_t r=next_random(rng),u=r|(next_random(rng)<<16);
        if(ember){o->a=-1;o->x=add(o->x,(int32_t)(r&255)-127);o->z=add(o->z,(int32_t)((u>>4)&255)-127);}
        else {o->b=-1;o->x=add(o->x,(int32_t)(r&127)-63);o->y=add(o->y,(int32_t)((u>>2)&127)-63);o->z=add(o->z,(int32_t)((u>>4)&127)-63);}
    } else if(o->state!=1)return;
    int32_t* age=ember?&o->a:&o->b;*age=add(*age,1);
    if(*age<(ember?32:16)) {
        o->clut[buffer]=(uint16_t)(0x7F50+(ember?*age>>1:*age));
        o->vy=add(o->vy,0x12000);o->y=add(o->y,-(o->vy>>16));
    } else finish(o);
}
int t3_psx_effect_pool_step(t3_psx_effect_pool_t* p,const t3_psx_trig_t* trig,
    const uint8_t* t,size_t bytes,const t3_psx_effect_actor_t f[2],uint32_t* rng,
    unsigned buffer,int enabled,int replay,int phase) {
    if(!p || !trig || !rng || buffer>1)return 0;
    if(enabled){++p->counter;if(!masks(p,t,bytes,f))return 0;}
    if(!p->replay_glows && replay){reset(p);glows(p);++p->replay_glows;}
    if(!p->end_cleared && phase>=3 && phase<=7){reset(p);++p->end_cleared;}
    unsigned count=0;
    for(unsigned i=p->head;i!=80;) {
        unsigned next=p->next[i];t3_psx_spark_t* o=&p->object[i];
        if(enabled) {
            ++count;
            switch(o->type) {
            case 0:
                if(!o->state){o->a=2;o->state=1;}
                if(o->state==1){o->a=add(o->a,-1);if(o->a<1 && (o->flags&0x80))release(p,i);}
                break;
            case 1:case 2:case 12:case 13:if(!t3_psx_spark_step(o,trig,rng,buffer))return 0;break;
            case 3:if(!glow(p,o,f,t,bytes,rng,buffer))return 0;break;
            case 4:case 5:rising(o,rng,buffer);break;
            case 16:if(!o->state)o->state=1;else if(o->state==1)finish(o);break;
            case 17:
                o->c=add(o->c,replay?-2:-1);
                if(!o->state){o->c=15;o->state=1;}
                else if(o->state<=2 && o->c<0){o->b=(o->b&~0x3C00)|(o->a*0x400+(o->state==1?0x3000:0x3800));o->c=15;if(++o->state==3)finish(o);}
                break;
            default:return 0;
            }
        }
        i=next;
    }
    p->live=count;return 1;
}
