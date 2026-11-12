/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * See psx_force_floor.h. */
#include "psx_force_floor.h"

static unsigned wrap(int32_t v) {
    int32_t m=v%T3_FORCE_FLOOR_PATTERN;
    return (unsigned)(m<0?m+T3_FORCE_FLOOR_PATTERN:m);
}

unsigned t3_force_floor_entry(int32_t tx,int32_t r) {
    return wrap(r)*T3_FORCE_FLOOR_PATTERN+wrap(tx+5);
}

void t3_force_floor_corners(int32_t tx,int32_t r,int32_t x[4],int32_t z[4]) {
    const int32_t x0=tx*T3_FORCE_FLOOR_TILE,x1=x0+T3_FORCE_FLOOR_TILE;
    const int32_t z1=T3_FORCE_FLOOR_EDGE-r*T3_FORCE_FLOOR_TILE,z0=z1-T3_FORCE_FLOOR_TILE;
    x[0]=x0;z[0]=z1;x[1]=x1;z[1]=z1;x[2]=x0;z[2]=z0;x[3]=x1;z[3]=z0;
}

void t3_force_floor_quad_order(unsigned orient,uint8_t out[4]) {
    /* FloorSetup's four corner sets, each listing c0..c3 as (u, v) in {0, 1} (1 = 63). */
    static const uint8_t set[4][4][2]={
        {{0,0},{1,0},{0,1},{1,1}},
        {{0,1},{0,0},{1,1},{1,0}},
        {{1,1},{0,1},{1,0},{0,0}},
        {{1,0},{1,1},{0,0},{0,1}},
    };
    /* Entry bits 25-26: the order in which c0..c3 go to v0..v3. */
    static const uint8_t order[4][4]={{0,1,2,3},{1,0,3,2},{2,3,0,1},{3,2,1,0}};
    const unsigned s=orient>>2&3u,o=orient&3u;
    uint8_t tex[4][2];
    for(unsigned v=0;v<4;++v){tex[v][0]=set[s][order[o][v]][0];tex[v][1]=set[s][order[o][v]][1];}
    /* top-left, top-right, bottom-right, bottom-left texture corners */
    static const uint8_t want[4][2]={{0,0},{1,0},{1,1},{0,1}};
    for(unsigned k=0;k<4;++k) {
        out[k]=0;
        for(unsigned v=0;v<4;++v)if(tex[v][0]==want[k][0] && tex[v][1]==want[k][1])out[k]=(uint8_t)v;
    }
}
