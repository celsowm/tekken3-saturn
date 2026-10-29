#include "psx_particle_prepare.h"
#include <inttypes.h>
#include <stdio.h>
int main(int argc,char** argv) {
    uint8_t trigdata[T3_PSX_TRIG_BYTES],assetdata[2948];
    if(argc!=3)return 2;
    FILE* f=fopen(argv[1],"rb");
    if(!f || fseek(f,0x800+0xE8C4,SEEK_SET) || fread(trigdata,1,sizeof trigdata,f)!=sizeof trigdata)return 2;
    fclose(f);f=fopen(argv[2],"rb");
    if(!f || fread(assetdata,1,sizeof assetdata,f)!=sizeof assetdata || fgetc(f)!=EOF)return 2;
    fclose(f);
    t3_psx_particle_assets_t assets;const t3_psx_trig_t trig={trigdata,sizeof trigdata};
    if(!t3_psx_particle_assets_open(&assets,assetdata,sizeof assetdata))return 2;
    int32_t row[15];
    while(scanf("%"SCNd32,&row[0])==1) {
        for(unsigned i=1;i<15;++i)if(scanf("%"SCNd32,&row[i])!=1)return 2;
        const t3_psx_camera_view_t camera={row[0],row[1],row[2],row[3],row[4],row[5],row[6]};
        t3_psx_particle_snapshot_item_t item={{row[7],row[8],row[9]},
            {(uint16_t)row[12],(uint16_t)row[13]},0,(uint8_t)row[10],(uint8_t)row[11]};
        t3_psx_particle_view_t view;t3_psx_particle_draw_t draw;
        if(!t3_psx_particle_build_view(&camera,&trig,&view) ||
           !t3_psx_particle_prepare(&item,&assets,&view,(unsigned)row[14],&draw))return 2;
        printf("[");
        for(unsigned i=0;i<4;++i)for(unsigned j=0;j<2;++j)printf("%d,",draw.quad.xy[i][j]);
        printf("%d,%u,%u,%u]\n",draw.quad.bucket,draw.quad.visible,draw.clut,draw.pattern);
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
