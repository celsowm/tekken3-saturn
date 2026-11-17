/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of Tekken Ball's backdrop placement as ported in the pinned Tekken3Remake
 * (tools/research/ball_sim.py background_draw). See ball_backdrop.h for the changes. */
#include "ball_backdrop.h"
#include "sh2_divide.h"

static int32_t w32(int64_t v){return (int32_t)(uint32_t)v;}
static int32_t trig16(const t3_psx_trig_t* t,unsigned byte_offset) {
    return (int16_t)(uint16_t)(t->data[byte_offset]|((uint16_t)t->data[byte_offset+1]<<8));
}

int t3_ball_backdrop_step(t3_ball_backdrop_t* s,const t3_psx_camera_view_t* camera,const t3_psx_camera_matrix_t* view,
                          const t3_psx_trig_t* trig,int32_t flow,int32_t kind,t3_ball_backdrop_out_t* out) {
    int32_t yaw,cs,sn,pos[3],v[3],scroll,point[3],projected[3],first;
    if(!s||!camera||!view||!trig||!trig->data||!out)return 0;
    if(kind==1)return 1;                                   /* the panorama is skipped */
    if(kind!=s->last_kind)s->yaw_ref=camera->yaw;
    yaw=camera->yaw&0xFFF;
    cs=trig16(trig,0x800u+2u*(unsigned)yaw);sn=trig16(trig,2u*(unsigned)yaw);
    s->last_kind=kind;
    pos[0]=camera->x;pos[1]=camera->y;pos[2]=camera->z;
    t3_psx_camera_view_point(view,pos,v);
    scroll=(t3_div32(w32((int64_t)v[0]*cs+(int64_t)v[2]*sn),0x60)>>12)+(w32((int64_t)s->yaw_ref-camera->yaw)>>1);
    scroll=w32(scroll);
    scroll-=BALL_STRIP_WIDTH*t3_div32(scroll,BALL_STRIP_WIDTH);   /* C remainder, as _cmod */
    if(scroll<0)scroll+=BALL_STRIP_WIDTH;
    first=2;
    if(flow==2&&kind!=0)first=16;
    else if(flow>=3)first=10;
    point[0]=w32((int64_t)w32((int64_t)camera->x-(int64_t)sn*0x1A00)>>12);
    point[1]=0x100;
    point[2]=w32((int64_t)w32((int64_t)camera->z+(int64_t)cs*0x1A00)>>12);
    t3_psx_camera_project(view,point,projected);
    out->scroll=scroll;out->first_shift=first*BALL_TILE;
    out->top=projected[1]-BALL_STRIP_ROWS*BALL_TILE;
    return 1;
}
