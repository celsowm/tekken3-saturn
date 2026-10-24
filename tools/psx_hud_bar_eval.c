/* Compare the C HUD transition with raw after-frame original RAM cases. */
#include "psx_hud_bar.h"
#include <stdio.h>

int main(void) {
    int v[10],case_no=0;
    while(scanf("%d %d %d %d %d %d %d %d %d %d",
                &v[0],&v[1],&v[2],&v[3],&v[4],&v[5],
                &v[6],&v[7],&v[8],&v[9])==10) {
        t3_psx_hud_bar_t bar={(int32_t)v[0],(int16_t)v[1],(int16_t)v[2],(int16_t)v[3]};
        if(!t3_psx_hud_bar_step(&bar,v[4],v[5]) ||
           bar.last_health!=v[6] || bar.target!=v[7] ||
           bar.drawn!=v[8] || bar.recent!=v[9]) {
            fprintf(stderr,"case %d: got %d %d %d %d; expected %d %d %d %d\n",
                    case_no,bar.last_health,bar.target,bar.drawn,bar.recent,
                    v[6],v[7],v[8],v[9]);
            return 1;
        }
        ++case_no;
    }
    if(ferror(stdin) || !feof(stdin) || !case_no)return 2;
    printf("%d original HUD bar transitions matched\n",case_no);
    return 0;
}
