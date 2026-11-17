/* Golden-case test of Tekken Ball's ball and shadow matrices (src/game/ball_pose.c): the cases come from
 * scripts/export_ball_pose_cases.py (set T3_BALL_POSE_CASES to build/ball-cases/pose.bin). Skips (77) when the file is
 * missing. */
#include "ball_pose.h"
#include <stdio.h>
#include <stdlib.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void) {
    const char* path=getenv("T3_BALL_POSE_CASES");
    FILE* fp=path?fopen(path,"rb"):NULL;
    if(!fp){puts("skip: no golden cases");return 77;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,fp)!=(size_t)size){fclose(fp);return 1;}
    fclose(fp);
    if((uint32_t)be32(d)!=0x54335042u||be32(d+4)!=1){fputs("bad case file\n",stderr);return 1;}
    const int cases=be32(d+8);
    const uint8_t* trig=d+12;
    const uint8_t* at=trig+10240;
    unsigned bad=0,bad_ball=0,bad_shadow=0;
    for(int c=0;c<cases;++c,at+=4*(7+18)) {
        const int32_t spin[3]={be32(at),be32(at+4),be32(at+8)},axis[3]={be32(at+12),be32(at+16),be32(at+20)},q=be32(at+24);
        int32_t ball[3][3],shadow[3][3];
        int ok_ball=1,ok_shadow=1;
        t3_ball_pose(trig,spin,axis,q,ball);
        t3_ball_shadow_pose(trig,spin[1],axis[1],q,shadow);
        for(int i=0;i<9;++i) {
            ok_ball&=((int16_t)ball[i/3][i%3])==be32(at+28+4*i);
            ok_shadow&=((int16_t)shadow[i/3][i%3])==be32(at+28+36+4*i);
        }
        if(!ok_ball||!ok_shadow) {
            if(bad<6)printf("case %d: ball %s shadow %s (spin %d %d %d, axis y %d z %d, squash %d)\n",c,ok_ball?"ok":"BAD",ok_shadow?"ok":"BAD",
                            spin[0],spin[1],spin[2],axis[1],axis[2],q);
            ++bad;bad_ball+=!ok_ball;bad_shadow+=!ok_shadow;
        }
    }
    printf("%d cases, %u mismatches (ball %u, shadow %u)\n",cases,bad,bad_ball,bad_shadow);
    free(d);
    return bad?1:0;
}
