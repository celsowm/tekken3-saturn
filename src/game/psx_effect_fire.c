/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the fire, cloud and gas objects in pinned Tekken3Remake
 * remake/core/effects/effect_objects.gd. Changes: caller-owned pool, no light
 * list or vibration, and the original's UV flipbooks are not kept: the Saturn
 * draws these objects with the glow sprites. See NOTICE.md. */
#include "psx_effect_fire.h"
enum { VISIBLE=0x40,SEGMENT=0x02,OWNER_0=0x10,FLOOR=-0xD8 };
enum { CLUT_DUST=0x7DC6,CLUT_GLOW=0x7DC7,CLUT_FIRE=0x7DC8,CLUT_BURST=0x7DC9,CLUT_CLOUD=0x7DCA };
enum { MODEL_GLOW=3,MODEL_RISE=4 };
enum { FIRE_BREATH=0x18,BREATH_CLOUD=0x19,GON_FLAME=0x1A,GAS=0x1B,DOCTOR_CLOUD=0x1C };
typedef struct {
    t3_psx_effect_pool_t* pool;const t3_psx_trig_t* trig;const uint8_t* tables;
    const t3_psx_effect_actor_t* actors;uint32_t* rng;unsigned buffer;int replay;
} fire_t;
static int32_t add(int32_t a,int32_t b){return (int32_t)((uint32_t)a+(uint32_t)b);}
static uint32_t next_random(const fire_t* e){*e->rng=*e->rng*0x41C64E6Du+0x3039u;return (*e->rng>>16)&0x7FFFu;}
static int16_t le16(const uint8_t* p){return (int16_t)(uint16_t)(p[0]|(p[1]<<8));}
static int32_t sine(const t3_psx_trig_t* t,unsigned angle,unsigned cosine) {
    const unsigned at=2u*(angle&0xFFFu)+(cosine?0x800u:0u);
    return le16(t->data+at);
}
static void finish(t3_psx_spark_t* o){o->type=o->state=0;o->flags=0x80;}
static void show(t3_psx_spark_t* o,unsigned model,uint16_t clut){o->model=(uint8_t)model;o->clut[0]=o->clut[1]=clut;}
static void aim(const fire_t* e,t3_psx_spark_t* o) {
    const unsigned p=(unsigned)-(int32_t)o->pitch&0xFFFu,w=(unsigned)-(int32_t)o->yaw&0xFFFu;
    o->vy=o->speed*sine(e->trig,p,0);
    const int32_t h=(o->speed*sine(e->trig,p,1))>>12;
    o->vx=h*sine(e->trig,w,0);o->vz=h*sine(e->trig,w,1);
}
static void aim_flat(const fire_t* e,t3_psx_spark_t* o) {
    const unsigned w=(unsigned)-(int32_t)o->yaw&0xFFFu;
    o->vx=o->speed*sine(e->trig,w,0);o->vz=o->speed*sine(e->trig,w,1);
}
static void move(t3_psx_spark_t* o) {
    o->x=add(o->x,(int16_t)(o->vx>>16));o->y=add(o->y,(int16_t)(o->vy>>16));o->z=add(o->z,(int16_t)(o->vz>>16));
}
static void save_previous(t3_psx_spark_t* o){o->px=o->x;o->py=o->y;o->pz=o->z;}
static int chance(const fire_t* e,uint32_t r,int32_t k) {
    return (int32_t)r<(((T3_PSX_EFFECT_CAPACITY-(int32_t)e->pool->live)*k)/80000)*4;
}
static int owner_active(const fire_t* e,const t3_psx_spark_t* o,unsigned code) {
    const t3_psx_effect_actor_t* f=&e->actors[(o->flags&OWNER_0)?0:1];
    return f->first_joint==code && f->pose>=f->active_first;
}
static void floor_clip(const fire_t* e,t3_psx_spark_t* o) {
    if(o->y>FLOOR){o->y=FLOOR;o->pitch=0;o->vy=0;aim_flat(e,o);}
}
static void spawn_smoke(const fire_t* e,const t3_psx_spark_t* o,int pitch,int yaw) {
    t3_psx_spark_t* s=t3_psx_effect_alloc(e->pool,7);
    if(s){s->x=o->x;s->y=o->y;s->z=o->z;s->pitch=(int16_t)pitch;s->yaw=(int16_t)yaw;}
}
static const uint8_t* segment_frames(const fire_t* e,unsigned at){return e->tables+T3_PSX_EFFECT_SEGMENT_FRAMES+at;}

