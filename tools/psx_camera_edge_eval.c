/* Original camera phase adapter; private tables stay in the local build. */
#include "psx_camera.h"
#include "t3_local_camera.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
static uint8_t* load(const char* path,size_t expected) {
    FILE* f=fopen(path,"rb");if(!f)return NULL;
    uint8_t* p=malloc(expected);if(!p){fclose(f);return NULL;}
    if(fread(p,1,expected,f)!=expected || fgetc(f)!=EOF){free(p);p=NULL;}
    fclose(f);return p;
}
int main(int argc,char** argv) {
    if(argc!=4 && argc!=5)return 2;
    uint8_t *trig=load(argv[1],T3_PSX_TRIG_BYTES),*atan=load(argv[2],1025),*sqrt=load(argv[3],384);
    if(!trig || !atan || !sqrt)return 2;
    const t3_psx_trig_t tr={trig,T3_PSX_TRIG_BYTES};
    t3_psx_camera_tables_t tables=t3_local_camera_tables;
    const t3_psx_camera_choice_t choices[4]={{0,0,-1},{0,0,-1},{1,4095,-1},{0,4095,-1}};
    tables.trig=&tr;tables.atan=atan;tables.sqrt=sqrt;tables.sqrt_size=384;
    tables.choices=choices;tables.choice_count=4;
    unsigned kind,actor;int screen0,screen1;int32_t old0,old1,third,ax,az,bx,bz,cx,cz;
    while(scanf("%u %u %d %d %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32" %"SCNd32,
        &kind,&actor,&screen0,&screen1,&old0,&old1,&third,&ax,&az,&bx,&bz,&cx,&cz)==13) {
        if(actor>1)return 2;
        t3_psx_camera_t camera={0};camera.h=camera.h_min=camera.h_max=500;camera.faithful=(uint8_t)(argc==4);
        t3_psx_camera_reset(&camera);camera.third_target_screen_x=third;
        camera.target[0][3]=old0;camera.target[1][3]=old1;
        camera.script=-2;camera.script_kind=-1;camera.thrower=(int16_t)actor;
        camera.source[0]=(t3_psx_camera_source_t){0,-1000,-8000,0,0,500};
        camera.source[2]=camera.source[0];
        t3_psx_camera_fighter_t f[2]={{0}};
        for(unsigned p=0;p<2;++p) {
            f[p].screen_x=(int16_t)(p?screen1:screen0);f[p].head_y=-1000;
            f[p].move_flags=1;f[p].move_id=0x801E0000u+0x100u*p;f[p].pose_frame=1;
            f[p].bank_choices=choices;f[p].bank_choice_count=4;
            f[p].throw_state=(int16_t)(p==actor?1:-1);
            f[p].root_x=p==actor?ax:bx;f[p].root_z=p==actor?az:bz;
            f[p].pos_x=f[p].root_x;f[p].pos_z=f[p].root_z;
        }
        uint32_t random=1;t3_psx_camera_frame_t frame={.fighter_distance=2000,.rand=&random};
        int32_t weight=-999;int ok;
        if(kind==53) {
            int16_t phase=2;ok=t3_psx_camera_step(&camera,&tables,f,&frame,&phase);
        } else if(kind==54 || kind==55) {
            camera.stream_sample[0]=(int16_t)cx;camera.stream_sample[2]=(int16_t)cz;
            if(kind==55){camera.script_kind=0;camera.script_move=f[actor].move_id;}
            else camera.script_move=0;
            ok=t3_psx_camera_stream_update(&camera,&tables,f,0,&frame,&weight);
        } else return 2;
        printf("%d %d %d %"PRId32" %d\n",ok,camera.side,camera.thrower,weight,camera.state);
    }
    free(trig);free(atan);free(sqrt);
    return ferror(stdin) || !feof(stdin)?2:0;
}
