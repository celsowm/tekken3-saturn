/* Golden-case test of the Tekken Force level runner (src/game/psx_force_level.c): the cases come from
 * scripts/export_force_level_cases.py (private, under build/force-cases/; set T3_FORCE_CASES to the
 * file). Skips (77) when it is missing. */
#include "psx_force_level.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t be32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

int main(void){
    const char* path=getenv("T3_FORCE_CASES");
    FILE* f=path?fopen(path,"rb"):NULL;
    if(!f){puts("skip: no golden cases");return 77;}
    fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,f)!=(size_t)size){fclose(f);return 1;}
    fclose(f);
    if(be32(d)!=0x54334643u||be32(d+8)!=T3_FORCE_VECTOR_WORDS||be32(d+12)!=T3_FORCE_EVENTS||be32(d+20)!=T3_FORCE_TABLE_BYTES){
        fputs("bad case file\n",stderr);return 1;
    }
    const uint32_t cases=be32(d+16);
    const uint8_t* tables=d+24;
    const uint8_t* at=tables+T3_FORCE_TABLE_BYTES;
    const uint8_t* scripts[4];
    for(int i=0;i<4;++i){
        const uint32_t n=be32(at);
        if(n<T3_FORCE_SCRIPT_BYTES){fputs("short script\n",stderr);return 1;}
        scripts[i]=at+4;at+=4+n;
    }
    unsigned bad=0,eventful=0;
    for(uint32_t c=0;c<cases;++c){
        const uint32_t level=be32(at);at+=4;
        int32_t in[T3_FORCE_VECTOR_WORDS],want[T3_FORCE_VECTOR_WORDS],got[T3_FORCE_VECTOR_WORDS];
        for(int i=0;i<T3_FORCE_VECTOR_WORDS;++i)in[i]=(int32_t)be32(at+4*i);
        at+=4*T3_FORCE_VECTOR_WORDS;
        for(int i=0;i<T3_FORCE_VECTOR_WORDS;++i)want[i]=(int32_t)be32(at+4*i);
        at+=4*T3_FORCE_VECTOR_WORDS;
        const uint32_t count=be32(at);
        const uint8_t* ev=at+4;
        at+=4+16*T3_FORCE_EVENTS;
        t3_force_level_t s;
        memset(&s,0,sizeof(s));
        t3_force_level_load(&s,in);
        for(int i=0;i<5;++i)s.countdown_sound[i]=(uint16_t)(0x4C60+i);
        t3_force_level_step(&s,tables,scripts[level]);
        t3_force_level_save(&s,got);
        int ok=memcmp(got,want,sizeof(got))==0&&s.event_count==count;
        for(unsigned i=0;ok&&i<count;++i){
            ok=(uint32_t)s.events[i].kind==be32(ev+16*i)&&(uint32_t)s.events[i].a==be32(ev+16*i+4)&&
               (uint32_t)s.events[i].b==be32(ev+16*i+8)&&(uint32_t)s.events[i].c==be32(ev+16*i+12);
        }
        eventful+=count!=0;
        if(!ok){
            if(bad<5){
                printf("case %u (level %u) differs:",c,level);
                for(int i=0;i<T3_FORCE_VECTOR_WORDS;++i)if(got[i]!=want[i])printf(" [%d] %d/%d",i,got[i],want[i]);
                if(s.event_count!=count)printf(" events %u/%u",s.event_count,count);
                for(unsigned i=0;i<count&&i<s.event_count;++i)
                    printf(" ev%u %d,%d,%d,%d/%d,%d,%d,%d",i,s.events[i].kind,s.events[i].a,s.events[i].b,s.events[i].c,
                           (int32_t)be32(ev+16*i),(int32_t)be32(ev+16*i+4),(int32_t)be32(ev+16*i+8),(int32_t)be32(ev+16*i+12));
                putchar('\n');
            }
            ++bad;
        }
    }
    printf("%u cases, %u with events, %u mismatches\n",cases,eventful,bad);
    free(d);
    return bad?1:0;
}
