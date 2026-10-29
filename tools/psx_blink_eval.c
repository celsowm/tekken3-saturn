#include "psx_blink.h"
#include <inttypes.h>
#include <stdio.h>
int main(void) {
    int timer;unsigned closed;uint32_t flags,state,random;
    while(scanf("%d %u %"SCNu32" %"SCNu32" %"SCNu32,
                &timer,&closed,&flags,&state,&random)==5) {
        t3_psx_blink_t blink={(int16_t)timer,(uint8_t)closed};
        t3_psx_blink_step(&blink,flags,state,&random);
        printf("%d %u %"PRIu32"\n",blink.timer,blink.closed,random);
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
