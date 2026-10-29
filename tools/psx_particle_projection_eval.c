#include "psx_particle_projection.h"
#include <inttypes.h>
#include <stdio.h>

int main(int argc,char** argv) {
    if(argc!=2)return 2;
    uint8_t bytes[T3_PSX_TRIG_BYTES];
    FILE* f=fopen(argv[1],"rb");
    if(!f || fseek(f,0x800+0xE8C4,SEEK_SET) || fread(bytes,1,sizeof bytes,f)!=sizeof bytes)return 2;
    fclose(f);
    const t3_psx_trig_t trig={bytes,sizeof bytes};
    int32_t row[27];
    while(scanf("%"SCNd32,&row[0])==1) {
        for(unsigned i=1;i<27;++i)if(scanf("%"SCNd32,&row[i])!=1)return 2;
        const t3_psx_camera_view_t camera={row[0],row[1],row[2],row[3],row[4],row[5],row[6]};
        t3_psx_particle_view_t view;t3_psx_particle_quad_t quad;t3_psx_particle_model_t model;
        for(unsigned i=0;i<4;++i)for(unsigned k=0;k<4;++k)model.corners[i][k]=(int16_t)row[11+4*i+k];
        if(!t3_psx_particle_build_view(&camera,&trig,&view) ||
           !t3_psx_particle_project(&view,&row[7],&model,(unsigned)row[10],&quad))return 2;
        printf("[");
        for(unsigned i=0;i<3;++i)for(unsigned k=0;k<3;++k)printf("%d,",view.m[i][k]);
        for(unsigned i=0;i<4;++i)for(unsigned k=0;k<2;++k)printf("%d,",quad.xy[i][k]);
        for(unsigned i=0;i<4;++i)printf("%u,",quad.depth[i]);
        printf("%d,%u]\n",quad.bucket,quad.visible);
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
