#include "psx_tap_latch.h"
#include <inttypes.h>
#include <stdio.h>

int main(void) {
    unsigned previous,lp,rp,both,used,entry;
    uint32_t frame,seen,scratch;
    while(scanf("%u %u %u %u %u %u %"SCNu32" %"SCNu32" %"SCNu32,
            &previous,&lp,&rp,&both,&used,&entry,&frame,&seen,&scratch)==9) {
        t3_psx_tap_latch_t s={(uint16_t)previous,(uint8_t)lp,(uint8_t)rp,
                            (uint8_t)both,(uint8_t)used};
        t3_psx_tap_latch_step(&s,(uint8_t)entry,frame,seen,&scratch);
        printf("%u %u %u %u %u %"PRIu32"\n",s.previous,s.lp,s.rp,s.both,s.condition_used,scratch);
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
