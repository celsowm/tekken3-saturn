/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of force.ovl FUN_800B545C / FUN_800B5504; see psx_force_camera.h. */
#include "psx_force_camera.h"

static int32_t s16at(const uint8_t* t,unsigned off){
    const uint16_t u=(uint16_t)(t[off]|(t[off+1]<<8));
    return u<32768u?(int32_t)u:(int32_t)u-65536;
}
static int32_t mul(int32_t a,int32_t b){return (int32_t)((uint32_t)a*(uint32_t)b);}   /* mult, mflo */
static int32_t sub(int32_t a,int32_t b){return (int32_t)((uint32_t)a-(uint32_t)b);}
static int32_t add(int32_t a,int32_t b){return (int32_t)((uint32_t)a+(uint32_t)b);}
/* The R3000's div: a zero divisor gives -1 (non-negative dividend) or 1; INT_MIN / -1 gives INT_MIN. */
static int32_t divide(int32_t a,int32_t b){
    if(b==0)return a>=0?-1:1;
    if(a==INT32_MIN&&b==-1)return a;
    return a/b;
}
/* The product of a value and a constant, then rounded up to the next multiple towards zero (+0xFFF when negative). */
static int32_t round12(int32_t v){return v<0?add(v,0xFFF):v;}

int32_t t3_force_camera_setup(t3_force_camera_t* c,uint32_t player,int level4,int32_t* start_x){
    c->right=1;c->y=-0xDAC;c->z=-0x1B58;c->mode=0;c->x=0;c->pitch=0x2AAA;c->yaw=0;c->h=500;
    c->pitch_target=0x2AAA;c->target_x=0;
    c->player=player;
    *start_x=c->x;
    return level4?0x82:0;
}

void t3_force_camera_frame(t3_force_camera_t* c,const uint8_t* trig,int32_t roll,int32_t player_x,int32_t player_z){
    int32_t t1=sub(0,c->pitch);
    if(t1<0)t1=add(t1,0x3F);
    const unsigned r=(unsigned)(-roll&0xFFF)*2u;
    const int32_t cr=s16at(trig,r+0x800),sr=s16at(trig,r);
    const int32_t dx=sub(player_x,c->x);
    const int32_t ny=sub(0,c->y);
    int32_t u=sub(mul(cr,dx),mul(sr,ny));
    const unsigned p=(unsigned)(t1>>5)&0x1FFEu;
    const int32_t sp=s16at(trig,p),cp=s16at(trig,p+0x800);
    const int32_t dz=sub(player_z,c->z);
    u=round12(u);
    int32_t w=add(mul(sr,dx),mul(cr,ny));
    const int32_t u12=u>>12;
    w=round12(w);
    const int32_t w12=w>>12;
    int32_t depth=sub(mul(cp,dz),mul(sp,w12));
    depth=round12(depth);
    int32_t up=add(mul(sp,dz),mul(cp,w12));
    const int32_t d12=depth>>12;
    up=round12(up);
    const int32_t up12=up>>12;
    const int32_t bottom=divide(divide(mul(up12,2500),d12),3);
    const int32_t screen=divide(mul(u12,500),d12);
    const int32_t mode=c->mode;
    c->target_x=c->x;
    if(mode==0||mode==2){
        if(mode==2&&screen<c->left)c->target_x=sub(c->x,mul(sub(c->left,screen),5));
        if(c->right<screen)c->target_x=add(c->x,mul(sub(screen,c->right),5));
    }
    if(mode!=1){
        if(screen>0&&c->x<c->target_x)c->x=c->target_x;
        else if(screen<0&&c->target_x<c->x)c->x=c->target_x;
    }
    if(mode==2){
        if(c->x<c->min)c->x=c->min;
        if(c->max<c->x)c->x=c->max;
    }
    if(bottom>=0xD5)c->pitch_target=add(c->pitch,0x2D8);
    else if(bottom<0xBC)c->pitch_target=sub(c->pitch,0x2D8);
    if(c->pitch<c->pitch_target||c->pitch_target<c->pitch){
        int32_t d=sub(c->pitch,c->pitch_target);
        if(d<0)d=add(d,7);
        c->pitch=sub(c->pitch,d>>3);
    }
    if(c->pitch<0x2AAA)c->pitch=0x2AAA;
    else if(0x10000<c->pitch)c->pitch=0x10000;
}

/* The screen x the camera's own follow math gives a world point (the player's follow reads root x and z at
 * height 0): the fighters' stored screen x (+0x1278) is the same projection of their joint 11. */
int32_t t3_force_camera_screen_x(const t3_force_camera_t* c,const uint8_t* trig,int32_t roll,
                                 int32_t px,int32_t py,int32_t pz){
    int32_t t1=sub(0,c->pitch);
    if(t1<0)t1=add(t1,0x3F);
    const unsigned r=(unsigned)(-roll&0xFFF)*2u;
    const int32_t cr=s16at(trig,r+0x800),sr=s16at(trig,r);
    const int32_t dx=sub(px,c->x),ny=sub(py,c->y);
    const int32_t u12=round12(sub(mul(cr,dx),mul(sr,ny)))>>12;
    const unsigned p=(unsigned)(t1>>5)&0x1FFEu;
    const int32_t sp=s16at(trig,p),cp=s16at(trig,p+0x800);
    const int32_t w12=round12(add(mul(sr,dx),mul(cr,ny)))>>12;
    const int32_t d12=round12(sub(mul(cp,sub(pz,c->z)),mul(sp,w12)))>>12;
    return divide(mul(u12,500),d12);
}
