#include "psx_particle_assets.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { SIZE=2948, PAL=272, MODEL=2788 };
static void put16(unsigned char* p,unsigned v) {p[0]=(unsigned char)v; p[1]=(unsigned char)(v>>8);}
static void synthetic(unsigned char* p) {
    memset(p,0,SIZE); memcpy(p,"T3FX001",7);
    put16(p+8,2); put16(p+10,37); put16(p+12,5); put16(p+14,16);
    for(unsigned i=0;i<37;++i) {
        unsigned id=i<5?0x7dc6+i:i<21?0x7f50+i-5:0x7f90+i-21;
        unsigned char* q=p+PAL+68*i; put16(q,id);
        put16(q+4,0x8000); put16(q+36,0x8000);
        put16(q+6,0x1234); put16(q+38,0x9234);
        put16(q+8,i+1); put16(q+40,0x8001+i);
    }
    put16(p+MODEL,0xffc9); put16(p+MODEL+2,0x7fff); p[16]=0xab;
}
int main(int argc,char** argv) {
    unsigned char data[SIZE]; t3_psx_particle_assets_t view={0},unchanged={data};
    uint16_t colors[16]; t3_psx_particle_model_t model;
    synthetic(data);
    assert(t3_psx_particle_assets_open(&view,data,sizeof data));
    assert(t3_psx_particle_pattern(&view,0)[0]==0xab);
    assert(!t3_psx_particle_pattern(&view,2));
    assert(t3_psx_particle_palette(&view,0x7dc8,colors));
    assert(colors[0]==0x8000 && colors[1]==0x1234 && colors[2]==3);
    for(unsigned id=0;id<65536u;++id) {
        const int index=(id>=0x7dc6 && id<=0x7dca)?(int)(id-0x7dc6):
            (id>=0x7f50 && id<=0x7f5f)?(int)(5+id-0x7f50):
            (id>=0x7f90 && id<=0x7f9f)?(int)(21+id-0x7f90):-1;
        const int ok=t3_psx_particle_palette(&view,(uint16_t)id,colors);
        assert(ok==(index>=0));if(ok)assert(colors[2]==(unsigned)index+1);
    }
    assert(!t3_psx_particle_palette(&view,0x7dcb,colors));
    assert(t3_psx_particle_model(&view,1,&model));
    assert(model.corners[0][0]==-55 && model.corners[0][1]==32767);
    assert(!t3_psx_particle_model(&view,0,&model) && !t3_psx_particle_model(&view,6,&model));
    assert(!t3_psx_particle_assets_open(&unchanged,data,SIZE-1));
    assert(unchanged.data==data);
    data[PAL+36]^=1; assert(!t3_psx_particle_assets_open(&unchanged,data,SIZE)); data[PAL+36]^=1;
    data[PAL+68]^=1; assert(!t3_psx_particle_assets_open(&unchanged,data,SIZE)); data[PAL+68]^=1;
    data[0]^=1; assert(!t3_psx_particle_assets_open(&unchanged,data,SIZE));
    if(argc>1) {
        FILE* f=fopen(argv[1],"rb");
        if(f) {
            assert(fread(data,1,SIZE,f)==SIZE && fgetc(f)==EOF); fclose(f);
            assert(t3_psx_particle_assets_open(&view,data,SIZE));
            assert(t3_psx_particle_model(&view,1,&model));
            assert(model.corners[0][0]==-55 && model.corners[0][1]==-55);
            assert(t3_psx_particle_palette(&view,0x7dc8,colors) && colors[0]!=0);
            assert(t3_psx_particle_palette(&view,0x7f90,colors) && colors[0]==0);
            puts("private original assets checked");
        }
    }
    puts("particle asset reader: ok");
    return 0;
}
