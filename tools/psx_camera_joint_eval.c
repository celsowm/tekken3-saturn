#include "psx_camera.h"
#include <inttypes.h>
#include <stdio.h>
int main(void) {
    int32_t row[25];
    while(scanf("%"SCNd32,&row[0])==1) {
        for(unsigned i=1;i<25;++i)if(scanf("%"SCNd32,&row[i])!=1)return 2;
        t3_psx_camera_matrix_t view={0},out;t3_psx_joint_t joint={0};
        for(unsigned i=0;i<9;++i){view.m[i/3][i%3]=(int16_t)row[i];joint.rotation.m[i/3][i%3]=(int16_t)row[13+i];}
        for(unsigned i=0;i<3;++i)view.t[i]=row[9+i];
        view.h=row[12];
        joint.origin=(t3_vec3i_t){row[22],row[23],row[24]};
        if(!t3_psx_camera_joint_matrix(&view,&joint,&out))return 2;
        putchar('[');for(unsigned i=0;i<9;++i)printf("%d,",out.m[i/3][i%3]);
        printf("%"PRId32",%"PRId32",%"PRId32",%"PRId32"]\n",out.t[0],out.t[1],out.t[2],out.h);
    }
    return ferror(stdin)||!feof(stdin)?2:0;
}
