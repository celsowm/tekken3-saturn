/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of CameraDirector's reset, round intro, fight framing, throw
 * presets, camera streams, hit cameras, round-end and replay cameras, the shake and blending in pinned Tekken3Remake remake/core/camera/camera_director.gd,
 * camera_math.gd, view_matrix.gd and backdrop_turn.gd (rules verified by its
 * tools/research/camera_sim.py). Changes: caller-owned state and fighter
 * inputs, a switch for original bugs 1 and 2, explicit 32-bit wrapping.
 * Attract,
 * Tekken Ball and Tekken Force cameras are outside this path. */
#include "psx_camera.h"
#include <stddef.h>

#define ONE 4096
#define DISTANCE 0x158D
#define MIN_DISTANCE 800
#define MARGIN 0x96
#define SPREAD_LIMIT 0x119
#define SWING_CAP 0xDDD0
#define SWING_KICK 0x600
#define INTRO_ROUND1 0x78
#define INTRO_LATER 0x35
#define TRACK_ROOT 0x20000000u
#define CAMERA_BYTE_SKIP 0x53
#define BANK_YOSHIMITSU 4
#define BANK_PAUL 0
#define CAMERA_BYTE_CUT 9
#define CAMERA_BYTE_KEEP 0x25
#define CAMERA_BYTE_STREAM 0x63
#define FIRST_BANK_ID 0x2B
#define REJECTED_COUNT 0x12
#define TRANSITION_THROW 0x21
#define STREAM_H 0x226
#define HIT_LIST_BASE 300
#define HIT_DISTANCE_H 0x2EE
#define HIT_KO_DAMAGE 0x1B
#define BANK_KUMA 11
#define SQRT_BYTES 384u
#define SHAKE_END (-128)
#define SCREEN_OFFSET_X 192   /* GTE OFX/OFY of display mode 0 (FUN_800B0D08) */
#define SCREEN_OFFSET_Y 240
#define STREAM_WEIGHT_FULL 0x140
#define ASPECT 0x1BD2

typedef t3_psx_camera_t cam_t;
typedef t3_psx_camera_tables_t tab_t;
typedef t3_psx_camera_fighter_t fig_t;

static int32_t w32(int64_t v) {return (int32_t)(uint32_t)(uint64_t)v;}
/* ApplyMatrixLV: 32-bit components split into 15-bit halves. */
static void apply_lv(const int16_t m[3][3],const int32_t e[3],int32_t out[3]) {
    int i,k;
    for(i=0;i<3;i++) {
        int64_t hi=0,lo=0;
        for(k=0;k<3;k++) {
            int32_t comp=e[k],h,l;
            if(comp>=0){h=comp>>15;l=comp&0x7FFF;}
            else {h=-(int32_t)((uint32_t)-(int64_t)comp>>15);l=-(int32_t)((uint32_t)-(int64_t)comp&0x7FFF);}
            hi+=(int64_t)m[i][k]*h;lo+=(int64_t)m[i][k]*l;
        }
        out[i]=w32((lo>>12)+hi*8);
    }
}
static int16_t s16(int64_t v) {uint16_t u=(uint16_t)(uint64_t)v;return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);}
static int64_t t12(int64_t v) {return v<0?-((-v)>>12):v>>12;}
static int32_t div_trunc(int64_t a64,int64_t b64) {
    int32_t a=w32(a64),b=w32(b64);   /* the game divides 32-bit values */
    return a==INT32_MIN && b==-1?a:a/b;
}
/* Arithmetic shift rounding toward zero. */
static int32_t sar(int64_t v,int n) {return w32(v<0?-((-v)>>n):v>>n);}
static int32_t half(int32_t v) {return (v+(v<0?1:0))>>1;}
static int32_t div8(int32_t v) {return (v<0?v+7:v)>>3;}
static int32_t sar6(int32_t v) {return (v<0?v+0x3F:v)>>6;}
static int32_t idx18(int64_t a) {int32_t v=w32(a);return ((v<0?v+0x3F:v)>>6)&0xFFF;}
static int32_t s18(int64_t v) {int32_t u=(int32_t)((uint64_t)v&0x3FFFFu);return u&0x20000?u-0x40000:u;}
static int32_t sign(int32_t v) {return (v>0)-(v<0);}
static int32_t iabs(int32_t v) {return v<0?-v:v;}
static int32_t sn(const tab_t* t,int32_t i) {
    size_t o=2u*((uint32_t)i&0xFFFu);
    return s16(t->trig->data[o]|((uint16_t)t->trig->data[o+1u]<<8));
}
static int32_t cs(const tab_t* t,int32_t i) {return sn(t,(i&0xFFF)+1024);}

/* C division; a zero divisor gives the R3000 quotient when faithful (bug 2),
 * otherwise it counts as 1. */
static int32_t cdiv(const cam_t* c,int64_t a,int64_t b) {
    int32_t x=w32(a),y=w32(b);
    if(y==0)return c->faithful?(x>=0?-1:1):x;
    if(x==INT32_MIN && y==-1)return x;
    return x/y;
}
/* ISqrt (0x8002A578). */
static int32_t isqrt(uint32_t n) {
    uint32_t guess=1,rest=n,prev;
    if(!n)return 0;
    while(guess<rest){guess<<=1;rest>>=1;}
    for(;;){prev=guess;guess=(n/prev+prev)>>1;if(guess>=prev)return (int32_t)prev;}
}
static int32_t octant(const tab_t* t,int32_t i) {return t->atan[i]+(i>>1);}
/* Atan2Units4096 (FUN_8004B634): the angle of (x, z) in 4096 units. */
static int32_t atan4096(const tab_t* t,int64_t x64,int64_t z64) {
    int32_t x=w32(x64),z=w32(z64),r;
    if(!x && !z)return 0;
    if(x<0) {
        if(z<0) r=-(int64_t)x<-(int64_t)z?0xC00-octant(t,div_trunc((int64_t)w32((int64_t)x*1024),z))
                                         :octant(t,div_trunc((int64_t)w32((int64_t)z*1024),x))+0x800;
        else r=-(int64_t)x<z?octant(t,div_trunc((int64_t)w32((int64_t)x*-0x400),z))+0x400
                            :0x800-octant(t,div_trunc((int64_t)w32((int64_t)z*1024),-(int64_t)x));
    } else if(z<0) {
        r=x<-(int64_t)z?octant(t,div_trunc((int64_t)w32((int64_t)x*1024),-(int64_t)z))+0xC00
                       :0x1000-octant(t,div_trunc((int64_t)w32((int64_t)z*-0x400),x));
    } else r=x<z?0x400-octant(t,div_trunc((int64_t)w32((int64_t)x*1024),z))
                :octant(t,div_trunc((int64_t)w32((int64_t)z*1024),x));
    return s16(r);
}

int t3_psx_camera_valid(const tab_t* t) {
    int i,k,r;
    if(!t || !t->trig || !t->trig->data || t->trig->size!=T3_PSX_TRIG_BYTES || !t->atan || !t->choices ||
       !t->sqrt || t->sqrt_size!=SQRT_BYTES)return 0;
    for(i=0;i<T3_PSX_CAMERA_HIT_ROWS;i++) {
        int list=t->hit_rows[i][2]-HIT_LIST_BASE;
        if(list<0 || list>=T3_PSX_CAMERA_HIT_LISTS)return 0;
    }
    for(i=0;i<T3_PSX_CAMERA_HIT_LISTS;i++) {
        if(t->hit_record_count[i]>T3_PSX_CAMERA_HIT_RECORDS)return 0;
        for(k=0;k<t->hit_record_count[i];k++)for(r=0;r<4;r++)
            if((uint16_t)t->hit_records[i][k].script[r]>=T3_PSX_CAMERA_HIT_SCRIPTS)return 0;
    }
    for(i=0;i<T3_PSX_CAMERA_SHAKES;i++) {
        for(k=0;k<T3_PSX_CAMERA_SHAKE_BYTES && t->shakes[i][k]!=SHAKE_END;k++){}
        if(k==T3_PSX_CAMERA_SHAKE_BYTES)return 0;
    }
    return 1;
}

void t3_psx_camera_reset(cam_t* c) {
    c->view.pitch=c->view.yaw=0;c->view.x=c->view.z=0;c->view.y=-0x4B0;
    __builtin_memset(c->weight,0,sizeof c->weight);__builtin_memset(c->prev_weight,0,sizeof c->prev_weight);
    c->weight[0]=ONE;c->state=T3_PSX_CINEMATIC_IDLE;
    c->wait=c->throw_player=c->count_total=c->count=c->hit_total=c->hit_count=c->skip_side=-1;
    c->stream_restart=0;
    c->enabled=1;c->intro_timer=0;
    c->cinematic=1;   /* every mode but demo, ball and force */
}

static int camera_byte(const fig_t* f) {return (int)(f->move_flags&0xFFu);}

/* CameraTrackFighters: the targets the framing reads. */
static void track(cam_t* c,const fig_t f[2],int force_root) {
    int k;int32_t* a=c->target[0];int32_t* b=c->target[1];
    if(f[0].throw_state || f[1].throw_state)force_root=1;
    for(k=0;k<2;k++) {
        int32_t* tg=c->target[k];
        __builtin_memcpy(c->prev_target[k],tg,sizeof c->prev_target[k]);
        if(c->skip_side!=k || (f[k].pose_frame&1)) {
            if(!(f[k].move_flags&TRACK_ROOT) && !f[k].throw_state && !force_root){tg[0]=f[k].pos_x;tg[2]=f[k].pos_z;}
            else {tg[0]=f[k].root_x;tg[2]=f[k].root_z;}
            tg[3]=f[k].screen_x;
            tg[1]=w32((int64_t)f[k].head_y-0x122);
            if(f[k].cur_slot==2 && f[k].bank_type==BANK_YOSHIMITSU)tg[1]=-0x6B8;
        }
    }
    if(a[0]==b[0] && a[2]==b[2]){b[0]=a[0]+1;b[2]=a[2]+1;a[0]-=1;a[2]-=1;}
}

static int32_t swing_limit(int b0,int b1) {
    static const int16_t limits[4][2]={{0x27,0x200},{0x28,0x180},{0x29,0x80},{0x2A,0x40}};
    int i;
    for(i=0;i<4;i++)if(b0==limits[i][0] || b1==limits[i][0])return limits[i][1];
    return -1;
}
/* The fight source's yaw swing: a kick when the camera falls behind the
 * fighters' axis, then a damped spring within the cap. Returns the offset. */
