#include "psx_vertex_exchange.h"
#include "psx_projected_mesh.h"
#include "psx_scene_ot.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint32_t reference_divide(uint32_t h,uint32_t sz) {
    if(h>=2u*sz)return 0x1ffffu;
    unsigned shift=0;while(!(sz&(0x8000u>>shift)))++shift;
    uint32_t n=h<<shift,d=sz<<shift,i=(d-0x7fc0u)>>7;
    uint32_t u=((0x40000u/(i+0x100u)+1u)>>1);if(u<0x101u)u=0x101u;
    d=(uint32_t)((int64_t)0x2000080-(int64_t)d*u)>>8;
    d=(uint32_t)((int64_t)0x80+(int64_t)d*u)>>8;
    n=(uint32_t)(((uint64_t)n*d+0x8000u)>>16);return n>0x1ffffu?0x1ffffu:n;
}
static int32_t reference_axis(uint32_t div,int32_t value,int32_t offset) {
    int32_t result=(int32_t)(((int64_t)div*value+(int64_t)offset*65536)>>16);
    return result< -1024?-1024:result>1023?1023:result;
}
int main(void) {
    t3_psx_scene_ot_entry_t mixed[]={{5,1,0},{3,1,1},{5,1,1},{65535,0,0}};
    uint16_t sorted[4]={91,92,93,94},next[4]={81,82,83,84};
    assert(t3_psx_scene_ot_order(mixed,4,sorted,next)==3);
    assert(sorted[0]==2 && sorted[1]==0 && sorted[2]==1);
    uint16_t saved_sorted[4],saved_next[4];memcpy(saved_sorted,sorted,sizeof sorted);memcpy(saved_next,next,sizeof next);
    mixed[3].visible=1;assert(t3_psx_scene_ot_order(mixed,4,sorted,next)==-1);
    assert(!memcmp(saved_sorted,sorted,sizeof sorted) && !memcmp(saved_next,next,sizeof next));
    assert(t3_psx_scene_ot_order(NULL,0,NULL,NULL)==0);
    for(unsigned depth=0;depth<65536u;++depth) {
        t3_psx_camera_matrix_t m={0};int32_t point[3]={0},got[3];
        m.h=(int32_t)((depth*7919u+173u)&65535u);m.t[2]=(int32_t)depth;
        m.t[0]=(depth&1u)?32767:-32768;m.t[1]=(int32_t)(int16_t)(depth*37u);
        uint32_t div=reference_divide((uint32_t)m.h,depth);
        t3_psx_camera_project(&m,point,got);
        assert(got[0]==reference_axis(div,m.t[0],192));
        assert(got[1]==reference_axis(div,m.t[1],240));assert(got[2]==(int32_t)depth);
    }
    uint32_t rng=0x182731u;
    for(unsigned sample=0;sample<50000;++sample) {
        t3_psx_camera_matrix_t m={0};int16_t p[3];int32_t wide[3],a[3],c[3];
        for(unsigned i=0;i<3;++i) {
            rng=rng*1664525u+1013904223u;p[i]=(int16_t)(rng>>16);wide[i]=p[i];
            rng=rng*1664525u+1013904223u;m.t[i]=(int32_t)(rng^0x80000000u);
            for(unsigned j=0;j<3;++j){rng=rng*1664525u+1013904223u;m.m[i][j]=(int16_t)(rng>>16);}
        }
        rng=rng*1664525u+1013904223u;m.h=(int32_t)(rng&65535u);
        t3_psx_camera_project(&m,wide,a);t3_psx_camera_project_s16(&m,p,c);
        assert(!memcmp(a,c,sizeof a));
    }
    t3_psx_vertex_exchange_t state,before;memset(&state,0,sizeof state);
    uint8_t bad_local[]={1};uint32_t xy[]={0x80010003u};int16_t z[]={-3};uint16_t halve[]={0};
    t3_psx_vertex_block_t b={0};b.scratch_import=bad_local;b.scratch_count=1;
    b.own_xy=xy;b.own_z=z;b.own_count=1;b.post[5]=halve;b.post_count[5]=1;
    before=state;assert(!t3_psx_vertex_exchange(&state,&b));assert(!memcmp(&state,&before,sizeof state));
    b.scratch_count=0;assert(t3_psx_vertex_exchange(&state,&b));
    assert(state.scratch.xy[0]==0xc0000001u && state.scratch.z[0]==-2);
    before=state;b.post_count[5]=2;
    assert(!t3_psx_vertex_exchange(&state,&b));assert(!memcmp(&state,&before,sizeof state));
    t3_psx_projected_vertex_t output[1]={{123,45}}, saved=output[0];
    uint16_t map[]={0,1}; int16_t position[1][3]={{0,0,100}};
    t3_psx_camera_matrix_t matrix={0}; matrix.h=500;
    t3_psx_projected_part_t part={0};part.positions=(const int16_t (*)[3])position;part.own_count=1;
    part.logical_map=map;part.map_count=1;
    assert(!t3_psx_projected_mesh_step(&state,&part,&matrix,output,1));
    assert(!memcmp(&state,&before,sizeof state));assert(!memcmp(output,&saved,sizeof saved));
    map[1]=0;assert(t3_psx_projected_mesh_step(&state,&part,&matrix,output,1));
    assert(output[0].xy==state.scratch.xy[0] && output[0].z==state.scratch.z[0]);
    t3_psx_joint_t joints[T3_PSX_DRAW_PARTS]={0};
    for(unsigned i=0;i<3;++i){matrix.m[i][i]=4096;joints[0].rotation.m[i][i]=4096;joints[1].rotation.m[i][i]=4096;}
    uint16_t export_global[]={0}, first_map[]={0,0}, second_map[]={0,1};
    uint8_t import_global[]={0};
    t3_psx_projected_part_t parts[2]={{0}};
    parts[0].positions=(const int16_t (*)[3])position;parts[0].own_count=1;
    parts[0].post[3]=export_global;parts[0].post_count[3]=1;parts[0].logical_map=first_map;parts[0].map_count=1;
    parts[1].part=1;parts[1].global_import=import_global;parts[1].global_count=1;
    parts[1].logical_map=second_map;parts[1].map_count=1;
    t3_psx_projected_vertex_t complete[2]={{0}};
    assert(t3_psx_projected_mesh_build(parts,2,&matrix,joints,3,complete,2));
    assert(complete[0].xy==complete[1].xy && complete[0].z==complete[1].z);
    assert(!t3_psx_projected_mesh_build(parts,2,&matrix,joints,1,complete,2));
    parts[0].part=1;parts[1].part=0;
    assert(!t3_psx_projected_mesh_build(parts,2,&matrix,joints,3,complete,2));
    puts("vertex exchange bounds and signed halves: ok");return 0;
}
