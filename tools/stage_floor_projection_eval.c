/* Host-side adapter for comparing the Saturn probe's floor projections with
 * privately captured original GPU packets. No game data is embedded. */
#include "psx_camera.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(int argc,char** argv) {
    int combined=argc==2 && !strcmp(argv[1],"--combined");
    if(argc!=1 && !combined)return 2;
    t3_psx_camera_matrix_t m={0};
    int value,origin_x,origin_z;
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j) {
        if(scanf("%d",&value)!=1 || value< -32768 || value>32767)return 2;
        m.m[i][j]=(int16_t)value;
    }
    if(scanf("%d %d %d %d %d %d",&m.t[0],&m.t[1],&m.t[2],&m.h,&origin_x,&origin_z)!=6)return 2;
    const int32_t origin[3]={origin_x,0,origin_z};
    if(!combined)t3_psx_camera_translate_view(&m,origin,&m);
    for(int z=0;z<11;++z)for(int x=0;x<11;++x) {
        int16_t point[3]={(int16_t)(-9000+x*1800),0,(int16_t)(-9000+z*1800)};
        int32_t out[3];
        if(combined) {
            const int32_t world[3]={origin_x+point[0],0,origin_z+point[2]};
            t3_psx_camera_project(&m,world,out);
        } else t3_psx_camera_project_s16(&m,point,out);
        printf("%d %d %d\n",out[0],out[1],out[2]);
    }
    return 0;
}
