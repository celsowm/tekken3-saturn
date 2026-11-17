/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Ball's ball as ported in the pinned Tekken3Remake (tools/research/ball_sim.py).
 * See ball_rules.h for the changes. */
#include "ball_rules.h"
#include "sh2_divide.h"

enum { HIT_RADIUS=0x260, HIT_RADIUS_SQUARED=0x5A400 };

static int32_t s16(int32_t x){return (int16_t)(uint16_t)x;}
static int32_t u16(int32_t x){return (uint16_t)x;}
static int32_t u8(int32_t x){return (uint8_t)x;}
static int32_t w32(int64_t v){return (int32_t)(uint32_t)v;}
static int32_t abs32(int32_t x){return x<0?w32(-(int64_t)x):x;}
/* C division with the R3000 quotient for a zero divisor. */
static int32_t rdiv(int32_t a,int32_t b) {
    if(b==0)return a>=0?-1:1;
    if(b==-1)return w32(-(int64_t)a);
    return t3_div32(a,b);
}
static int32_t cdiv(int32_t a,int32_t b){return t3_div32(a,b);}

static void emit(t3_ball_events_t* ev,int32_t kind,int32_t a,int32_t b,int32_t c,int32_t d,int32_t e) {
    t3_ball_event_t* x;
    if(!ev||ev->count>=BALL_EV_MAX)return;
    x=&ev->e[ev->count++];x->kind=kind;x->a=a;x->b=b;x->c=c;x->d=d;x->e=e;
}
static void sound(t3_ball_events_t* ev,int32_t id){emit(ev,BALL_EV_SOUND,id,0,0,0,0);}
static void popup(t3_ball_events_t* ev,const t3_ball_t* b,int32_t number,int32_t colour) {
    emit(ev,BALL_EV_POPUP,number,colour,b->pos[0],b->pos[1],b->pos[2]);
}

static int32_t trig(const t3_ball_tables_t* t,int32_t angle,unsigned cosine) {
    const uint8_t* d=t->tables.ground.trig.data;
    unsigned off=2u*((unsigned)angle&4095u)+(cosine?2048u:0u);
    return (int16_t)(uint16_t)(d[off]|((uint16_t)d[off+1]<<8));
}
/* FUN_8004B634: the angle of (x, z) in 4096 units from the u8 atan table plus t / 2. */
static int32_t atan_term(const t3_ball_tables_t* t,int32_t i){return t->tables.ground.atan[i]+(i>>1);}
static int32_t atan2_4096(const t3_ball_tables_t* t,int32_t x,int32_t z) {
    int32_t r,q;
    if(x==0&&z==0)return 0;
    if(x<0) {
        if(z<0) {
            if(-(int64_t)x<-(int64_t)z){q=rdiv(w32((int64_t)x<<10),z);r=0xC00-atan_term(t,q);}
            else{q=rdiv(w32((int64_t)z<<10),x);r=atan_term(t,q)+0x800;}
        } else if(-(int64_t)x<z){q=rdiv(w32((int64_t)x*-0x400),z);r=atan_term(t,q)+0x400;}
        else{q=rdiv(w32((int64_t)z<<10),-x);r=0x800-atan_term(t,q);}
    } else if(z<0) {
        if(x<-(int64_t)z){q=rdiv(w32((int64_t)x<<10),-z);r=atan_term(t,q)+0xC00;}
        else{q=rdiv(w32((int64_t)z*-0x400),x);r=0x1000-atan_term(t,q);}
    } else if(x<z){q=rdiv(w32((int64_t)x<<10),z);r=0x400-atan_term(t,q);}
    else{q=rdiv(w32((int64_t)z<<10),x);r=atan_term(t,q);}
    return s16(r);
}
/* ISqrt (0x8002A578): Newton iteration from a power-of-two estimate. */
static uint32_t isqrt(uint32_t n) {
    uint32_t guess=1,rest=n,prev;
    if(n==0)return 0;
    while(guess<rest){guess<<=1;rest>>=1;}
    for(;;) {
        prev=guess;guess=(n/prev+prev)>>1;
        if(guess>=prev)return prev;
    }
}

