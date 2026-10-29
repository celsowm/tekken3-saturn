/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Billboard conventions adapted from pinned Tekken3Remake effects.md and
 * projection_sim.py, checked against original 0x8006F8A8/0x8003A6E4/0x8006E734.
 * Changes: portable C, caller-owned image state, exact s16 deltas, explicit
 * cull/depth results; no original pointers/GPU packets. See NOTICE.md. */
#include "psx_particle_projection.h"

static int16_t s16(uint32_t v) {
    const uint32_t u=v&0xFFFFu;
    return (int16_t)(u<0x8000u?(int32_t)u:(int32_t)u-0x10000);
}
static int32_t trig_value(const t3_psx_trig_t* t,unsigned angle,unsigned cosine) {
    const unsigned at=2u*(angle&0xFFFu)+(cosine?0x800u:0u);
    return s16((uint32_t)t->data[at]|((uint32_t)t->data[at+1]<<8));
}
static int32_t mul(int32_t a,int32_t b) {return (a*b)>>12;}

int t3_psx_particle_build_view(const t3_psx_camera_view_t* c,
                              const t3_psx_trig_t* t,t3_psx_particle_view_t* out) {
    if(!c || !t || !t->data || t->size<T3_PSX_TRIG_BYTES || !out)return 0;
    const unsigned p=(unsigned)c->pitch,y=(unsigned)c->yaw,z=0u-(unsigned)c->roll;
    const int32_t sp=trig_value(t,p,0),cp=trig_value(t,p,1);
    const int32_t sy=trig_value(t,y,0),cy=trig_value(t,y,1);
    const int32_t sr=trig_value(t,z,0),cr=trig_value(t,z,1);
    const int32_t a=mul(sp,cr),b=mul(sp,sr),d=mul(cp,cr),e=mul(cp,sr);
    /* Preserve the original sequence of GPF/truncation operations. A float
     * Euler matrix or combined product changes the last bits. */
    out->m[0][0]=s16((uint32_t)mul(cy,cr));
    out->m[0][1]=s16((uint32_t)mul(cy,-sr));
    out->m[0][2]=(int16_t)sy;
    out->m[1][0]=s16((uint32_t)(mul(sy,a)+e));
    out->m[1][1]=s16((uint32_t)(d-mul(sy,b)));
    out->m[1][2]=s16((uint32_t)-mul(cy,sp));
    out->m[2][0]=s16((uint32_t)(b-mul(sy,d)));
    out->m[2][1]=s16((uint32_t)(mul(e,sy)+a));
    out->m[2][2]=s16((uint32_t)mul(cy,cp));
    for(unsigned k=0;k<3;++k)out->m[1][k]=(int16_t)((out->m[1][k]*20)/12);
    out->origin[0]=c->x;out->origin[1]=c->y;out->origin[2]=c->z;out->h=c->h;
    return 1;
}

int t3_psx_particle_project(const t3_psx_particle_view_t* v,const int32_t p[3],
                            const t3_psx_particle_model_t* model,unsigned flags,
                            t3_psx_particle_quad_t* out) {
    if(!v || !p || !model || !out)return 0;
    /* SetRotMatrix(identity scaled by 20/12); SetTransMatrix(rotated centre).
     * Translation is a MAC result, not a saturated IR result. */
    t3_psx_camera_matrix_t billboard={{{4096,0,0},{0,6826,0},{0,0,4096}},{0,0,0},v->h};
    int32_t delta[3];
    for(unsigned k=0;k<3;++k)delta[k]=s16((uint32_t)p[k]-(uint32_t)v->origin[k]);
    for(unsigned r=0;r<3;++r) {
        int32_t sum=0;
        for(unsigned k=0;k<3;++k)sum+=v->m[r][k]*delta[k];
        billboard.t[r]=sum>>12;
    }
    for(unsigned i=0;i<4;++i) {
        const int32_t point[3]={model->corners[i][0],model->corners[i][1],model->corners[i][2]};
        int32_t projected[3];
        t3_psx_camera_project(&billboard,point,projected);
        out->xy[i][0]=(int16_t)projected[0];out->xy[i][1]=(int16_t)projected[1];
        out->depth[i]=(uint16_t)projected[2];
    }
    int bucket=(out->depth[1]>>5)&1023;
    if(flags&0x20u)bucket-=5;
    out->bucket=(int16_t)bucket;
    out->visible=(uint8_t)(out->xy[0][0]<=368 && out->xy[1][0]>=0 &&
                          out->xy[0][1]<=468 && out->xy[2][1]>=20 && bucket>=0);
    return 1;
}
