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
#ifdef T3_REPLAY_OWN_CAMERA
#include "t3_local_camera.h"
static t3_psx_fight_camera_config_t owned_camera;
static t3_psx_camera_tables_t camera_tables;
static t3_psx_trig_t camera_trig;
#endif
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
#if T3_PSX_FIGHTERS>2
static int ai_block_check;
#endif
static int original_round_flow;
static int reference_exclude_values;
static FILE* reference_debug_labels;
static unsigned reference_debug_target;
static void check(unsigned i,const char* category,const char* field,int64_t got,int64_t expected) {
    if(reference_debug_labels && !reference_debug_done && !reference_exclude_values)
        fprintf(reference_debug_labels,"%u\t%s\t%s\n",i,category,field);
    if(reference_hashes && !reference_exclude_values)reference_value(got);
    if(got==expected)return;
    printf("%s{\"fighter\":%u,\"category\":\"%s\",\"field\":\"%s\",\"actual\":%lld,\"expected\":%lld}",
           differences?",":"",i,category,field,(long long)got,(long long)expected);++differences;
}
#include "psx_match_reference.inc"
#include "psx_ai_tape.h"
#if T3_PSX_FIGHTERS>2
static void force_ai_trace_off(t3_psx_ai_game_t* ai,uint32_t address,unsigned size,uint32_t value){(void)ai;(void)address;(void)size;(void)value;}
#endif
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
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
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
#ifdef T3_PRIVATE_ACTOR_SCALES
    {
        static const unsigned scales[T3_PSX_FIGHTERS]=T3_PRIVATE_ACTOR_SCALES;
        static const uint8_t radii[T3_PSX_FIGHTERS][16]=T3_PRIVATE_ACTOR_RADII;
        static uint8_t shapes[T3_PSX_FIGHTERS][48];
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
            memcpy(shapes[i],t3_local_body_blob+384u,32u);memcpy(shapes[i]+32u,radii[i],16u);
            config.actor_scale_percent[i]=scales[i];config.actor_body_shapes[i]=shapes[i];
        }
    }
#endif
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)config.parts[i]=t3_private_actor_parts[i];
    config.ik=(t3_psx_ik_tables_t){
        {t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},t3_local_ik_blob,T3_LOCAL_IK_BYTES};
    config.body_tables=t3_local_body_blob;config.body_tables_size=T3_LOCAL_BODY_BYTES;
#if defined(T3_LOCAL_RSQRT_BYTES) && T3_LOCAL_RSQRT_BYTES > 0
    config.rsqrt=(t3_psx_rsqrt_t){t3_local_rsqrt_blob,T3_LOCAL_RSQRT_BYTES};
#endif
    config.enable_body=config.enable_combat=1;config.collapse_on_ko=getenv("T3_FORCE_START")?0:u8(0x800AFA93u); /* Tekken Force takes the alternate KO */
    config.all_attacks=getenv("T3_QUALIFIED_ATTACKS")==NULL;
    config.enable_rounds=original_round_flow?2:0;
    config.replay_storage=original_round_flow?&history:0;
    config.chip_enabled=u32(0x800AFA58u)!=0;
    config.reactions=t3_local_combat_reactions;config.reactions_size=T3_LOCAL_COMBAT_REACTION_BYTES;
    config.close_reactions=t3_local_combat_close;config.close_reactions_size=T3_LOCAL_COMBAT_CLOSE_BYTES;
    config.throw_victim_slots=t3_local_combat_throws;config.throw_victim_slots_size=T3_LOCAL_COMBAT_THROW_BYTES;
#if defined(T3_LOCAL_REVERSAL_BYTES)
    config.reversal_lists=t3_local_reversal_lists;config.reversal_lists_size=T3_LOCAL_REVERSAL_BYTES;
#endif
    const char* start_seed=getenv("T3_ORIGINAL_MATCH_START");
