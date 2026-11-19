/* The compact Saturn save encoding and checksum are newly authored, as force_progress.c's. */
#include "ball_progress.h"
static uint32_t checksum(const uint8_t* p){uint32_t h=0x811C9DC5u;for(unsigned i=0;i<T3_BALL_PROGRESS_BYTES-4u;++i)h=(h^p[i])*0x01000193u;return h;}
int t3_ball_progress_pack(const t3_ball_choice_t* in,uint8_t out[T3_BALL_PROGRESS_BYTES]){
    if(!in||!out||in->first_done>1u)return 0;
    out[0]='T';out[1]='3';out[2]='B';out[3]='L';out[4]=1;out[5]=in->first_done;out[6]=out[7]=0;
    for(unsigned c=0;c<BALL_CHARACTERS;++c){out[8+2*c]=(uint8_t)(in->met[c]>>8);out[9+2*c]=(uint8_t)in->met[c];}
    const uint32_t h=checksum(out);
    for(unsigned i=0;i<4;++i)out[T3_BALL_PROGRESS_BYTES-4u+i]=(uint8_t)(h>>(24u-8u*i));
    return 1;
}
int t3_ball_progress_unpack(const uint8_t* in,uint32_t bytes,t3_ball_choice_t* out){
    if(!in||!out||bytes!=T3_BALL_PROGRESS_BYTES||in[0]!='T'||in[1]!='3'||in[2]!='B'||in[3]!='L'||in[4]!=1||in[5]>1u||in[6]||in[7])
        return 0;
    const uint8_t* q=in+T3_BALL_PROGRESS_BYTES-4u;
    if(((uint32_t)q[0]<<24|(uint32_t)q[1]<<16|(uint32_t)q[2]<<8|q[3])!=checksum(in))return 0;
    out->first_done=in[5];
    for(unsigned c=0;c<BALL_CHARACTERS;++c)out->met[c]=(uint16_t)(in[8+2*c]<<8|in[9+2*c]);
    return 1;
}
