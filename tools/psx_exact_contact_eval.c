#include "psx_exact_contact.h"
#include <stdio.h>
int main(void) {
    t3_world_segment_t s={0};t3_hurt_cylinder_t c={0};unsigned r;
    while(scanf("%d %d %d %d %d %d %d %d %d %u",&s.start.x,&s.start.y,&s.start.z,
          &s.end.x,&s.end.y,&s.end.z,&c.center.x,&c.center.y,&c.center.z,&r)==10) {
        int result;if(r>32767)return 2;c.radius=(uint16_t)r;
        result=t3_psx_segment_hits_cylinder(&s,&c);if(result<0)return 2;
        printf("%d\n",result);
    }
    return 0;
}