int32_t t3_ball_atan2(const t3_ball_tables_t* t,int32_t x,int32_t z){return atan2_4096(t,x,z);}
int32_t t3_ball_sin(const t3_ball_tables_t* t,int32_t angle){return trig(t,angle,0);}
int32_t t3_ball_cos(const t3_ball_tables_t* t,int32_t angle){return trig(t,angle,1);}
uint32_t t3_ball_isqrt(uint32_t n){return isqrt(n);}

static void hit_volume(t3_ball_t* b,const t3_ball_world_t* w) {
    int j,i;
    for(j=0;j<4;++j) {
        for(i=0;i<3;++i)b->volume[j][i]=b->volume[j][3+i];
        for(i=0;i<3;++i)b->volume[j][3+i]=w32((int64_t)b->pos[i]+s16(w->hit_offsets[j][i]));
    }
}

static int hits(const int32_t seg[6],const int32_t centre[3],int32_t radius) {
    t3_world_segment_t s={{seg[0],seg[1],seg[2]},{seg[3],seg[4],seg[5]},0};
    t3_hurt_cylinder_t c={{centre[0],centre[1],centre[2]},(uint16_t)radius};
    return t3_psx_segment_hits_cylinder(&s,&c)==1;
}

/* FUN_80048030: the fighters' attack segments against the ball's cylinder; bit 2 for fighter 0, bit 1 for
 * fighter 1, bit 8 when a fighter's move uses projectile segments. */
static int32_t attack_touch(const t3_ball_t* b,const t3_ball_fighter_t f[2],int32_t out[3]) {
    int32_t mask=0;
    int pass,k,i;
    for(pass=0;pass<2;++pass) {
        const t3_ball_fighter_t* x=&f[pass];
        int count=x->seg_count;
        if(x->projectile)mask|=8;
        else if(count>4)count=4;
        for(k=0;k<count;++k) {
            if(hits(x->segs[k],b->prev,HIT_RADIUS)) {
                for(i=0;i<3;++i)out[i]=w32((int64_t)x->segs[k][3+i]-x->segs[k][i]);
                mask|=pass?1:2;break;
            }
        }
    }
    return mask;
}

/* FUN_800481AC: the ball's four swept hit points against each fighter's 14 hurt cylinders; the first contact
 * is recorded in the fighter. */
static int32_t body_touch(const t3_ball_t* b,t3_ball_fighter_t f[2]) {
    int32_t mask=0;
    int pass,k,j,i;
    for(pass=0;pass<2;++pass) {
        t3_ball_fighter_t* x=&f[pass];
        int done=0;
        for(k=0;k<14&&!done;++k) {
            if(s16(x->cyl[k][3])==0)continue;
            for(j=0;j<4;++j) {
                if(hits(b->volume[j],x->cyl[k],s16(x->cyl[k][3]))) {
                    x->contact_cyl=k;
                    for(i=0;i<3;++i) {
                        x->contact_end[i]=b->volume[j][3+i];
                        x->contact_dir[i]=s16(u16(b->volume[j][3+i])-u16(b->volume[j][i]));
                    }
                    mask|=pass?1:2;done=1;break;
                }
            }
        }
    }
    return mask;
}

/* volley.ovl FUN_800B3118: the squash axis angles from the hit direction. */
static void squash_axis(const t3_ball_tables_t* t,int32_t out[3],const int32_t d[3]) {
    int32_t a;
    out[0]=0;
    out[1]=s16(atan2_4096(t,d[0],d[2]));
    a=atan2_4096(t,d[0],d[1]);
    out[2]=s16(d[0]>0?a:-a);
}

static void target_of(const t3_ball_fighter_t f[2],int32_t who,int32_t* x,int32_t* z){*x=f[who].x;*z=f[who].z;}

/* The shared part of a charging hit: the re-hit bookkeeping, the hitter's power gain, the charge and the
 * damage popup; returns the speed step count clamped to 20..cap. */