static int32_t swing(t3_psx_camera_source_state_t* st,const fig_t f[2],int32_t err,int32_t sg) {
    int32_t phase=st->phase,frames=st->frames,angle=st->angle,speed=st->speed,offset=0;
    if(phase==0) {
        frames=err<0xE38?0:s16(frames+1);
        if(frames>0){phase=1;frames=0;speed=w32((int64_t)speed+sg*SWING_KICK);}
    } else if(phase==1) {
        int32_t limit,hi=SWING_CAP,lo=-SWING_CAP;
        if(err<0x38E4) {
            angle=w32((int64_t)angle-sar(w32((int64_t)angle*0xE),7));
            speed=w32((int64_t)speed-half(speed)+sg*0x300);
            if(err<0xE39) {
                speed=w32((int64_t)speed-half(speed));
                frames=s16(frames+1);
                if(frames>=0x20){angle=0;phase=0;frames=0;}
            } else frames=0;
        } else speed=w32((int64_t)speed+sg*SWING_KICK);
        angle=w32((int64_t)angle+speed);
        limit=swing_limit(camera_byte(&f[0]),camera_byte(&f[1]));
        if(limit>=1){hi=sar((int64_t)limit*SWING_CAP,8);lo=sar(-(int64_t)limit*SWING_CAP,8);}
        if(hi<angle){angle=hi;speed=w32((int64_t)speed-SWING_KICK);}
        else if(angle<lo){angle=lo;speed=w32((int64_t)speed+SWING_KICK);}
        offset=sar(angle,6);
    }
    st->phase=(int16_t)phase;st->frames=(int16_t)frames;st->angle=angle;st->speed=speed;
    return offset;
}
/* The pitch target grows with the yaw error; the fight source follows it while it moves. */
static void pitch_follow(cam_t* c,const tab_t* t,t3_psx_camera_source_state_t* st,int32_t err) {
    t3_psx_camera_source_state_t* s0=&c->source_state[0];
    if(err<0x38E3)st->pitch_target=0;
    else {
        int32_t q=sar(div_trunc((int64_t)w32((int64_t)(err-0x38E3)*0x1000),0xC71D),8);
        st->pitch_target=(int32_t)t12(w32((int64_t)sn(t,q)*0x44440));
    }
    if(!s0->moved)s0->pitch_step=0;
    else {
        int32_t step=sar(w32((int64_t)s0->pitch_target-s0->pitch),4);
        if(step<-0x100)step=-0x100;
        if(step>0x100)step=0x100;
        s0->pitch_step=step;s0->pitch=w32((int64_t)s0->pitch+step);
    }
    s0->error=err;
}
/* CameraYawSpring (0x80064AF8): sets *restart_out and *yaw. */
static void yaw_spring(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr,
                       int restart,int src,int* restart_out,int32_t* yaw) {
    t3_psx_camera_source_state_t* st=&c->source_state[src];
    int32_t cam_yaw=c->source[src].yaw,err,sg;
    int32_t axis=atan4096(t,(int64_t)c->target[1][0]-c->target[0][0],(int64_t)c->target[1][2]-c->target[0][2]);
    if(f[1].screen_x<=f[0].screen_x)axis=(axis+0x800)&0xFFF;
    err=s18((int64_t)w32((int64_t)axis*0x40)-cam_yaw);
    sg=sign(err);err=iabs(err);
    if(restart) {
        st->angle=st->speed=st->pitch_target=st->pitch=st->pitch_step=0;
        st->cleared_a=st->cleared_b=0;
        *restart_out=restart;*yaw=w32((int64_t)axis*0x40);return;
    }
    *restart_out=0;
    if(src!=0 || err>0x1AAAA || fr->fighter_distance<200) {
        st->cleared_a=st->cleared_b=0;*yaw=cam_yaw;return;
    }
    {
        int32_t offset=swing(st,f,err,sg);
        pitch_follow(c,t,st,err);
        *yaw=w32((int64_t)cam_yaw+offset);
    }
}

/* One CameraFrameFighters call: targets in view space around the orbit
 * centre and the camera's offsets from it. */
typedef struct {
    int32_t pitch,yaw,distance,restart,src;
    int32_t sp,cp,sy,cy,mx,mz,cam_y;
    int32_t u[2],v[2],z[2],depth[3];
    int32_t e2,back,lateral,lift,scale;
} framing_t;

static void orbit_centre(const cam_t* c,const tab_t* t,framing_t* fr) {
    const t3_psx_camera_source_t* rec=&c->source[fr->src];
    int32_t ox,oz;
    fr->mx=cdiv(c,(int64_t)c->target[0][0]+c->target[1][0],2);
    fr->mz=cdiv(c,(int64_t)c->target[0][2]+c->target[1][2],2);
    if(!fr->restart) {
        int32_t i=idx18(s18((int64_t)fr->yaw-rec->yaw)),s_=sn(t,i),c_=cs(t,i);
        int32_t dx=w32((int64_t)rec->x-fr->mx),dz=w32((int64_t)rec->z-fr->mz);
        ox=(int32_t)t12((int64_t)c_*dx-(int64_t)s_*dz);
        oz=(int32_t)t12((int64_t)s_*dx+(int64_t)c_*dz);
    } else {
        int32_t i=idx18((int64_t)fr->yaw+0x30000);
        ox=(int32_t)t12((int64_t)fr->distance*cs(t,i));
        oz=(int32_t)t12((int64_t)fr->distance*sn(t,i));
    }
    fr->mx=w32((int64_t)fr->mx+ox);fr->mz=w32((int64_t)fr->mz+oz);
}
static void project_targets(const cam_t* c,framing_t* fr) {
    int k;int32_t m,up,e10,d;
    for(k=0;k<2;k++) {
        int32_t tx=w32((int64_t)c->target[k][0]-fr->mx),tz=w32((int64_t)c->target[k][2]-fr->mz);
        int32_t ty=w32((int64_t)c->target[k][1]-fr->cam_y);
        int32_t a1=(int32_t)t12((int64_t)fr->sy*tx+(int64_t)fr->cy*tz);
        fr->u[k]=(int32_t)t12((int64_t)fr->cy*tx-(int64_t)fr->sy*tz);
        fr->z[k]=(int32_t)t12((int64_t)fr->cp*a1-(int64_t)fr->sp*ty);
        fr->v[k]=(int32_t)t12((int64_t)fr->sp*a1+(int64_t)fr->cp*ty);
        fr->depth[k]=a1;
    }
    m=cdiv(c,(int64_t)fr->depth[0]+fr->depth[1],2);
    up=w32(-0x1C2-(int64_t)fr->cam_y);
    e10=(int32_t)t12((int64_t)fr->cp*m-(int64_t)fr->sp*up);
    fr->e2=(int32_t)t12(w32((int64_t)fr->sp*m+(int64_t)fr->cp*up));
    d=fr->distance-e10;
    fr->depth[2]=e10+d;fr->back=-d;
    fr->z[0]+=d;fr->z[1]+=d;
}
static int32_t settle(int32_t s) {
    if(s<0)return s-((s+0x1F)>>5);
    return s>0?s-(s>>3):0;
}
/* Back off until both targets fit across the screen; centre them sideways. */
static void fit_horizontal(cam_t* c,framing_t* fr) {
    t3_psx_camera_source_state_t* st=&c->source_state[fr->src];
    int lo=0,hi=1;int32_t left_px,edge,zr,right_px,backoff,h=c->h;
    if(fr->u[1]<=fr->u[0]) {
        left_px=cdiv(c,(int64_t)(fr->u[1]-MARGIN)*h,fr->z[1]);
        edge=fr->u[0]+MARGIN;fr->u[1]-=MARGIN;fr->u[0]=edge;zr=fr->z[0];lo=1;hi=0;
    } else {
        left_px=cdiv(c,(int64_t)(fr->u[0]-MARGIN)*h,fr->z[0]);
        edge=fr->u[1]+MARGIN;fr->u[0]-=MARGIN;fr->u[1]=edge;zr=fr->z[1];
    }
    right_px=cdiv(c,(int64_t)edge*h,zr);
    if(right_px-left_px<SPREAD_LIMIT) {
        int centred=0;
        if(!fr->restart) {
            int32_t a7=cdiv(c,(int64_t)fr->z[hi]*0x30,h),a13=cdiv(c,(int64_t)fr->z[lo]*-0x30,h);
            if(a7-(fr->u[hi]-fr->u[lo])<a13)centred=1;
            else {
                a13=fr->u[lo]-a13;
                if(left_px>-0x31){if(right_px>=0x31)fr->lateral+=div8(fr->u[hi]-a7);}
                else fr->lateral+=div8(a13);
            }
        } else centred=1;
        if(centred)fr->lateral=div8(fr->u[lo]+cdiv(c,(int64_t)(fr->u[hi]-fr->u[lo])*fr->z[lo],
                                                   (int64_t)fr->z[lo]+fr->z[hi]));
        if(fr->restart && centred){st->lateral=0;return;}
        backoff=settle(st->lateral);
    } else {
        int32_t e10b=cdiv(c,(int64_t)h*(fr->u[hi]-fr->u[lo])+(int64_t)(fr->z[hi]-fr->z[lo])*-0x8C,0x118);
        backoff=fr->z[lo]-e10b;
        if(!fr->restart) {
            int32_t s=st->lateral;
            if(s<backoff)backoff=s+((backoff-s)>>5);
            else backoff=s-div8(s-backoff);
        }
        fr->lateral=div8(fr->u[lo]-cdiv(c,(int64_t)e10b*-0x8C,h));
    }
    st->lateral=backoff;
    fr->back+=backoff;fr->depth[2]-=backoff;fr->z[0]-=backoff;fr->z[1]-=backoff;
}
/* Script cameras looking down lift with the target's height. */
static void script_lift(cam_t* c,framing_t* fr,int32_t height) {
    int32_t g=c->height_term,new_g=g;
    if(fr->src==T3_PSX_CAMERA_SCRIPT && fr->pitch>0) {
        int32_t lift=0;
        if(height>-1001)lift=cdiv(c,(int64_t)(height+1000)*0x170,0x280);
        if(!fr->restart) {
            if(g<lift && lift-g>0x1C)lift=g+0x1C;
            else if(lift<g && g-lift>0x1C)lift=g-0x1C;
        }
        new_g=lift;fr->lift=lift;
    }
    c->height_term=new_g;
}
/* Raise the camera so the higher target stays below the top of the screen. */
static int32_t fit_vertical(cam_t* c,framing_t* fr) {
    t3_psx_camera_source_state_t* st=&c->source_state[fr->src];
    int32_t top=100,ty_world=c->target[1][1],top_v=fr->v[1],top_z=fr->z[1],height,want,vs,h=c->h;
    if(cdiv(c,(int64_t)fr->v[0]*h,fr->z[0])<cdiv(c,(int64_t)fr->v[1]*h,fr->z[1])) {
        top_v=fr->v[0];top_z=fr->z[0];ty_world=c->target[0][1];
    }
    height=(int32_t)t12((int64_t)ty_world*fr->cp);
    if(fr->src==T3_PSX_CAMERA_SCRIPT && fr->pitch<0)top=0x80;
    script_lift(c,fr,height);
    fr->scale=(int32_t)t12((int64_t)fr->cp*(top-cdiv(c,(int64_t)h*0x1C2,fr->distance)));
    want=cdiv(c,(int64_t)w32((int64_t)w32((int64_t)(top_z-fr->depth[2])*-0x70)-w32((int64_t)h*(top_v-fr->e2))),
              (int64_t)fr->scale+0x70)-fr->depth[2];
    if(want<0)want=0;
    if(!fr->restart) {
        int32_t cur=st->depth;
        if(want<cur) {
            int32_t step=cur-want,big=step*16,q;
            if(step>0x4B0){step=0x4B0;big=0x4B00;}
            q=w32((int64_t)(big-step)*8);
            q=(q<0?q+0x7F:q)>>7;
            st->depth_step=q-step;want=cur+(q-step);
        } else if(want>cur) {
            int32_t step=want-cur<800?want-cur:800;
            st->depth_step=div8(step);want=cur+div8(step);
        }
    } else {want=0;st->depth_step=0;}
    st->depth=want;
    if(want>0){fr->back-=want;fr->depth[2]+=want;}
    vs=fr->scale;
    if(fr->src==T3_PSX_CAMERA_FIGHT) {
        int32_t q=cdiv(c,(int64_t)(int32_t)t12(w32((int64_t)want*fr->cp))*h,fr->distance);
        vs+=(q<0?q+0xF:q)>>4;
    }
    return fr->e2-cdiv(c,(int64_t)vs*fr->depth[2],h)+fr->lift;
}
static void place_source(cam_t* c,const tab_t* t,framing_t* fr,int32_t vertical) {
    t3_psx_camera_source_t* rec=&c->source[fr->src];
    int32_t cam_p=rec->pitch,cam_yw=rec->yaw;
    int32_t sp2=sn(t,idx18(fr->pitch)),cp2=cs(t,idx18(fr->pitch));
    int32_t sy2=sn(t,idx18(fr->yaw)),cy2=cs(t,idx18(fr->yaw));
    int32_t r2=(int32_t)t12((int64_t)fr->back*cp2-(int64_t)vertical*sp2);
    int32_t r12=(int32_t)t12((int64_t)fr->back*sp2+(int64_t)vertical*cp2);
    int32_t xa=w32((int64_t)fr->lateral*cy2+(int64_t)r2*-sy2);
    int32_t za=w32((int64_t)fr->lateral*sy2+(int64_t)r2*cy2);
    int32_t new_x=w32((int64_t)fr->mx+t12(xa)),new_z=w32((int64_t)fr->mz+t12(za));
    if(!fr->restart) {
        int32_t dx=w32((int64_t)new_x-rec->x),dz=w32((int64_t)new_z-rec->z);
        if(fr->src==T3_PSX_CAMERA_FIGHT)
            c->source_state[fr->src].moved=w32((int64_t)dx*dx+(int64_t)dz*dz)<0x3E9?0:1;
        cam_p=w32((int64_t)cam_p+s18((int64_t)fr->pitch-cam_p));
        cam_yw=w32((int64_t)cam_yw+s18((int64_t)fr->yaw-cam_yw));
    } else {cam_p=fr->pitch;cam_yw=fr->yaw;}
    rec->x=new_x;rec->y=w32((int64_t)fr->cam_y+r12);rec->z=new_z;rec->pitch=cam_p;rec->yaw=cam_yw;
}
/* CameraFrameFighters (0x800650F8). */
static void frame_fighters(cam_t* c,const tab_t* t,int32_t pitch,int32_t yaw,int restart,
                           int32_t distance,int src) {
    framing_t fr;int32_t vertical;
    __builtin_memset(&fr,0,sizeof fr);
    fr.pitch=pitch;fr.yaw=yaw;fr.distance=distance<0x321?MIN_DISTANCE:distance;
    fr.restart=restart;fr.src=src;fr.cam_y=c->source[src].y;
    fr.sp=sn(t,idx18(-(int64_t)pitch));fr.cp=cs(t,idx18(-(int64_t)pitch));
    fr.sy=sn(t,idx18(-(int64_t)yaw));fr.cy=cs(t,idx18(-(int64_t)yaw));
    orbit_centre(c,t,&fr);
    project_targets(c,&fr);
    fit_horizontal(c,&fr);
    vertical=fit_vertical(c,&fr);
    place_source(c,t,&fr,vertical);
}
/* CameraFight (0x8006636C). */
static void camera_fight(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr,int restart) {
    int r;int32_t yaw;
    yaw_spring(c,t,f,fr,restart,T3_PSX_CAMERA_FIGHT,&r,&yaw);
    frame_fighters(c,t,c->source_state[0].pitch,yaw,r,DISTANCE,T3_PSX_CAMERA_FIGHT);
}