#if defined(T3_LOCAL_SOUND_TABLE_BYTES) && T3_LOCAL_SOUND_TABLE_BYTES > 0
    if(start_seed) {
        config.sound_tables=(t3_psx_sound_tables_t){t3_local_sound_tables,T3_LOCAL_SOUND_TABLE_BYTES};
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)config.sound_voice_set[i]=(uint8_t)i16(0x800A9228u+i*0x188Cu+0x1Au);
    }
#endif
    if(start_seed) {
        if(!original_round_flow)return 0;
        config.start_intro=1;config.start_seed=(uint32_t)strtoul(start_seed,NULL,0);
        config.start_force=getenv("T3_FORCE_START")!=NULL;
#if T3_PSX_FIGHTERS>2 && defined(T3_LOCAL_EFFECT_TABLE_BYTES) && T3_LOCAL_EFFECT_TABLE_BYTES > 0
        if(config.start_force && !getenv("T3_REPLAY_NO_EFFECTS")) {
            static t3_psx_effect_pool_t force_effects;
            config.effects=&force_effects;config.effect_tables=t3_local_effect_tables;config.effect_table_bytes=T3_LOCAL_EFFECT_TABLE_BYTES;
        }
#endif
#if T3_PSX_FIGHTERS>2
        /* A Force level: T3_FORCE_TABLES (TABLES.BIN), T3_FORCE_LEVEL (LEVn.BIN) with T3_FORCE_LEVEL_INDEX, T3_FORCE_TRIG (TRIG.BIN). */
        if(config.start_force && getenv("T3_FORCE_TABLES")) {
            static uint8_t tables[T3_FORCE_TABLE_BYTES],script[T3_FORCE_SCRIPT_BYTES],trig[0x2800];
            uint8_t head[52];FILE* f;
            if(!(f=fopen(getenv("T3_FORCE_TABLES"),"rb")) || fread(tables,1,sizeof tables,f)!=sizeof tables){return 0;}fclose(f);
            if(!(f=fopen(getenv("T3_FORCE_LEVEL"),"rb")) || fread(head,1,sizeof head,f)!=sizeof head){return 0;}
            {   /* the stage archive: big-endian header, the script's offset and size at words 11 and 12 */
                uint32_t at=(uint32_t)head[44]<<24|head[45]<<16|head[46]<<8|head[47],size=(uint32_t)head[48]<<24|head[49]<<16|head[50]<<8|head[51];
                if(size>sizeof script || fseek(f,(long)at,SEEK_SET) || fread(script,1,size,f)!=size){return 0;}
            }
            fclose(f);
            if(!(f=fopen(getenv("T3_FORCE_TRIG"),"rb")) || fread(trig,1,sizeof trig,f)!=sizeof trig){return 0;}fclose(f);
            config.force_tables=tables;config.force_script=script;config.force_trig=trig;
            if(getenv("T3_FORCED_REACTION")) {
                static uint8_t forced[44];
                if(!(f=fopen(getenv("T3_FORCED_REACTION"),"rb")) || fread(forced,1,sizeof forced,f)!=sizeof forced){return 0;}fclose(f);
                config.forced_reaction=forced;
            }
            config.force_level=(uint8_t)strtoul(getenv("T3_FORCE_LEVEL_INDEX")?getenv("T3_FORCE_LEVEL_INDEX"):"0",NULL,0);
        }
#endif
        const char* pads=getenv("T3_ORIGINAL_START_PADS");
        if(pads) {
            char* end;
            unsigned long first=strtoul(pads,&end,0),last=strtoul(end,NULL,0);
            if(first>65535u || last>65535u)return 0;
            config.start_physical[0]=(uint16_t)first;config.start_physical[1]=(uint16_t)last;
        }
    }
