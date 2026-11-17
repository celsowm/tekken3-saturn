/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * See ball_pose.h. */
#include "ball_pose.h"

static int32_t sine(const uint8_t* trig,int32_t angle,unsigned cosine) {
    const unsigned off=2u*((unsigned)angle&4095u)+(cosine?2048u:0u);
    return (int16_t)(uint16_t)(trig[off]|((uint16_t)trig[off+1]<<8));
}

/* FUN_8003A6E4 (RotMatrix) with the GTE's three products per step (GPF: the scalar times each word, >> 12) and the
 * matrix stored as s16: row 0 = (cy cz, -cy sz, sy), row 1 = (sy sx cz + cx sz, cx cz - sy sx sz, -sx cy),
 * row 2 = (sx sz - sy cx cz, sy cx sz + sx cz, cy cx). */
void t3_ball_rotation(const uint8_t* trig,const int32_t a[3],int32_t m[3][3]) {
    const int32_t sx=sine(trig,a[0],0),cx=sine(trig,a[0],1),sy=sine(trig,a[1],0),cy=sine(trig,a[1],1),
                  sz=sine(trig,a[2],0),cz=sine(trig,a[2],1);
    const int32_t cxcz=(cx*cz)>>12,cxsz=(cx*sz)>>12;
    const int32_t a1=(sx*cz)>>12,a2=(sx*sz)>>12,a3=(sx*cy)>>12;
    const int32_t b1=(sy*a1)>>12,b2=(sy*a2)>>12,b3=(sy*cxcz)>>12;
    m[0][0]=(int16_t)((cy*cz)>>12);m[0][1]=(int16_t)((cy*-sz)>>12);m[0][2]=(int16_t)sy;
    m[1][0]=(int16_t)(b1+cxsz);m[1][1]=(int16_t)(cxcz-b2);m[1][2]=(int16_t)-a3;
    m[2][0]=(int16_t)(a2-b3);m[2][1]=(int16_t)(((cxsz*sy)>>12)+a1);m[2][2]=(int16_t)((cy*cx)>>12);
}

void t3_ball_multiply(const int32_t a[3][3],const int32_t b[3][3],int32_t out[3][3]) {
    int32_t t[3][3];
    for(unsigned i=0;i<3u;++i)for(unsigned j=0;j<3u;++j)t[i][j]=(a[i][0]*b[0][j]+a[i][1]*b[1][j]+a[i][2]*b[2][j])>>12;
    for(unsigned i=0;i<3u;++i)for(unsigned j=0;j<3u;++j)out[i][j]=t[i][j];
}

static void diagonal(int32_t q,int32_t s[3][3]) {
    for(unsigned i=0;i<3u;++i)for(unsigned j=0;j<3u;++j)s[i][j]=0;
    s[0][0]=0x1000-q;s[1][1]=0x1000+q;s[2][2]=0x1000+q;
}

void t3_ball_pose(const uint8_t* trig,const int32_t spin[3],const int32_t axis[3],int32_t squash,int32_t m[3][3]) {
    t3_ball_rotation(trig,spin,m);
    if(squash!=0) {
        int32_t a[3][3],s[3][3],t[3][3];
        t3_ball_rotation(trig,axis,a);
        diagonal(squash,s);
        for(unsigned i=0;i<3u;++i)for(unsigned j=0;j<3u;++j)t[i][j]=a[j][i];
        t3_ball_multiply(a,s,a);t3_ball_multiply(a,t,a);t3_ball_multiply(a,m,m);
    }
}

void t3_ball_shadow_pose(const uint8_t* trig,int32_t yaw,int32_t axis_yaw,int32_t squash,int32_t m[3][3]) {
    const int32_t ya[3]={0,yaw,0},qa[3]={0,axis_yaw,0};
    int32_t a[3][3],s[3][3];
    t3_ball_rotation(trig,ya,m);
    t3_ball_rotation(trig,qa,a);
    diagonal(squash,s);
    t3_ball_multiply(a,s,a);t3_ball_multiply(a,m,m);
}
