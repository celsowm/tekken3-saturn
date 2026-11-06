/* Golden-case test of the Tekken Force target choice (src/game/psx_force_target.c): the cases come from
 * scripts/export_force_target_cases.py (set T3_FORCE_TARGET_CASES to build/force-cases/target.bin).
 * Skips (77) when the file is missing. */
#include "psx_force_target.h"
#include <stdio.h>
#include <stdlib.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void){
    const char* path=getenv("T3_FORCE_TARGET_CASES");
    FILE* f=path?fopen(path,"rb"):NULL;
    if(!f){puts("skip: no golden cases");return 77;}
    fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,f)!=(size_t)size){fclose(f);return 1;}
    fclose(f);
    if((uint32_t)be32(d)!=0x54335447u||be32(d+8)!=26){fputs("bad case file\n",stderr);return 1;}
    const int32_t cases=be32(d+12);
    const uint8_t* at=d+16;
    unsigned bad=0;
    for(int32_t c=0;c<cases;++c){
        int32_t v[26],w[3];
        for(int i=0;i<26;++i)v[i]=be32(at+4*i);
        at+=4*26;
        for(int i=0;i<3;++i)w[i]=be32(at+4*i);
        at+=12;
        t3_force_target_t s={v[0],(uint32_t)v[1],(uint32_t)v[2],v[3]};
        t3_force_target_inputs_t in={0};
        t3_force_candidate_t a={(uint8_t)v[7],(uint8_t)v[9],(uint32_t)v[4],(int16_t)v[17],(int16_t)v[18]};
        t3_force_candidate_t b={(uint8_t)v[8],(uint8_t)v[10],(uint32_t)v[5],(int16_t)v[19],(int16_t)v[20]};
        in.player_index=(uint8_t)v[11];
        for(int i=0;i<5;++i)in.pair[i]=v[12+i];
        for(int i=0;i<4;++i)in.quadrant_order[i]=v[22+i];
        in.pad_pressed=(int16_t)v[6];
        const int pick=t3_force_choose_target(&s,&in,&a,&b);
        if(pick!=w[0]||s.wait!=w[1]||(int32_t)s.manual_side!=w[2]){
            if(bad<5)printf("case %d: pick %d/%d wait %d/%d side %d/%d\n",c,pick,w[0],s.wait,w[1],(int)s.manual_side,w[2]);
            ++bad;
        }
    }
    printf("%d cases, %u mismatches\n",cases,bad);
    free(d);
    return bad?1:0;
}
