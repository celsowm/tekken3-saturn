#include "psx_skin_bridge.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void fill(t3_psx_joint_t joints[T3_PSX_DRAW_PARTS]) {
    unsigned i,j,k;
    memset(joints,0,sizeof(t3_psx_joint_t)*T3_PSX_DRAW_PARTS);
    for(i=0;i<T3_PSX_DRAW_PARTS;++i) {
        joints[i].origin=(t3_vec3i_t){(int32_t)i*10-50,(int32_t)i*15,-100};
        for(j=0;j<3;++j)
            for(k=0;k<3;++k)
                joints[i].rotation.m[j][k]=j==k?4096:0;
    }
}
static void test_valid_and_axis_mapping(void) {
    t3_psx_joint_t pose[T3_PSX_DRAW_PARTS];
    sat_mat4_t matrices[T3_PSX_DRAW_PARTS];
    fill(pose);
    memset(matrices,0xA5,sizeof(matrices));
    pose[0].rotation.m[0][0]=0;
    pose[0].rotation.m[0][1]=-4096;
    pose[0].rotation.m[1][0]=4096;
    pose[0].rotation.m[1][1]=0;
    assert(t3_psx_skin_bridge(pose,T3_PSX_VISIBLE_MASK,matrices));
    assert(matrices[0].m[0]==0);
    assert(matrices[0].m[1]==-65536);
    assert(matrices[0].m[4]==65536);
    assert(matrices[0].m[5]==0);
    assert(matrices[0].m[10]==65536);
    assert(matrices[0].m[3]==-800);
    assert(matrices[0].m[7]==0);
    assert(matrices[0].m[11]==-1600);
    assert(matrices[0].m[15]==SAT_FX16_ONE);
    assert(matrices[17].m[3]==(17*10-50)*16);
    /* Four accessory matrices remain untouched, since those KMD body
     * meshes are absent from the importer rather than invented. */
    for(unsigned i=18;i<T3_PSX_DRAW_PARTS;++i)
        for(unsigned j=0;j<16u;++j)
            assert((uint32_t)matrices[i].m[j]==0xA5A5A5A5u);
}
static void test_fails_atomically(void) {
    t3_psx_joint_t pose[T3_PSX_DRAW_PARTS];
    sat_mat4_t matrices[T3_PSX_DRAW_PARTS];
    sat_mat4_t saved[T3_PSX_DRAW_PARTS];
    fill(pose);
    memset(matrices,0x4B,sizeof(matrices));
    memcpy(saved,matrices,sizeof(saved));
    assert(!t3_psx_skin_bridge(pose,T3_PSX_VISIBLE_MASK ^ (1u<<13),matrices));
    assert(!memcmp(saved,matrices,sizeof(saved)));
    pose[12].origin.x=30001;
    assert(!t3_psx_skin_bridge(pose,T3_PSX_VISIBLE_MASK,matrices));
    assert(!memcmp(saved,matrices,sizeof(saved)));
    assert(!t3_psx_skin_bridge(NULL,T3_PSX_VISIBLE_MASK,matrices));
    assert(!t3_psx_skin_bridge(pose,T3_PSX_VISIBLE_MASK,NULL));
}
int main(void) {
    test_valid_and_axis_mapping();
    test_fails_atomically();
    puts("PS1 Q12 pose -> LibSaturn Q16 skin matrix bridge: passed");
    return 0;
}