static int32_t charge_hit(t3_ball_t* b,t3_ball_world_t* w,const t3_ball_fighter_t* hitter,const t3_ball_fighter_t f[2],
                          int32_t base_add,int32_t cap,t3_ball_events_t* ev) {
    int32_t add,steps,p=hitter->player&1;
    if(b->charge) {b->rehit=1;b->rehit_count=u8(b->rehit_count+1);}
    add=cdiv(w32((int64_t)s16(hitter->damage)*s16(w->gain)),100);
    b->power[p]=s16(u16(b->power[p])+add);
    b->charge=s16(b->power[p]);
    b->homing=1;
    popup(ev,b,add,f[b->owner&1].index==0?2:5);
    steps=s16(u16(hitter->damage)+base_add);
    if(steps>=cap+1)steps=cap;
    else if(steps<0x14)steps=0x14;
    return steps;
}
static void speed_from_steps(t3_ball_t* b,const t3_ball_world_t* w,int32_t steps) {
    int32_t speed=w32((int64_t)(steps+b->rehit_count)*s16(w->spike_unit));
    if(b->rehit)speed=w32((int64_t)speed+(b->speed>>4));
    b->speed=speed;
}

/* A fighter's attack hits the ball (the branch after FUN_80048030). */
static void attack_hit(t3_ball_t* b,t3_ball_world_t* w,t3_ball_fighter_t f[2],int hi,int32_t mask,const int32_t d[3],
                       const t3_ball_tables_t* t,t3_ball_events_t* ev) {
    t3_ball_fighter_t* hitter=&f[hi];
    const int oi=hi^1;
    int32_t tx,tz,g=s16(w->gravity_unit),unit=s16(w->speed_unit),x=b->pos[0];
    int32_t level=u16(hitter->level),damage=s16(hitter->damage),lob;
    emit(ev,BALL_EV_VIBRATE,hitter->player,5,0,0,0);
    b->sweep=0;b->owner=hi;b->target=oi;b->homing=0;
    emit(ev,BALL_EV_STAND_IN,hi,0,0,0,0);
    w->stand_in.source=hi;w->stand_in.damage=damage;w->stand_in.level=0xFFFF;
    w->stand_in.hit83[0]=hitter->hit83[0];w->stand_in.hit83[1]=hitter->hit83[1];
    w->stand_in.hit88=hitter->f88;w->stand_in.hitD1=hitter->fd1;w->stand_in.hit23=hitter->f23;
    w->stand_in.move_byte=hitter->move_byte;w->stand_in.x=hitter->x;w->stand_in.z=hitter->z;
    hitter->locked=1;hitter->f88=1;hitter->fd1=1;hitter->f23=2;
    b->touch_wait=5;
    target_of(f,oi,&tx,&tz);
    b->angle=s16(atan2_4096(t,w32((int64_t)tx-b->pos[0]),w32((int64_t)tz-b->pos[2])));
    squash_axis(t,b->squash_axis,d);
    b->squash_hold=1;
    b->squash=s16(w32((int64_t)u16(hitter->damage)*8+0x140));
    lob=w32((int64_t)tz-2*(int64_t)b->pos[1]-(int32_t)w->lob_base);
    if((mask&3)==3) {                                        /* both players at once */
        b->charge=0;b->speed=0;b->state=BALL_FLY;b->vel[1]=w32(-0x25*(int64_t)g);b->gravity=g;
    } else if(s16(hitter->kind)==4&&(level==0x607||level==0x706)&&!(mask&8)&&s16(hitter->slot)!=0x181) {
        int32_t hp=w32((int64_t)hitter->health-((int64_t)s16(b->power[oi])<<16));
        hitter->health=hp>0?hp:0;
        b->power[1]=0;b->power[0]=0;b->state=BALL_POINT;b->timer=5;w->point=hitter->index+1;
    } else if(damage==0) {                                   /* a set */
        if(s16(b->charge)>0) {
            int32_t v;
            b->state=BALL_SET;b->vel[1]=w32(-0x25*(int64_t)g);b->gravity=g;
            b->spike_speed=s16(u16(b->speed)+u16(w->spike_unit)*5);
            v=w32((int64_t)(damage+8)*unit);
            v=w32((int64_t)(x>=0?w32((int64_t)v+x):w32((int64_t)v-x))-(b->speed>>2));
            b->speed=v>=0?v:0;
            b->charge=0;b->touch_wait=u8(b->touch_wait+2);
            popup(ev,b,-1,1);sound(ev,0x7082);
        } else goto flat;
    } else if(level==0x607||level==0x706) {                  /* unblockable: homing with the charge */
        b->homing=1;b->speed=w32((int64_t)s16(w->spike_unit)*60);b->gravity=s16(w->gravity_unit)>>2;
        b->charge=s16(b->power[hitter->player&1]);b->state=BALL_FLY;b->vel[1]=lob;
        popup(ev,b,-1,2);
    } else if(hitter->airborne==0) {
        if(level==0x31F||level==0x217) {                     /* mids: a charged lob */
            speed_from_steps(b,w,charge_hit(b,w,hitter,f,0x12,0x2E,ev));
            b->gravity=s16(w->gravity_unit)>>2;b->state=BALL_FLY;b->vel[1]=lob;
        } else if(level==0x10F||level==0x51F) {              /* lows */
            b->vel[1]=w32(-0x32*(int64_t)g);b->gravity=g;
            b->speed=w32((int64_t)(s16(u16(hitter->damage))>>2)*unit+abs32(x));
            b->charge=0;b->state=BALL_FLY;
        } else if(level==0x412) {                            /* highs */
            if(s16(b->state)!=BALL_SET)goto flat;
            b->state=BALL_SPIKE;b->spike_target=oi;b->vel[1]=w32(-0x48*(int64_t)g);b->gravity=g;
            b->speed=w32((int64_t)unit*10);sound(ev,0x4DAF);
            (void)charge_hit(b,w,hitter,f,0,0x7FFF,ev);
        }
    } else {                                                 /* the hitter is airborne */
        speed_from_steps(b,w,charge_hit(b,w,hitter,f,0xF,0x28,ev));
        b->state=BALL_FLY;b->gravity=s16(w->gravity_unit)>>1;
        b->vel[1]=w32((int64_t)tz-b->pos[1]-(int32_t)w->lob_base);
    }
    return;
flat: {
        int32_t v,quarter=b->speed>>2;
        b->gravity=g;b->vel[1]=w32(-0x25*(int64_t)g);
        v=w32((int64_t)(damage+10)*unit);
        v=x<0?w32((int64_t)v-x):w32((int64_t)v+x);
        v=w32((int64_t)v-quarter);
        b->speed=v>0?v:0;b->charge=0;b->state=BALL_FLY;
    }
}