/* FUN_800661C0. */
static void init_sources(cam_t* c,const fig_t f[2]) {
    int i;
    c->h=c->h_min=c->h_max=500;c->view.h=500;c->stream_rescale=1;
    track(c,f,0);track(c,f,0);
    __builtin_memset(c->source_state,0,sizeof c->source_state);
    for(i=0;i<2;i++) {
        t3_psx_camera_source_t* s=&c->source[i];
        s->x=0;s->y=-0x5AA;s->z=-0x1F40;s->pitch=s->yaw=0;s->h=c->h;
    }
}

static t3_psx_camera_source_t look_source(const tab_t* t,const int32_t eye[3],const int32_t target[3],
                                          int32_t plane,int32_t keep) {
    t3_psx_camera_source_t s;
    int32_t dx=w32((int64_t)target[0]-eye[0]),dz=w32((int64_t)target[2]-eye[2]);
    int32_t flat=isqrt((uint32_t)w32((int64_t)dx*dx+(int64_t)dz*dz));
    s.x=eye[0];s.y=eye[1];s.z=eye[2];
    s.pitch=(int32_t)s16(atan4096(t,flat,(int64_t)target[1]-eye[1]))*64;
    s.yaw=(int32_t)s16(atan4096(t,dz,-(int64_t)dx))*64;
    s.h=plane>0?plane:keep;
    return s;
}

static void use_source(cam_t* c,int i) {
    const t3_psx_camera_source_t r=c->source[i];
    c->source[T3_PSX_CAMERA_OUTPUT]=r;
    c->view.x=r.x;c->view.y=r.y;c->view.z=r.z;
    c->view.pitch=sar6(r.pitch);c->view.yaw=sar6(r.yaw);
    c->view.h=r.h<c->h_min?c->h_min:r.h>c->h_max?c->h_max:r.h;
}
static int32_t distance_to(const cam_t* c,const int32_t tg[4],const t3_psx_camera_source_t* r,int32_t z) {
    int32_t dx=w32((int64_t)tg[0]-r->x),dy=w32((int64_t)tg[1]-r->y),dz=w32((int64_t)z-r->z);
    (void)c;
    return isqrt((uint32_t)w32((int64_t)w32((int64_t)dx*dx)+w32((int64_t)dy*dy)+w32((int64_t)dz*dz)));
}
/* CameraLookPoint (0x80063FE8): mean distance r to the targets and the point r along the view. */
static int32_t look_point(const cam_t* c,const tab_t* t,const t3_psx_camera_source_t* r,int32_t look[3]) {
    int32_t sp=sn(t,idx18(r->pitch)),cp=cs(t,idx18(r->pitch));
    int32_t i=idx18((int64_t)r->yaw+0x10000),sy=sn(t,i),cy=cs(t,i);
    int32_t d0=distance_to(c,c->target[0],r,c->target[0][2]);
    int32_t d1=distance_to(c,c->target[1],r,c->faithful?c->target[0][2]:c->target[1][2]);
    int32_t rr=(int32_t)(((uint32_t)d0+(uint32_t)d1)>>1);
    look[0]=w32((int64_t)r->x+t12(w32((int64_t)(int32_t)t12(w32((int64_t)rr*cy))*cp)));
    look[1]=w32((int64_t)r->y+t12(w32((int64_t)rr*sp)));
    look[2]=w32((int64_t)r->z+t12(w32((int64_t)(int32_t)t12(w32((int64_t)rr*sy))*cp)));
    return rr;
}
/* CameraBlendPair (0x80064458): a and b (weight w of a) around their look points. */
static void blend_pair(cam_t* c,const tab_t* t,t3_psx_camera_source_t a,t3_psx_camera_source_t b,int32_t w) {
    int32_t iw=ONE-w,pa[3],pb[3],look[3],pos[3],ra,rb,r,pitch,yaw,sp,cp,yi,ys,yc,bh;int k;
    ra=look_point(c,t,&a,pa);rb=look_point(c,t,&b,pb);
    r=(int32_t)t12(w32((int64_t)ra*w+(int64_t)rb*iw));
    for(k=0;k<3;k++)look[k]=(int32_t)t12(w32((int64_t)pa[k]*w+(int64_t)pb[k]*iw));
    pitch=w32((int64_t)a.pitch+t12(w32((int64_t)s18((int64_t)b.pitch-a.pitch)*iw)));
    yaw=w32((int64_t)a.yaw+t12(w32((int64_t)s18((int64_t)b.yaw-a.yaw)*iw)));
    sp=sn(t,idx18(pitch));cp=cs(t,idx18(pitch));
    yi=(sar6(yaw)+0x400)&0xFFF;ys=sn(t,yi);yc=cs(t,yi);
    pos[0]=w32((int64_t)look[0]-t12(w32((int64_t)(int32_t)t12(w32((int64_t)r*yc))*cp)));
    pos[1]=w32((int64_t)look[1]-t12(w32((int64_t)r*sp)));
    pos[2]=w32((int64_t)look[2]-t12(w32((int64_t)(int32_t)t12(w32((int64_t)r*ys))*cp)));
    bh=(int32_t)t12(w32((int64_t)a.h*w+(int64_t)b.h*iw));
    c->source[T3_PSX_CAMERA_OUTPUT]=look_source(t,pos,look,bh,c->source[T3_PSX_CAMERA_OUTPUT].h);
    use_source(c,T3_PSX_CAMERA_OUTPUT);
}
/* CameraBlend (0x80062E5C). */
static void blend(cam_t* c,const tab_t* t) {
    int i,mask=0;int32_t w0=c->weight[0];
    for(i=0;i<T3_PSX_CAMERA_SOURCES;i++)if(c->weight[i]>0)mask|=1<<i;
    switch(mask) {
    case 1:use_source(c,0);break;
    case 2:use_source(c,1);break;
    case 4:use_source(c,2);break;
    case 0x10:use_source(c,4);break;
    case 3:case 5:blend_pair(c,t,c->source[0],c->source[mask==3?1:2],w0);break;
    case 0x11: {
        int32_t e=sn(t,(div_trunc(w0,2)-0x400)&0xFFF);
        blend_pair(c,t,c->source[0],c->source[4],div_trunc(e,2)+0x800);
        break;
    }
    default:t3_psx_camera_reset(c);break;
    }
    __builtin_memcpy(c->prev_weight,c->weight,sizeof c->weight);
}

static void intro_start(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr,int count) {
    if(count)c->intro_counter+=1;
    c->intro_index=(int16_t)(c->intro_counter%T3_PSX_CAMERA_INTROS);
    c->intro_length=fr->rounds_played==1?INTRO_ROUND1:INTRO_LATER;
    c->intro_timer=c->intro_length;
    camera_fight(c,t,f,fr,1);
    c->intro_base=c->source[T3_PSX_CAMERA_FIGHT];
}
/* FUN_8006879C: the intro's source 2; returns its weight. */
static int32_t intro_step(cam_t* c,const tab_t* t) {
    const int16_t* off=t->intro_offsets[c->intro_index];
    int32_t k=sn(t,(div_trunc((int64_t)c->intro_timer*1024,c->intro_length)+0xC00)&0xFFF)+ONE;
    int32_t eye[3],target[3];
    eye[0]=w32((int64_t)c->intro_base.x+t12((int64_t)off[0]*k));
    eye[1]=w32((int64_t)c->intro_base.y+t12((int64_t)off[1]*k));
    eye[2]=w32((int64_t)c->intro_base.z+t12((int64_t)off[2]*k));
    target[0]=c->intro_base.x;target[1]=c->intro_base.y;target[2]=0;
    c->source[T3_PSX_CAMERA_STREAM]=look_source(t,eye,target,c->source[0].h,c->source[0].h);
    c->intro_timer=s16(c->intro_timer-1);
    return c->intro_timer>0x10?ONE:(int32_t)c->intro_timer*256;
}

