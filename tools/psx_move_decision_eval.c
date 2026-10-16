/* PRIVATE host build only: include the local generated DIVMOT header/blob.
 * Input: 11 LE int32 fields + 60 oldest-to-newest direction/held/pressed bytes.
 * Output: six LE int32 fields: status,slot,transition,kind,entry,window.
 * No retail bytes in this source; no opcode execution or live RAM writes. */
#include "move_vm.h"
#include "t3_local_divmot.h"
#include <stdint.h>
#include <stdio.h>

static int32_t read32(const uint8_t* p) {
    uint32_t u=(uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
    return u<0x80000000u?(int32_t)u:(int32_t)((int64_t)u-4294967296LL);
}
static void put32(uint8_t* p,int32_t n) {
    uint32_t u=(uint32_t)n;
    for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(u>>(8u*i));
}
int main(int argc,char** argv) {
    t3_divmot_t bank,common;
    uint8_t input[224],output[24];
    FILE *in,*out;
    unsigned count=0;
    if(argc!=3)return 2;
    if(!t3_divmot_open(&bank,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES) ||
       !t3_divmot_open(&common,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES))return 2;
#if T3_LOCAL_COMMON_BRANCH_BYTES > 0
    if(!t3_divmot_set_common_branches(&bank,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES) ||
       !t3_divmot_set_common_branches(&common,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES))return 2;
#endif
    in=fopen(argv[1],"rb");if(!in)return 2;
    out=fopen(argv[2],"wb");if(!out){fclose(in);return 2;}
    for(;;) {
        int32_t x[11],y[6];
        t3_move_vm_t vm;
        t3_input_history_t history={0};
        size_t got=fread(input,1,sizeof(input),in);
        if(!got && !ferror(in))break;
        if(got!=sizeof(input) || ++count>20000u)goto fail;
        for(unsigned i=0;i<11;++i)x[i]=read32(input+i*4u);
        if(!t3_move_vm_start_phased(&vm,&bank,&common,(unsigned)x[0]) ||
           x[10]<1 || x[10]>60)goto fail;
        vm.clock=(t3_move_clock_t){(int16_t)x[1],(int16_t)x[2],(int16_t)x[3],
            (int16_t)x[4],(int16_t)x[5],(int16_t)x[6],(uint8_t)x[7],(uint8_t)x[8]};
        vm.last_entry_frame=(int16_t)x[9];
        for(int i=0;i<x[10];++i) {
            const uint8_t* h=input+44u+3u*i;
            if(h[0]<1 || h[0]>9 || h[1]>15 || h[2]>15)goto fail;
            history.frames[i]=(t3_command_frame_t){h[0],h[1],h[2]};
        }
        history.count=(uint8_t)x[10];history.next=(uint8_t)(x[10]%60);
        y[0]=t3_move_vm_queue_input(&vm,&history,NULL,t3_local_sequences,T3_LOCAL_SEQUENCE_COUNT);
        y[1]=vm.pending_valid?vm.pending_slot:-1;
        y[2]=vm.queued.transition;y[3]=vm.queued.branch_kind;
        y[4]=vm.queued.entry_frame;y[5]=vm.queued.branch_window;
        for(unsigned i=0;i<6;++i)put32(output+4u*i,y[i]);
        if(fwrite(output,1,sizeof(output),out)!=sizeof(output))goto fail;
    }
    fclose(in);if(fclose(out)!=0){remove(argv[2]);return 1;}return 0;
fail:
    fclose(in);fclose(out);remove(argv[2]);return 1;
}
