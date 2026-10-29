#include "psx_particle_order.h"
#include <stdio.h>
int main(void) {
    unsigned n;
    while(scanf("%u",&n)==1) {
        t3_psx_particle_draw_t d[80]={0};uint8_t out[80];
        if(n>80)return 2;
        for(unsigned i=0;i<n;++i) {
            int bucket,visible;
            if(scanf("%d%d",&bucket,&visible)!=2 || bucket< -32768 || bucket>32767 || visible<0 || visible>1)return 2;
            d[i].quad.bucket=(int16_t)bucket;d[i].quad.visible=(uint8_t)visible;
        }
        const int count=t3_psx_particle_order(d,n,out);if(count<0)return 2;
        putchar('[');for(int i=0;i<count;++i)printf("%s%u",i?",":"",out[i]);puts("]");
    }
    return ferror(stdin)||!feof(stdin)?2:0;
}