/* The charged ball reaches the opponent: the resident hit system with the stand-in attacker, whose damage is
 * the charge. */
static void charged_touch(t3_ball_t* b,t3_ball_world_t* w,t3_ball_fighter_t f[2],int hi,t3_ball_events_t* ev) {
    t3_ball_fighter_t* hitter=&f[hi];
    const int oi=hi^1;
    int32_t charge,lethal;
    w->stand_in.damage=u16(b->charge);
    b->owner=hi;b->target=oi;b->state=BALL_FLY;w->charged_hit=1;b->charge=0;
    w->stand_in.hit83[oi]=1;w->stand_in.hit88=1;w->stand_in.hitD1=1;w->stand_in.hit23=f[oi].index;
    hitter->f87=1;hitter->fce=1;hitter->fee=u8(w->stand_in.move_byte);hitter->f15d=1;hitter->f15c=2;
    charge=s16(w->stand_in.damage);
    if(charge!=0||hitter->airborne)hitter->f104=4;
    lethal=charge>99||b->kind==9;
    if((u16(hitter->guard)&u16(w->stand_in.level))==0) {     /* not guarded */
        int32_t g;
        if(lethal) {
            emit(ev,BALL_EV_BURN,hitter->index,0,0,0,0);
            emit(ev,BALL_EV_REPLAY,hitter->index,0x82,0,0,0);
            if(w->option==2)w->result=6;
        }
        g=s16(u16(w->gravity_unit));
        b->speed=0;b->rehit_count=0;b->power[0]=0;b->power[1]=0;b->state=BALL_DEAD;
        b->owner=hi;b->target=hi;b->touch_wait=5;
        b->vel[1]=w32((int64_t)g*-0x25);b->gravity=g>>1;
        sound(ev,charge<0x32?0x4E6D:0x4DAF);
    } else {
        sound(ev,0x7043);
        if(lethal&&(int64_t)hitter->health<=w32((int64_t)charge<<13)) {
            emit(ev,BALL_EV_BURN,hitter->index,0,0,0,0);
            emit(ev,BALL_EV_REPLAY,hitter->index,0x82,0,0,0);
        }
    }
}

