#include "stage_floor_bitmap.h"
int t3_stage_floor_bitmap_row(const uint8_t cells[2048],unsigned y,uint8_t row[256]) {
    if(!cells || !row || y>=256)return 0;
    const unsigned cellrow=(y&63u)>>3,inside=y&7u;
    for(unsigned cell=0;cell<64;++cell) {
        const unsigned source=(cellrow*8u+(cell&7u))*32u+inside*4u;
        for(unsigned b=0;b<4;++b)row[cell*4u+b]=cells[source+b];
    }
    return 1;
}
