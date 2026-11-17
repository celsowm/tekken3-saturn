/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the CPU opponent's Tekken Ball hooks as ported in the pinned Tekken3Remake (tools/research/ai_update.py).
 * See ball_ai.h for the changes. */
#include "ball_ai.h"
#include "sh2_divide.h"

static int32_t w32(int64_t v){return (int32_t)(uint32_t)v;}
/* (v < 0 ? v + 0xFFF : v) >> 12 */
static int32_t trunc12(int64_t v){const int32_t x=w32(v);return (x<0?x+0xFFF:x)>>12;}
static int32_t rdiv(int32_t a,int32_t b) {
    if(b==0)return a>=0?-1:1;
    if(b==-1)return w32(-(int64_t)a);
    return t3_div32(a,b);
}

void t3_ball_ai_project(const t3_ball_tables_t* t,const int32_t ball[3],const int32_t p0[2],const int32_t p1[2],int32_t line[BALL_LINE_WORDS]) {
    const int32_t x0=p0[0],z0=p0[1],x1=p1[0],z1=p1[1];
    const int32_t dx=w32((int64_t)x1-x0),dz=w32((int64_t)z1-z0);
    const int32_t angle=t3_ball_atan2(t,dx,dz);
    const int32_t c=t3_ball_cos(t,-angle),s=t3_ball_sin(t,-angle);
    const int32_t k=w32((int64_t)z0-rdiv(w32((int64_t)dz*x0),dx));
    line[0]=trunc12((int64_t)x0*c-(int64_t)w32((int64_t)z0-k)*s);
    line[1]=trunc12((int64_t)x1*c-(int64_t)w32((int64_t)z1-k)*s);
    line[2]=trunc12((int64_t)ball[0]*c-(int64_t)w32((int64_t)ball[2]-k)*s);
    line[3]=ball[1];
    line[4]=trunc12((int64_t)ball[0]*s+(int64_t)w32((int64_t)ball[2]-k)*c);
}

int32_t t3_ball_ai_distance(const int32_t line[BALL_LINE_WORDS],int player) {
    const int32_t d=w32((int64_t)line[player&1]-line[2]),lat=line[4];
    return (int32_t)t3_ball_isqrt((uint32_t)w32((int64_t)w32((int64_t)d*d)+w32((int64_t)lat*lat)));
}

int t3_ball_ai_behind(const int32_t line[BALL_LINE_WORDS],int player) {
    return player==0?line[2]<line[0]:line[1]<line[2];
}

int t3_ball_ai_ahead(const int32_t line[BALL_LINE_WORDS],int player) {
    const int32_t ball=line[2];
    if(player==0){const int32_t p=line[0];return p<ball&&(int64_t)p+0x7D0<ball;}
    {const int32_t p=line[1];return ball<p&&(int64_t)p-0x7D0<ball;}
}
