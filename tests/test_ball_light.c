/* Golden-case test of Tekken Ball's ball lighting (src/game/ball_light.c): the cases come from
 * scripts/export_ball_light_cases.py (set T3_BALL_LIGHT_CASES to build/ball-cases/light.bin). Skips (77) when the file is
 * missing. */
#include "ball_light.h"
#include <stdio.h>
#include <stdlib.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void) {
    const char* path=getenv("T3_BALL_LIGHT_CASES");
    FILE* fp=path?fopen(path,"rb"):NULL;
    if(!fp){puts("skip: no golden cases");return 77;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,fp)!=(size_t)size){fclose(fp);return 1;}
    fclose(fp);
    if((uint32_t)be32(d)!=0x54334C54u||be32(d+4)!=1){fputs("bad case file\n",stderr);return 1;}
    const int cases=be32(d+8);
    const uint8_t* at=d+12;
    unsigned bad=0;
    for(int c=0;c<cases;++c,at+=4*20) {
        t3_ball_light_t l;
        int32_t n[3],lit[3];
        unsigned i,ok=1;
        for(i=0;i<3u;++i){l.light[i]=be32(at+4*i);l.colour[i]=(uint8_t)be32(at+12+4*i);l.back[i]=(uint8_t)be32(at+24+4*i);n[i]=(int16_t)be32(at+44+4*i);}
        l.power=(uint8_t)be32(at+36);l.fill=(uint8_t)be32(at+40);
        t3_ball_light_vertex(&l,n,lit);
        for(i=0;i<3u;++i)ok&=t3_ball_light_channel((unsigned)be32(at+56+4*i),lit[i])==(unsigned)be32(at+68+4*i);
        if(!ok){if(bad<6)printf("case %d: light %d %d %d power %u normal %d %d %d\n",c,l.light[0],l.light[1],l.light[2],l.power,n[0],n[1],n[2]);++bad;}
    }
    printf("%d cases, %u mismatches\n",cases,bad);
    free(d);
    return bad?1:0;
}