int t3_psx_camera_round_start(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr) {
    if(!c || !t3_psx_camera_valid(t) || !f || !fr)return 0;
    init_sources(c,f);
    intro_start(c,t,f,fr,0);
    return 1;
}

static int intro_frame(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr) {
    int32_t w;
    track(c,f,0);
    c->script=-1;c->stream_frame=-1;c->stream=-1;
    w=intro_step(c,t);
    c->weight[0]=s16(ONE-w);c->weight[2]=s16(w);
    if(c->weight[0]>0)camera_fight(c,t,f,fr,c->prev_weight[0]<1);
    blend(c,t);
    if(c->weight[2]>0)return 0;
    c->state=T3_PSX_CINEMATIC_IDLE;
    return 1;
}

/* The game's rand() (0x8007A73C). */
static int32_t next_rand(uint32_t* x) {
    *x=*x*0x41C64E6Du+0x3039u;
    return (int32_t)((*x>>16)&0x7FFFu);
}
/* Screen order of the targets: fighter 0 left of fighter 1. On a tie the
 * game reads a stale value (bug 53); the previous frame's order is used. */
static int screen_order(const cam_t* c) {
    if(c->target[0][3]==c->target[1][3])return c->prev_target[0][3]<c->prev_target[1][3];
    return c->target[0][3]<c->target[1][3];
}
/* FUN_80066430: the targets' axis in 18-bit units, turned when fighter 1 is left on screen. */
static int32_t axis_yaw(const cam_t* c,const tab_t* t,const fig_t f[2]) {
    int32_t axis=atan4096(t,(int64_t)c->target[1][0]-c->target[0][0],(int64_t)c->target[1][2]-c->target[0][2]);
    if(f[1].screen_x<=f[0].screen_x)axis=(axis+0x800)&0xFFF;
    return axis*64;
}
/* FUN_800677C8: preset index + 1, -1 for a stream, 0 otherwise. */
static int16_t kind_of(const tab_t* t,int32_t script) {
    if(script>=0 && script<T3_PSX_CAMERA_PRESETS)return t->preset_index[script];
    return script<-1?-1:0;
}
/* CameraChoose (0x80067548): a weighted pick from the id's list; -1 none,
 * -2 on a malformed list. A random number is drawn even without a camera. */
static int32_t choose(cam_t* c,const tab_t* t,const fig_t* owner,int id,int side,int player,
                      const t3_psx_camera_frame_t* fr) {
    const t3_psx_camera_choice_t* e=t->choices;size_t n=t->choice_count,k;
    int32_t r=next_rand(fr->rand),value;int16_t index=0;
    if(id<1)id=1;
    if(fr->practice || (id>=FIRST_BANK_ID-REJECTED_COUNT && id<FIRST_BANK_ID))return -1;
    k=(size_t)id+1u;
    if(id>=FIRST_BANK_ID){e=owner->bank_choices;n=owner->bank_choice_count;k=(size_t)(id-FIRST_BANK_ID)+1u;}
    if(!e)return -2;
    while(k<n && e[k].weight<(r&0xFFF)){k++;index++;}
    if(k>=n)return -2;
    c->mirror=0;value=e[k].script;
    if(e[k].kind!=1 && side) {
        if(k+1u>=n)return -2;
        if(e[k+1u].kind==0 || e[k].weight<e[k+1u].weight)c->mirror=1;
        else value=e[k+1u].script;
    }
    c->choice_index=index;c->script_player=(int16_t)player;
    return value;
}
/* The common part of CameraThrowStart and FUN_80067B14: the blend-in length,
 * 0 for a stream, -1 for none, -2 on bad data. */
static int32_t choose_for(cam_t* c,const tab_t* t,const fig_t* thr,int side,int player,
                          const t3_psx_camera_frame_t* fr) {
    int32_t blend_in;
    c->script=choose(c,t,thr,camera_byte(thr),side,player,fr);
    if(c->script==-2){c->script=-1;return -2;}
    if(c->script==-1)return -1;
    c->script_kind=kind_of(t,c->script);
    if(c->script_kind<0){c->switch_mode=0;c->script_move=thr->move_id;return 0;}
    if(c->script_kind==0)return -1;
    if(c->script_kind>T3_PSX_CAMERA_PRESETS)return -2;
    c->script_move=thr->move_id;
    blend_in=t->presets[c->script_kind-1][0];
    c->switch_mode=blend_in<2;
    return blend_in;
}
/* FUN_800664A0 with FUN_80067DE4: a random pitch and yaw offset around the axis. */
static void preset_start(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr) {
    const int16_t* p=t->presets[c->script_kind-1];
    int32_t yaw=axis_yaw(c,t,f),pitch,offset;
    pitch=p[2+(next_rand(fr->rand)&3)]*32;
    offset=p[6+(next_rand(fr->rand)&3)]*64;
    if(c->side)offset=-offset;
    c->preset_yaw=yaw+offset;c->preset_pitch=pitch;
}
/* CameraThrowEnds (0x80067818). */
static int throw_ends(const cam_t* c,const fig_t* thr,const fig_t* vic) {
    int32_t left;
    if(thr->throw_state<1 || vic->throw_state>=0)return 1;
    if(camera_byte(thr)==CAMERA_BYTE_CUT)return 1;
    if(c->switch_mode==1)return 0;
    if(vic->move_state&0x400000u)return 1;
    if(thr->event_left_b<0 && vic->event_left_c<0)return 0;
    left=thr->event_left_b<=vic->event_left_c?vic->event_left_c:thr->event_left_b;
    return left==0 && thr->transition!=TRANSITION_THROW;
}
static int thrower_done(const cam_t* c,const fig_t f[2]) {
    if(c->thrower==0)return throw_ends(c,&f[0],&f[1]);
    if(c->thrower==1)return throw_ends(c,&f[1],&f[0]);
    return 0;
}
/* -limit <= v <= limit, as the game's unsigned test. */
static int within(int32_t v,int32_t limit) {return (uint32_t)v+(uint32_t)limit<2u*(uint32_t)limit+1u;}
/* FUN_80067C8C: the running script's blend-out, 5 when source 1 already looks along the axis. */
static int32_t blend_out(const cam_t* c,const tab_t* t,const fig_t f[2]) {
    int32_t v=t->outside_blend_out;
    if(c->script>=0 && c->script<T3_PSX_CAMERA_PRESETS && t->preset_index[c->script]>=1)
        v=t->presets[t->preset_index[c->script]-1][1];
    if(v<2) {
        const t3_psx_camera_source_t* s=&c->source[T3_PSX_CAMERA_SCRIPT];
        if(within(s->pitch,0xE37) && within(s18((int64_t)s->yaw-axis_yaw(c,t,f)),0xE37))v=5;
    }
    return v;
}
static void transition_start(cam_t* c) {
    c->weight[4]=ONE;c->weight[1]=c->weight[0]=0;
    c->state=T3_PSX_CINEMATIC_TRANSITION;
    c->source[T3_PSX_CAMERA_TRANSITION]=c->source[T3_PSX_CAMERA_OUTPUT];
}
static void cancel(cam_t* c) {c->script=-1;c->wait=-1;c->state=T3_PSX_CINEMATIC_IDLE;}

/* Camera bank section 8 uses the same keyed spline as pose motion, with a
 * camera-specific shift of four. Decode one frame from the original compact
 * words so the full set of authored streams fits in stock Work RAM. */
