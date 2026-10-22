#include "compiled_renderer.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static sat_skin_rigid_t rigid[]={{{32767,-32768,0},0,0},{{12,-7,11},0,0},{{-15,9,3},0,0}};
static sat_skin_blend_t blend[]={{{0,1,0},2,{16384,0},{{-11,9,17},{13,-21,15},{0,0,0}}}};
static const uint16_t indices[]={0,1,2,2,0,1,3,3};
static const uint8_t two_sided[]={0,0};
static const uint16_t joint_offsets[]={0,3,3}, rigid_ids[]={0,1,2};
static const uint16_t vertex_offsets[]={0,2,4,5,6}, vertex_faces[]={0,1,0,1,0,1};
static const uint8_t face_joints[]={0,255};
static const t3_compiled_plan_t plan={4,2,2,6,65535,3,1,
    joint_offsets,rigid_ids,vertex_offsets,vertex_faces,face_joints};
static const sat_skin_asset_t skin={rigid,blend,NULL,3,1,2,15,1};

static uint32_t seed=0x9ac785e1u;
static uint32_t random_word(void) {seed=seed*1664525u+1013904223u;return seed;}
static int16_t random_short(void) {return (int16_t)((int32_t)(random_word()>>16)-32768);}

/* Independent reference: Q16 bridge coefficients, Q16 anchors, a signed
 * 64-bit sum and explicit floor division (no assumption about negative >>). */
static int64_t floor_div(int64_t value,int64_t divisor) {
    return value>=0?value/divisor:-((-value+divisor-1)/divisor);
}
static int32_t wide_axis(const t3_psx_joint_t* j,const int16_t* q,unsigned axis) {
    int64_t dot=32768;
    for(unsigned c=0;c<3;++c) {
        const int sign=(axis? -1:1)*(c? -1:1);
        dot+=(int64_t)j->rotation.m[axis][c]*16*sign*((int32_t)q[c]*2);
    }
    const int32_t origin[]={j->origin.x,-j->origin.y,-j->origin.z};
    return (int32_t)floor_div(dot,65536)+origin[axis]*16;
}
static sat_vec3_t reference(const t3_psx_joint_t* joints,unsigned v,int cx,int cz) {
    int32_t value[3];
    for(unsigned axis=0;axis<3;++axis) {
        if(v<3)value[axis]=wide_axis(&joints[rigid[v].joint],rigid[v].pos,axis);
        else {
            const int64_t a=wide_axis(&joints[blend[0].joint[0]],blend[0].pos[0],axis);
            const int64_t b=wide_axis(&joints[blend[0].joint[1]],blend[0].pos[1],axis);
            value[axis]=(int32_t)floor_div(a+b,2);
        }
    }
    return (sat_vec3_t){value[0]*6-cx*96,value[1]*6,value[2]*6+cz*96};
}

static void test_exact_arithmetic_and_ranges(void) {
    t3_compiled_renderer_t renderer={0};
    assert(t3_compiled_renderer_init(&renderer,&plan,&skin,indices,2,two_sided));
    for(unsigned sample=0;sample<10000;++sample) {
        t3_psx_joint_t originals[T3_PSX_DRAW_PARTS]={0};
        t3_compiled_pose_t packed;
        memset(&packed,0x5a,sizeof(packed));
        for(unsigned j=0;j<2;++j) {
            originals[j].origin=(t3_vec3i_t){(int32_t)(random_word()%60001)-30000,
                (int32_t)(random_word()%60001)-30000,(int32_t)(random_word()%60001)-30000};
            for(unsigned a=0;a<3;++a)for(unsigned c=0;c<3;++c)
                originals[j].rotation.m[a][c]=sample<2?(sample?32767:-32768):random_short();
        }
        const int cx=sample==0?-30000:(int)(random_word()%60001)-30000;
        const int cz=sample==0?30000:(int)(random_word()%60001)-30000;
        assert(t3_compiled_pose_pack(&renderer,originals,3,cx,cz,&packed));
        if(!sample)assert(packed.joint[0][1]==32768);
        sat_vec3_t out[6],saved[6];
        memset(out,0x5a,sizeof(out));memcpy(saved,out,sizeof(out));
        assert(t3_compiled_deform_range(&renderer,&packed,1,2,out,6));
        assert(!memcmp(out,saved,sizeof(*out)));
        assert(!memcmp(out+3,saved+3,3*sizeof(*out)));
        assert(t3_compiled_deform_range(&renderer,&packed,3,1,out,6));
        assert(t3_compiled_deform_range(&renderer,&packed,0,1,out,6));
        for(unsigned v=0;v<4;++v) {
            const sat_vec3_t expected=reference(originals,v,cx,cz);
            assert(out[v].x==expected.x && out[v].y==expected.y && out[v].z==expected.z);
        }
        assert(!memcmp(out+4,saved+4,2*sizeof(*out)));
        assert(t3_compiled_deform_range(&renderer,&packed,4,0,out,6));
        memcpy(saved,out,sizeof(out));
        assert(!t3_compiled_deform_range(&renderer,&packed,3,2,out,6));
        assert(!t3_compiled_deform_range(&renderer,&packed,0,4,out,3));
        assert(!t3_compiled_deform_range(&renderer,&packed,UINT32_MAX,1,out,6));
        assert(!memcmp(saved,out,sizeof(out)));
        const t3_compiled_pose_t before=packed;
        assert(!t3_compiled_pose_pack(&renderer,originals,1,cx,cz,&packed));
        assert(!t3_compiled_pose_pack(&renderer,originals,3,30001,cz,&packed));
        originals[1].origin.y=-30001;
        assert(!t3_compiled_pose_pack(&renderer,originals,3,cx,cz,&packed));
        assert(!memcmp(&packed,&before,sizeof(packed)));
    }
}

static void test_bad_metadata_and_unsupported_assets(void) {
    t3_compiled_renderer_t out={0},before=out;
    assert(!t3_compiled_renderer_init(&out,&plan,&skin,indices,1,two_sided));
    t3_compiled_plan_t bad=plan;
    bad.maximum_l1=65536;
    assert(!t3_compiled_renderer_init(&out,&bad,&skin,indices,2,two_sided));
    bad=plan;bad.joint_mask=1;
    assert(!t3_compiled_renderer_init(&out,&bad,&skin,indices,2,two_sided));
    const uint16_t missing[]={0,1,3,3,4};
    bad=plan;bad.vertex_offsets=missing;
    assert(!t3_compiled_renderer_init(&out,&bad,&skin,indices,2,two_sided));
    const uint8_t wrong_joint[]={0,0};
    bad=plan;bad.face_joints=wrong_joint;
    assert(!t3_compiled_renderer_init(&out,&bad,&skin,indices,2,two_sided));
    const uint16_t duplicate[]={0,0,2};
    bad=plan;bad.rigid_vertices=duplicate;
    assert(!t3_compiled_renderer_init(&out,&bad,&skin,indices,2,two_sided));
    blend[0].weight[0]=16383;
    assert(!t3_compiled_renderer_init(&out,&plan,&skin,indices,2,two_sided));
    blend[0].weight[0]=16384;
    blend[0].count=3;
    assert(!t3_compiled_renderer_init(&out,&plan,&skin,indices,2,two_sided));
    blend[0].count=2;
    assert(!memcmp(&out,&before,sizeof(out)));
}

int main(void) {
    test_bad_metadata_and_unsupported_assets();
    test_exact_arithmetic_and_ranges();
    puts("Compiled renderer: topology, exact wide-reference arithmetic and atomic fallback passed");
    return 0;
}
