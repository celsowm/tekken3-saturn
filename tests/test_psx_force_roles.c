/* Golden-case test of Tekken Force's roles (psx_force_target.c t3_force_roles): cases from
 * scripts/export_force_roles_cases.py (T3_FORCE_ROLES_CASES=build/force-cases/roles.bin). Skips (77) without. */
#include "psx_force_target.h"
#include <stdio.h>
#include <stdlib.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void){
    const char* path=getenv("T3_FORCE_ROLES_CASES");
    FILE* f=path?fopen(path,"rb"):NULL;
    if(!f){puts("skip: no golden cases");return 77;}
    fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,f)!=(size_t)size){fclose(f);return 1;}
    fclose(f);
    if((uint32_t)be32(d)!=0x54335247u||be32(d+8)!=27){fputs("bad case file\n",stderr);return 1;}
    const int32_t cases=be32(d+12);
    const uint8_t* at=d+16;
    unsigned bad=0;
    for(int32_t c=0;c<cases;++c){
        int32_t v[27],w[7];
        for(int i=0;i<27;++i)v[i]=be32(at+4*i);
        at+=4*27;
        for(int i=0;i<7;++i)w[i]=be32(at+4*i);
        at+=28;
        t3_force_target_t s={v[0],(uint32_t)v[1],(uint32_t)v[2],v[3]};
        t3_force_target_inputs_t in={0};
        t3_force_roles_in_t r;
        in.pad_pressed=(int16_t)v[4];
        in.player_index=0;
        for(int i=0;i<3;++i){
            r.hidden[i]=(uint8_t)v[6+i];r.screen_x[i]=(uint16_t)v[9+i];
            r.quadrant[i]=(int16_t)v[12+i];r.rel[i]=(int16_t)v[15+i];r.index[i]=(uint8_t)i;
        }
        for(int i=0;i<5;++i)in.pair[i]=v[18+i];
        for(int i=0;i<4;++i)in.quadrant_order[i]=v[23+i];
        uint8_t opp[3],both;
        const unsigned target=t3_force_roles(&s,&in,&r,(uint8_t)v[5],opp,&both);
        if(opp[0]!=w[0]||opp[1]!=w[1]||opp[2]!=w[2]||both!=w[3]||s.wait!=w[4]||(int32_t)s.manual_side!=w[5]||(int32_t)target!=w[6]){
            if(bad<5)printf("case %d: opp %d,%d,%d/%d,%d,%d both %d/%d wait %d/%d side %d/%d target %u/%d\n",c,opp[0],opp[1],opp[2],
                w[0],w[1],w[2],both,w[3],s.wait,w[4],(int)s.manual_side,w[5],target,w[6]);
            ++bad;
        }
    }
    printf("%d cases, %u mismatches\n",cases,bad);
    free(d);
    return bad?1:0;
}
