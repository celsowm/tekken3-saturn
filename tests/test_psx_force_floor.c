#include <stdio.h>
#include "psx_force_floor.h"
/* Self-checks of the documented layout (stages.md, Floor). */
int main(void) {
    int bad=0;
    /* set 0, order 0: u runs along +x, v along -z: top-left = v0, top-right = v1, bottom-right = v3, bottom-left = v2 */
    uint8_t q[4];
    t3_force_floor_quad_order(0,q);
    if(q[0]!=0||q[1]!=1||q[2]!=3||q[3]!=2){printf("set0/order0 %u%u%u%u\n",q[0],q[1],q[2],q[3]);++bad;}
    /* every orientation is a permutation of the four vertices */
    for(unsigned o=0;o<16;++o) {
        t3_force_floor_quad_order(o,q);
        unsigned seen=0;
        for(unsigned k=0;k<4;++k)seen|=1u<<q[k];
        if(seen!=15u){printf("orient %u is not a permutation\n",o);++bad;}
    }
    /* order 1 swaps c0/c1 and c2/c3: the quad of set 0 is mirrored in u */
    t3_force_floor_quad_order(1,q);
    if(q[0]!=1||q[1]!=0||q[2]!=2||q[3]!=3){printf("set0/order1 %u%u%u%u\n",q[0],q[1],q[2],q[3]);++bad;}
    /* the entry: column (tx + 5) mod 10 and a row mod 10, negative columns and rows wrap */
    if(t3_force_floor_entry(0,0)!=5 || t3_force_floor_entry(-5,0)!=0 || t3_force_floor_entry(5,0)!=0 ||
       t3_force_floor_entry(0,1)!=15 || t3_force_floor_entry(0,-1)!=95){printf("entry\n");++bad;}
    int32_t x[4],z[4];
    t3_force_floor_corners(2,0,x,z);
    if(x[0]!=3600||x[1]!=5400||z[0]!=T3_FORCE_FLOOR_EDGE||z[2]!=T3_FORCE_FLOOR_EDGE-1800||x[2]!=3600||x[3]!=5400||z[3]!=z[2]){printf("corners\n");++bad;}
    printf("%d mismatches\n",bad);
    return bad!=0;
}
