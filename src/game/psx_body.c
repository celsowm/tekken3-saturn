/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of normal ArenaBounds, BodyOverlapSolve and BodySeparate from
 * pinned Tekken3Remake tools/research/fight_sim.py and fight_math.py.
 * Sphere profiles also adapt remake/core/combat/combat.gd.
 * Changes: caller-owned state/tables, explicit wrap, atomic publication;
 * unsupported game modes/throws excluded by the caller. See NOTICE.md. */
#include "psx_body.h"
static int32_t wrap(int64_t n) {
    uint32_t u=(uint32_t)(uint64_t)n;
    return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);
}
static int16_t s16(int32_t n) {
    uint16_t u=(uint16_t)n;return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);
}
static int32_t le16(const uint8_t* p){return s16(p[0]|((uint16_t)p[1]<<8));}
static int64_t mag(int32_t n){return n<0?-(int64_t)n:n;}
static int32_t half(int32_t n){return n>=0?n/2:-(int32_t)((-(int64_t)n+1)/2);}
static int trig(const t3_psx_body_tables_t* t,int angle,unsigned cos,int32_t* out) {
    unsigned off=2u*((unsigned)angle&4095u)+(cos?0x800u:0u);
    if(!t || !t->ground.trig.data || t->ground.trig.size!=T3_PSX_TRIG_BYTES)return 0;
    *out=le16(t->ground.trig.data+off);return 1;
}
int t3_psx_body_sqrt(const t3_psx_body_tables_t* t,uint32_t n,uint32_t* out) {
    unsigned lz=0;uint32_t high,v,result;int shift;int32_t coefficient;
    if(!t || !out || !t->sqrt || t->sqrt_size!=T3_PSX_BODY_SQRT_BYTES || n>0x7FFFFFFFu)return 0;
    if(!n){*out=0;return 1;}
    high=n;while(!(high&0x80000000u)){++lz;high<<=1;}
    lz&=~1u;shift=24-(int)lz;
    v=shift>=0?n>>(unsigned)shift:n<<(unsigned)-shift;
    if(v<64u || v>255u)return 0;
    coefficient=le16(t->sqrt+2*(v-64u));
    result=((uint32_t)coefficient<<((31u-lz)>>1))>>12;
    *out=result;return 1;
}
static int distance3(const t3_psx_body_tables_t* t,int32_t x,int32_t y,int32_t z,uint32_t* out) {
    return t3_psx_body_sqrt(t,(uint32_t)wrap((int64_t)x*x+(int64_t)y*y+(int64_t)z*z),out);
}
static int atan4096(const t3_psx_body_tables_t* t,int32_t x,int32_t z,int* out) {
    int64_t ax=mag(x),az=mag(z);uint32_t ratio;int angle;
    const uint8_t* lut=t->ground.atan;
    if(!lut || t->ground.atan_size!=1025u)return 0;
    if(!x && !z){*out=0;return 1;}
    /* The bounded arena keeps the original x<<10/z<<10 operations in range. */
    if(ax>2097151 || az>2097151)return 0;
    if(ax>=az) {
        ratio=(uint32_t)((az*1024)/ax);angle=lut[ratio]+(int)(ratio>>1);
    } else {
        ratio=(uint32_t)((ax*1024)/az);angle=1024-lut[ratio]-(int)(ratio>>1);
    }
    if(x<0)angle=2048-angle;
    if(z<0)angle=4096-angle;
    *out=s16(angle);return 1;
}
static int solve(t3_psx_body_actor_t a[2],const t3_psx_body_tables_t* t) {
    int overlap=0,charging=0,axis[2];uint32_t moved[2];int32_t cs[2],sn[2];
    int64_t share[2];int toward;
    for(unsigned i=0;i<2;++i)a[i].push=(t3_vec3i_t){0};
    for(unsigned i=0;i<8;++i)for(unsigned j=0;j<8;++j) {
        const t3_psx_body_sphere_t *p=&a[0].spheres[i],*q=&a[1].spheres[j];
        int reach=p->radius+q->radius;uint32_t d;
        int32_t x=s16(wrap((int64_t)q->center.x-p->center.x));
        int32_t y=s16(wrap((int64_t)q->center.y-p->center.y));
        int32_t z=s16(wrap((int64_t)q->center.z-p->center.z));
        if(!p->radius || !q->radius)continue;
        if(mag(x)<(reach&65535) && mag(y)<(reach&65535) && mag(z)<(reach&65535)) {
            if(!distance3(t,x,y,z,&d))return 0;
            if(overlap<s16(reach-(int32_t)d))overlap=s16(reach-(int32_t)d);
        }
    }
    if(a[0].active_first && a[1].active_first) {
        int32_t fx[2],fz[2],gx,gz;
        if(overlap<1 && mag(wrap((int64_t)a[0].root.y-a[1].root.y))>299)return 1;
        for(unsigned i=0;i<2;++i) {
            int angle=1024-s16(a[i].facing)/16;
            if(!trig(t,angle,1,&cs[i]) || !trig(t,angle,0,&sn[i]))return 0;
            fx[i]=wrap((int64_t)cs[i]*200)/4096;fz[i]=wrap((int64_t)sn[i]*200)/4096;
        }
        gx=wrap((int64_t)a[1].root.x+fx[1]-a[0].root.x-fx[0]);
        gz=wrap((int64_t)a[1].root.z+fz[1]-a[0].root.z-fz[0]);
        if(mag(gx)<500 && mag(gz)<500 &&
           wrap((int64_t)wrap((int64_t)a[1].anchor.x-a[0].anchor.x)*gx+
                (int64_t)wrap((int64_t)a[1].anchor.z-a[0].anchor.z)*gz)<0) {
            charging=1;if(overlap<400)overlap=400;
        }
    }
    if(overlap<=0)return 1;
    for(unsigned i=0;i<2;++i) {
        if(!distance3(t,s16(wrap((int64_t)a[i].root.x-a[i].previous_root.x)),
                        s16(wrap((int64_t)a[i].root.y-a[i].previous_root.y)),
                        s16(wrap((int64_t)a[i].root.z-a[i].previous_root.z)),&moved[i]))return 0;
        moved[i]&=65535u;
    }
    if(!atan4096(t,wrap((int64_t)a[1].root.x-a[0].root.x),wrap((int64_t)a[1].root.z-a[0].root.z),&toward))return 0;
    for(unsigned i=0;i<2;++i) {
        unsigned off;int angle=1024-s16(a[i].facing)/16;
        off=(unsigned)(angle-(toward+(i?2048:0)))&4095u;
        if(off>2047u)off=4096u-off;
        if(off>1024u)angle=3072-s16(a[i].facing)/16;
        axis[i]=angle&4095;
        if(!trig(t,axis[i],1,&cs[i]) || !trig(t,axis[i],0,&sn[i]))return 0;
    }
    {
        uint32_t w0=moved[0],w1=moved[1];if(!w0 && !w1)w0=w1=1;
        share[0]=wrap((int64_t)overlap*w0)/(int64_t)(w0+w1);
        share[1]=wrap((int64_t)overlap*w1)/(int64_t)(w0+w1);
    }
    for(unsigned i=0;i<2;++i) {
        unsigned j=1u-i;
        int32_t x=wrap((int64_t)wrap((int64_t)cs[j]*moved[j])/4096-wrap((int64_t)cs[i]*share[i])/4096);
        int32_t z=wrap((int64_t)wrap((int64_t)sn[j]*moved[j])/4096-wrap((int64_t)sn[i]*share[i])/4096);
        a[i].push.x=charging?x:half(x);a[i].push.z=charging?z:half(z);
    }
    return 1;
}
int t3_psx_body_separate(t3_psx_body_actor_t actors[2],uint32_t pair_distance,const t3_psx_body_tables_t* t) {
    t3_psx_body_actor_t n[2];
    if(!actors || !t)return 0;
    n[0]=actors[0];n[1]=actors[1];
    if(pair_distance<=3000u && n[0].active && n[1].active &&
       n[0].invulnerable!=1 && n[1].invulnerable!=1 && (!n[0].no_push || !n[1].no_push)) {
        if(!solve(n,t))return 0;
        for(unsigned i=0;i<2;++i) {
            if(n[i].push.x || n[i].push.y || n[i].push.z) {
                n[i].contact=1;n[i].contact_with|=(uint8_t)(1u<<(1u-i));
                n[i].anchor.x=wrap((int64_t)n[i].anchor.x+n[i].push.x);
                n[i].anchor.y=wrap((int64_t)n[i].anchor.y+n[i].push.y);
                n[i].anchor.z=wrap((int64_t)n[i].anchor.z+n[i].push.z);
                n[i].root.x=wrap((int64_t)n[i].root.x+n[i].push.x);
                n[i].root.y=wrap((int64_t)n[i].root.y+n[i].push.y);
                n[i].root.z=wrap((int64_t)n[i].root.z+n[i].push.z);
            }
        }
    }
    actors[0]=n[0];actors[1]=n[1];return 1;
}
static int32_t excess(int32_t v) {
    int64_t over=mag(v)-300000;
    return over>0?(int32_t)(v>0?-over:over):0;
}
static int32_t clamp(int32_t v,int32_t ramp) {return mag(v)>ramp?(v<1?-ramp:ramp):v;}
int t3_psx_arena_bounds(t3_psx_body_actor_t* actor,const t3_psx_body_actor_t* opp,const t3_psx_body_tables_t* t) {
    t3_psx_body_actor_t n;int32_t dx,dz;
    if(!actor || !opp || !t || actor->ramp<0)return 0;
    n=*actor;dx=excess(n.root.x);dz=excess(n.root.z);
    if(n.distance>0x1CFFu) {
        int32_t x=wrap((int64_t)opp->placed.x-n.root.x),z=wrap((int64_t)opp->placed.z-n.root.z);
        uint32_t d;
        if(!distance3(t,x,0,z,&d))return 0;
        if(d<=0x1E00u) {
            if(n.distance<=d){dx=wrap((int64_t)n.placed.x-n.root.x);dz=wrap((int64_t)n.placed.z-n.root.z);}
        } else if(!t3_psx_polar(&t->ground,(int32_t)d-0x1E00u,n.target_dir,&dx,&dz))return 0;
    }
    dx=clamp(dx,n.ramp);dz=clamp(dz,n.ramp);
    n.anchor.x=wrap((int64_t)n.anchor.x+dx);n.anchor.z=wrap((int64_t)n.anchor.z+dz);
    n.root.x=wrap((int64_t)n.root.x+dx);n.root.z=wrap((int64_t)n.root.z+dz);
    *actor=n;return 1;
}

