/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of SegmentHitsCylinder and baked attack _place from pinned
 * Tekken3Remake tools/research/fight_math.py and fight_sim.py.
 * Changes: typed caller-owned data, bounded input, atomic publication.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_exact_contact.h"
static int32_t w(int64_t n) {
    uint32_t u=(uint32_t)(uint64_t)n;
    return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);
}
static int16_t s16(uint16_t u) {return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);}
static int64_t mag(int64_t n){return n<0?-n:n;}
static int bounded(t3_vec3i_t p) {
    return mag(p.x)<=T3_PSX_UNIT_LIMIT && mag(p.y)<=T3_PSX_UNIT_LIMIT && mag(p.z)<=T3_PSX_UNIT_LIMIT;
}
static int32_t div32(int32_t a,int32_t b){return w((int64_t)a/b);}
static int32_t square(int32_t a,int32_t b){return w((int64_t)a*a+(int64_t)b*b);}
static int32_t lerp(int32_t origin,int32_t t,int32_t delta){return w((int64_t)origin+w((int64_t)t*delta)/4096);}
int t3_psx_segment_hits_cylinder(const t3_world_segment_t* seg,const t3_hurt_cylinder_t* c) {
    int32_t x0,y0,z0,x1,y1,z1,cx,cy,cz,r,sx,sz,ex,ez,dx,dy,dz,vx,vz,px,pz,len,proj,d2;
    if(!seg || !c || !bounded(seg->start) || !bounded(seg->end) || !bounded(c->center))return -1;
    x0=seg->start.x;y0=seg->start.y;z0=seg->start.z;
    x1=seg->end.x;y1=seg->end.y;z1=seg->end.z;
    cx=c->center.x;cy=c->center.y;cz=c->center.z;r=s16(c->radius);
    if(mag((int64_t)x1-x0)/2+r<mag((int64_t)cx-((int64_t)x0+x1)/2) ||
       mag((int64_t)z1-z0)/2+r<mag((int64_t)cz-((int64_t)z0+z1)/2) ||
       mag((int64_t)y1-y0)/2+r<mag((int64_t)cy-((int64_t)y0+y1)/2))return 0;
    sx=x0;sz=z0;ex=x1;ez=z1;dx=x1-x0;dy=y1-y0;dz=z1-z0;
    if(dy) {
        int32_t lo=cy-r,hi=cy+r,t;
        if(y1<y0) {
            if(hi<y0){t=div32(w((int64_t)(hi-y0)*4096),dy);sx=lerp(x0,t,dx);sz=lerp(z0,t,dz);}
            if(y1<lo){t=div32(w((int64_t)(lo-y0)*4096),dy);ex=lerp(x0,t,dx);ez=lerp(z0,t,dz);}
        } else {
            if(hi<y1){t=div32(w((int64_t)(hi-y0)*4096),dy);ex=lerp(x0,t,dx);ez=lerp(z0,t,dz);}
            if(y0<lo){t=div32(w((int64_t)(lo-y0)*4096),dy);sx=lerp(x0,t,dx);sz=lerp(z0,t,dz);}
        }
    }
    vx=w((int64_t)ex-sx);vz=w((int64_t)ez-sz);
    px=w((int64_t)sx-cx);pz=w((int64_t)sz-cz);
    len=square(vx,vz);proj=w(-((int64_t)vx*px+(int64_t)vz*pz));
    if(!len || proj<0)d2=square(px,pz);
    else if(len<proj)d2=square(w((int64_t)ex-cx),w((int64_t)ez-cz));
    else {
        px=w((int64_t)px+div32(w((int64_t)vx*proj),len));
        pz=w((int64_t)pz+div32(w((int64_t)vz*proj),len));
        d2=square(px,pz);
    }
    return d2<=r*r;
}
static int32_t trig(const t3_psx_trig_t* t,int angle,unsigned cosine) {
    int negative=-angle;
    int index=(negative/16)&4095;
    unsigned off=2u*(unsigned)index+(cosine?0x800u:0u);
    return s16((uint16_t)(t->data[off]|((uint16_t)t->data[off+1]<<8)));
}
static int place(t3_attack_point_t p,t3_vec3i_t origin,t3_vec3i_t root,
                  int32_t sn,int32_t cs,int32_t ds,int32_t dc,int delta,t3_vec3i_t* out) {
    int32_t ox=origin.x,oz=origin.z;
    if(delta) {
        int32_t dx=w((int64_t)root.x-ox),dz=w((int64_t)root.z-oz);
        ox=w((int64_t)root.x-w((int64_t)dx*dc-(int64_t)dz*ds)/4096);
        oz=w((int64_t)root.z-w((int64_t)dx*ds+(int64_t)dz*dc)/4096);
    }
    out->x=w((int64_t)ox+w((int64_t)p.x*cs)/4096-w((int64_t)p.z*sn)/4096);
    out->y=w((int64_t)origin.y+p.y);
    out->z=w((int64_t)oz+w((int64_t)p.x*sn)/4096+w((int64_t)p.z*cs)/4096);
    return bounded(*out);
}
int t3_psx_attack_place(const t3_attack_sample_t* s,unsigned frame,
                         t3_vec3i_t anchor,t3_vec3i_t previous_anchor,t3_vec3i_t root,
                         uint16_t heading,uint16_t facing,const t3_psx_trig_t* t,
                         const t3_world_attack_t* previous,t3_world_attack_t* out) {
    t3_world_attack_t result={0};int h=s16(heading),delta=h-s16(facing);
    int32_t sn,cs,ds,dc;
    if(!s || !out || !t || !t->data || t->size!=T3_PSX_TRIG_BYTES ||
       s->segment_count>2u || !bounded(anchor) || !bounded(previous_anchor) || !bounded(root))return 0;
    sn=trig(t,h,0);cs=trig(t,h,1);ds=trig(t,delta,0);dc=trig(t,delta,1);
    result.segment_count=s->segment_count;
    for(unsigned i=0;i<s->segment_count;++i) {
        if(s->segments[i].previous_start && frame) {
            if(!previous || previous->segment_count<=i || !bounded(previous->segments[i].end))return 0;
            result.segments[i].start=previous->segments[i].end;
        } else if(!place(s->segments[i].start,s->segments[i].previous_start?previous_anchor:anchor,
                          root,sn,cs,ds,dc,delta,&result.segments[i].start))return 0;
        if(!place(s->segments[i].end,anchor,root,sn,cs,ds,dc,delta,&result.segments[i].end))return 0;
        result.segments[i].previous_start=s->segments[i].previous_start;
    }
    *out=result;return 1;
}
int t3_psx_attack_contact(const t3_world_attack_t* a,const t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES],unsigned* segment) {
    if(!a || !zones || a->segment_count>2u)return -2;
    for(unsigned i=0;i<a->segment_count;++i)for(unsigned j=0;j<T3_PSX_HURT_ZONES;++j) {
        int hit=t3_psx_segment_hits_cylinder(&a->segments[i],&zones[j]);
        if(hit<0)return -2;
        if(hit){if(segment)*segment=i;return (int)j;}
    }
    return -1;
}
