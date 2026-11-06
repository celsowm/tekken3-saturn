/* Golden-case test of the Tekken Force walls (src/game/psx_force_walls.c); cases from
 * scripts/export_force_walls_cases.py (T3_FORCE_WALLS_CASES=build/force-cases/walls.bin). Skips (77) without. */
#include "psx_force_walls.h"
#include <stdio.h>
#include <stdlib.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void){
    const char* path=getenv("T3_FORCE_WALLS_CASES");
    FILE* f=path?fopen(path,"rb"):NULL;
    if(!f){puts("skip: no golden cases");return 77;}
    fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,f)!=(size_t)size){fclose(f);return 1;}
    fclose(f);
    if((uint32_t)be32(d)!=0x54335753u){fputs("bad case file\n",stderr);return 1;}
    const int32_t cases=be32(d+8);
    const uint8_t* at=d+12;
    unsigned bad=0;
    for(int32_t c=0;c<cases;++c,at+=28){
        int32_t v[7],dx,dz;
        for(int i=0;i<7;++i)v[i]=be32(at+4*i);
        const t3_force_walls_t w=t3_force_walls_for_level((unsigned)v[0]);
        t3_force_walls(&w,v[1],v[2],v[3],v[4],15u+(unsigned)v[0],&dx,&dz);
        if(dx!=v[5]||dz!=v[6]){if(bad<5)printf("case %d: dx %d/%d dz %d/%d\n",c,dx,v[5],dz,v[6]);++bad;}
    }
    printf("%d cases, %u mismatches\n",cases,bad);
    free(d);
    return bad?1:0;
}
