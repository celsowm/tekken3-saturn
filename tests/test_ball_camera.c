/* Golden-case test of the fight camera's framing with Tekken Ball's ball as a third target (t3_psx_camera_frame_fighters in
 * src/game/psx_camera.c): the cases come from scripts/export_ball_camera_cases.py (set T3_BALL_CAMERA_CASES to
 * build/ball-cases/camera.bin). Mode 7 cases frame the ball, the others check that nothing changed without it. Skips (77) when the
 * file is missing. */
#include "psx_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void) {
    const char* path=getenv("T3_BALL_CAMERA_CASES");
    FILE* fp=path?fopen(path,"rb"):NULL;
    if(!fp){puts("skip: no golden cases");return 77;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,fp)!=(size_t)size){fclose(fp);return 1;}
    fclose(fp);
    if((uint32_t)be32(d)!=0x54334341u||be32(d+4)!=1){fputs("bad case file\n",stderr);return 1;}
    const int cases=be32(d+8);
    const uint8_t* trig=d+12;
    const uint8_t* at=trig+10240;
    t3_psx_trig_t trig_table={trig,10240};
    static t3_psx_camera_tables_t tables;
    static t3_psx_camera_t cam;
    unsigned bad=0,mode7=0;
    tables.trig=&trig_table;
    for(int c=0;c<cases;++c) {
        int32_t v[36];
        for(int i=0;i<36;++i)v[i]=be32(at+4*i);
        at+=4*36;
        const int src=v[0];
        memset(&cam,0,sizeof cam);
        cam.faithful=1;
        cam.ball_on=(uint8_t)v[5];mode7+=cam.ball_on;
        for(int i=0;i<3;++i)cam.ball[i]=v[6+i];
        cam.source[src]=(t3_psx_camera_source_t){v[9],v[10],v[11],v[12],v[13],v[20]};
        for(int i=0;i<3;++i){cam.target[0][i]=v[14+i];cam.target[1][i]=v[17+i];}
        cam.h=(int16_t)v[20];
        cam.source_state[src].depth_step=v[21];cam.source_state[src].depth=v[22];cam.source_state[src].lateral=v[23];
        cam.height_term=v[24];cam.source_state[src].moved=(int16_t)v[25];
        t3_psx_camera_frame_fighters(&cam,&tables,v[1],v[2],v[3],v[4],src);
        const t3_psx_camera_source_t* r=&cam.source[src];
        const int32_t got[10]={r->x,r->y,r->z,r->pitch,r->yaw,cam.source_state[src].depth_step,cam.source_state[src].depth,
                               cam.source_state[src].lateral,cam.height_term,cam.source_state[src].moved};
        int same=1;
        for(int i=0;i<10;++i)if(got[i]!=v[26+i])same=0;
        if(!same) {
            if(bad<6)printf("case %d (src %d, ball %d): got %d %d %d %d %d / %d %d %d / %d %d, want %d %d %d %d %d / %d %d %d / %d %d\n",
                            c,src,(int)cam.ball_on,got[0],got[1],got[2],got[3],got[4],got[5],got[6],got[7],got[8],got[9],
                            v[26],v[27],v[28],v[29],v[30],v[31],v[32],v[33],v[34],v[35]);
            ++bad;
        }
    }
    printf("%d cases (%u with the ball), %u mismatches\n",cases,mode7,bad);
    free(d);
    return bad?1:0;
}
