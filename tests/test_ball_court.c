/* Golden-case test of Tekken Ball's court bounds (t3_psx_arena_bounds_ball in src/game/psx_body.c): the cases come from
 * scripts/export_ball_court_cases.py (set T3_BALL_COURT_CASES to build/ball-cases/court.bin). Skips (77) when the file is
 * missing. */
#include "psx_body.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void) {
    const char* path=getenv("T3_BALL_COURT_CASES");
    FILE* fp=path?fopen(path,"rb"):NULL;
    if(!fp){puts("skip: no golden cases");return 77;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,fp)!=(size_t)size){fclose(fp);return 1;}
    fclose(fp);
    if((uint32_t)be32(d)!=0x54334354u||be32(d+4)!=1){fputs("bad case file\n",stderr);return 1;}
    const int cases=be32(d+8);
    const uint8_t* at=d+12;
    unsigned bad=0;
    for(int c=0;c<cases;++c) {
        int32_t v[16];
        for(int i=0;i<16;++i)v[i]=be32(at+4*i);
        at+=64;
        t3_psx_body_actor_t a;
        memset(&a,0,sizeof a);
        a.ramp=v[2];
        a.anchor=(t3_vec3i_t){v[3],v[4],v[5]};a.root=(t3_vec3i_t){v[6],v[7],v[8]};
        int across=0;
        if(v[1])a.ramp=0;                    /* the match restarts a throw victim's ramp before the call */
        const int ok=t3_psx_arena_bounds_ball(&a,(unsigned)v[0],&across);
        const int want[7]={v[9],v[10],v[11],v[12],v[13],v[14],v[15]};
        const int got[7]={a.anchor.x,a.anchor.y,a.anchor.z,a.root.x,a.root.y,a.root.z,across};
        int same=ok;
        for(int i=0;i<7;++i)if(got[i]!=want[i])same=0;
        if(!same) {
            if(bad<5)printf("case %d: got %d %d %d / %d %d %d / %d, want %d %d %d / %d %d %d / %d\n",c,got[0],got[1],got[2],got[3],got[4],got[5],got[6],
                            want[0],want[1],want[2],want[3],want[4],want[5],want[6]);
            ++bad;
        }
    }
    printf("%d cases, %u mismatches\n",cases,bad);
    free(d);
    return bad?1:0;
}