static uint16_t stream_word(const uint8_t* p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
int t3_psx_camera_decode_compact(const uint8_t* data,size_t limit,
                                 unsigned frame,const t3_motion_tables_t* spline,
                                 int16_t out[7]) {
    size_t at=4;
    unsigned frames,channels;
    if(!data || !spline || !out || limit<4u)return 0;
    frames=stream_word(data+2)&0x3ffu;channels=stream_word(data+2)>>10;
    if(channels!=7u || frame>=frames)return 0;
    for(unsigned ch=0;ch<7u;++ch) {
        unsigned time=ch&1u?frames-frame-1u:frame;
        unsigned start=0,segments;
        int found=0;int16_t value=0;
        if(at+2u>limit)return 0;
        segments=stream_word(data+at)>>12;
        for(unsigned j=0;j<=segments;++j) {
            uint16_t desc;unsigned kind,length,words;
            if(at+2u>limit)return 0;
            desc=stream_word(data+at);at+=2;
            kind=(desc>>10)&3u;length=desc&0x3ffu;
            words=kind==0u?1u:kind==1u?2u:length;
            if(!length || words>(limit-at)/2u)return 0;
            if(kind==2u) {
                unsigned end=start;
                for(unsigned k=1;k<length;++k)end+=1u+(stream_word(data+at+2u*k)&31u);
                if(!found && time>=start && time<=end) {
                    if(t3_motion_spline_words(data+at,words*2u,length,
                            time-start,end-start+1u,spline,&value)!=1)return 0;
                    found=1;
                }
                start=end+1u;
            } else {
                if(!found && time>=start && time<start+length) {
                    if(kind==0u)value=(int16_t)stream_word(data+at);
                    else if(kind==1u) {
                        const int32_t initial=(int16_t)stream_word(data+at);
                        const int32_t delta=(int16_t)stream_word(data+at+2u);
                        value=(int16_t)(initial+delta*(int32_t)(time-start)/(frames<10u?1:32));
                    } else value=(int16_t)stream_word(data+at+2u*(time-start));
                    found=1;
                }
                start+=length;
            }
            at+=2u*words;
        }
        if(!found)return 0;
        out[ch]=value;
    }
    return at==limit;
}

/* FUN_800666E0 selecting a stream of `player`'s bank: returns its yaw mode, -2 when missing. */
static int32_t stream_start(cam_t* c,const fig_t f[2],int32_t script,int player) {
    const t3_psx_camera_streams_t* b=f[player].bank_streams;size_t i;
    c->stream=script;c->stream_player=(int16_t)player;c->stream_frame=0;
    if(!b)return -2;
    for(i=0;i<b->stream_count;i++) {
        const t3_psx_camera_stream_t* e=&b->streams[i];
        if(e->offset!=((uint32_t)script&0xFFFFu))continue;
        if(!e->frames || (b->samples ? e->first+e->frames>b->sample_count :
            !b->encoded || e->first>b->encoded_size ||
            e->encoded_bytes>b->encoded_size-e->first))return -2;
        c->stream_frames=(int16_t)e->frames;
        return e->yaw_mode;
    }
    return -2;
}
/* FUN_800666E0 sampling: the next frame scaled by 20/32, weight / 8 in 0..320
 * (320 from the last frame on), H 550. Past the end the game decodes beyond
 * the stream; the last frame is repeated (it only feeds an unused source). */
static int stream_sample(cam_t* c,const fig_t f[2]) {
    const t3_psx_camera_streams_t* b=f[c->stream_player].bank_streams;
    const int16_t* raw;int16_t decoded[7];size_t i;int32_t index=c->stream_frame,w;
    c->stream_frame=s16(c->stream_frame+1);
    if(!b)return 0;
    for(i=0;i<b->stream_count;i++)if(b->streams[i].offset==((uint32_t)c->stream&0xFFFFu))break;
    if(i==b->stream_count)return 0;
    if(index<0)index=0;
    if(index>=b->streams[i].frames)index=b->streams[i].frames-1;
    if(b->samples)raw=b->samples[b->streams[i].first+(uint32_t)index];
    else {
        if(!t3_psx_camera_decode_compact(b->encoded+b->streams[i].first,
                b->streams[i].encoded_bytes,(unsigned)index,
                f[c->stream_player].bank_spline,decoded))return 0;
        raw=decoded;
    }
    w=div_trunc(raw[6],8);
    if(w<0)w=0;
    else if(w>STREAM_WEIGHT_FULL || c->stream_frames<=c->stream_frame)w=STREAM_WEIGHT_FULL;
    for(i=0;i<6;i++)c->stream_sample[i]=s16(div_trunc((int64_t)raw[i]*0x14,32));
    c->stream_sample[6]=(int16_t)w;c->stream_sample[7]=STREAM_H;
    return 1;
}
/* FUN_800669D8: source 2 from the current sample around the stream origin,
 * turned by the stream yaw, mirrored for the other side and rescaled to the
 * projection plane; then the next sample. Returns the used weight, -2 on bad data. */
static int32_t stream_view(cam_t* c,const tab_t* t,const fig_t f[2]) {
    const int16_t* s=c->stream_sample;
    int32_t sample_h=s[7],weight=s[6],ex=s[0],ey=s[1],ez=s[2],tx=s[3],ty=s[4],tz=s[5],plane=sample_h;
    int32_t sn_=sn(t,c->preset_yaw&0xFFF),cs_=cs(t,c->preset_yaw&0xFFF),eye[3],target[3];
    if(c->mirror){ez=s16(-(int32_t)s[2]);tz=s16(-(int32_t)s[5]);}
    if(c->stream_rescale) {
        plane=c->h;
        ey=s16(ty+s16(div_trunc((int64_t)(s[1]-ty)*plane,sample_h)));
        ex=s16(tx+s16(div_trunc((int64_t)(s[0]-tx)*plane,sample_h)));
        ez=s16(tz+s16(div_trunc((int64_t)(ez-tz)*plane,sample_h)));
    }
    eye[0]=w32((int64_t)c->stream_origin[0]+t12((int64_t)cs_*ex-(int64_t)sn_*ez));
    eye[1]=w32((int64_t)c->stream_origin[1]+ey);
    eye[2]=w32((int64_t)c->stream_origin[2]+t12((int64_t)sn_*ex+(int64_t)cs_*ez));
    target[0]=w32((int64_t)c->stream_origin[0]+t12((int64_t)cs_*tx-(int64_t)sn_*tz));
    target[1]=w32((int64_t)c->stream_origin[1]+ty);
    target[2]=w32((int64_t)c->stream_origin[2]+t12((int64_t)sn_*tx+(int64_t)cs_*tz));
    c->source[T3_PSX_CAMERA_STREAM]=look_source(t,eye,target,plane,c->source[T3_PSX_CAMERA_STREAM].h);
    if(!stream_sample(c,f))return -2;
    return weight;
}
/* FUN_800676CC: the same choice index in the thrower's new camera id (>= 0x2B). */
static int32_t bank_script(cam_t* c,const fig_t* thr,int player) {
    const t3_psx_camera_choice_t* e=thr->bank_choices;size_t n=thr->bank_choice_count;
    int32_t value;size_t k;
    if(!e || camera_byte(thr)<FIRST_BANK_ID)return -2;
    k=(size_t)(camera_byte(thr)-FIRST_BANK_ID)+1u+(size_t)c->choice_index;
    if(k>=n)return -2;
    c->mirror=0;value=e[k].script;
    if(e[k].kind!=1 && c->side) {
        if(k+1u>=n)return -2;
        if(e[k+1u].kind==0 || e[k].weight<e[k+1u].weight)c->mirror=1;
        else value=e[k+1u].script;
    }
    c->script_player=(int16_t)player;
    return value;
}
/* FUN_8006312C / FUN_8006320C: the camera of the thrower's next move of a
 * multi-part throw; `start_stream` also starts a stream around the victim. */
static int switch_camera(cam_t* c,const tab_t* t,const fig_t f[2],int side,int start_stream,
                         const t3_psx_camera_frame_t* fr) {
    const fig_t* vic=&f[c->thrower!=1];int32_t r=-1;
    c->side=(int16_t)side;c->hit_total=-1;c->throw_player=c->thrower;
    if(c->script==-1)t3_psx_camera_reset(c);
    else r=choose_for(c,t,&f[c->thrower==1],side,c->thrower==1,fr);
    if(r==-2)return 0;
    if(r<0)c->script=-1;
    else if(c->script_kind<0) {
        if(start_stream) {
            c->preset_yaw=(0x400-div_trunc(vic->heading,16))&0xFFF;
            c->stream_origin[0]=vic->root_x;c->stream_origin[1]=vic->pos_y;c->stream_origin[2]=vic->root_z;
            c->stream_frame=0;
            if(stream_start(c,f,c->script,c->script_player)<0 || !stream_sample(c,f))return 0;
            c->stream_restart=1;
        }
        c->state=T3_PSX_CINEMATIC_STREAM;c->count=c->count_total=(int16_t)r;
    } else {
        c->state=T3_PSX_CINEMATIC_PRESET;c->count_total=c->count=1;c->stream=-1;
    }
    c->wait=-1;c->weight[1]=ONE;c->weight[0]=c->weight[2]=0;c->switch_mode=1;
    return 1;
}
/* FUN_800670E4: a stream (re)start or the switch at the thrower's next move,
 * then the stream step. Sets *weight to the stream's weight, -1 when the
 * switch went to preset framing. Returns 0 on bad data. */
static int stream_step(cam_t* c,const tab_t* t,const fig_t f[2],int32_t restart_script,
                       const t3_psx_camera_frame_t* fr,int32_t* weight) {
    const fig_t* thr=&f[c->thrower==1];const fig_t* vic=&f[c->thrower!=1];
    int player=c->thrower==1;
    if(restart_script==0) {
        if(thr->move_id!=c->script_move && thr->throw_state>0 && camera_byte(thr)!=CAMERA_BYTE_KEEP) {
            c->script_move=thr->move_id;
            if(camera_byte(thr)<FIRST_BANK_ID) {
                /* NormalClip of (thrower root, roots' midpoint, stream eye) in x/z.
                 * The game passes stack addresses (bug 54) and always picks one
                 * side; the values are used here. */
                int64_t ax=s16(thr->root_x),az=s16(thr->root_z);
                int64_t bx=s16(div_trunc((int64_t)thr->root_x+vic->root_x,2));
                int64_t bz=s16(div_trunc((int64_t)thr->root_z+vic->root_z,2));
                int64_t cx=c->stream_sample[0],cz=c->stream_sample[2];
                int32_t clip=w32(ax*bz+bx*cz+cx*az-ax*cz-bx*az-cx*bz);
                if(!switch_camera(c,t,f,clip<0?1:0,0,fr))return 0;
                *weight=-1;return 1;
            }
            c->script=bank_script(c,thr,player);
            if(c->script==-2)return 0;
            c->script_kind=kind_of(t,c->script);
            if(c->script_kind<0) {
                if(stream_start(c,f,c->script,player)<0 || !stream_sample(c,f))return 0;
                c->stream_frame=0;
            }
        }
    } else {
        int32_t mode,k,yaw=0;
        c->stream_frame=0;
        mode=stream_start(c,f,restart_script,c->script_player);
        if(mode<0 || !stream_sample(c,f))return 0;
        k=div_trunc(vic->heading,16);
        switch(mode) {
        case 0:case 4:yaw=0x400-k;break;
        case 1:yaw=0x800-k;break;
        case 2:yaw=-k;break;
        case 3:case 5:yaw=-(k+0x400);break;
        default:break;
        }
        c->preset_yaw=yaw&0xFFF;
        c->stream_origin[0]=vic->pos_x;c->stream_origin[1]=vic->pos_y;c->stream_origin[2]=vic->pos_z;
    }
    if(c->script_kind<0) {
        *weight=stream_view(c,t,f);
        return *weight!=-2;
    }
    /* The game returns its caller's $s3 here, 1 (bug 55); the switch's -1 is used. */
    t3_psx_camera_reset(c);
    *weight=-1;
    return 1;
}

/* State IDLE: a starting throw picks its camera. Returns -1 for unported paths. */
/* CameraHitChoose (0x80067FD4): the defender whose clean hit gets a hit
 * camera, -1 none. Rates the hit, finds the attacker's row (a KO uses the
 * first row) and draws one random number per matching row. */
static int hit_choose(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr) {
    int i,k;
    if(f[0].hit_clean && f[1].hit_clean)return -1;
    for(i=0;i<2;i++) {
        const fig_t* d=&f[i];const fig_t* a=&f[i^1];int rating=0;
        if(!d->hit_clean)continue;
        if(d->ko && d->last_damage>HIT_KO_DAMAGE)rating=3;
        else if(d->counter_hit)rating=2;
        else if(d->close_hit)rating=1;
        for(k=0;k<T3_PSX_CAMERA_HIT_ROWS;k++) {
            int list=t->hit_rows[k][2]-HIT_LIST_BASE,j;int32_t r;
            if(rating!=3 && (a->bank_type!=t->hit_rows[k][0] || a->cur_slot!=t->hit_rows[k][1]))continue;
            r=next_rand(fr->rand)&0xFFF;
            for(j=0;j<t->hit_record_count[list];j++) {
                const t3_psx_camera_hit_record_t* e=&t->hit_records[list][j];
                if(r>e->threshold)continue;
                if(t->hit_scripts[e->script[rating]].kind==-1)return -1;
                c->hit_script=e->script[rating];
                c->hit_point[0]=d->hit_point[0];c->hit_point[1]=d->hit_point[1];c->hit_point[2]=d->hit_point[2];
                return i;
            }
            return -1;
        }
    }
    c->hit_script=-1;
    return -1;
}
/* FUN_80068244: source 1 for a hit camera. */
static void hit_camera(cam_t* c,const tab_t* t,const fig_t f[2],int restart,const t3_psx_camera_frame_t* fr) {
    const t3_psx_camera_hit_script_t* s;
    int32_t k,dist,sp,cp,yi,eye[3];
    if((uint16_t)c->hit_script>=T3_PSX_CAMERA_HIT_SCRIPTS){t3_psx_camera_reset(c);return;}
    s=&t->hit_scripts[c->hit_script];
    if(s->kind!=0 && s->kind!=1){t3_psx_camera_reset(c);return;}
    if(restart) {
        int32_t axis=axis_yaw(c,t,f),pitch;
        pitch=s->pitches[next_rand(fr->rand)&3]*64;
        c->preset_yaw=axis+s->yaws[next_rand(fr->rand)&3]*(c->side?-0x40:0x40);
        c->preset_pitch=pitch;
    }
    if(s->kind==0) {
        frame_fighters(c,t,c->preset_pitch,c->preset_yaw,restart,div_trunc((int64_t)DISTANCE*0x72,128),T3_PSX_CAMERA_SCRIPT);
        return;
    }
    /* end + (start - end) * max(0, remaining / hold - 1/4), scaled to the projection plane. */
    k=cdiv(c,(int64_t)c->hit_count*4096,c->hit_total)-0x400;
    if(k<1)k=0;
    dist=w32(t12((int64_t)(s->start_distance-s->end_distance)*k));
    dist=div_trunc(((int64_t)dist+s->end_distance)*c->h,HIT_DISTANCE_H);
    sp=sn(t,idx18(c->preset_pitch));cp=cs(t,idx18(c->preset_pitch));
    yi=(sar6(c->preset_yaw)+0x400)&0xFFF;
    eye[0]=w32((int64_t)c->hit_point[0]-t12(t12((int64_t)dist*cs(t,yi))*cp));
    eye[1]=w32((int64_t)c->hit_point[1]-t12((int64_t)dist*sp));
    eye[2]=w32((int64_t)c->hit_point[2]-t12(t12((int64_t)dist*sn(t,yi))*cp));
    c->source[T3_PSX_CAMERA_SCRIPT]=look_source(t,eye,c->hit_point,c->h,c->source[T3_PSX_CAMERA_SCRIPT].h);
}
static int cinematic_start(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr) {
    int32_t r;
    c->script=-1;c->count=c->hit_total=-1;
    if(!c->cinematic)return 1;
    if(c->throw_player<0 && (f[0].throw_state>0 || f[1].throw_state>0)) {
        c->thrower=f[0].throw_state<1?1:0;
        c->side=screen_order(c)?0:1;
        if(f[0].throw_state<1)c->side=(int16_t)(1-c->side);
        c->throw_player=c->thrower;
        r=choose_for(c,t,&f[c->thrower],c->side,c->thrower,fr);
        if(r==-2)return 0;
        if(r<0){c->script=-1;return 1;}
        if(c->script_kind<0){c->state=T3_PSX_CINEMATIC_STREAM;c->wait=-1;}
        else {
            preset_start(c,t,f,fr);
            c->wait=0x10;c->state=T3_PSX_CINEMATIC_PRESET_IN;
        }
        c->count=c->count_total=(int16_t)r;
        return 1;
    }
    if((r=hit_choose(c,t,f,fr))<0)return 1;
    c->thrower=(int16_t)r;
    c->side=screen_order(c)?0:1;
    if(r)c->side=(int16_t)(1-c->side);
    c->script_kind=1;
    r=t->hit_scripts[c->hit_script].blend_in;
    if(r>=0) {
        c->state=T3_PSX_CINEMATIC_HIT;
        c->hit_count=c->hit_total=s16(t->hit_scripts[c->hit_script].frames-t->hit_scripts[c->hit_script].blend_out);
    }
    c->count=c->count_total=(int16_t)r;
    return 1;
}
/* FUN_80066568: source 1 for a preset, or the stream step for a stream.
 * Sets *weight to the stream's weight (0 for a preset, -1 when the director
 * stops for this frame). Returns 0 on bad data. */
static int script_camera(cam_t* c,const tab_t* t,const fig_t f[2],int restart,
                         const t3_psx_camera_frame_t* fr,int32_t* weight) {
    const fig_t* thr=&f[c->thrower==1];
    *weight=0;
    if(c->script_kind<0)return stream_step(c,t,f,restart?c->script:0,fr,weight);
    if(c->script_kind<1){t3_psx_camera_reset(c);return 1;}
    frame_fighters(c,t,c->preset_pitch,c->preset_yaw,restart,div_trunc((int64_t)DISTANCE*0x72,128),T3_PSX_CAMERA_SCRIPT);
    if(restart || thr->move_id==c->script_move)return 1;
    if(thr->throw_state>0 && camera_byte(thr)!=CAMERA_BYTE_KEEP) {
        int to_stream=thr->bank_type==BANK_PAUL && camera_byte(thr)==CAMERA_BYTE_STREAM;
        c->script_move=thr->move_id;
        if(to_stream)return switch_camera(c,t,f,c->side,1,fr);
    }
    return 1;
}
/* State STREAM. Returns 0 on bad data, -1 when the director stops for this frame. */
static int stream_frame_state(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr) {
    int restart=0;int32_t r;
    if(!c->stream_restart){if(c->wait==-1){restart=1;c->wait=-2;}}
    else {c->wait=-3;c->stream_restart=0;}
    if(!script_camera(c,t,f,restart,fr,&r))return 0;
    if(r<0)return -1;
    c->weight[1]=0;
    c->weight[0]=s16(div_trunc((int64_t)r*4096,STREAM_WEIGHT_FULL));
    c->weight[2]=s16(ONE-c->weight[0]);
    if(camera_byte(&f[0])==CAMERA_BYTE_CUT || camera_byte(&f[1])==CAMERA_BYTE_CUT) {
        blend_out(c,t,f);
        c->count_total=c->count=8;
        if(r==STREAM_WEIGHT_FULL)c->state=T3_PSX_CINEMATIC_IDLE;
        else {transition_start(c);c->weight[2]=0;}
        c->wait=-1;c->script=-1;
    } else if((c->thrower==0 && f[0].throw_state<0) || (c->thrower==1 && f[1].throw_state<0)) {
        c->count_total=c->count=0x23;
        transition_start(c);c->weight[2]=0;
        c->wait=-1;c->script=-1;
    } else if(c->wait==-2 && r==0)c->wait=-3;
    else if(c->wait==-3 && r==STREAM_WEIGHT_FULL) {
        c->count=c->count_total=s16(blend_out(c,t,f));
        c->state=T3_PSX_CINEMATIC_IDLE;c->wait=-1;c->stream=-1;
    }
    return 1;
}

static int fight_frame(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr) {
    int r;
    track(c,f,0);
    if((c->throw_player==0 && !f[0].throw_state) || (c->throw_player==1 && !f[1].throw_state))
        c->throw_player=-1;
    switch(c->state) {
    case T3_PSX_CINEMATIC_IDLE:
        if((r=cinematic_start(c,t,f,fr))<=0)return r;
        break;
    case T3_PSX_CINEMATIC_PRESET:
        c->weight[1]=ONE;c->weight[2]=c->weight[0]=0;
        if(thrower_done(c,f)) {
            c->count=c->count_total=s16(blend_out(c,t,f));
            c->state=T3_PSX_CINEMATIC_PRESET_OUT;
        }
        break;
    case T3_PSX_CINEMATIC_PRESET_IN:
        if(c->wait<0) {
            c->weight[0]=s16(cdiv(c,(int64_t)c->count*4096,c->count_total));
            c->weight[1]=s16(ONE-c->weight[0]);
            if(thrower_done(c,f)) {
                c->count=c->count_total=s16(blend_out(c,t,f));
                if(c->weight[1]==0)c->state=T3_PSX_CINEMATIC_IDLE;
                else if(c->weight[0]==0)c->state=T3_PSX_CINEMATIC_PRESET_OUT;
                else transition_start(c);
            } else {
                c->count=s16(c->count-1);
                if(c->count<0)c->state=T3_PSX_CINEMATIC_PRESET;
            }
        } else if(camera_byte(&f[0])==CAMERA_BYTE_CUT)cancel(c);
        else {
            c->wait=s16(c->wait-1);
            if(camera_byte(&f[1])==CAMERA_BYTE_CUT)cancel(c);
        }
        break;
    case T3_PSX_CINEMATIC_PRESET_OUT: {
        int32_t n=c->count;
        c->count=s16(c->count-1);
        c->weight[1]=s16(cdiv(c,(int64_t)n*4096,c->count_total));
        c->weight[0]=s16(ONE-c->weight[1]);
        if(c->count<0){c->count_total=c->count=-1;c->state=T3_PSX_CINEMATIC_IDLE;}
        break;
    }
    case T3_PSX_CINEMATIC_TRANSITION: {
        int32_t n=c->count;
        c->count=s16(c->count-1);
        c->weight[4]=s16(cdiv(c,(int64_t)n*4096,c->count_total));
        c->weight[0]=s16(ONE-c->weight[4]);
        if(c->count<0)c->state=T3_PSX_CINEMATIC_IDLE;
        break;
    }
    case T3_PSX_CINEMATIC_STREAM:
        if((r=stream_frame_state(c,t,f,fr))<=0)return r<0?1:0;
        break;
    case T3_PSX_CINEMATIC_HIT:
        c->weight[1]=ONE;c->weight[2]=c->weight[0]=0;
        c->hit_count=s16(c->hit_count-1);
        if(c->hit_count<1) {
            c->hit_total=-1;c->state=T3_PSX_CINEMATIC_IDLE;
            c->weight[0]=ONE;c->weight[2]=c->weight[1]=0;
            c->count_total=c->count=-1;
        }
        break;
    default:t3_psx_camera_reset(c);break;
    }
    if(c->weight[1]>0) {
        int32_t w;
        if(c->hit_total>=0)hit_camera(c,t,f,c->prev_weight[1]<1,fr);
        else if(!script_camera(c,t,f,c->prev_weight[1]<1,fr,&w))return 0;
    }
    if(c->weight[0]>0)camera_fight(c,t,f,fr,c->prev_weight[0]<1);
    blend(c,t);
    return 1;
}

/* ==== round end ==== */

/* One step of the camera generator 0x800A95F4; its upper half. */
static int32_t camera_random(const t3_psx_camera_frame_t* fr) {
    *fr->camera_rand=(*fr->camera_rand+1u)*0x10DCDu;
    return (int32_t)(*fr->camera_rand>>16);
}
/* The game's table square root (as psx_body). Returns -1 outside the table. */
static int32_t sqrt_table(const tab_t* t,int64_t n64) {
    int32_t n=w32(n64),v,e;uint32_t mag;int bits=0,lz,shift;
    if(!n)return 0;
    mag=n>0?(uint32_t)n:~(uint32_t)n;
    while(bits<32 && (mag>>bits))bits++;
    lz=(32-bits)&~1;shift=0x18-lz;
    v=shift>=0?n>>shift:w32((int64_t)n*((int64_t)1<<-shift));
    if(v<0x40 || v>0xFF)return -1;
    e=s16(t->sqrt[2*(v-0x40)]|((uint16_t)t->sqrt[2*(v-0x40)+1]<<8));
    return (int32_t)(((uint32_t)e<<(((0x1F-lz)>>1)&0x1F))>>12);
}
/* FUN_80046180: signed pitch and yaw - 90 degrees from eye to target, 4096 units. */
static void aim4096(const tab_t* t,const int32_t eye[3],const int32_t target[3],int32_t out[2]) {
    int32_t dx=w32((int64_t)target[0]-eye[0]),dz=w32((int64_t)target[2]-eye[2]);
    int32_t flat=isqrt((uint32_t)w32((int64_t)dx*dx+(int64_t)dz*dz));
    int32_t pitch=atan4096(t,flat,(int64_t)target[1]-eye[1])&0xFFF;
    if(pitch&0x800)pitch-=0x1000;
    out[0]=pitch;out[1]=atan4096(t,dx,dz)-0x400;
}
/* FUN_80046508: the view from `eye` at the winner's root into out, its pitch
 * limited by the head and the floor, the eye pushed out to 5800 when closer. */
static void winner_view(const tab_t* t,const fig_t* f,const int32_t eye[3],int32_t out[9]) {
    int32_t root[3],a[2],ex,ez,e[3];
    e[0]=eye[0];e[1]=eye[1];e[2]=eye[2];
    root[0]=f->root_x;root[1]=f->root_y;root[2]=f->root_z;
    aim4096(t,e,root,a);
    out[0]=a[0]*64;out[1]=a[1]*64;out[2]=0;out[5]=e[0];out[6]=e[1];out[7]=e[2];
    root[1]=w32((int64_t)f->head_y-0x122);aim4096(t,e,root,a);
    if(a[0]*64+0xFA4<out[0])out[0]=a[0]*64+0xFA4;
    root[1]=0;aim4096(t,e,root,a);
    if(a[0]*64-0x1B05<out[0])out[0]=a[0]*64-0x1B05;
    ex=w32((int64_t)out[5]-f->root_x);ez=w32((int64_t)out[7]-f->root_z);
    if(w32((int64_t)ex*ex+(int64_t)ez*ez)<0x2014E40) {
        int32_t i=idx18(out[1]);
        out[5]=w32((int64_t)f->root_x+t12((int64_t)sn(t,i)*0x16A8));
        out[7]=w32((int64_t)f->root_z-t12((int64_t)cs(t,i)*0x16A8));
    }
}
/* FUN_800463A4: a round-end view (18-bit angles) becomes the camera; H stays. */
static void use_round_end_view(cam_t* c,const int32_t v[9]) {
    c->view.pitch=sar6(v[0]);c->view.yaw=sar6(v[1]);
    c->view.x=v[5];c->view.y=v[6];c->view.z=v[7];
}
/* FUN_800466F8: style A starts 3953 away at a random 60-120 degrees off the facing. */
static void winner_a_start(cam_t* c,const tab_t* t,const fig_t* f,const t3_psx_camera_frame_t* fr) {
    int32_t r=camera_random(fr),a,eye[3];
    a=(((r*0x2AB)>>16)+0x2AB-div_trunc(f->facing,16))&0xFFF;
    eye[0]=w32((int64_t)f->root_x+t12((int64_t)cs(t,a)*0xF71));eye[1]=-0x352;
    eye[2]=w32((int64_t)f->root_z+t12((int64_t)sn(t,a)*0xF71));
    winner_view(t,f,eye,c->winner_a);
}
/* FUN_80046854: the eye closes in by 8 a frame while farther than 2513; the
 * view turns with 1/32 smoothing. */
static int winner_a_step(cam_t* c,const tab_t* t,const fig_t* f) {
    int32_t* w=c->winner_a;int32_t dx,dy,dz,d,aim[9];
    dx=w32((int64_t)f->root_x-w[5]);dy=w32((int64_t)f->root_y-w[6]);dz=w32((int64_t)f->root_z-w[7]);
    if((d=sqrt_table(t,(int64_t)dx*dx+(int64_t)dy*dy+(int64_t)dz*dz))<0)return 0;
    if(d-0x9D1>0) {
        w[5]+=div_trunc((int64_t)dx*8,d);w[6]+=div_trunc((int64_t)dy*8,d);w[7]+=div_trunc((int64_t)dz*8,d);
    }
    __builtin_memset(aim,0,sizeof aim);
    winner_view(t,f,w+5,aim);
    w[0]+=div_trunc(s18((int64_t)aim[0]-w[0]),32);
    w[1]+=div_trunc(s18((int64_t)aim[1]-w[1]),32);
    use_round_end_view(c,w);
    return 1;
}
/* FUN_80046A38: style B picks a lateral offset, a forward distance, a height and a side. */
static void winner_b_start(cam_t* c,const tab_t* t,const fig_t* f,const t3_psx_camera_frame_t* fr) {
    int32_t lateral=t->winner_lateral[(camera_random(fr)*3)>>16];
    int32_t forward=t->winner_forward[(camera_random(fr)*3)>>16];
    int32_t height=t->winner_height[(camera_random(fr)*3)>>16];
    int32_t a,sn_,cs_,fx,fz,eye[3];
    c->winner_b[6]=height;
    if(height<-0x79D)forward=t->winner_forward[2];
    a=(0x400-div_trunc(f->facing,16))&0xFFF;
    sn_=sn(t,a);cs_=cs(t,a);
    c->winner_drift[0]=s16(div_trunc(-cs_,512));c->winner_drift[1]=s16(div_trunc(-sn_,512));
    fx=forward*sn_;fz=forward*-cs_;
    if(camera_random(fr)<0x8000){fx=-fx;fz=-fz;}
    eye[0]=w32((int64_t)f->root_x+t12((int64_t)lateral*cs_+fx));eye[1]=c->winner_b[6];
    eye[2]=w32((int64_t)f->root_z+t12((int64_t)lateral*sn_+fz));
    winner_view(t,f,eye,c->winner_b);
}
/* FUN_80046CB8: the eye drifts back along the facing and is pushed out to
 * 2308; yaw follows with 1/32, pitch with 1/16, and the eye sinks when looking up. */
static int winner_b_step(cam_t* c,const tab_t* t,const fig_t* f) {
    int32_t* w=c->winner_b;int32_t dx,dy,dz,d,push,aim[9];
    w[5]+=c->winner_drift[0];w[7]+=c->winner_drift[1];
    dx=w32((int64_t)f->root_x-w[5]);dy=w32((int64_t)f->root_y-w[6]);dz=w32((int64_t)f->root_z-w[7]);
    if((d=sqrt_table(t,(int64_t)dx*dx+(int64_t)dy*dy+(int64_t)dz*dz))<0)return 0;
    push=0x904-d;
    if(push>0) {
        int32_t flat=sqrt_table(t,(int64_t)dx*dx+(int64_t)dz*dz);
        if(flat<0)return 0;
        w[5]-=div_trunc(cdiv(c,(int64_t)push*dx,flat),16);
        w[7]-=div_trunc(cdiv(c,(int64_t)push*dz,flat),16);
    }
    __builtin_memset(aim,0,sizeof aim);
    winner_view(t,f,w+5,aim);
    w[0]+=div_trunc(s18((int64_t)aim[0]-w[0]),32);
    w[1]+=div_trunc(s18((int64_t)aim[1]-w[1]),16);
    if(w[0]>0) {
        int32_t floor_y=w32(t12((int64_t)sn(t,idx18(w[0]))*-0x1D4C));
        if(floor_y<w[6])w[6]+=div_trunc((int64_t)floor_y-w[6],16);
    }
    use_round_end_view(c,w);
    return 1;
}
/* FUN_80068998: the winner camera of a round that does not end the match; the styles alternate. */
static void winner_start(cam_t* c,const tab_t* t,const fig_t* f,const t3_psx_camera_frame_t* fr) {
    c->enabled=0;
    c->winner_style=(int16_t)((c->winner_style+1)&1);
    if(!c->winner_style)winner_a_start(c,t,f,fr);
    else winner_b_start(c,t,f,fr);
}
static int winner_step(cam_t* c,const tab_t* t,const fig_t f[2]) {
    const fig_t* w=&f[c->round_end_player==1];
    c->enabled=0;
    return !c->winner_style?winner_a_step(c,t,w):winner_b_step(c,t,w);
}
int t3_psx_camera_loser_start(cam_t* c,const tab_t* t,const fig_t* f,int alive,const t3_psx_camera_frame_t* fr) {
    int32_t a,reach;
    if(!c || !t3_psx_camera_valid(t) || !f || !fr || !fr->rand || !fr->camera_rand)return 0;
    c->weight[0]=ONE;c->weight[1]=c->weight[2]=0;
    if(!alive) {
        c->loser_height=0;
        c->loser_yaw=(next_rand(fr->rand)&0x7F)-0x40;
        return 1;
    }
    a=(((camera_random(fr)*0x401)>>16)+0x200-div_trunc(f->heading,16))&0xFFF;
    reach=f->bank_type==BANK_KUMA?0x19C4:0xF8A;
    c->loser[0]=w32((int64_t)f->root_x+t12((int64_t)cs(t,a)*reach));
    c->loser[2]=w32((int64_t)f->root_z+t12((int64_t)sn(t,a)*reach));
    c->loser[1]=-0x5DC;c->loser[3]=f->root_x;c->loser[4]=-0x591;c->loser[5]=f->root_z;c->loser[6]=0;
    return 1;
}
/* FUN_80068C44: the loser camera; the overhead KO view when the loser is down. */
static int loser_step(cam_t* c,const tab_t* t,const fig_t* f) {
    c->enabled=0;
    if(f->health) {
        int32_t dx=w32((int64_t)c->loser[3]-c->loser[0]),dz=w32((int64_t)c->loser[5]-c->loser[2]),d,a[2];
        if((d=sqrt_table(t,(int64_t)dx*dx+(int64_t)dz*dz))<0)return 0;
        if(d-(f->bank_type==BANK_KUMA?0x188A:0xE50)>0) {
            c->loser[0]+=cdiv(c,(int64_t)dx*2,d);c->loser[2]+=cdiv(c,(int64_t)dz*2,d);
        }
        c->loser[3]+=div_trunc(((int64_t)f->root_x-c->loser[3])*6,128);
        c->loser[4]+=div_trunc(((int64_t)f->head_y-c->loser[4])*6,128);
        c->loser[5]+=div_trunc(((int64_t)f->root_z-c->loser[5])*6,128);
        aim4096(t,c->loser,c->loser+3,a);
        c->view.pitch=a[0];c->view.yaw=a[1];
        c->view.x=c->loser[0];c->view.y=c->loser[1];c->view.z=c->loser[2];
        return 1;
    }
    c->overhead=1;   /* not in Tekken Ball */
    c->view.pitch=0x400;
    c->view.yaw=c->loser_yaw+0x800-div_trunc(f->heading,16);
    c->view.x=f->root_x;c->view.y=-0x1EAD-c->loser_height;c->view.z=f->root_z;
    return 1;
}

/* ==== KO replay ==== */

/* FUN_800569D0: a random value below n from rand() and the AI generator. */
static int32_t ai_random(const t3_psx_camera_frame_t* fr,int32_t n) {
    int32_t r=next_rand(fr->rand),v;
    v=w32((int64_t)n*(int32_t)(((uint32_t)r+*fr->ai_rand)&0x7FFFu));
    *fr->ai_rand=*fr->ai_rand*5u+3u;
    if(v<0)v+=0x7FFF;
    return v>>15;
}
/* FUN_800695EC: the roots (at head height at most), taken every fourth frame
 * while Yoshimitsu's 'S' camera byte makes the replay skip. */
static void replay_targets(cam_t* c,const fig_t f[2]) {
    int skip=-1,k;
    if(f[0].bank_type==BANK_YOSHIMITSU && camera_byte(&f[0])==CAMERA_BYTE_SKIP)skip=0;
    else if(f[1].bank_type==BANK_YOSHIMITSU && camera_byte(&f[1])!=CAMERA_BYTE_SKIP)skip=-1;
    else if(f[1].bank_type==BANK_YOSHIMITSU)skip=1;
    if(skip<0)c->replay_track=-1;
    else {
        int16_t was=c->replay_track;
        c->replay_track=s16(c->replay_track+1);
        if(was<0)c->replay_track=0;
    }
    for(k=0;k<2;k++) {
        if(c->replay_track>=0 && (c->replay_track&3))continue;
        c->replay_targets[k]=f[k].root_x;c->replay_targets[4+k]=f[k].root_z;
        c->replay_targets[2+k]=f[k].root_y<f[k].head_y?f[k].root_y:f[k].head_y;
        c->target[k][0]=c->replay_targets[k];c->target[k][1]=c->replay_targets[2+k];
        c->target[k][2]=c->replay_targets[4+k];
    }
}
void t3_psx_camera_replay_step(cam_t* c,const tab_t* t,const fig_t f[2]) {
    int32_t d;
    if(!c || !t3_psx_camera_valid(t) || !f || !c->replay_on)return;
    replay_targets(c,f);
    frame_fighters(c,t,c->replay_pitch,(int32_t)c->replay_swing+c->replay_yaw,c->replay_reset,c->replay_distance,
                   T3_PSX_CAMERA_SCRIPT);
    d=s18((int64_t)c->replay_swing_to-c->replay_swing);
    c->replay_reset=0;
    c->replay_swing=s16(c->replay_swing+div_trunc(d,128));
    c->replay_distance=s16(c->replay_distance+div_trunc((int64_t)(0x113D-c->replay_distance)*6,256));
    use_source(c,T3_PSX_CAMERA_SCRIPT);
    c->enabled=0;
}
int t3_psx_camera_replay_start(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr) {
    int32_t axis;
    if(!c || !t3_psx_camera_valid(t) || !f || !fr || !fr->rand || !fr->ai_rand)return 0;
    t3_psx_camera_reset(c);
    init_sources(c,f);
    c->replay_track=-1;c->replay_on=1;
    c->weight[0]=0;c->weight[1]=ONE;c->weight[2]=0;
    c->replay_reset=1;c->replay_distance=0x2B1A;c->replay_pitch=0;
    c->replay_style=(uint8_t)((c->replay_style+1)&1);
    axis=atan4096(t,(int64_t)c->target[1][0]-c->target[0][0],(int64_t)c->target[1][2]-c->target[0][2]);
    c->replay_yaw=s16((int64_t)axis*64);
    next_rand(fr->rand);
    if(!c->replay_style) {
        int32_t a,b;
        c->replay_yaw=s16(c->replay_yaw-0x71C7+ai_random(fr,0xE38E));
        a=(ai_random(fr,0x1E)+0x19)*0x40000;
        c->replay_swing=s16(div_trunc(a,360));
        b=(ai_random(fr,0x1E)+0x19)*0x40000;
        c->replay_swing_to=s16(-div_trunc(b,360));
        if(next_rand(fr->rand)&1){c->replay_swing=s16(-c->replay_swing);c->replay_swing_to=s16(-c->replay_swing_to);}
    } else {
        int32_t r=ai_random(fr,0x15555);
        c->replay_swing_to=c->replay_swing=0;
        c->replay_yaw=s16(c->replay_yaw+0x5556+r);
    }
    t3_psx_camera_replay_step(c,t,f);
    return 1;
}

int t3_psx_camera_step(cam_t* c,const tab_t* t,const fig_t f[2],const t3_psx_camera_frame_t* fr,int16_t* phase) {
    if(!c || !t3_psx_camera_valid(t) || !f || !fr || !fr->rand || !phase)return 0;
    c->skip_side=-1;
    if(f[0].bank_type==BANK_YOSHIMITSU && camera_byte(&f[0])==CAMERA_BYTE_SKIP)c->skip_side=0;
    else if(f[1].bank_type==BANK_YOSHIMITSU && camera_byte(&f[1])==CAMERA_BYTE_SKIP)c->skip_side=1;
    switch(*phase) {
    case T3_PSX_CAMERA_RESET:case T3_PSX_CAMERA_INTRO:
        if(*phase==T3_PSX_CAMERA_RESET) {
            t3_psx_camera_reset(c);
            init_sources(c,f);init_sources(c,f);
            c->replay_on=0;   /* FUN_800699F0 */
            intro_start(c,t,f,fr,1);
            c->script=-1;c->stream_frame=-1;c->stream=-1;
            *phase=T3_PSX_CAMERA_INTRO;
        }
        if(c->enabled && intro_frame(c,t,f,fr))*phase=T3_PSX_CAMERA_FIGHT_PHASE;
        return 1;
    case T3_PSX_CAMERA_FIGHT_PHASE:
        return c->enabled?fight_frame(c,t,f,fr):1;
    case T3_PSX_CAMERA_WINNER_0:case T3_PSX_CAMERA_WINNER_1:
        if(!fr->camera_rand)return 0;
        c->shake_script=-1;
        c->round_end_player=(int16_t)(*phase==T3_PSX_CAMERA_WINNER_1);
        winner_start(c,t,&f[c->round_end_player],fr);
        *phase=T3_PSX_CAMERA_WINNER;
        return winner_step(c,t,f);
    case T3_PSX_CAMERA_WINNER:
        return winner_step(c,t,f);
    case T3_PSX_CAMERA_LOSER_0:case T3_PSX_CAMERA_LOSER_1:
        c->shake_script=-1;
        return loser_step(c,t,&f[*phase==T3_PSX_CAMERA_LOSER_1]);
    default:
        return -1;
    }
}

void t3_psx_camera_shake_start(cam_t* c,int script) {
    if(!c)return;
    c->shake_script=(int16_t)(script>=0 && script<T3_PSX_CAMERA_SHAKES?script:-1);
    c->shake_pos=0;
}
void t3_psx_camera_shake_step(cam_t* c,const tab_t* t) {
    int32_t v;
    if(!c || !t3_psx_camera_valid(t) || c->shake_script<0 || c->shake_script>=T3_PSX_CAMERA_SHAKES ||
       c->shake_pos<0 || c->shake_pos>=T3_PSX_CAMERA_SHAKE_BYTES)return;
    v=t->shakes[c->shake_script][c->shake_pos++];
    if(v==SHAKE_END){c->shake_script=-1;return;}
    c->view.pitch+=v;
}

int t3_psx_camera_build_view(const t3_psx_camera_view_t* v,const tab_t* t,t3_psx_camera_matrix_t* out) {
    int32_t sy,cy,sx,cx,ry[9],rx[9],r[9],e[3];int i,k,col;
    if(!v || !t3_psx_camera_valid(t) || !out || v->roll)return 0;
    sy=sn(t,-v->yaw&0xFFF);cy=cs(t,-v->yaw&0xFFF);
    sx=sn(t,-v->pitch&0xFFF);cx=cs(t,-v->pitch&0xFFF);
    ry[0]=cy;ry[1]=0;ry[2]=-sy;ry[3]=0;ry[4]=ONE;ry[5]=0;ry[6]=sy;ry[7]=0;ry[8]=cy;
    rx[0]=ONE;rx[1]=0;rx[2]=0;rx[3]=0;rx[4]=cx;rx[5]=sx;rx[6]=0;rx[7]=-sx;rx[8]=cx;
    /* MulMatrix: one MVMVA per column, saturated to 16 bits. */
    for(col=0;col<3;col++)for(i=0;i<3;i++) {
        int64_t s=((int64_t)rx[3*i]*ry[col]+(int64_t)rx[3*i+1]*ry[3+col]+(int64_t)rx[3*i+2]*ry[6+col])>>12;
        r[3*i+col]=s<-32768?-32768:s>32767?32767:(int32_t)s;
    }
    for(k=3;k<6;k++)r[k]=s16(t12((int64_t)r[k]*ASPECT));
    for(i=0;i<3;i++)for(k=0;k<3;k++)out->m[i][k]=(int16_t)r[3*i+k];
    e[0]=-v->x;e[1]=-v->y;e[2]=-v->z;
    apply_lv((const int16_t(*)[3])out->m,e,out->t);
    out->h=v->h;
    return 1;
}

/* The GTE's H / SZ3 (0.16, at most 0x1FFFF) by its unsigned Newton-Raphson
 * reciprocal. */
static int32_t gte_divide(uint32_t h,uint32_t sz) {
    uint32_t z=0,n,d,u,i;
    if(h>=sz*2u)return 0x1FFFF;
    while(z<16u && !(sz&(0x8000u>>z)))z++;
    n=h<<z;d=sz<<z;
    i=(d-0x7FC0u)>>7;
    u=((0x40000u/(i+0x100u)+1u)>>1);
    u=(u>0x101u?u-0x101u:0u)+0x101u;
    d=(uint32_t)((int64_t)0x2000080-(int64_t)d*u)>>8;
    d=(uint32_t)((int64_t)0x0000080+(int64_t)d*u)>>8;
    n=(uint32_t)(((uint64_t)n*d+0x8000u)>>16);
    return n>0x1FFFFu?0x1FFFF:(int32_t)n;
}
static int32_t sat16(int32_t v){return v<-32768?-32768:v>32767?32767:v;}
static int32_t clamp(int32_t v,int32_t lo,int32_t hi){return v<lo?lo:v>hi?hi:v;}
void t3_psx_camera_project(const t3_psx_camera_matrix_t* m,const int32_t p[3],int32_t out[3]) {
    int32_t v[3],sz,div;int i;
    apply_lv(m->m,p,v);
    for(i=0;i<3;i++)v[i]=w32((int64_t)v[i]+m->t[i]);
    sz=clamp(v[2],0,0xFFFF);
    div=gte_divide((uint32_t)m->h&0xFFFFu,(uint32_t)sz);
    out[0]=clamp((int32_t)(((int64_t)div*sat16(v[0])+(SCREEN_OFFSET_X<<16))>>16),-0x400,0x3FF);
    out[1]=clamp((int32_t)(((int64_t)div*sat16(v[1])+(SCREEN_OFFSET_Y<<16))>>16),-0x400,0x3FF);
    out[2]=sz;
}

void t3_psx_backdrop_setup(t3_psx_backdrop_turn_t* b,const t3_psx_camera_view_t* v) {
    b->angle=0;b->prev_yaw=b->yaw=v->yaw&0xFFF;
}
/* StageBackgroundDraw's turn (FUN_8006E4A0): while the camera holds its yaw,
 * the panorama turns by 2/3 of the camera's sideways slide, at most 0xAA. */
int t3_psx_backdrop_step(t3_psx_backdrop_turn_t* b,const t3_psx_camera_view_t* v,const tab_t* t,int intro) {
    int32_t bearing,y,u,turned,d,turn=0;
    if(!b || !v || !t3_psx_camera_valid(t))return 0;
    bearing=s16(atan4096(t,(int64_t)b->point_x-v->x,(int64_t)b->point_z-v->z));
    y=v->yaw&0xFFF;
    b->prev_yaw=b->yaw&0xFFF;b->yaw=y;
    b->point_x=w32((int64_t)sn(t,-y&0xFFF)+v->x);
    b->point_z=w32((int64_t)cs(t,-y&0xFFF)+v->z);
    u=(bearing-0x400)&0xFFF;
    if(!intro) {
        turned=s16(b->prev_yaw*16-y*16);
        if(turned<0)turned=s16(-turned);
        if(turned<0x11) {
            d=s16(u*16-y*16);
            if(d<0) {
                int32_t q=(int32_t)((uint32_t)((y-u)*16)&0xFFFFu)/0x18;
                turn=(q>0xAA?-0xAA:-q)&0xFFF;
            } else {
                int32_t q=d/0x18;
                turn=q<=0xAA?q&0xFFF:0xAA;
            }
        }
    }
    b->angle=(b->angle+turn)&0xFFF;
    return 1;
}