/* The attack and body tests of a flying ball; returns the hit mask (the game's $s7). */
static int32_t touches(t3_ball_t* b,t3_ball_world_t* w,t3_ball_fighter_t f[2],const t3_ball_tables_t* t,
                       t3_ball_events_t* ev) {
    int32_t d[3]={0,0,0},mask,tx,tz,lob,k,dist,charge;
    int hi,oi,i;
    for(i=0;i<3;++i)b->prev[i]=b->pos[i];
    mask=attack_touch(b,f,d);
    if(mask&7) {
        hi=(mask&2)?0:1;
        if(f[hi].locked==0)attack_hit(b,w,f,hi,mask,d,t,ev);
        return mask;
    }
    mask=0;
    if(f[0].busy4==0&&f[1].busy4==0)mask=body_touch(b,f);
    if(w->round_state>5) {
        if(f[1].busy3==0)mask&=2;
        else if(f[0].busy3==0)mask&=1;
    }
    if(mask==0)return 0;
    hi=(mask&2)?0:1;oi=hi^1;
    charge=s16(b->charge);
    if(hi==b->owner&&charge!=0)return mask;
    target_of(f,oi,&tx,&tz);
    b->sweep=0;b->homing=0;
    if(s16(b->state)!=BALL_SET)b->state=BALL_FLY;
    b->angle=s16(atan2_4096(t,w32((int64_t)tx-b->pos[0]),w32((int64_t)tz-b->pos[2])));
    lob=(int32_t)w->lob_base;
    {
        const int32_t g=s16(w->gravity_unit);
        k=w32((int64_t)(int32_t)((uint32_t)lob>>7)+12);
        b->gravity=g;
        b->vel[1]=w32((int64_t)(w32(-(int64_t)g*k)>>1)-w32((int64_t)b->pos[1]+0x1000));
    }
    dist=(uint32_t)lob>0x800u?w32((int64_t)(int32_t)(uint32_t)lob-0x800):0x400;
    b->speed=rdiv(w32((int64_t)dist<<8),k);
    if(charge==0||b->owner==hi) {
        if(w->touch_sound_wait==0){sound(ev,0x704E);w->touch_sound_wait=10;}
        b->owner=hi;b->target=oi;
    } else charged_touch(b,w,f,hi,ev);
    return mask;
}

static int32_t floor_and_touches(t3_ball_t* b,t3_ball_world_t* w,t3_ball_fighter_t f[2],const t3_ball_tables_t* t,
                                 int32_t mask,t3_ball_events_t* ev) {
    const int32_t g=s16(w->gravity_unit);
    int32_t x=b->pos[0],n;
    if(b->pos[1]>=-0x180||abs32(x)>0x2800) {                 /* it comes down (or leaves the court) */
        int32_t vy;
        b->pos[1]=-0x180;b->gravity=g;
        vy=b->vel[1];
        if(vy>0xD00)sound(ev,0x4E6E);
        if(w->option==2) {
            if(vy>0x3000)emit(ev,BALL_EV_SHAKE,1,0,0,0,0);
            else if(vy>0x1800)emit(ev,BALL_EV_SHAKE,0,0,0,0,0);
        }
        x=b->pos[0];
        if(abs32(x)<=s16(w->neutral)) {                      /* bounce in the neutral zone */
            int i;
            b->state=BALL_FLY;b->vel[1]=w32(-(int64_t)b->vel[1])>>1;
            for(i=0;i<3;++i)b->spin_rate[i]=s16(b->spin_rate[i])>>1;
        } else {
            t3_ball_fighter_t* loser;
            int32_t hp;
            b->vel[1]=0;b->speed=0;
            if(w->no_score!=0||s16(b->state)==BALL_DEAD){b->state=BALL_RESET;b->timer=0x10;return mask;}
            b->power[x<0?0:1]=0;
            loser=&f[x<0?0:1];
            hp=w32((int64_t)loser->health-0x1E0000);
            loser->health=hp;
            loser->opponent_flag=(loser->index+1)&1;
            if(hp<0)loser->health=0;
            b->state=BALL_POINT;b->timer=5;
            w->point=w->no_winner_mark?0:loser->index+1;
            w->stand_in.x=b->pos[0];w->stand_in.z=b->pos[2];
            return mask;
        }
    }
    if(b->pos[1]<-0x189)w->idle=0;
    else {
        n=w32((int64_t)w->idle+1);w->idle=n;
        if(n>=600){b->state=BALL_RESET;b->timer=0x10;return mask;}
    }
    if(s16(b->homing)==0) {
        int32_t v=w32((int64_t)b->speed-0x20);
        b->speed=v>=0?v:0;
    }
    if(b->touch_wait){b->touch_wait=u8(b->touch_wait-1);return mask;}
    return touches(b,w,f,t,ev);
}

