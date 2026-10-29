#include "psx_fighter_ot.h"
#include <inttypes.h>
#include <stdio.h>
int main(void) {
    unsigned z0,z1;uint32_t flags;int32_t nclip;
    while(scanf("%u %u %"SCNu32" %"SCNd32,&z0,&z1,&flags,&nclip)==4) {
        if(z0>65535 || z1>65535)return 2;
        printf("%u\n",t3_psx_fighter_ot_bucket((uint16_t)z0,(uint16_t)z1,flags,nclip));
    }
    return ferror(stdin)||!feof(stdin)?2:0;
}
