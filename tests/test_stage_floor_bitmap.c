#include "stage_floor_bitmap.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned cell_pixel(const unsigned char* c,unsigned x,unsigned y) {
    unsigned tile=((y%64)/8)*8+(x%64)/8;
    unsigned value=c[tile*32+(y%8)*4+(x%8)/2];
    return (x&1)?value&15:value>>4;
}
static void check(const unsigned char* cells) {
    unsigned char row[256];
    for(unsigned y=0;y<256;++y) {
        assert(t3_stage_floor_bitmap_row(cells,y,row));
        for(unsigned x=0;x<512;++x) {
            unsigned got=(x&1)?row[x/2]&15:row[x/2]>>4;
            assert(got==cell_pixel(cells,x,y));
        }
    }
    memset(row,0x5a,sizeof row);
    assert(!t3_stage_floor_bitmap_row(cells,256,row));
    for(unsigned i=0;i<sizeof row;++i)assert(row[i]==0x5a);
}
int main(int argc,char** argv) {
    unsigned char cells[2048];
    for(unsigned i=0;i<sizeof cells;++i)cells[i]=(unsigned char)(i*73u+(i>>4));
    check(cells);
    if(argc>1) {
        FILE* f=fopen(argv[1],"rb");
        if(f) {
            assert(!fseek(f,2048,SEEK_SET));assert(fread(cells,1,sizeof cells,f)==sizeof cells);fclose(f);
            check(cells);puts("private floor tile checked");
        }
    }
    puts("floor bitmap expansion: ok");return 0;
}
