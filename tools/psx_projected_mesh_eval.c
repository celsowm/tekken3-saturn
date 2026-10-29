#include "psx_projected_mesh.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
enum { BLOCKS=24, SLOTS=128, OUTPUT=4096 };
static t3_psx_projected_part_t parts[BLOCKS];
static uint8_t local[BLOCKS][SLOTS],global[BLOCKS][SLOTS];
static int16_t position[BLOCKS][SLOTS][3];
static uint16_t post[BLOCKS][6][SLOTS],map[BLOCKS][SLOTS*2];
static t3_psx_projected_vertex_t output[OUTPUT];
int main(void) {
    unsigned count;
    while(scanf("%u",&count)==1) {
        if(count>BLOCKS)return 2;
        memset(parts,0,sizeof parts);
        t3_psx_camera_matrix_t view={0};view.h=500;
        for(unsigned i=0;i<3;++i)view.m[i][i]=4096;
        t3_psx_joint_t joints[T3_PSX_DRAW_PARTS]={0};uint32_t mask=0;unsigned total=0;
        for(unsigned b=0;b<count;++b) {
            unsigned p,nl,ng,no;if(scanf("%u %u %u %u",&p,&nl,&ng,&no)!=4 ||
                p>=T3_PSX_DRAW_PARTS || nl+ng+no>SLOTS || total+nl+ng+no>OUTPUT)return 2;
            t3_psx_projected_part_t* a=&parts[b];a->part=p;a->scratch_import=local[b];a->scratch_count=nl;
            a->global_import=global[b];a->global_count=ng;a->positions=(const int16_t (*)[3])position[b];a->own_count=no;
            for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){int v;if(scanf("%d",&v)!=1 || v< -32768 || v>32767)return 2;joints[p].rotation.m[i][j]=(int16_t)v;}
            if(scanf("%"SCNd32" %"SCNd32" %"SCNd32,&joints[p].origin.x,&joints[p].origin.y,&joints[p].origin.z)!=3)return 2;
            mask|=1u<<p;
            for(unsigned i=0;i<nl;++i){unsigned v;if(scanf("%u",&v)!=1 || v>255)return 2;local[b][i]=(uint8_t)v;}
            for(unsigned i=0;i<ng;++i){unsigned v;if(scanf("%u",&v)!=1 || v>255)return 2;global[b][i]=(uint8_t)v;}
            for(unsigned i=0;i<no;++i)for(unsigned j=0;j<3;++j){int v;if(scanf("%d",&v)!=1 || v< -32768 || v>32767)return 2;position[b][i][j]=(int16_t)v;}
            for(unsigned g=0;g<6;++g){unsigned n;if(scanf("%u",&n)!=1 || n>SLOTS)return 2;a->post[g]=post[b][g];a->post_count[g]=n;
                for(unsigned i=0;i<n;++i){unsigned v;if(scanf("%u",&v)!=1 || v>511)return 2;post[b][g][i]=(uint16_t)v;}}
            a->logical_map=map[b];a->map_count=nl+ng+no;
            for(unsigned i=0;i<nl+ng+no;++i){map[b][2*i]=(uint16_t)i;map[b][2*i+1]=(uint16_t)total++;}
        }
        if(!t3_psx_projected_mesh_build(parts,count,&view,joints,mask,output,total))return 2;
        putchar('[');for(unsigned i=0;i<total;++i)printf("%s[%"PRIu32",%d]",i?",":"",output[i].xy,output[i].z);puts("]");
    }
    return ferror(stdin)||!feof(stdin)?2:0;
}
