/* Required Notice: Copyright (c) 2026 n0madic, PolyForm Noncommercial 1.0.0.
 * Original Force end hook (FUN_800B61D4), adapted from Tekken3Remake's
 * remake/core/game/mode_rules.gd and docs/research/code/modes.md. The compact
 * Saturn save encoding/checksum is newly authored; see NOTICE.md. */
#include "force_progress.h"
static void put32(uint8_t* p,uint32_t x){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(x>>(24u-8u*i));}
static uint32_t get32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint32_t checksum(const uint8_t* p){uint32_t h=0x811C9DC5u;for(unsigned i=0;i<12;++i)h=(h^p[i])*0x01000193u;return h;}
int t3_force_progress_pack(const t3_force_progress_t* in,uint8_t out[T3_FORCE_PROGRESS_BYTES]){
    if(!in||!out||in->keys>3u||in->doctor>1u||in->hiscore>99999990u)return 0;
    out[0]='T';out[1]='3';out[2]='F';out[3]='S';out[4]=1;out[5]=in->keys;out[6]=in->doctor;out[7]=0;
    put32(out+8,in->hiscore);put32(out+12,checksum(out));return 1;
}
int t3_force_progress_unpack(const uint8_t* in,uint32_t bytes,t3_force_progress_t* out){
    if(!in||!out||bytes!=T3_FORCE_PROGRESS_BYTES||in[0]!='T'||in[1]!='3'||in[2]!='F'||in[3]!='S'||
       in[4]!=1||in[5]>3u||in[6]>1u||in[7]||get32(in+8)>99999990u||get32(in+12)!=checksum(in))return 0;
    *out=(t3_force_progress_t){get32(in+8),in[5],in[6]};return 1;
}
int t3_force_run_finish(const t3_force_handoff_t* start,const t3_force_level_t* level,
    int won,int32_t health,uint32_t seed,t3_force_handoff_t* out){
    *out=*start;
    out->level=(uint8_t)level->level;out->section=T3_FORCE_SECTION_RESULT;
    out->score=level->score;out->hiscore=level->hiscore_run;
    out->keys=(uint8_t)(level->keys>3?3:level->keys<0?0:level->keys);
    out->flags=level->mode_flags;out->timer=level->timer;out->health=health;
    out->kills=level->kills;out->seed=seed;out->new_record=level->hiscore_flag!=0;
    if(won!=1||!level->flag_70e8)return T3_FORCE_RUN_LOST;
    if(level->level<3 || (level->level==3 && level->final_challenge)){
        out->level=(uint8_t)(level->level+1);out->section=T3_FORCE_SECTION_SCROLL;
        return T3_FORCE_RUN_NEXT;
    }
    return T3_FORCE_RUN_RESULT;
}
