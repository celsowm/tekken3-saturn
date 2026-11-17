/* Golden-case test of Tekken Ball's backdrop placement (src/game/ball_backdrop.c): the cases come from
 * scripts/export_ball_backdrop_cases.py (set T3_BALL_BACKDROP_CASES to build/ball-cases/backdrop.bin). Skips (77) when
 * the file is missing. */
#include "ball_backdrop.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void) {
    const char* path=getenv("T3_BALL_BACKDROP_CASES");
    FILE* fp=path?fopen(path,"rb"):NULL;
    if(!fp){puts("skip: no golden cases");return 77;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,fp)!=(size_t)size){fclose(fp);return 1;}
    fclose(fp);
    if((uint32_t)be32(d)!=0x54334244u||be32(d+4)!=1){fputs("bad case file\n",stderr);return 1;}
    const int cases=be32(d+8);
    const uint8_t* trig=d+12;
    const uint8_t* at=trig+10240;
    t3_psx_trig_t t={trig,10240};
    unsigned bad=0;
    for(int c=0;c<cases;++c) {
        const int32_t yaw=be32(at),ref=be32(at+4),last=be32(at+8),kind=be32(at+12),flow=be32(at+16);
        t3_psx_camera_view_t cam;
        t3_psx_camera_matrix_t m;
        memset(&cam,0,sizeof cam);memset(&m,0,sizeof m);
        cam.yaw=yaw;cam.x=be32(at+20);cam.y=be32(at+24);cam.z=be32(at+28);
        for(int i=0;i<9;++i)m.m[i/3][i%3]=(int16_t)be32(at+32+4*i);
        for(int i=0;i<3;++i)m.t[i]=be32(at+68+4*i);
        m.h=be32(at+80);
        const int32_t scroll=be32(at+84),top=be32(at+88);
        at+=92;
        t3_ball_backdrop_t s={ref,last};
        t3_ball_backdrop_out_t out={0,0,0};
        const int ok=t3_ball_backdrop_step(&s,&cam,&m,&t,flow,kind,&out);
        if(!ok||out.scroll!=scroll||out.top!=top) {
            if(bad<6)printf("case %d: scroll %d/%d top %d/%d (yaw %d ref %d flow %d kind %d)\n",c,out.scroll,scroll,out.top,top,yaw,ref,flow,kind);
            ++bad;
        }
    }
    printf("%d cases, %u mismatches\n",cases,bad);
    free(d);
    return bad?1:0;
}