static void tail(t3_ball_t* b,t3_ball_world_t* w,t3_ball_fighter_t f[2],const t3_ball_tables_t* t,int32_t mask) {
    int32_t angle=b->angle&0xFFF,cs=trig(t,angle,1),sn=trig(t,angle,0),z_old,mid,tx,tz,a,i;
    b->vel[0]=w32((int64_t)w32((int64_t)cs*b->speed)>>12);
    b->vel[2]=w32((int64_t)w32((int64_t)sn*b->speed)>>12);
    if(s16(b->state)==BALL_SERVE) {                          /* the serve: bob until touched */
        const int32_t tm=b->timer,v=trig(t,(tm<<6&0x1FC0)/2,0);
        b->timer=u8(tm+1);
        b->pos[1]=w32((int64_t)(s16(v)>>4)-0x500);
        if(w->round_clock>=600){b->state=BALL_FLY;b->vel[1]=0;}
    } else for(i=0;i<3;++i)b->pos[i]=w32((int64_t)b->pos[i]+(b->vel[i]>>8));
    hit_volume(b,w);
    b->vel[1]=w32((int64_t)b->vel[1]+b->gravity);
    if(mask&&s16(b->state)!=BALL_SET) {                      /* spin from the new flight */
        int32_t spin;
        angle=b->angle&0xFFF;cs=trig(t,angle,1);sn=trig(t,angle,0);
        spin=w32((int64_t)w32((int64_t)cs*b->speed))>>(s16(b->homing)?0x11:0x13);
        b->spin_rate[2]=s16(spin);
        b->spin_rate[0]=s16(s16(spin)>>2);
        b->spin_rate[1]=s16(w32((int64_t)w32((int64_t)sn*b->speed))>>0x13);
    }
    for(i=0;i<3;++i)b->spin[i]=s16(u16(b->spin[i])+u16(b->spin_rate[i]));
    z_old=b->pos[2];
    mid=w32((int64_t)f[0].z+f[1].z)>>1;
    if(w32((int64_t)z_old-mid)>0x80)b->pos[2]=w32((int64_t)mid+0x80);
    if(w32((int64_t)b->pos[2]-mid)<-0x80)b->pos[2]=w32((int64_t)mid-0x80);
    b->spin[0]=s16(u16(b->spin[0])+u16(b->pos[2])-u16(z_old));
    target_of(f,b->target&1,&tx,&tz);
    a=s16(b->angle);
    if(a<-0x800)b->angle=s16(a+0x1000);
    if(s16(b->angle)>0x800)b->angle=s16(u16(b->angle)-0x1000);
    if(s16(b->homing)==0)return;
    b->angle=s16(atan2_4096(t,w32((int64_t)tx-b->pos[0]),w32((int64_t)tz-b->pos[2])));
    if(b->speed<=0x1000)return;
    {
        const int32_t dx=w32((int64_t)b->pos[0]-tx),dz=w32((int64_t)b->pos[2]-tz);
        const int32_t sq=w32((int64_t)w32((int64_t)dx*dx)+w32((int64_t)dz*dz));
        const int32_t dist=(int32_t)isqrt((uint32_t)sq),n=rdiv(dist,b->speed>>8),vy=b->vel[1],head=f[b->target&1].head;
        const int32_t fall=w32((int64_t)vy+w32((int64_t)n*b->gravity))>>8;
        const int32_t miss=w32((int64_t)w32((int64_t)b->pos[1]+fall-head)<<8);
        const int32_t q=rdiv(w32((int64_t)vy-miss),n);
        b->vel[1]=w32((int64_t)w32((int64_t)w32((int64_t)vy*13+(int64_t)q*3)<<8)>>12);
    }
}

