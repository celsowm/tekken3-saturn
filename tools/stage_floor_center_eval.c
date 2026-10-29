/* Line-oriented host oracle adapter; reads private lookup tables at runtime. */
#include "stage_floor_center.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char** argv) {
    uint8_t trig_data[T3_PSX_TRIG_BYTES],body_data[T3_PSX_BODY_SQRT_BYTES],before[640];
    if(argc!=5)return 2;
    FILE* f=fopen(argv[1],"rb");if(!f)return 2;
    size_t n=fread(trig_data,1,sizeof trig_data,f);fclose(f);if(n!=sizeof trig_data)return 2;
    f=fopen(argv[2],"rb");if(!f)return 2;
    n=fread(body_data,1,sizeof body_data,f);fclose(f);if(n!=sizeof body_data)return 2;
    f=fopen(argv[3],"rb");if(!f)return 2;
    if(fseek(f,strtol(argv[4],NULL,0)-640,SEEK_SET))return 2;
    n=fread(before,1,sizeof before,f);fclose(f);if(n!=sizeof before)return 2;
    const t3_psx_trig_t trig={trig_data,sizeof trig_data};
    const t3_psx_body_tables_t body={.sqrt=body_data,.sqrt_size=sizeof body_data};
    t3_psx_camera_view_t view={0};int32_t mx,mz,out[2];
    while(scanf("%d %d %d %d %d %d %d",&view.pitch,&view.yaw,&view.x,&view.y,&view.z,&mx,&mz)==7) {
        int status=t3_stage_floor_center(&view,mx,mz,&trig,&body,before,out);
        printf("%d %d %d\n",status,status==1?out[0]:0,status==1?out[1]:0);
    }
    return ferror(stdin)?1:0;
}