#ifdef T3_REPLAY_OWN_CAMERA
    if(!start_seed || getenv("T3_AI_TAPE"))return 0;
    camera_trig=(t3_psx_trig_t){t3_local_trig_blob,T3_LOCAL_TRIG_BYTES};
    camera_tables=t3_local_camera_tables;camera_tables.trig=&camera_trig;
    camera_tables.atan=t3_local_ground_blob;
    camera_tables.sqrt=t3_local_body_blob;camera_tables.sqrt_size=384u;
    owned_camera=(t3_psx_fight_camera_config_t){
        .tables=&camera_tables,.bank_choices=t3_local_camera_bank_choices,
        .bank_choice_count=T3_LOCAL_CAMERA_BANK_CHOICES,
        .bank_streams=&t3_local_camera_bank_streams,.spline=&config.spline};
#if defined(T3_LOCAL_SECOND_CAMERA_BANK_CHOICES)
    owned_camera.second_bank_choices=t3_local_second_camera_bank_choices;
    owned_camera.second_bank_choice_count=T3_LOCAL_SECOND_CAMERA_BANK_CHOICES;
    owned_camera.second_bank_streams=&t3_local_second_camera_bank_streams;
#endif
    config.camera=&owned_camera;
    static t3_psx_effect_pool_t effects;
    if(!getenv("T3_REPLAY_NO_EFFECTS"))config.effects=&effects;
#if defined(T3_LOCAL_EFFECT_TABLE_BYTES) && T3_LOCAL_EFFECT_TABLE_BYTES > 0
    config.effect_tables=t3_local_effect_tables;config.effect_table_bytes=T3_LOCAL_EFFECT_TABLE_BYTES;
#endif
#else
    if(getenv("T3_REPLAY_REQUIRE_CAMERA"))return 0;
#endif
#if T3_PSX_FIGHTERS>2
    /* Tekken Force: the enemies' pads from the CPU opponent (T3_FORCE_AI_TABLES: psx_ai.bin), live, with the
     * settings the recorded game had (T3_FORCE_AI_DIFFICULTY, T3_FORCE_AI_LEVEL). */
    if(config.start_force && getenv("T3_FORCE_AI_TABLES")) {
        size_t tsize;
        if(!(ai_tables=ai_read_file(getenv("T3_FORCE_AI_TABLES"),&tsize)) ||
           !t3_psx_ai_game_init(&ai_game,ai_tables,tsize,6u,
               (uint16_t)strtoul(getenv("T3_FORCE_AI_DIFFICULTY")?getenv("T3_FORCE_AI_DIFFICULTY"):"1",NULL,0),
               (uint8_t)strtoul(getenv("T3_FORCE_AI_LEVEL")?getenv("T3_FORCE_AI_LEVEL"):"0",NULL,0)))return 0;
#if T3_PSX_FIGHTERS>2
        if(getenv("T3_AI_NOCACHE"))ai_game.trace=force_ai_trace_off; /* a listener switches the bus's caches off */
#endif
#if T3_PSX_FIGHTERS>2
        if(getenv("T3_AI_TRACE") && getenv("T3_AI_TRACE_OUT")) {
            /* every bus read of one frame's AiUpdates, to diff against force_ai_trace.py's */
            ai_trace_frame=(uint32_t)strtoul(getenv("T3_AI_TRACE"),NULL,0);
            if(!(ai_trace_out=fopen(getenv("T3_AI_TRACE_OUT"),"w")))return 0;
            ai_game.trace=ai_trace;
        }
#endif
        ai_game.game_mode=8;ai_game.attract=0;ai_game.force_layout=1;
        if(getenv("T3_FORCE_AI_BANKS")) {
            /* the recorded game's bank placement (fighter 0, the enemies', divmot 99): the bus's addresses then equal the original's */
            char* end=getenv("T3_FORCE_AI_BANKS");
            for(unsigned k=0;k<3;++k)ai_game.bank_base[k]=(uint32_t)strtoul(end,&end,0);
        }ai_block_check=getenv("T3_AI_BLOCK_CHECK")!=NULL;
        config.ai=&ai_game;
    }