static void fireball(const fire_t* e,t3_psx_spark_t* o) {
    const int own_buffer=!(o->flags&OWNER_0)==(e->buffer==0u);
    if(o->d) {
        if(!owner_active(e,o,FIRE_BREATH))o->d=0;
        if(own_buffer)save_previous(o);
    }
    if(o->state==0) {
        o->state=1;o->flags|=VISIBLE;show(o,MODEL_GLOW,CLUT_FIRE);save_previous(o);
        const uint32_t r=next_random(e);
        o->b=(int32_t)((r>>2)&3u)+5;o->c=-1;o->d=1;o->speed=0xA00;o->a=0;
        o->pitch=(int16_t)(o->pitch-0x3F+(int32_t)((r>>4)&0x7Fu));
        o->yaw=(int16_t)(o->yaw-0x1F+(int32_t)((r>>6)&0x3Fu));
        aim(e,o);
    }
    if(o->state==1) {
        o->c=add(o->c,1);
        if(o->c>6){o->state=2;o->c=(int32_t)next_random(e);o->speed=0x1000;aim(e,o);}
    } else if(o->state==2) {
        o->c=add(o->c,1);o->a=add(o->a,1);
        if(o->b<=o->a){o->c=-1;o->state=3;(void)next_random(e);}
    } else if(o->state==3) {
        o->clut[0]=o->clut[1]=CLUT_BURST;
        o->c=add(o->c,1);
        if(o->c<8){if(o->d && !segment_frames(e,0)[o->c])o->d=0;}
        else finish(o);
    }
    move(o);
    floor_clip(e,o);
    o->flags&=(uint8_t)~SEGMENT;
    if(o->d && !own_buffer)o->flags|=SEGMENT;
    uint32_t r=next_random(e);
    if(chance(e,r,0x3C000))spawn_smoke(e,o,o->pitch,o->yaw);
    else if(o->y==FLOOR && o->d) {
        r=next_random(e);
        if(chance(e,r,0x24000)) {
            t3_psx_spark_t* s=t3_psx_effect_alloc(e->pool,9);
            if(s){s->x=o->x;s->z=o->z;}
        }
    }
}
static void smoke(const fire_t* e,t3_psx_spark_t* o) {
    if(o->state<2) {
        if(o->state==0) {
            o->state=1;o->flags|=VISIBLE;show(o,MODEL_RISE,CLUT_GLOW);
            const uint32_t r1=next_random(e),r2=next_random(e),u=r1|(r2<<16);
            o->b=(int32_t)((u>>10)&0x1Fu);o->a=0;
            o->x=add(o->x,-0x7F+(int32_t)(r1&0xFFu));
            o->y=add(o->y,-0x7F+(int32_t)((u>>2)&0xFFu));
            o->z=add(o->z,-0x7F+(int32_t)((u>>4)&0xFFu));
            o->pitch=(int16_t)(o->pitch-0x3F+(int32_t)((u>>6)&0x7Fu));
            o->yaw=(int16_t)(o->yaw-0x3F+(int32_t)((u>>8)&0x7Fu));
            o->speed=(int16_t)((((u>>12)&0x3FFu)*5u>>2)+0x200u);
            aim(e,o);
        }
        o->a=add(o->a,1);
        if(o->b<=o->a){o->a=-1;o->state=2;}
    } else {
        o->a=add(o->a,1);
        if(o->a>=0xC)finish(o);
    }
    o->vy=add(o->vy,-0x30000);
    move(o);
    if(o->y>=0)finish(o);
}
static void flame(const fire_t* e,t3_psx_spark_t* o) {
    if(o->flags&SEGMENT) {
        if(!e->replay) {
            if(!owner_active(e,o,GON_FLAME))o->flags&=(uint8_t)~SEGMENT;
        } else if(e->pool->burn_flags&((o->flags&OWNER_0)?8u:0x10u)){finish(o);return;}
        if((o->flags&SEGMENT) && o->state>=1 && o->state<=2) {
            const t3_psx_effect_actor_t* target=&e->actors[(o->flags&OWNER_0)?1:0];
            if(target->hit_clean){finish(o);return;}
            if(target->guarded)o->state=3;
        }
    }
    if(o->state<2) {
        if(o->state==0) {
            o->state=1;o->flags|=VISIBLE|SEGMENT;show(o,MODEL_GLOW,CLUT_FIRE);
            const uint32_t r=next_random(e);
            o->b=(int32_t)((r>>2)&3u)+5;o->c=-1;o->a=0;o->speed=0x800;
            aim(e,o);
        }
        o->c=add(o->c,1);
        if(o->c>6){o->state=2;o->c=(int32_t)next_random(e);}
    } else if(o->state==2) {
        o->c=add(o->c,1);o->a=add(o->a,1);
        if(o->b<=o->a){o->state=4;o->c=-1;}
    } else {
        if(o->state==3){o->state=4;o->vx=o->vy=o->vz=0;o->c=-1;}
        o->clut[0]=o->clut[1]=CLUT_BURST;
        o->c=add(o->c,1);
        if(o->c<9){if((o->flags&SEGMENT) && !segment_frames(e,8)[o->c])o->flags&=(uint8_t)~SEGMENT;}
        else finish(o);
    }
    save_previous(o);move(o);floor_clip(e,o);
    if(chance(e,next_random(e),0x3C000))spawn_smoke(e,o,o->pitch,o->yaw);
}
static void floor_fire(const fire_t* e,t3_psx_spark_t* o) {
    if(o->state<2) {
        if(o->state==0) {
            o->state=1;o->flags|=VISIBLE;show(o,MODEL_GLOW,CLUT_GLOW);
            const uint32_t r1=next_random(e),r2=next_random(e),u=r1|(r2<<16);
            o->y=-0x100;o->vy=0;o->a=(int32_t)(u>>2);o->b=(int32_t)(r1&0x1Fu)+0x3C;
        }
        o->a=add(o->a,1);o->b=add(o->b,-1);
        if(o->b<0){o->a=-1;o->state=2;}
    } else {
        o->a=add(o->a,1);
        if(o->a<0x10){o->vy=add(o->vy,-0x24000);o->y=add(o->y,o->vy>>16);}
        else finish(o);
    }
    if(chance(e,next_random(e),0x28000))spawn_smoke(e,o,0x400,0);
}
static void cloud(const fire_t* e,t3_psx_spark_t* o) {
    if((o->flags&SEGMENT) && !owner_active(e,o,o->d?DOCTOR_CLOUD:BREATH_CLOUD))o->flags&=(uint8_t)~SEGMENT;
    if(o->state==0) {
        o->state=1;o->flags|=VISIBLE|SEGMENT;show(o,MODEL_GLOW,o->d?CLUT_DUST:CLUT_CLOUD);
        const uint32_t r1=next_random(e),r2=next_random(e),u=r1|(r2<<16);
        o->c=-1;o->speed=0x300;
        o->pitch=(int16_t)(o->pitch-0x7F+(int32_t)(r1&0xFFu));
        o->yaw=(int16_t)(o->yaw-0x7F+(int32_t)((u>>2)&0xFFu));
        aim(e,o);
    }
    if(o->state==1) {
        o->c=add(o->c,1);
        if(o->c<0x15){if((o->flags&SEGMENT) && !segment_frames(e,17)[o->c])o->flags&=(uint8_t)~SEGMENT;}
        else finish(o);
    }
    save_previous(o);move(o);
}
static void burn_follow(const fire_t* e,t3_psx_spark_t* o) {
    const t3_psx_effect_actor_t* f=&e->actors[(o->flags&OWNER_0)||!(o->flags&8u)?0:1];
    o->b=add(o->b,1);
    const t3_vec3i_t p=f->world[o->d].origin;
    o->x=p.x;o->y=p.y;o->z=p.z;
    if(chance(e,next_random(e),0x3C000))spawn_smoke(e,o,0x400,0);
    o->a=add(o->a,-1);
    if(o->a<0){o->a=0;o->vy=0;o->state=(uint8_t)(o->state+1u);}
}
static void burn(const fire_t* e,t3_psx_spark_t* o) {
    if(o->state<2) {
        if(o->state==0) {
            o->state=1;o->flags|=VISIBLE;show(o,MODEL_RISE,CLUT_FIRE);
            const uint32_t r=next_random(e);
            o->a=(int32_t)(r&0xFu)+0x3C;o->b=(int32_t)(r>>4);
        }
        burn_follow(e,o);
    } else if(o->state==2) {
        o->vy=add(o->vy,-0x18000);o->a=add(o->a,1);o->y=add(o->y,o->vy>>16);
        if(o->a>6){o->a=-1;o->state=3;}
    } else {
        o->a=add(o->a,1);
        if(o->a<5){o->clut[0]=o->clut[1]=CLUT_BURST;o->vy=add(o->vy,-0x18000);o->y=add(o->y,o->vy>>16);}
        else finish(o);
    }
}
static void gas_emitter(const fire_t* e,t3_psx_spark_t* o) {
    if(!owner_active(e,o,GAS)){finish(o);return;}
    if(o->state==0){o->a=-1;o->b=0;o->state=1;}
    else if(o->state!=1)return;
    o->a=add(o->a,1);
    const uint8_t* entry=e->tables+T3_PSX_EFFECT_GAS_TIMES+6u*(unsigned)o->b;
    if(le16(entry)<=o->a) {
        t3_psx_spark_t* s=t3_psx_effect_alloc(e->pool,20);
        if(s) {
            s->flags|=o->flags&(0x10u|8u);
            s->x=o->x;s->y=-(int32_t)le16(entry+4);s->z=o->z;s->pitch=0;
            s->speed=(int16_t)(le16(entry+2)<<4);s->yaw=o->yaw;
        }
        o->b=add(o->b,1);
        if(o->b>=T3_PSX_EFFECT_GAS_ENTRIES)finish(o);
    }
}
static void gas(const fire_t* e,t3_psx_spark_t* o) {
    if((o->flags&SEGMENT) && !owner_active(e,o,GAS))o->flags&=(uint8_t)~SEGMENT;
    save_previous(o);
    if(o->state==0) {
        o->state=1;o->flags|=VISIBLE|SEGMENT;(void)next_random(e);show(o,MODEL_GLOW,CLUT_DUST);
        o->c=-1;aim(e,o);move(o);o->vy=0;
    }
    if(o->state==1) {
        o->c=add(o->c,1);
        if(o->c<0x36) {
            o->vy=add(o->vy,-0x3999);o->y=add(o->y,o->vy>>16);
            if((o->flags&SEGMENT) && !segment_frames(e,38)[o->c>>1])o->flags&=(uint8_t)~SEGMENT;
        } else finish(o);
    }
}
int t3_psx_effect_fire_step(t3_psx_effect_pool_t* pool,t3_psx_spark_t* o,const t3_psx_trig_t* trig,
    const uint8_t* tables,size_t bytes,const t3_psx_effect_actor_t actors[2],uint32_t* rng,unsigned buffer,int replay) {
    const fire_t e={pool,trig,tables,actors,rng,buffer,replay};
    if(!tables || bytes!=T3_PSX_EFFECT_TABLE_BYTES)return 0;
    switch(o->type) {
    case 6:fireball(&e,o);break;
    case 7:smoke(&e,o);break;
    case 8:flame(&e,o);break;
    case 9:floor_fire(&e,o);break;
    case 10:cloud(&e,o);break;
    case 11:burn(&e,o);break;
    case 19:gas_emitter(&e,o);break;
    case 20:gas(&e,o);break;
    default:return 0;
    }
    return 1;
}
void t3_psx_effect_burn(t3_psx_effect_pool_t* pool,const uint8_t* tables,unsigned player,int gon) {
    const uint8_t* joints=tables+T3_PSX_EFFECT_BURN_JOINTS;
    const unsigned count=gon?1u:10u;
    if(gon)pool->burn_flags|=(uint8_t)(0x10u>>player);
    for(unsigned i=0;i<count;++i) {
        t3_psx_spark_t* o=t3_psx_effect_alloc(pool,11);
        if(!o)return;
        if(gon)o->flags|=0x20u;
        o->c=(int32_t)i;o->d=le16(joints+4u*i);o->flags|=(uint8_t)(0x10u>>player);
    }
}