int t3_ball_update(t3_ball_t* b,t3_ball_world_t* w,t3_ball_fighter_t f[2],const t3_ball_tables_t* t,int32_t s7,
                   t3_ball_events_t* ev) {
    int32_t st,mask,g,y;
    if(!b||!w||!f||!t||!t->tables.ground.atan||!t->tables.ground.trig.data)return 0;
    b->rehit=b->touch_wait?u8(b->rehit):0;
    w->charged_hit=0;w->point=0;
    st=s16(b->state);
    if(st==BALL_SERVE_WAIT) {
        b->timer=u8(b->timer-1);
        if(b->timer==0){b->state=BALL_FIRST_WAIT;b->timer=0x20;sound(ev,0x4CE8);}
        return 1;
    }
    if(st==BALL_WAIT||st==BALL_FIRST_WAIT) {
        int32_t x0,x1,z0,dx,dz;
        b->timer=u8(b->timer-1);
        if(b->timer)return 1;
        b->state=st==BALL_FIRST_WAIT?BALL_SERVE:BALL_FLY;
        b->speed=0;b->touch_wait=0;b->angle=0;b->homing=0;
        x0=f[0].x<0?f[0].x:0;x1=f[1].x>0?f[1].x:0;z0=f[0].z;
        dx=w32((int64_t)x1-x0);dz=w32((int64_t)f[1].z-z0);
        if(dx!=0)b->pos[2]=w32((int64_t)z0+rdiv(w32((int64_t)(b->pos[0]-x0)*dz),dx));
        else b->pos[0]=w32((int64_t)x0+rdiv(0,dz));
        return 1;
    }
    if(st==BALL_RESET) {
        int i;
        b->charge=0;b->rehit_count=0;
        b->timer=u8(b->timer-1);
        if(b->timer)return 1;
        b->state=BALL_WAIT;b->timer=0x20;
        b->pos[0]=0;b->pos[1]=-0xE40;
        for(i=0;i<3;++i){b->vel[i]=0;b->spin_rate[i]=0;}
        b->gravity=s16(w->gravity_unit)>>1;
        b->pos[2]=w32((int64_t)f[0].z+f[1].z)>>1;
        hit_volume(b,w);hit_volume(b,w);
        if(w->no_score==0)sound(ev,0x4CE9);
        return 1;
    }
    if(st==BALL_POINT) {
        b->charge=0;b->rehit_count=0;
        b->timer=u8(b->timer-1);
        if(b->timer==0){b->state=BALL_RESET;b->timer=0x10;}
        return 1;
    }
    mask=s7;g=s16(w->gravity_unit);y=b->pos[1];
    if(st==BALL_SPIKE&&y<-0x1400) {                          /* a spike reaches the ceiling */
        b->pos[1]=-0x1400;b->gravity=g;b->state=BALL_FLY;b->target=b->spike_target;
        b->speed=s16(b->spike_speed);b->vel[1]=w32((int64_t)cdiv(s16(b->spike_speed)*2,3)+0x1000);
    } else if(st!=BALL_SPIKE&&y<-0x2000) {                   /* the ceiling */
        b->pos[1]=-0x2000;b->vel[1]=0;b->gravity=g;
    } else mask=floor_and_touches(b,w,f,t,mask,ev);
    tail(b,w,f,t,mask);
    return 1;
}

/* FUN_800B4720: once per player and size, the glow of a charged ball (effect type 15). */
static void glow(t3_ball_world_t* w,t3_ball_events_t* ev,int32_t player,int32_t big) {
    const int32_t u=player+2*big,bit=1<<(u&31);
    if(w->glows&bit)return;
    emit(ev,BALL_EV_GLOW,player,big,0,0,0);
    w->glows=(w->glows|(bit&0xFF))&0xFF;
}

