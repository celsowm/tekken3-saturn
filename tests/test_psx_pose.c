#include "psx_pose.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t trig[T3_PSX_TRIG_BYTES];
static void put16(unsigned at,int v) {
    trig[at]=(uint8_t)v;
    trig[at+1u]=(uint8_t)((uint16_t)v>>8u);
}
static t3_psx_matrix_t identity(void) {
    t3_psx_matrix_t r={{{4096,0,0},{0,4096,0},{0,0,4096}}};return r;
}
static void verify_matrix(t3_psx_matrix_t m,
                          const int expect[3][3]) {
    for(unsigned i=0;i<3;++i)
        for(unsigned j=0;j<3;++j)
            assert(m.m[i][j]==expect[i][j]);
}
static void init(void) {
    memset(trig,0,sizeof(trig));
    /* Positions in the PS1 EXE circular sine lookup (plus quarter cycle
     * for cos); only exact cardinal angles are used in these fixtures. */
    put16(0x0000,0);
    put16(0x0800,4096);
    put16(0x1000,0);
    put16(0x1800,-4096);
    put16(0x2000,0);
}
static void euler_tests(void) {
    t3_psx_trig_t table={trig,sizeof(trig)};
    t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS]={0};
    t3_psx_matrix_t before[T3_PSX_ROTATION_SLOTS];
    int16_t pose[T3_PSX_POSE_CHANNELS]={0};
    uint32_t valid=0xFFFFFFFFu;
    const uint32_t mask=(1u<<0)|(1u<<1)|(1u<<11)|(1u<<2)|
                        (1u<<3)|(1u<<7)|(1u<<6)|(1u<<10)|
                        (1u<<14)|(1u<<17);
    const int I[3][3]={{4096,0,0},{0,4096,0},{0,0,4096}};
    const int X90[3][3]={{4096,0,0},{0,0,-4096},{0,4096,0}};
    const int Y90[3][3]={{0,0,4096},{0,4096,0},{-4096,0,0}};
    const int Z90[3][3]={{0,-4096,0},{4096,0,0},{0,0,4096}};
    init();
    assert(t3_psx_pose_fk(pose,&table,slots,&valid));
    assert(valid==mask);
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)
        if(valid&(1u<<i))verify_matrix(slots[i],I);
    assert(slots[4].m[0][0]==0);
    pose[19]=0x4000;        /* Euler triple 0 -> slot 0, X rotation */
    pose[19+3+1]=0x4000;    /* Euler triple 1 -> slot 1, Y rotation */
    pose[19+6+2]=0x4000;    /* Euler triple 2 -> slot 11, Z rotation */
    assert(t3_psx_pose_fk(pose,&table,slots,&valid));
    verify_matrix(slots[0],X90);
    verify_matrix(slots[1],Y90);
    verify_matrix(slots[11],Z90);
    for(unsigned i=0;i<T3_PSX_ROTATION_SLOTS;++i)before[i]=slots[i];
    table.size=0x1000;valid=mask;
    assert(!t3_psx_pose_fk(pose,&table,slots,&valid));
    assert(valid==mask);
    assert(memcmp(slots,before,sizeof(slots))==0);
    table.size=sizeof(trig);
    assert(!t3_psx_pose_fk(NULL,&table,slots,&valid));
    assert(!t3_psx_pose_fk(pose,NULL,slots,&valid));
}
static void compose_tests(void) {
    t3_psx_trig_t table={trig,sizeof(trig)};
    t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS]={0};
    t3_psx_joint_t root,world[T3_PSX_DRAW_PARTS];
    t3_psx_part_t parts[T3_PSX_DRAW_PARTS]={0};
    int16_t pose[T3_PSX_POSE_CHANNELS]={0};
    uint32_t valid=0u,mask=0u;
    const int I[3][3]={{4096,0,0},{0,4096,0},{0,0,4096}};
    const int Z90[3][3]={{0,-4096,0},{4096,0,0},{0,0,4096}};
    init();
    pose[21]=0x4000; /* slot 0: rotate root's X into world Y */
    assert(t3_psx_pose_fk(pose,&table,slots,&valid));
    root.rotation=identity();
    root.origin=(t3_vec3i_t){100,200,300};
    for(unsigned i=0;i<T3_PSX_DRAW_PARTS;++i) {
        parts[i].parent=0;
        parts[i].slot=4u; /* unknown IK limb */
        parts[i].offset=(t3_vec3i_t){10,0,0};
    }
    parts[0].parent=-1;parts[0].slot=0;parts[0].offset=(t3_vec3i_t){50,0,0};
    parts[1].parent=0; parts[1].slot=1;
    parts[2].parent=1; parts[2].slot=11;
    parts[3].parent=1; parts[3].slot=12;/* unsupported limb -> missing */
    parts[4].parent=3; parts[4].slot=1; /* descendant must be missing too */
    parts[5].parent=1; parts[5].slot=2;
    parts[6].parent=5; parts[6].slot=3;
    parts[18].parent=1; parts[18].slot=18; /* attachment: not an error */
    assert(t3_psx_compose_parts(parts,slots,valid,&root,world,&mask));
    assert(mask==((1u<<0)|(1u<<1)|(1u<<2)|(1u<<5)|(1u<<6)));
    assert(t3_psx_count_parts(mask)==5);
    verify_matrix(world[0].rotation,Z90);
    assert(world[0].origin.x==150 && world[0].origin.y==200);
    assert(world[1].origin.x==150 && world[1].origin.y==210);
    assert(world[2].origin.x==150 && world[2].origin.y==220);
    assert(world[6].origin.y==230);
    verify_matrix(world[3].rotation,(const int[3][3]){{0,0,0},{0,0,0},{0,0,0}});
    /* Malformed hierarchy and numeric overflow fail, without output write. */
    parts[7].parent=8;
    assert(!t3_psx_compose_parts(parts,slots,valid,&root,world,&mask));
    assert(t3_psx_count_parts(mask)==5);
    parts[7].parent=0;
    root.origin.x=T3_PSX_UNIT_LIMIT+1;
    assert(!t3_psx_compose_parts(parts,slots,valid,&root,world,&mask));
    root.origin.x=100;
    root.rotation=identity();
    {
        t3_psx_matrix_t out;
        t3_psx_matrix_t rx=slots[0];
        assert(t3_psx_mul_rot(&rx,&rx,&out));
        const int Z180[3][3]={{-4096,0,0},{0,-4096,0},{0,0,4096}};
        verify_matrix(out,Z180);
    }
    verify_matrix(slots[1],I);
}
int main(void) {
    euler_tests();
    compose_tests();
    puts("PS1 ten Euler/FK slots and parent partial-composition: passed");
    return 0;
}
