/* The first record of the PS1 trace (fight frames 3523..3531, after each frame's update):
 * spawn at the hit with the contact point and the slot's vector, then step once per frame.
 * The numbers are read from PCSX-Redux RAM (USA 0x800A3408), not game data files. */
#include <stdio.h>
#include "psx_flipbook.h"
int main(void) {
    static const int32_t pos[9][3]={{-4257,-407,-168},{-4294,-428,-170},{-4328,-448,-173},{-4359,-466,-175},
        {-4387,-483,-178},{-4414,-499,-180},{-4438,-513,-182},{-4461,-526,-183},{-4482,-539,-185}};
    static const int32_t vel[9][3]={{-5084,-2986,-390},{-4687,-2753,-360},{-4321,-2538,-332},{-3984,-2340,-307},
        {-3673,-2158,-284},{-3387,-1990,-262},{-3123,-1835,-242},{-2880,-1692,-224},{-2655,-1560,-207}};
    t3_psx_flipbook_pool_t pool={0};
    const int32_t point[3]={-4217,-383,-164};const int16_t vector[3]={-417,-245,-32};
    t3_psx_flipbook_spawn(&pool,3,0,27,point,vector,0,0);
    t3_psx_flipbook_spawn(&pool,0,0,30,point,vector,1,0);
    int bad=0;
    for(unsigned f=0;f<9;++f) {
        t3_psx_flipbook_step(&pool);
        const t3_psx_flipbook_t* r=&pool.record[0];
        for(unsigned k=0;k<3;++k) {
            if((r->pos[k]>>7)!=pos[f][k] || r->vel[k]!=vel[f][k]) {
                printf("frame %u axis %u: pos %d (want %d) vel %d (want %d)\n",f,k,r->pos[k]>>7,pos[f][k],r->vel[k],vel[f][k]);++bad;
            }
        }
        if(r->frame!=f+2u){printf("frame counter %u want %u\n",r->frame,f+2u);++bad;}
        const t3_psx_flipbook_t* c=&pool.record[1];
        if(c->vel[0]||c->vel[1]||c->vel[2]||(c->pos[0]>>7)!=-4217){printf("character record moved\n");++bad;}
    }
    for(unsigned f=0;f<30;++f)t3_psx_flipbook_step(&pool);
    if(pool.record[0].active||pool.record[1].active){printf("records did not end\n");++bad;}
    printf(bad?"FAIL\n":"flipbook state matches the PS1 trace\n");
    return bad?1:0;
}
