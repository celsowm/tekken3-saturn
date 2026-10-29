#include "psx_vertex_exchange.h"
#include "psx_vertex_project.h"
#include "psx_projected_mesh.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
int main(int argc,char** argv) {
    const int mesh=argc==2 && !strcmp(argv[1],"--mesh");
    const int project=mesh || (argc==2 && !strcmp(argv[1],"--project"));
    if(argc>2 || (argc==2 && !project))return 2;
    t3_psx_vertex_exchange_t state={0};unsigned reset,nlocal,nglobal,nown;
    while(scanf("%u %u %u %u",&reset,&nlocal,&nglobal,&nown)==4) {
        if(reset)memset(&state,0,sizeof state);
        if(nlocal>128 || nglobal>128 || nown>128 || nlocal+nglobal+nown>128)return 2;
        uint8_t local[128],global[128];uint32_t xy[128];int16_t z[128],positions[128][3];uint16_t post[6][128];
        t3_psx_camera_matrix_t matrix={0};
        if(project) {
            for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j){int v;if(scanf("%d",&v)!=1 || v< -32768 || v>32767)return 2;matrix.m[i][j]=(int16_t)v;}
            for(unsigned i=0;i<3;++i)if(scanf("%"SCNd32,&matrix.t[i])!=1)return 2;
            if(scanf("%"SCNd32,&matrix.h)!=1)return 2;
        }
        t3_psx_vertex_block_t block={0};block.scratch_import=local;block.scratch_count=nlocal;
        block.global_import=global;block.global_count=nglobal;block.own_xy=xy;block.own_z=z;block.own_count=nown;
        for(unsigned i=0;i<nlocal;++i){unsigned v;if(scanf("%u",&v)!=1 || v>255)return 2;local[i]=(uint8_t)v;}
        for(unsigned i=0;i<nglobal;++i){unsigned v;if(scanf("%u",&v)!=1 || v>255)return 2;global[i]=(uint8_t)v;}
        for(unsigned i=0;i<nown;++i) {
            if(project)for(unsigned k=0;k<3;++k){int v;if(scanf("%d",&v)!=1 || v< -32768 || v>32767)return 2;positions[i][k]=(int16_t)v;}
            else {int depth;if(scanf("%"SCNu32" %d",&xy[i],&depth)!=2 || depth< -32768 || depth>32767)return 2;z[i]=(int16_t)depth;}
        }
        for(unsigned g=0;g<6;++g){unsigned n;if(scanf("%u",&n)!=1 || n>128)return 2;block.post[g]=post[g];block.post_count[g]=n;
            for(unsigned i=0;i<n;++i){unsigned v;if(scanf("%u",&v)!=1 || v>511)return 2;post[g][i]=(uint16_t)v;}}
        unsigned total=nlocal+nglobal+nown;
        if(mesh) {
            uint16_t mapping[256];t3_psx_projected_vertex_t out[128]={{0}};
            t3_psx_projected_part_t part={0};
            part.scratch_import=local;part.scratch_count=nlocal;part.global_import=global;part.global_count=nglobal;
            part.positions=(const int16_t (*)[3])positions;part.own_count=nown;
            for(unsigned g=0;g<6;++g){part.post[g]=post[g];part.post_count[g]=block.post_count[g];}
            for(unsigned i=0;i<total;++i){mapping[2*i]=(uint16_t)i;mapping[2*i+1]=(uint16_t)(total-1-i);}
            part.logical_map=mapping;part.map_count=total;
            if(!t3_psx_projected_mesh_step(&state,&part,&matrix,out,total))return 2;
            for(unsigned i=0;i<total;++i)
                if(out[total-1-i].xy!=state.scratch.xy[i] || out[total-1-i].z!=state.scratch.z[i])return 2;
        } else if(project ? !t3_psx_vertex_project_exchange(&state,&block,&matrix,(const int16_t (*)[3])positions)
                          : !t3_psx_vertex_exchange(&state,&block))return 2;
        printf("{\"scratch\":[");
        for(unsigned i=0;i<total;++i)printf("%s[%"PRIu32",%d]",i?",":"",state.scratch.xy[i],state.scratch.z[i]);
        printf("],\"global\":[");
        for(unsigned i=0;i<128;++i)printf("%s[%"PRIu32",%d]",i?",":"",state.global.xy[i],state.global.z[i]);
        puts("]}");
    }
    return ferror(stdin)||!feof(stdin)?2:0;
}
