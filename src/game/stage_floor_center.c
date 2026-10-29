/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * C adaptation of FloorDraw and its ground intersection helper, from pinned
 * Tekken3Remake stages.md and resident-functions.md; see NOTICE.md. */
#include "stage_floor_center.h"

static int32_t sine(const t3_psx_trig_t* t,int32_t angle) {
    unsigned i=((unsigned)angle&4095u)*2u;
    uint16_t v=(uint16_t)t->data[i]|((uint16_t)t->data[i+1]<<8);
    return v<0x8000u?(int32_t)v:(int32_t)v-65536;
}
static int32_t wrap32(int64_t x) {
    uint32_t n=(uint32_t)x;
    return n<=0x7fffffffu?(int32_t)n:(int32_t)((int64_t)n-0x100000000LL);
}
static int32_t mul12(int32_t a,int32_t b) {return wrap32((int64_t)a*b)/4096;}
/* Negative wrapped input makes PsyQ SquareRoot0 read before its table.
 * The caller supplies those bytes from the selected private executable. */
static int floor_sqrt(const t3_psx_body_tables_t* body,const uint8_t* before,uint32_t n,uint32_t* out) {
    if(!(n&0x80000000u))return t3_psx_body_sqrt(body,n,out);
    if(!before)return 0;
    uint32_t scan=~n;unsigned leading=0;
    if(!scan)leading=32;
    else while(!(scan&0x80000000u)){++leading;scan<<=1;}
    leading&=~1u;
    int shift=24-(int)leading;
    int32_t value=shift>=0?-1-(int32_t)((~n)>>(unsigned)shift):wrap32((int64_t)(int32_t)n*(1u<<(unsigned)-shift));
    int index=640+2*(value-64);
    if(index<0 || index>638)return 0;
    uint16_t raw=(uint16_t)before[index]|((uint16_t)before[index+1]<<8);
    int32_t coefficient=raw<0x8000u?(int32_t)raw:(int32_t)raw-65536;
    *out=((uint32_t)coefficient<<((31u-leading)>>1&31u))>>12;
    return 1;
}

int t3_stage_floor_center(const t3_psx_camera_view_t* v,int32_t mx,int32_t mz,
    const t3_psx_trig_t* trig,const t3_psx_body_tables_t* body,const uint8_t before_sqrt[640],int32_t out[2]) {
    if(!v || !trig || !trig->data || trig->size!=T3_PSX_TRIG_BYTES || !body || !out)return -1;
    const int32_t pitch=v->pitch&4095;
    const int32_t sp=sine(trig,pitch+239),cp=sine(trig,pitch+239+1024);
    if(sp<=0)return 0;
    if(pitch && !(pitch&2048) && v->y< -1720) {
        int32_t ray=(int32_t)((int64_t)(v->y<0?-(int64_t)v->y:v->y)*cp/sp);
        uint32_t square=(uint32_t)((uint64_t)((int64_t)ray*ray)+(uint64_t)((int64_t)v->y*v->y));
        uint32_t root;
        if(!floor_sqrt(body,before_sqrt,square,&root))return -1;
        int32_t reach=(int32_t)((int64_t)root*184/536);
        int32_t sy=sine(trig,v->yaw+2048),cy=sine(trig,v->yaw+1024);
        int32_t ix=v->x+mul12(ray,sy),iz=v->z+mul12(ray,cy);
        int32_t margin=(int32_t)((int64_t)(-1230-v->y)*((1800<<12)/300)/4096);
        int32_t margin_x=margin<9000?margin:9000,margin_z=margin<9000?margin:9000;
        unsigned quadrant=((unsigned)(v->yaw+1536)&4095u)>>10;
        unsigned side=((unsigned)(v->yaw+1536)&4095u)>>9&1u;
        if(quadrant==0 || quadrant==2) {
            if(!sy)return -1;
            int32_t offset=mul12(reach,cy);
            if(side)offset=-offset;
            ix=quadrant==0?ix+offset-900+margin_x:ix+offset+900-margin_x;
            iz=wrap32((int64_t)(wrap32((int64_t)cy*wrap32((int64_t)ix-v->x))/sy)+v->z);
        } else {
            if(!cy)return -1;
            int32_t offset=mul12(reach,sy);
            if(!side)offset=-offset;
            iz=quadrant==1?iz+offset-900+margin_z:iz+offset+900-margin_z;
            ix=wrap32((int64_t)(wrap32((int64_t)sy*wrap32((int64_t)iz-v->z))/cy)+v->x);
        }
        out[0]=ix;out[1]=iz;
    } else {
        int32_t signed_pitch=(pitch&2048)?pitch-4096:pitch;
        if(signed_pitch< -37) {
            mx-=mul12(1800,sine(trig,v->yaw+2048));
            mz-=mul12(1800,sine(trig,v->yaw+1024));
        }
        out[0]=mx;out[1]=mz;
    }
    return 1;
}
