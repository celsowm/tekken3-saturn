#include "psx_hands.h"
#include <stdio.h>
/* Per line: both channels' current, target and rate, then 0 hands shape speed
 * (HandFaceCommand) or 1 0 0 0 (one FighterHandPoses frame); prints the
 * channels and the variants after it. */
int main(void) {
    int s[6],op,a,b,d;
    while(scanf("%d %d %d %d %d %d %d %d %d %d",&s[0],&s[1],&s[2],&s[3],&s[4],&s[5],&op,&a,&b,&d)==10) {
        t3_psx_hands_t h={{(int16_t)s[0],(int16_t)s[3]},{(int16_t)s[1],(int16_t)s[4]},
                          {(int16_t)s[2],(int16_t)s[5]},{0,0}};
        if(op)t3_psx_hands_step(&h);
        else t3_psx_hands_command(&h,(unsigned)a,(unsigned)b,d);
        printf("%d %d %d %d %d %d %u %u\n",h.current[0],h.target[0],h.rate[0],
               h.current[1],h.target[1],h.rate[1],h.variant[0],h.variant[1]);
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
