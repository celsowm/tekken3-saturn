/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Tekken Ball's ball lighting; adapted from the pinned Tekken3Remake's tools/research/projection_sim.py (color_mat_col) and
 * ball_sim.py (ball_draw). See ball_light.h. */
#include "ball_light.h"

static int32_t clamp15(int32_t v){return v<0?0:v>0x7FFF?0x7FFF:v;}

void t3_ball_light_vertex(const t3_ball_light_t* l,const int32_t n[3],int32_t out[3]) {
    int32_t ir[3],lcm[3][3];
    unsigned i,k;
    /* LIGHT: the light matrix · normal. Row 0 is the stage's light, doubled (SetLightMatrix of the doubled first row, a
     * halfword wrap); row 1 the fill light from below; row 2 is the effects' point light, off. */
    ir[0]=clamp15(((int16_t)(uint16_t)(l->light[0]*2)*n[0]+(int16_t)(uint16_t)(l->light[1]*2)*n[1]+(int16_t)(uint16_t)(l->light[2]*2)*n[2])>>12);
    ir[1]=clamp15((4096*n[1])>>12);
    ir[2]=0;
    for(k=0;k<(unsigned)(l->power>1u?l->power:1u);++k)   /* SQR, at least once */
        for(i=0;i<3u;++i)ir[i]=clamp15((ir[i]*ir[i])>>12);
    /* CC: the colour matrix (column 0 the light colour, column 1 half of it, both << 4) and the back colour (<< 4) */
    for(i=0;i<3u;++i){lcm[i][0]=l->colour[i]<<4;lcm[i][1]=l->fill?(l->colour[i]>>1)<<4:0;lcm[i][2]=0;}
    for(i=0;i<3u;++i)out[i]=clamp15(((int32_t)(l->back[i]<<4)+((lcm[i][0]*ir[0]+lcm[i][1]*ir[1]+lcm[i][2]*ir[2])>>12)));
}

unsigned t3_ball_light_channel(unsigned face,int32_t light) {
    const int32_t v=((int32_t)face*light)>>12;
    return (unsigned)(v>255?255:v);
}
