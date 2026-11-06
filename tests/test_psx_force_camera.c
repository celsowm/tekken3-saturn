/* Golden-case test of the Tekken Force camera (src/game/psx_force_camera.c): cases from the original code via
 * scripts/export_force_camera_cases.py (T3_FORCE_CAMERA_CASES=build/force-cases/camera.bin). Skips (77) without. */
#include "psx_force_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}
static void load(t3_force_camera_t* c,const uint8_t* p){
    c->player=(uint32_t)be32(p);
    int32_t* f=&c->x;
    for(int i=0;i<13;++i)f[i]=be32(p+4+4*i);
}
static int same(const t3_force_camera_t* a,const t3_force_camera_t* b){return memcmp(a,b,sizeof(*a))==0;}

int main(void){
    const char* path=getenv("T3_FORCE_CAMERA_CASES");
    FILE* f=path?fopen(path,"rb"):NULL;
    if(!f){puts("skip: no golden cases");return 77;}
    fseek(f,0,SEEK_END);long size=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,f)!=(size_t)size){fclose(f);return 1;}
    fclose(f);
    if((uint32_t)be32(d)!=0x5433434Du){fputs("bad case file\n",stderr);return 1;}
    const int32_t cases=be32(d+8);
    const uint8_t* trig=d+12;
    const uint8_t* at=trig+0x2800;
    unsigned bad=0,setups=0;
    for(int32_t n=0;n<cases;++n){
        const int32_t kind=be32(at),flag=be32(at+4);
        at+=8;
        if(kind==0){
            t3_force_camera_t want,got;
            memset(&got,0,sizeof(got));
            load(&want,at);at+=56;
            const int32_t want_roll=be32(at),want_start=be32(at+4);at+=8;
            int32_t start;
            const int32_t roll=t3_force_camera_setup(&got,want.player,flag==3,&start);
            ++setups;
            if(!same(&got,&want)||roll!=want_roll||start!=want_start){if(bad<5)printf("setup %d differs\n",n);++bad;}
        }else{
            t3_force_camera_t in,want;
            load(&in,at);at+=56;
            const int32_t roll=be32(at),px=be32(at+4),pz=be32(at+8);at+=12;
            load(&want,at);at+=56;
            t3_force_camera_frame(&in,trig,roll,px,pz);
            if(!same(&in,&want)){
                if(bad<5){const int32_t* g=&in.x;const int32_t* w=&want.x;
                    printf("frame %d differs:",n);
                    for(int i=0;i<13;++i)if(g[i]!=w[i])printf(" [%d] %d/%d",i,g[i],w[i]);
                    putchar('\n');}
                ++bad;
            }
        }
    }
    printf("%d cases (%u set-ups), %u mismatches\n",cases,setups,bad);
    free(d);
    return bad?1:0;
}
