/* PRIVATE native replay: seed once at original FightFrame entry, then run the
 * same owned core as Saturn without replacing any fighter state between ticks.
 * Expected RAM ranges are read only for comparison, never copied into match.
 * Generated headers/data are explicit local build inputs, never public fixtures. */
#include "psx_match.h"
#include "attack_records.h"
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
static t3_divmot_t bank,common,second;
static t3_psx_replay_storage_t history;
static t3_motion_sample_cache_t samples;
static unsigned differences;
static int original_round_flow;
static FILE* reference_debug_labels;
static unsigned reference_debug_target;
static void check(unsigned i,const char* category,const char* field,int64_t got,int64_t expected) {
    if(reference_debug_labels && !reference_debug_done)
        fprintf(reference_debug_labels,"%u\t%s\t%s\n",i,category,field);
    if(reference_hashes)reference_value(got);
    if(got==expected)return;
    printf("%s{\"fighter\":%u,\"category\":\"%s\",\"field\":\"%s\",\"actual\":%lld,\"expected\":%lld}",
           differences?",":"",i,category,field,(long long)got,(long long)expected);++differences;
}
#include "psx_match_reference.inc"
#include "psx_ai_tape.h"
static int seed(const char* path,uint32_t sx,uint32_t sz,uint32_t previous,uint32_t ramp) {
    if(!snapshot_full(path) || !t3_divmot_open(&bank,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES) ||
       !t3_divmot_open(&common,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES) ||
       !t3_divmot_set_common_branches(&bank,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES) ||
       !t3_divmot_set_common_branches(&common,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES))return 0;
    config.bank=&bank;config.common=&common;
    /* Each fighter's bank by its seed bankType (+0x16): the main bank or
     * the optional second (t3_local_second.c). */
#if defined(T3_LOCAL_SECOND_BANK_BYTES)
    if(T3_LOCAL_SECOND_BANK_BYTES && (!t3_divmot_open(&second,t3_local_second_bank,T3_LOCAL_SECOND_BANK_BYTES) ||
       !t3_divmot_set_common_branches(&second,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES)))return 0;
#endif
    for(unsigned i=0;i<2;++i) {
        const uint32_t base=0x800A9228u+i*0x188Cu;
        const unsigned type=(unsigned)i16(base+0x16);
        config.character[i]=(uint8_t)i16(base+0x18);
        if(type==bank.bank_type)config.actor_bank[i]=NULL;
        else if(second.data && type==second.bank_type)config.actor_bank[i]=&second;
        else return 0;
    }
    /* The merged move index links each fighter's bank to the other's. */
    {
        t3_divmot_t* b0=config.actor_bank[0]?&second:&bank;
        t3_divmot_t* b1=config.actor_bank[1]?&second:&bank;
        b0->linked=b1;b1->linked=b0;
    }
    if(T3_LOCAL_COMBAT_ATTACK_BYTES==T3_BUILTIN_ATTACK_BYTES)
        bank.builtin_attacks=second.builtin_attacks=common.builtin_attacks=t3_local_combat_attacks;
    config.sample_cache=getenv("T3_DISABLE_SAMPLE_CACHE")?NULL:&samples;
    t3_motion_sample_cache_reset(config.sample_cache);
    if(!t3_psx_ground_tables_bind((t3_psx_trig_t){t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},
            t3_local_ground_blob,T3_LOCAL_GROUND_BYTES,&config.ground_tables))return 0;
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
    config.all_attacks=getenv("T3_QUALIFIED_ATTACKS")==NULL;
    config.enable_rounds=original_round_flow?2:0;
    config.replay_storage=original_round_flow?&history:0;
    config.chip_enabled=u32(0x800AFA58u)!=0;
    config.reactions=t3_local_combat_reactions;config.reactions_size=T3_LOCAL_COMBAT_REACTION_BYTES;
    config.close_reactions=t3_local_combat_close;config.close_reactions_size=T3_LOCAL_COMBAT_CLOSE_BYTES;
    config.throw_victim_slots=t3_local_combat_throws;config.throw_victim_slots_size=T3_LOCAL_COMBAT_THROW_BYTES;
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
    const char* debug_path=getenv("T3_REFERENCE_VALUES_OUTPUT");
    if(debug_path && !(reference_debug_values=fopen(debug_path,"wb")))return 2;
    const char* debug_tick=getenv("T3_REFERENCE_VALUES_TICK");
    if(debug_tick)reference_debug_target=(unsigned)strtoul(debug_tick,NULL,0);
    const char* labels_path=getenv("T3_REFERENCE_LABELS_OUTPUT");
    if(labels_path && !(reference_debug_labels=fopen(labels_path,"w")))return 2;
    if(!seed(argv[1],sx,sz,prev,ramp)){fprintf(stderr,"invalid or unsupported neutral seed\n");return 2;}
    reference_record_reads=0;
    if(!ai_tape_open(&config,&game)){fprintf(stderr,"invalid T3_AI_TAPE or T3_AI_TABLES\n");return 2;}
    if(reference_output && (!original_round_flow || !reference_export_seed(ram,sx,sz,prev,ramp)))return 2;
    input=fopen(argv[2],"r");if(!input)return 2;
    while(fgets(line,sizeof(line),input)) {
        char path[2048];unsigned held0,pressed0,held1,pressed1,call,frame,physical0=0,physical1=0,edge0=0,edge1=0,side0=0,side1=0;
        int fields=sscanf(line,"%2047[^\t] %u %u %u %u %u %u %u %u %u %u %u %u",path,&held0,&pressed0,&held1,&pressed1,&call,&frame,
                          &physical0,&physical1,&edge0,&edge1,&side0,&side1);
        /* Original-flow tapes may append each fighter's side (no camera here). */
        if((original_round_flow?fields!=11 && fields!=13:fields!=7) || physical0>65535 || physical1>65535 || edge0>65535 || edge1>65535 ||
           side0>1 || side1>1 ||
           (held0&~(2047u|T3_PSX_INPUT_CLEAR)) || pressed0>2047 ||
           (held1&~(2047u|T3_PSX_INPUT_CLEAR)) || pressed1>2047){fclose(input);return 2;}
        t3_input_t p0={(uint16_t)held0,(uint16_t)pressed0},p1={(uint16_t)held1,(uint16_t)pressed1};
        /* The loaded RAM is still the previous frame's (or the seed). */
        /* The fighter view (Rev.1 0x800AE2E8) and H (0x800A919C), USA addresses. */
        t3_psx_camera_matrix_t view={{{0}},{0},0};
        const int viewed=!getenv("T3_REPLAY_NO_VIEW") && has(0x800ADE20u,0x20u) && has(0x800A8CD4u,4u);
        if(viewed) {
            for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k)view.m[r][k]=i16(0x800ADE20u+6u*r+2u*k);
            for(unsigned k=0;k<3;++k)view.t[k]=i32(0x800ADE34u+4u*k);
            view.h=i32(0x800A8CD4u);
        }
        const t3_psx_match_frame_t video={frame,{(uint16_t)physical0,(uint16_t)physical1},{(uint16_t)edge0,(uint16_t)edge1},
                                          fields==13,{(uint8_t)side0,(uint8_t)side1},
                                          !getenv("T3_REPLAY_NO_SCREEN"),
                                          {i16(0x800A9228u+0x1278u),i16(0x800A9228u+0x188Cu+0x1278u)},
                                          viewed?&view:NULL};
        ai_frame=call;
        int ok=original_round_flow?
            (getenv("T3_REPLAY_IN_PLACE")?t3_psx_match_step_original_in_place(&game,p0,p1,&video):
                                     t3_psx_match_step_original(&game,p0,p1,&video)):
            t3_psx_match_step(&game,p0,p1);
        /* The screen x this tick's PairwiseDistances reads (+0x1278). */
        if(reference_views && !reference_add_screen(i16(0x800A9228u+0x1278u),
                                                    i16(0x800A9228u+0x188Cu+0x1278u)))
            {fclose(input);return 2;}
        /* Only ticks whose RoundFlow draw consumed the view get a view record. */
        if(reference_views && game.frame_view_used) {
            if(!reference_write_view(rows,&view)){fclose(input);return 2;}
            ++reference_view_count;
        }
        if(!snapshot(path)){fclose(input);return 2;}
        printf("{\"call\":%u,\"source_frame\":%u,\"native_tick\":%u,\"supported\":%s,\"clear_inputs\":[%u,%u],\"body_checked\":true,\"blend_checked\":%s,\"differences\":[",call,frame,game.ticks,ok?"true":"false",game.fighter[0].input.direction_cleared,game.fighter[1].input.direction_cleared,config.rsqrt.data?"true":"false");
        differences=0;
        reference_debug_done=rows!=reference_debug_target;
        if(reference_hashes){t3_sha256_init(&reference_digest);reference_value_count=0;}
        for(unsigned i=0;i<2;++i)compare(i,sx,sz);
        compare_round();
        if(reference_hashes) {
            uint8_t digest[32];t3_sha256_finish(&reference_digest,digest);
            if(fwrite(digest,1,32,reference_hashes)!=32 ||
               fprintf(reference_inputs,"%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",
                 frame,held0,pressed0,held1,pressed1,physical0,physical1,edge0,edge1,
                 reference_value_count,side0,side1)<0)return 2;
        }
        reference_debug_done=1;
        const char* history_path=getenv("T3_REPLAY_HISTORY_OUTPUT");
        if(history_path && rows==reference_debug_target) {
            FILE* history=fopen(history_path,"wb");if(!history)return 2;
            for(unsigned i=0;i<2;++i)
                if(fwrite(&game.fighter[i].input,1,sizeof(t3_input_history_t),history)!=sizeof(t3_input_history_t))return 2;
            if(fclose(history))return 2;
        }
        const char* move_path=getenv("T3_REPLAY_MOVE_OUTPUT");
        if(move_path && rows==reference_debug_target) {
            FILE* move=fopen(move_path,"w");if(!move)return 2;
            for(unsigned actor=0;actor<2;++actor) {
                const t3_psx_fighter_t* f=&game.fighter[actor];
                const t3_divmot_t* bank=f->vm.bank;
                fprintf(move,"%u %u %u %u %u %u %u %u %u %u %u %d %u %u %u %u\n",
                    (unsigned)f->vm.slot,f->vm.move.branch_index,f->vm.move.animation_offset,
                    f->vm.move.state_word,f->vm.move.flags,(unsigned)f->vm.move.length,
                    bank?(unsigned)bank->bank_type:255u,bank?(unsigned)bank->branch_count:0u,
                    bank?(unsigned)bank->common_branch_count:0u,
                    bank&&bank->linked?(unsigned)bank->linked->bank_type:255u,
                    (unsigned)f->vm.clock.pose_frame,(int)config.character[actor],
                    (unsigned)f->side_flag,(unsigned)f->ground.distance,
                    (unsigned)f->vm.move.attack_word,(unsigned)(uint16_t)f->vm.branch_window);
            }
            if(fclose(move))return 2;
        }
        const char* flow_path=getenv("T3_REPLAY_FLOW_OUTPUT");
        if(flow_path && rows==reference_debug_target) {
            FILE* flow=fopen(flow_path,"w");if(!flow)return 2;
            fprintf(flow,"%d %d %u %u %d %u %u %u\n",
                game.flow.input[0],game.flow.input[1],
                game.round_actor[0].script_input,game.round_actor[1].script_input,
                game.flow.state,game.flow.human_mask,
                game.fighter[0].ground.is_cpu,game.fighter[1].ground.is_cpu);
            if(fclose(flow))return 2;
        }
        printf("],\"unsupported\":[[%u,%u,%u],[%u,%u,%u]]",
               game.fighter[0].unsupported_moves,game.fighter[0].unsupported_slot,game.fighter[0].unsupported_transition,
               game.fighter[1].unsupported_moves,game.fighter[1].unsupported_slot,game.fighter[1].unsupported_transition);
        if(getenv("T3_TRACE_GATE"))
            for(unsigned i=0;i<2;++i)fprintf(stderr,"frame %u fighter %u gate %d zone %d segments %u/%u slot %u pose %d\n",frame,i,
                (int)game.fighter[i].hit_gate.status,(int)game.fighter[i].geometry.zone,
                (unsigned)game.fighter[i].ground.active_segments,(unsigned)game.fighter[i].strike.segment_count,
                (unsigned)game.fighter[i].vm.slot,(int)game.fighter[i].vm.clock.pose_frame);
        printf(",\"passed\":%s}\n",ok && !differences && !bad?"true":"false");++rows;
        if(!ok || bad)break;
    }
    fclose(input);
    ai_tape_close();
    if(!reference_close_views())return 2;
    if(reference_hashes && (fclose(reference_hashes) || fclose(reference_inputs)))return 2;
    if(reference_debug_values && fclose(reference_debug_values))return 2;
    if(reference_debug_labels && fclose(reference_debug_labels))return 2;
    return rows?0:2;
}
