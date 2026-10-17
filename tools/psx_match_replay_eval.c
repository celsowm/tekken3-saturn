/* PRIVATE native replay: seed once at original FightFrame entry, then run the
 * same owned core as Saturn without replacing any fighter state between ticks.
 * Expected RAM ranges are read only for comparison, never copied into match.
 * Generated headers/data are explicit local build inputs, never public fixtures. */
#include "psx_match.h"
#include "psx_ground_eval_fields.h"
#include "psx_round_flow_eval_fields.h"
#include "t3_local_divmot.h"
#include "t3_generated_character.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sha256.h"
#include "psx_reference_export.h"
#define T3_RAM_READ_OBSERVER(address,size) reference_read(address,size)
#include "psx_ram_ranges.h"
static t3_psx_match_t game;
static t3_psx_match_config_t config;
static t3_divmot_t bank,common;
static t3_psx_replay_storage_t history;
static t3_motion_sample_cache_t samples;
static unsigned differences;
static int original_round_flow;
static void check(unsigned i,const char* category,const char* field,int64_t got,int64_t expected) {
    if(reference_hashes)reference_value(got);
    if(got==expected)return;
    printf("%s{\"fighter\":%u,\"category\":\"%s\",\"field\":\"%s\",\"actual\":%lld,\"expected\":%lld}",
           differences?",":"",i,category,field,(long long)got,(long long)expected);++differences;
}
#include "psx_match_reference.inc"
static int seed(const char* path,uint32_t sx,uint32_t sz,uint32_t previous,uint32_t ramp) {
    if(!snapshot_full(path) || !t3_divmot_open(&bank,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES) ||
       !t3_divmot_open(&common,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES) ||
       !t3_divmot_set_common_branches(&bank,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES) ||
       !t3_divmot_set_common_branches(&common,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES))return 0;
    config.bank=&bank;config.common=&common;
    config.sample_cache=getenv("T3_DISABLE_SAMPLE_CACHE")?NULL:&samples;
    t3_motion_sample_cache_reset(config.sample_cache);
    config.ground_tables=(t3_psx_ground_tables_t){{t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},
        t3_local_ground_blob,1025,t3_local_ground_blob+1025,T3_LOCAL_GROUND_BYTES-1025};
    config.spline=(t3_motion_tables_t){t3_local_spline_blob,T3_LOCAL_SPLINE_BYTES};
    config.sequences=t3_local_sequences;config.sequence_count=T3_LOCAL_SEQUENCE_COUNT;
    config.scale_percent=T3_KMD_SCALE_PERCENT;config.max_health_q16=i32(0x800A9228u+0x3F8);
    config.parts[0]=t3_private_actor_parts[0];config.parts[1]=t3_private_actor_parts[1];config.ik=(t3_psx_ik_tables_t){
        {t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},t3_local_ik_blob,T3_LOCAL_IK_BYTES};
    config.body_tables=t3_local_body_blob;config.body_tables_size=T3_LOCAL_BODY_BYTES;
#if defined(T3_LOCAL_RSQRT_BYTES) && T3_LOCAL_RSQRT_BYTES > 0
    config.rsqrt=(t3_psx_rsqrt_t){t3_local_rsqrt_blob,T3_LOCAL_RSQRT_BYTES};
#endif
    config.enable_body=config.enable_combat=1;config.collapse_on_ko=u8(0x800AFA93u);
    config.enable_rounds=original_round_flow?2:0;
    config.replay_storage=original_round_flow?&history:0;
    config.chip_enabled=u32(0x800AFA58u)!=0;
    config.reactions=t3_local_combat_reactions;config.reactions_size=T3_LOCAL_COMBAT_REACTION_BYTES;
    if(!t3_psx_match_init(&game,&config))return 0;
    return reference_seed_state(sx,sz,previous,ramp);
}
int main(int argc,char** argv) {
    char line[4096];FILE* input;uint32_t sx,sz,prev,ramp;unsigned rows=0;
    if(argc!=7 && argc!=8)return 2;
    if(argc==8){if(strcmp(argv[7],"--original-round-flow"))return 2;original_round_flow=1;}
    sx=(uint32_t)strtoul(argv[3],NULL,0);sz=(uint32_t)strtoul(argv[4],NULL,0);
    prev=(uint32_t)strtoul(argv[5],NULL,0);ramp=(uint32_t)strtoul(argv[6],NULL,0);
    reference_output=getenv("T3_REFERENCE_OUTPUT");reference_record_reads=reference_output!=NULL;
    if(!seed(argv[1],sx,sz,prev,ramp)){fprintf(stderr,"invalid or unsupported neutral seed\n");return 2;}
    reference_record_reads=0;
    if(reference_output && (!original_round_flow || !reference_export_seed(ram,sx,sz,prev,ramp)))return 2;
    input=fopen(argv[2],"r");if(!input)return 2;
    while(fgets(line,sizeof(line),input)) {
        char path[2048];unsigned held0,pressed0,held1,pressed1,call,frame,physical0=0,physical1=0,edge0=0,edge1=0;
        int fields=sscanf(line,"%2047[^\t] %u %u %u %u %u %u %u %u %u %u",path,&held0,&pressed0,&held1,&pressed1,&call,&frame,
                          &physical0,&physical1,&edge0,&edge1);
        if(fields!=(original_round_flow?11:7) || physical0>65535 || physical1>65535 || edge0>65535 || edge1>65535 ||
           (held0&~(2047u|T3_PSX_INPUT_CLEAR)) || pressed0>2047 ||
           (held1&~(2047u|T3_PSX_INPUT_CLEAR)) || pressed1>2047){fclose(input);return 2;}
        t3_input_t p0={(uint16_t)held0,(uint16_t)pressed0},p1={(uint16_t)held1,(uint16_t)pressed1};
        const t3_psx_match_frame_t video={frame,{(uint16_t)physical0,(uint16_t)physical1},{(uint16_t)edge0,(uint16_t)edge1}};
        int ok=original_round_flow?t3_psx_match_step_original(&game,p0,p1,&video):t3_psx_match_step(&game,p0,p1);
        if(!snapshot(path)){fclose(input);return 2;}
        printf("{\"call\":%u,\"source_frame\":%u,\"native_tick\":%u,\"supported\":%s,\"clear_inputs\":[%u,%u],\"body_checked\":true,\"blend_checked\":%s,\"differences\":[",call,frame,game.ticks,ok?"true":"false",game.fighter[0].input.direction_cleared,game.fighter[1].input.direction_cleared,config.rsqrt.data?"true":"false");
        differences=0;
        if(reference_hashes){t3_sha256_init(&reference_digest);reference_value_count=0;}
        for(unsigned i=0;i<2;++i)compare(i,sx,sz);
        compare_round();
        if(reference_hashes) {
            uint8_t digest[32];t3_sha256_finish(&reference_digest,digest);
            if(fwrite(digest,1,32,reference_hashes)!=32 ||
               fprintf(reference_inputs,"%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",
                 frame,held0,pressed0,held1,pressed1,physical0,physical1,edge0,edge1,reference_value_count)<0)return 2;
        }
        printf("],\"passed\":%s}\n",ok && !differences && !bad?"true":"false");++rows;
        if(!ok || bad)break;
    }
    fclose(input);
    if(reference_hashes && (fclose(reference_hashes) || fclose(reference_inputs)))return 2;
    return rows?0:2;
}