int t3_ball_after_update(t3_ball_t* b,t3_ball_world_t* w,const t3_ball_tables_t* t,t3_ball_events_t* ev) {
    int32_t s,kind;
    int relax=1;
    if(!b||!w||!t)return 0;
    s=s16(b->squash);
    if(s16(b->homing)) {
        if(s<0x80) {
            relax=0;b->squash_hold=0;
            squash_axis(t,b->squash_axis,b->vel);
            b->squash_axis[2]=s16(u16(b->squash_axis[2])+0x400);
            b->squash=s16(cdiv(s16(u16(b->squash))*2,3)+cdiv(b->speed>>6,3));
        } else if(b->squash_hold==0)relax=0;
    }
    if(relax){s=s16(b->squash);b->squash=s16(u16(b->squash)-((s+0x10)>>4));}
    switch(s16(b->state)) {
    case BALL_RESET:kind=1;break;
    case BALL_WAIT:case BALL_FIRST_WAIT:b->kind=0;return 1;
    case BALL_POINT:kind=2;break;
    default: {
        const int32_t charge=s16(b->charge);
        if(charge==0) {
            if(b->touch_wait)kind=5;
            else{b->kind=s16(b->state)==BALL_SET?4:3;return 1;}
        } else {
            b->timer=b->owner&1;     /* the original keeps the owner's index in +0x147 */
            if(charge<100) {
                if(b->touch_wait==5)glow(w,ev,b->timer,0);
                b->kind=b->timer?7:6;
            } else {
                if(b->touch_wait==5)glow(w,ev,b->timer,1);
                b->kind=9;
            }
            if(b->touch_wait==0)return 1;
            kind=b->rehit?8:5;
        }
    }
    }
    b->kind=kind;
    return 1;
}

void t3_ball_move_tuning(t3_ball_fighter_t* f) {
    static const int8_t kinds[10]={0,-1,0,-1,-1,-1,1,2,1,2};
    int32_t k=s16(f->slot-0x18),i;
    f->drift[0]=0;
    for(i=1;i<5;++i)f->drift[i]=0;
    if(k<0||k>=10)return;
    switch(kinds[k]) {
    case 0:f->drift[0]=s16(0x8000);f->drift[1]=0x1E;f->drift[2]=3;f->drift[3]=9;break;
    case 1:f->drift[0]=s16(0x8000);f->drift[1]=0x96;f->drift[2]=5;f->drift[3]=0xC;f->drift[4]=0x12;break;
    case 2:f->drift[4]=0x12;break;
    default:break;
    }
}

void t3_ball_init(t3_ball_t* b,const t3_ball_world_t* w) {
    int i,j;
    b->state=BALL_SERVE_WAIT;
    b->timer=w->first_round==1?100:40;
    b->pos[0]=w->serve_side?0xD00:-0xD00;
    b->owner=w->serve_side&1;b->target=(w->serve_side&1)^1;
    b->pos[1]=-0x500;b->pos[2]=0;
    b->spin_rate[0]=0x20;b->spin_rate[1]=0x10;b->spin_rate[2]=4;
    b->speed=0;b->sweep=0;
    for(i=0;i<3;++i){b->vel[i]=0;b->spin[i]=0;b->squash_axis[i]=0;}
    b->power[0]=b->power[1]=0;b->charge=0;b->angle=0;b->homing=0;b->squash=0;
    b->touch_wait=0;b->rehit_count=0;
    b->gravity=s16(w->gravity_unit);
    for(i=0;i<3;++i)b->prev[i]=b->pos[i];
    for(j=0;j<4;++j)for(i=0;i<6;++i)b->volume[j][i]=0;
    hit_volume(b,w);hit_volume(b,w);
}

void t3_ball_fight_start(t3_ball_t* b,t3_ball_world_t* w,const uint16_t row[12]) {
    w->spike_unit=row[0];w->speed_unit=row[2];w->gravity_unit=row[4];w->gain=row[8];w->neutral=row[10];
    t3_ball_init(b,w);
    w->result=2;w->glows=0;w->touch_sound_wait=0;
}
