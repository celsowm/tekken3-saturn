#include "psx_scene_ot.h"
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    unsigned n;
    while(scanf("%u",&n)==1) {
        if(n>65535u)return 2;
        t3_psx_scene_ot_entry_t* entries=calloc(n?n:1,sizeof(*entries));
        uint16_t* order=calloc(n?n:1,sizeof(*order));uint16_t* next=calloc(n?n:1,sizeof(*next));
        if(!entries || !order || !next)return 2;
        for(unsigned i=0;i<n;++i) {
            unsigned bucket,visible,kind;
            if(scanf("%u %u %u",&bucket,&visible,&kind)!=3 || bucket>65535 || visible>1 || kind>255)return 2;
            entries[i]=(t3_psx_scene_ot_entry_t){(uint16_t)bucket,(uint8_t)visible,(uint8_t)kind};
        }
        int count=t3_psx_scene_ot_order(entries,n,order,next);if(count<0)return 2;
        putchar('[');for(int i=0;i<count;++i)printf("%s%u",i?",":"",order[i]);puts("]");
        free(entries);free(order);free(next);
    }
    return ferror(stdin)||!feof(stdin)?2:0;
}