int t3_psx_body_shapes(t3_psx_body_actor_t* a,const t3_psx_joint_t parts[T3_PSX_DRAW_PARTS],
                        uint32_t mask,const uint8_t tables[48],const t3_move_record_t* m,
                        int16_t frame,uint8_t in_air,uint8_t was_hit) {
    t3_psx_body_sphere_t result[8];int profile;
    if(!a || !parts || !tables || !m)return 0;
    profile=m->body_profile;
    for(unsigned k=0;k<8;++k) {
        uint32_t joint=(uint32_t)tables[4*k]|((uint32_t)tables[4*k+1]<<8)|
            ((uint32_t)tables[4*k+2]<<16)|((uint32_t)tables[4*k+3]<<24);
        int32_t radius=le16(tables+32+2*k),r0=le16(tables+32),r3=le16(tables+38);
        int zero=0;
        if(joint>=T3_PSX_DRAW_PARTS || !(mask&t3_psx_bit(joint)) || radius<0)return 0;
        switch(profile&15) {
        case 0: {
            int drop=!(profile&0x1000) || !m->active_first ||
                (m->active_first-8<frame && frame<m->active_last);
            if(drop && ((uint32_t)profile&t3_psx_bit(k+4)))radius=0;
            break;
        }
        case 1:radius=k==0?r0:k==3?r3:1;break;
        case 2:radius=1;break;
        case 3:radius/=2;break;
        case 4:radius=0;break;
        case 5:radius=k==0?r0/2:k==3?r3/2:1;break;
        case 6:radius=k==0?r0+60:k>=4?1:radius;break;
        case 7:radius+=100;break;
        default:break;
        }
        if(!m->active_first) {
            if(!in_air || !was_hit)zero=(m->flags&0x20000u) && k>=4;
            else zero=k>=6;
        } else zero=k==1 || k==2;
        result[k]=(t3_psx_body_sphere_t){parts[joint].origin,zero?0u:(uint16_t)s16(radius)};
    }
    for(unsigned k=0;k<8;++k)a->spheres[k]=result[k];
    return 1;
}
