/* Golden-case test of the CPU opponent's Tekken Ball hooks (src/game/ball_ai.c): the cases come from scripts/export_ball_ai_cases.py
 * (set T3_BALL_AI_CASES to build/ball-cases/ai.bin). Skips (77) when the file is missing. */
#include "ball_ai.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

int main(void) {
    const char* path=getenv("T3_BALL_AI_CASES");
    FILE* fp=path?fopen(path,"rb"):NULL;
    if(!fp){puts("skip: no golden cases");return 77;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,fp)!=(size_t)size){fclose(fp);return 1;}
    fclose(fp);
    if((uint32_t)be32(d)!=0x54334149u||be32(d+4)!=1){fputs("bad case file\n",stderr);return 1;}
    const int cases=be32(d+8),trig_bytes=be32(d+12),atan_bytes=be32(d+16);
    const uint8_t* trig=d+20;
    const uint8_t* atan=trig+trig_bytes;
    const uint8_t* at=atan+atan_bytes;
    t3_ball_tables_t t;
    memset(&t,0,sizeof t);
    t.tables.ground.trig.data=trig;t.tables.ground.trig.size=(size_t)trig_bytes;
    t.tables.ground.atan=atan;t.tables.ground.atan_size=(size_t)atan_bytes;
    unsigned bad=0;
    for(int c=0;c<cases;++c) {
        int32_t v[18];
        for(int i=0;i<18;++i)v[i]=be32(at+4*i);
        at+=4*18;
        const int32_t ball[3]={v[0],v[1],v[2]},p0[2]={v[3],v[4]},p1[2]={v[5],v[6]};
        int32_t line[BALL_LINE_WORDS];
        t3_ball_ai_project(&t,ball,p0,p1,line);
        int ok=1;
        for(int i=0;i<5;++i)if(line[i]!=v[7+i])ok=0;
        for(int n=0;n<2;++n) {
            if(t3_ball_ai_distance(line,n)!=v[12+n])ok=0;
            if(t3_ball_ai_behind(line,n)!=v[14+n])ok=0;
            if(t3_ball_ai_ahead(line,n)!=v[16+n])ok=0;
        }
        if(!ok) {
            if(bad<5)printf("case %d: line %d %d %d %d %d / want %d %d %d %d %d\n",c,line[0],line[1],line[2],line[3],line[4],v[7],v[8],v[9],v[10],v[11]);
            ++bad;
        }
    }
    printf("%d cases, %u mismatches\n",cases,bad);
    free(d);
    return bad?1:0;
}
