/* Golden-case test of Tekken Ball's opponent choice (src/game/ball_flow.c): the cases come from
 * scripts/export_ball_flow_cases.py (set T3_BALL_FLOW_CASES to build/ball-cases/flow.bin). Skips (77) when the
 * file is missing. */
#include "ball_flow.h"
#include <stdio.h>
#include <stdlib.h>

static uint32_t be32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

int main(void) {
    const char* path=getenv("T3_BALL_FLOW_CASES");
    FILE* fp=path?fopen(path,"rb"):NULL;
    if(!fp){puts("skip: no golden cases");return 77;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,fp)!=(size_t)size){fclose(fp);return 1;}
    fclose(fp);
    if(be32(d)!=0x54334246u||be32(d+4)!=1){fputs("bad case file\n",stderr);return 1;}
    const uint32_t cases=be32(d+8);
    const uint8_t* at=d+12;
    unsigned bad=0;
    for(uint32_t c=0;c<cases;++c) {
        t3_ball_choice_t s={be32(at),be32(at+4),be32(at+8),{0},(uint8_t)be32(at+12)};
        for(int i=0;i<BALL_CHARACTERS;++i)s.met[i]=(uint16_t)be32(at+16+4*i);
        at+=4*26;
        const uint32_t key=be32(at),lcg=be32(at+4),first=be32(at+8);
        uint32_t want_met[BALL_CHARACTERS];
        for(int i=0;i<BALL_CHARACTERS;++i)want_met[i]=be32(at+12+4*i);
        at+=4*25;
        const uint32_t got=t3_ball_opponent(&s);
        int ok=got==key&&s.lcg==lcg&&s.first_done==first;
        for(int i=0;i<BALL_CHARACTERS;++i)if(s.met[i]!=want_met[i])ok=0;
        if(!ok) {
            if(bad<5)printf("case %u: key %u/%u lcg %u/%u first %u/%u\n",c,got,key,s.lcg,lcg,s.first_done,first);
            ++bad;
        }
    }
    printf("%u cases, %u mismatches\n",cases,bad);
    free(d);
    return bad?1:0;
}
