#include "stage_floor_visibility.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    int32_t p[4][3]={{10,20,1},{400,-1,250},{-10,500,600},{500,500,900}};
    assert(t3_stage_floor_visible((const int32_t (*)[3])p));
    /* Positive depth below H/2 is accepted by the original. */
    p[0][2]=0;
    assert(!t3_stage_floor_visible((const int32_t (*)[3])p));
    p[0][2]=1;
    /* Different vertices may satisfy the horizontal and vertical tests. */
    p[0][1]=-1;p[1][1]=0;
    assert(t3_stage_floor_visible((const int32_t (*)[3])p));
    p[0][0]=368;
    assert(!t3_stage_floor_visible((const int32_t (*)[3])p));
    p[0][0]=367;p[1][1]=480;
    assert(!t3_stage_floor_visible((const int32_t (*)[3])p));
    p[1][1]=479;
    assert(t3_stage_floor_visible((const int32_t (*)[3])p));
    puts("floor visibility boundaries: ok");
    return 0;
}
