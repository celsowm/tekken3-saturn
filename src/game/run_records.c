#include "run_records.h"
static uint32_t checksum(const uint8_t* p){uint32_t h=0x811C9DC5u;for(unsigned i=0;i<12;++i)h=(h^p[i])*0x01000193u;return h;}
static void put32(uint8_t* p,uint32_t x){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(x>>(24u-8u*i));}
static uint32_t get32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
int t3_run_records_pack(const t3_run_records_t* in,uint8_t out[T3_RUN_RECORDS_BYTES]){
    if(!in||!out||in->attack_seconds>T3_RUN_RECORDS_MAX_SECONDS)return 0;
    out[0]='T';out[1]='3';out[2]='R';out[3]='R';out[4]=1;out[5]=in->survival_wins;
    out[6]=(uint8_t)(in->attack_seconds>>8);out[7]=(uint8_t)in->attack_seconds;
    put32(out+8,0);put32(out+12,checksum(out));return 1;
}
int t3_run_records_unpack(const uint8_t* in,uint32_t bytes,t3_run_records_t* out){
    if(!in||!out||bytes!=T3_RUN_RECORDS_BYTES||in[0]!='T'||in[1]!='3'||in[2]!='R'||in[3]!='R'||in[4]!=1||
       get32(in+8)||get32(in+12)!=checksum(in))return 0;
    const unsigned seconds=(unsigned)in[6]<<8|in[7];
    if(seconds>T3_RUN_RECORDS_MAX_SECONDS)return 0;
    *out=(t3_run_records_t){in[5],(uint16_t)seconds};return 1;
}
int t3_run_records_survival(t3_run_records_t* records,unsigned wins){
    if(wins>T3_RUN_RECORDS_MAX_WINS)wins=T3_RUN_RECORDS_MAX_WINS;
    if(!wins||wins<=records->survival_wins)return 0;
    records->survival_wins=(uint8_t)wins;return 1;
}
int t3_run_records_attack(t3_run_records_t* records,unsigned seconds,int completed){
    if(!completed||!seconds)return 0;
    if(seconds>T3_RUN_RECORDS_MAX_SECONDS)seconds=T3_RUN_RECORDS_MAX_SECONDS;
    if(records->attack_seconds && seconds>=records->attack_seconds)return 0;
    records->attack_seconds=(uint16_t)seconds;return 1;
}
