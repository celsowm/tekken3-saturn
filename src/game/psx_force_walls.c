/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of fight_sim.py _force_walls in the pinned Tekken3Remake; see psx_force_walls.h. */
#include "psx_force_walls.h"

static int32_t w32(int64_t v){return (int32_t)(uint32_t)(uint64_t)v;}
static int32_t line_at(t3_force_line_t l,int32_t v){
    return w32((int64_t)w32((int64_t)v*l.k)/1000+l.c);
}

t3_force_walls_t t3_force_walls_for_level(unsigned level){
    t3_force_walls_t w={{-323,-2553},{329,1971},{-259,-2285}};
    if(level==3){w.left.k=-375;w.left.c=-2445;w.right.k=333;w.right.c=2175;}
    return w;
}

void t3_force_walls(const t3_force_walls_t* w,int32_t root_x,int32_t root_z,int enemy,int32_t view_x,
                    unsigned stage,int32_t* dx_out,int32_t* dz_out){
    int32_t dz=0,dx=0,v;
    if(w32((int64_t)root_z-0x8FC)>0)dz=w32(-(int64_t)w32((int64_t)root_z-0x8FC));
    v=w32((int64_t)root_z+0x898);
    if(stage==0x12)v=w32((int64_t)root_z-line_at(w->lower_z,w32((int64_t)root_x-view_x)));
    if(v<0)dz=w32(-(int64_t)v);
    if(!enemy){
        const int32_t rel=w32((int64_t)root_x-view_x);
        const int32_t lo=line_at(w->left,root_z),hi=line_at(w->right,root_z);
        if(w32((int64_t)rel-lo)<0)dx=w32(-(int64_t)w32((int64_t)rel-lo));
        v=w32((int64_t)rel-hi);
    }else{
        const int32_t t=w32((int64_t)w32((int64_t)root_x+0x1068)-view_x);
        if(t<0)dx=w32(-(int64_t)t);
        v=w32((int64_t)w32((int64_t)root_x-0x1068)-view_x);
    }
    if(v>0)dx=w32(-(int64_t)v);
    *dx_out=dx;*dz_out=dz;
}
