/* Host adapter: nine little-endian int32 inputs/outputs per record.
 * State order: pose,root,move,step,freeze,hold,ballistic,changed.
 * Last input = authored hold frame; last output = bind rootMove to poseMove. */
#include "move_clock.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int32_t load32(const uint8_t* p) {
    uint32_t u=(uint32_t)p[0]|((uint32_t)p[1]<<8)|
               ((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);
}
static void put32(uint8_t* p,int32_t n) {
    uint32_t u=(uint32_t)n;
    p[0]=(uint8_t)u;p[1]=(uint8_t)(u>>8);p[2]=(uint8_t)(u>>16);p[3]=(uint8_t)(u>>24);
}
int main(int argc,char** argv) {
    FILE *in,*out;
    uint8_t bytes[88];
    unsigned records=0;
    const int starts=argc==4 && strcmp(argv[1],"--start")==0;
    const unsigned count=starts?22u:9u,output_count=starts?10u:9u;
    const char *input_path,*output_path;
    if(argc!=3 && !starts)return 2;
    input_path=argv[starts?2:1];output_path=argv[starts?3:2];
    in=fopen(input_path,"rb");if(!in)return 2;
    out=fopen(output_path,"wb");if(!out){fclose(in);return 2;}
    for(;;) {
        int32_t x[22],y[10];
        t3_move_clock_t c;
        int bind;
        unsigned i;
        const size_t got=fread(bytes,1,count*4u,in);
        if(got==0 && !ferror(in))break;
        if(got!=count*4u || ++records>100000u)goto failure;
        for(i=0;i<count;++i)x[i]=load32(bytes+4*i);
        for(i=0;i<6u;++i)if(x[i]<-32768 || x[i]>32767)goto failure;
        if(x[6]<0 || x[6]>1 || x[7]<0 || x[7]>1)goto failure;
        c=(t3_move_clock_t){(int16_t)x[0],(int16_t)x[1],(int16_t)x[2],
            (int16_t)x[3],(int16_t)x[4],(int16_t)x[5],(uint8_t)x[6],(uint8_t)x[7]};
        if(starts) {
            t3_move_start_t s;
            int ready;
            for(i=8;i<11u;++i)if(x[i]<-32768 || x[i]>32767)goto failure;
            for(i=11;i<22u;++i)if(x[i]<0 || x[i]>255)goto failure;
            s=(t3_move_start_t){(int16_t)x[8],(int16_t)x[9],(int16_t)x[10],
                (uint8_t)x[11],(uint8_t)x[12],(uint8_t)x[13],(uint8_t)x[14],
                (uint8_t)x[15],(uint8_t)x[16],(uint8_t)x[17],(uint8_t)x[18],
                (uint8_t)x[19],(uint8_t)x[20]};
            ready=t3_move_clock_pending_ready(&c,s.entry_frame,s.current_length);
            if(ready<0)goto failure;
            bind=ready?t3_move_clock_begin(&c,&s):t3_move_clock_advance(&c,(uint8_t)x[21]);
            y[9]=ready;
        } else {
            if(x[8]<0 || x[8]>255)goto failure;
            bind=t3_move_clock_advance(&c,(uint8_t)x[8]);
        }
        if(bind<0)goto failure;
        y[0]=c.pose_frame;y[1]=c.root_frame;y[2]=c.move_frame;y[3]=c.frame_step;
        y[4]=c.hit_freeze;y[5]=c.hold_frames;y[6]=c.ballistic;y[7]=c.move_changed;y[8]=bind;
        for(i=0;i<output_count;++i)put32(bytes+4*i,y[i]);
        if(fwrite(bytes,1,output_count*4u,out)!=output_count*4u)goto failure;
    }
    if(!records)goto failure;
    fclose(in);if(fclose(out)!=0){remove(output_path);return 1;}return 0;
failure:
    fclose(in);fclose(out);remove(output_path);return 1;
}