#endif
    if(!t3_psx_match_init(&game,&config))return 0;
    if(start_seed)return 1; /* No original state is copied into a fresh match. */
    for(unsigned i=0;i<T3_PSX_FIGHTERS;++i) {
        const uint32_t b=0x800A9228u+i*0x188Cu;
        game.fighter[i].blink.timer=i16(b+0x128E);
        game.fighter[i].blink.closed=u8(b+0x1290);
    }
    return reference_seed_state(sx,sz,previous,ramp);
}
#ifdef T3_REPLAY_OWN_CAMERA
#include "psx_owned_reference.inc"
#endif
int main(int argc,char** argv) {
    char line[4096];FILE* input;uint32_t sx,sz,prev,ramp;unsigned rows=0;
    if(argc!=7 && argc!=8)return 2;
    if(argc==8){if(strcmp(argv[7],"--original-round-flow"))return 2;original_round_flow=1;}
    sx=(uint32_t)strtoul(argv[3],NULL,0);sz=(uint32_t)strtoul(argv[4],NULL,0);
    prev=(uint32_t)strtoul(argv[5],NULL,0);ramp=(uint32_t)strtoul(argv[6],NULL,0);
    reference_output=getenv("T3_REFERENCE_OUTPUT");reference_record_reads=reference_output!=NULL;
#ifdef T3_REPLAY_OWN_CAMERA
    if(reference_output && setenv("T3_REPLAY_NO_VIEW","1",1))return 2;
#endif
    const char* debug_path=getenv("T3_REFERENCE_VALUES_OUTPUT");
    if(debug_path && !(reference_debug_values=fopen(debug_path,"wb")))return 2;
    const char* debug_tick=getenv("T3_REFERENCE_VALUES_TICK");
    if(debug_tick)reference_debug_target=(unsigned)strtoul(debug_tick,NULL,0);
    const char* labels_path=getenv("T3_REFERENCE_LABELS_OUTPUT");
    if(labels_path && !(reference_debug_labels=fopen(labels_path,"w")))return 2;
    if(!seed(argv[1],sx,sz,prev,ramp)){fprintf(stderr,"invalid or unsupported neutral seed\n");return 2;}
    if(getenv("T3_EFFECT_ATOMIC_AUDIT") && config.effects) {
        t3_psx_effect_pool_t bad=*config.effects,before,probe;
        bad.object[bad.head].type=255;before=bad;probe=bad;
        t3_psx_match_config_t bad_config=config;bad_config.effects=&bad;
        t3_psx_match_t trial=game;trial.config=&bad_config;
        const t3_psx_match_t initial=trial;
        const t3_psx_match_frame_t f={.frame_counter=2,.physical_inputs=1};
        if(t3_psx_match_step_original(&trial,(t3_input_t){0},(t3_input_t){0},&f) ||
           memcmp(&trial,&initial,sizeof trial) || memcmp(&bad,&before,sizeof bad))return 2;
        trial=initial;bad_config.effects=&probe;
        if(t3_psx_match_step_original_in_place(&trial,(t3_input_t){0},(t3_input_t){0},&f) ||
           probe.counter!=before.counter+1)return 2;
        fprintf(stderr,"effect transaction audit passed: failure after pool mutation rolls back\n");
    }
    reference_record_reads=0;
    if(!ai_tape_open(&config,&game)){fprintf(stderr,"invalid T3_AI_TAPE or T3_AI_TABLES\n");return 2;}
    if(reference_output && (!original_round_flow || !reference_export_seed(ram,sx,sz,prev,ramp)))return 2;
    input=fopen(argv[2],"r");if(!input)return 2;
    while(fgets(line,sizeof(line),input)) {
        char path[2048];unsigned held0,pressed0,held1,pressed1,call,frame,physical0=0,physical1=0,edge0=0,edge1=0,side0=0,side1=0,held2=0,pressed2=0,side2=0;int view_x=0;
        int fields=sscanf(line,"%2047[^\t] %u %u %u %u %u %u %u %u %u %u %u %u %u %u %d %u",path,&held0,&pressed0,&held1,&pressed1,&call,&frame,
                          &physical0,&physical1,&edge0,&edge1,&side0,&side1,&held2,&pressed2,&view_x,&side2);
        /* Original-flow tapes may append each fighter's side (no camera here). */
        if((original_round_flow?fields!=11 && fields!=13 && fields!=15 && fields!=16 && fields!=17:fields!=7) || physical0>65535 || physical1>65535 || edge0>65535 || edge1>65535 ||
           side0>1 || side1>1 ||
           (held0&~(2047u|T3_PSX_INPUT_CLEAR)) || pressed0>2047 ||
           (held1&~(2047u|T3_PSX_INPUT_CLEAR)) || pressed1>2047 ||
           (held2&~(2047u|T3_PSX_INPUT_CLEAR)) || pressed2>2047){fclose(input);return 2;}
        t3_input_t p0={(uint16_t)held0,(uint16_t)pressed0},p1={(uint16_t)held1,(uint16_t)pressed1};
#ifdef T3_REPLAY_OWN_CAMERA
        p0=p1=(t3_input_t){0};
#endif
        /* The loaded RAM is still the previous frame's (or the seed). */
        /* The fighter view (Rev.1 0x800AE2E8) and H (0x800A919C), USA addresses. */
        t3_psx_camera_matrix_t view={{{0}},{0},0};
        const int viewed=
#ifdef T3_REPLAY_OWN_CAMERA
            0 &&
#endif
            !getenv("T3_REPLAY_NO_VIEW") && has(0x800ADE20u,0x20u) && has(0x800A8CD4u,4u);
        if(viewed) {
            for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k)view.m[r][k]=i16(0x800ADE20u+6u*r+2u*k);
            for(unsigned k=0;k<3;++k)view.t[k]=i32(0x800ADE34u+4u*k);
            view.h=i32(0x800A8CD4u);
        }
        t3_psx_match_frame_t video={frame,{(uint16_t)physical0,(uint16_t)physical1},{(uint16_t)edge0,(uint16_t)edge1},
                                          fields>=13,{(uint8_t)side0,(uint8_t)side1
#if T3_PSX_FIGHTERS>2
                                          ,(uint8_t)(fields==17?side2:side1)
#endif
                                          },
                                          !getenv("T3_REPLAY_NO_SCREEN"),
                                          {config.camera?0:i16(0x800A9228u+0x1278u),config.camera?0:i16(0x800A9228u+0x188Cu+0x1278u)
#if T3_PSX_FIGHTERS>2
                                           ,config.camera?0:i16(0x800A9228u+2u*0x188Cu+0x1278u)
#endif
                                          },
                                          viewed?&view:NULL,0,{(uint16_t)held2,(uint16_t)pressed2}};
#ifdef T3_REPLAY_OWN_CAMERA
        video.side_known=0;video.screen_known=0;video.fighter_view=NULL;video.physical_inputs=1;
        video.screen_x[0]=video.screen_x[1]=0;
#endif
        /* Tekken Force: the camera's view-space x the walls read (the owned build computes it from its view) */
#if T3_PSX_FIGHTERS>2
        if(config.start_force && fields>=16)game.force_view_x=view_x;
#endif
        ai_frame=call;
        int ok=original_round_flow?
            (getenv("T3_REPLAY_IN_PLACE")?t3_psx_match_step_original_in_place(&game,p0,p1,&video):
                                     t3_psx_match_step_original(&game,p0,p1,&video)):
            t3_psx_match_step(&game,p0,p1);
#ifdef T3_MATCH_FAILURE_TRACE
        if(getenv("T3_FLOW_DEBUG")){extern volatile uint32_t g_t3_match_failure_phase;fprintf(stderr,"phase %u\n",g_t3_match_failure_phase);}
#endif
#ifdef T3_MATCH_FAILURE_TRACE
        extern volatile uint32_t g_t3_match_failure_phase;
#endif
        if(getenv("T3_FLOW_DEBUG"))fprintf(stderr,"tick %u phase %u ok %d flow state %d frame %d camera %d replay %d ticks %u\n",rows,
#ifdef T3_MATCH_FAILURE_TRACE
            g_t3_match_failure_phase,
#else
            0u,
#endif
            ok,game.flow.state,game.flow.frame,game.flow.camera,(int)game.replay.mode,game.ticks);
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
        for(unsigned i=0;i<T3_PSX_FIGHTERS;++i)compare(i,sx,sz);
        compare_round();
#if T3_PSX_FIGHTERS>2
        if(ai_block_check && config.ai && has(0x8009F690u,0x690u)) {
            /* The CPU opponent's own memory (the index, the countdowns, both records) against the recorded game's */
            for(unsigned k=0;k<0x690u;++k)
                if(config.ai->block[k]!=u8(0x8009F690u+k) && !(k>=0x0 && 0)) {
                    char name[32];snprintf(name,sizeof name,"block+%x",k);
                    check(0,"ai",name,config.ai->block[k],u8(0x8009F690u+k));
                }
        }
#endif
        if(config.start_force && has(0x800A39B8u,4))check(0,"rng","rand",game.camera.rand,u32(0x800A39B8u)); /* the shared generator */
        if(config.start_force && has(0x800ADCA0u,4))check(0,"rng","ai_rand",game.camera.ai_rand,u32(0x800ADCA0u)); /* the CPU opponent's */
        const int hud_checked=config.start_intro && !config.start_force && has(0x80097D10u,0x20u);
        /* Tekken Force draws its own bars: the versus HUD record is not compared. */
        const int sound_checked=config.start_intro && config.sound_tables.size && has(0x800971DCu,0x1Au);
        /* These additional host HUD/input audits are outside the existing shared
         * combat-state digest schema; it still fails the complete audit. */
        reference_exclude_values=1;
#ifdef T3_REPLAY_OWN_CAMERA
        reference_exclude_values=0;
        compare_owned_auxiliary();
#else
        if(config.start_intro) {
            check(0,"input","frame_seen",game.input_frame_seen,u32(0x80095464u));
            for(unsigned i=0;i<2;++i) {
                const uint32_t b=0x800A9228u+i*0x188Cu;
                if(sound_checked && i<2u) { /* the sound state is kept per player slot: the third record shares the second's */
                    const t3_psx_sound_state_t* s=&game.sound[i];
                    check(i,"sound","script",s->script,i16(b+0x70));
                    check(i,"sound","last_script",s->last_script,i16(b+0x72));
                    check(i,"sound","position",s->position,i16(b+0x11E));
                    check(i,"sound","frame",s->frame,i16(b+0x120));
                    check(i,"sound","voice_cooldown",s->voice_cooldown,i16(0x800971E8u+2*i));
                    check(i,"sound","damage_voice_timer",s->damage_voice_timer,i32(0x800971DCu+4*i));
                    check(i,"sound","hold",s->hold,i16(0x800971F0u+2*i));
                }
                /* Internal RNG/eye parity requires a capture that includes
                 * the shared generator, beyond the older combat schema. */
                if((config.camera || config.start_force) && has(0x800A39B8u,4)) {
                    check(i,"animation","blink_timer",game.fighter[i].blink.timer,i16(b+0x128E));
                    check(i,"animation","blink_closed",game.fighter[i].blink.closed,u8(b+0x1290));
                }
                const t3_psx_tap_latch_t* tap=&game.fighter[i].taps;
                check(i,"input","tap_lp",tap->lp,u8(b+0xE0));
                check(i,"input","tap_rp",tap->rp,u8(b+0xE1));
                check(i,"input","tap_both",tap->both,u8(b+0xE2));
                check(i,"input","tap_condition_used",tap->condition_used,u8(b+0x8C));
                if(has(0x80097048u,4))
                    check(i,"input","tap_previous",tap->previous,u16(0x80097048u+2*i));
            }
        }
        if(hud_checked)for(unsigned i=0;i<2;++i) {
            const uint32_t b=0x80097D14u+16u*i;
            const t3_psx_hud_bar_t* bar=&game.hud_bars[i];
            check(i,"hud","last_health",bar->last_health,i32(b));
            check(i,"hud","target",bar->target,i16(b+4));
            check(i,"hud","drawn",bar->drawn,i16(b+6));
            check(i,"hud","recent",bar->recent,i16(b+8));
        }
#endif
#ifdef T3_REPLAY_OWN_CAMERA
        compare_owned_camera();
#endif
        reference_exclude_values=0;
        if(reference_hashes) {
            uint8_t digest[32];t3_sha256_finish(&reference_digest,digest);
            if(fwrite(digest,1,32,reference_hashes)!=32 ||
               fprintf(reference_inputs,"%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\t%u\n",
                 frame,config.camera?0:held0,config.camera?0:pressed0,config.camera?0:held1,config.camera?0:pressed1,physical0,physical1,edge0,edge1,
                 reference_value_count,config.camera?0:side0,config.camera?0:side1)<0)return 2;
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
        printf(",\"camera_owned\":%s",config.camera?"true":"false");
        printf(",\"effects_owned\":%s",config.effects?"true":"false");
        printf(",\"physical_inputs\":%s,\"input_direction\":[%u,%u]",video.physical_inputs?"true":"false",
            t3_history_at(&game.fighter[0].input,0).direction,t3_history_at(&game.fighter[1].input,0).direction);
        printf(",\"camera_targets_checked\":%s",config.camera && has(0x800ADCA8u,0x50u)?"true":"false");
        printf(",\"sound_checked\":%s",sound_checked?"true":"false");
        printf(",\"camera_rng_checked\":%s",config.camera &&
            has(0x800A39B8u,4) && has(0x800A912Cu,4) && has(0x800ADCA0u,4) &&
            has(0x800A02A8u,120) && has(0x800A8C68u,0x70u)?"true":"false");
        if(getenv("T3_TRACE_GATE"))
            for(unsigned i=0;i<2;++i)fprintf(stderr,"frame %u fighter %u gate %d zone %d segments %u/%u slot %u pose %d\n",frame,i,
                (int)game.fighter[i].hit_gate.status,(int)game.fighter[i].geometry.zone,
                (unsigned)game.fighter[i].ground.active_segments,(unsigned)game.fighter[i].strike.segment_count,
                (unsigned)game.fighter[i].vm.slot,(int)game.fighter[i].vm.clock.pose_frame);
        if(getenv("T3_EFFECT_POOL_TRACE") && config.effects) {
            const t3_psx_effect_pool_t* pool=config.effects;printf(",\"effects\":[");unsigned n=0;
            for(unsigned at=pool->head;at!=80;at=pool->next[at]) {
                const t3_psx_spark_t* o=&pool->object[at];
                printf("%s[%u,%u,%u,%u,%d,%d,%d,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d]",n++?",":"",
                    o->type,o->flags,o->state,o->model,o->pitch,o->yaw,o->speed,o->clut[0],o->clut[1],
                    o->x,o->y,o->z,o->vx,o->vy,o->vz,o->a,o->b,o->c,o->d);
            }
            printf("]");
        }
        printf(",\"tap_checked\":%s,\"tap_previous_checked\":%s,\"hud_checked\":%s,\"hud_health\":[%d,%d],\"fighter_health\":[%d,%d],\"passed\":%s}\n",
               config.start_intro?"true":"false",config.start_intro && has(0x80097048u,4)?"true":"false",
               hud_checked?"true":"false",game.hud_bars[0].last_health,game.hud_bars[1].last_health,
               game.fighter[0].health_q16,game.fighter[1].health_q16,
               ok && !differences && !bad?"true":"false");++rows;
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
