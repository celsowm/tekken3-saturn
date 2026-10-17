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
#include "psx_ram_ranges.h"
static t3_psx_match_t game;
static t3_psx_match_config_t config;
static t3_divmot_t bank,common;
static unsigned differences;
static int original_round_flow;
static void check(unsigned i,const char* category,const char* field,int64_t got,int64_t expected) {
    if(got==expected)return;
    printf("%s{\"fighter\":%u,\"category\":\"%s\",\"field\":\"%s\",\"actual\":%lld,\"expected\":%lld}",
           differences?",":"",i,category,field,(long long)got,(long long)expected);++differences;
}
static int seed(const char* path,uint32_t sx,uint32_t sz,uint32_t previous,uint32_t ramp) {
    if(!snapshot_full(path) || !t3_divmot_open(&bank,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES) ||
       !t3_divmot_open(&common,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES) ||
       !t3_divmot_set_common_branches(&bank,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES) ||
       !t3_divmot_set_common_branches(&common,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES))return 0;
    config.bank=&bank;config.common=&common;
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
    config.chip_enabled=u32(0x800AFA58u)!=0;
    config.reactions=t3_local_combat_reactions;config.reactions_size=T3_LOCAL_COMBAT_REACTION_BYTES;
    if(!t3_psx_match_init(&game,&config))return 0;
    if(original_round_flow) {
#define READ_FLOW(member,offset,kind) game.flow.member=kind(offset);
        T3_ROUND_FLOW_EVAL_FIELDS(READ_FLOW)
#undef READ_FLOW
        game.replay_mode=i32(0x8009BCA8u);
        game.fight_clock=u32(0x800AFAB4u);
    }
    for(unsigned i=0;i<2;++i) {
        uint32_t b=0x800A9228u+i*0x188Cu;
        t3_psx_fighter_t* f=&game.fighter[i];
        if(u16(b+0x18)!=0 || u16(b+0xA0)!=3 || u32(b+0x1A4) || u32(b+0x4C)!=u32(b+0x54) ||
           u16(b+0x404) || u16(b+0x406))return 0;
#define READ(member,offset,kind) f->ground.member=kind(b+offset);
        T3_GROUND_EVAL_FIELDS(READ)
#undef READ
        f->ground.step_accum_x=i32(sx+4*i);f->ground.step_accum_z=i32(sz+4*i);
        uint32_t push=u32(b+0x110);
        if(push>=0x8001075Cu && push<=0x80010CE8u)f->ground.push_cursor=(uint16_t)(push-0x8001075Cu);
        else if(f->ground.push_table_frames)return 0;
        f->vm.clock=(t3_move_clock_t){i16(b+0x58),i16(b+0x50),i16(b+0x5C),i16(b+0x6E),
            i16(b+0x76),i16(b+0x6A),(uint8_t)i16(b+0x92),u8(b+0xDA)};
        f->vm.current_transition=(uint8_t)u16(b+0xA2);f->vm.current_branch_kind=u8(b+0x8B);
        f->vm.last_entry_frame=i16(b+0x198);
        f->health_q16=i32(b+0x3F4);f->hit_done=u8(b+0x83+1u-i);
        if(original_round_flow) {
            t3_psx_round_actor_t* a=&game.round_actor[i];
            a->wins=i16(b+0x44);a->won=i16(b+0x46);a->win_pose=i16(b+0x48);
            a->health_left=i16(b+0x400);a->carried_health=i32(b+0x3FC);
            a->script_input=u16(b+0x408);a->costume=i16(b+0x1C);
        }
        f->contact=u8(b+0xD1);f->contact_this_move=u8(b+0x88);f->got_hit=u8(b+0xCE);
        f->hit_freeze_in=i16(b+0xEE);f->previous_anchor=f->ground.anchor;
        f->blend_guarded_previous=u8(b+0x89);f->blend_in_reaction=u8(b+0x8A);
        /* USA release-layout mapping of JP HeadLookAt state 0x8009E920.
         * Only the initial full RAM seeds it; expected exits never reseed. */
        uint32_t look=0x8009E578u+20*i;
        if(u32(0x8009546Cu)!=2)return 0;
        for(unsigned k=0;k<4;++k){f->head.current[k]=i16(look+2*k);f->head.target[k]=i16(look+8+2*k);}
        f->head.active=u8(look+16);f->head.snap=u8(look+17);f->head.hold=u8(look+18);
        f->no_look_at=u8(b+0xC9);
        f->parts_mask=0x3FFFFu;
        for(unsigned k=0;k<18;++k) {
            uint32_t p=b+0x8F4+0x44*k;
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                f->world[k].rotation.m[r][c]=i16(p+2*(3*r+c));
            f->world[k].origin=(t3_vec3i_t){i32(p+0x14),i32(p+0x18),i32(p+0x1C)};
        }
        f->blend.mode=(uint8_t)i32(b+0x181C);f->blend.active=u8(b+0xE5);
        f->blend.frames=i32(b+0x1870);f->blend.counter=i32(b+0x1878);
        f->blend.weight=i16(b+0x186E);f->blend.last_frame=i32(b+0x182C);
        f->blend.src_frame=u8(b+0x1884);f->blend.dst_frame=u8(b+0x1885);
        /* Seed is neutral slot3 with last row equal to the live pose row. */
        if(u32(b+0x1824)!=u32(b+0x54) || f->blend.mode)return 0;
        const uint8_t* slot=bank.data+bank.sections[2]+4*3;
        f->blend.last_row=(uint32_t)slot[0]|((uint32_t)slot[1]<<8)|((uint32_t)slot[2]<<16)|((uint32_t)slot[3]<<24);
        for(unsigned k=0;k<3;++k){f->blend.previous_root[k]=i16(b+0x1810+2*k);f->blend.root_delta[k]=i16(b+0x1868+2*k);}
        for(unsigned k=0;k<17;++k)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
            f->blend.previous[k].m[r][c]=i16(b+0x13B0+32*k+2*(r*3+c));
            f->blend.delta[k].m[r][c]=i16(b+0x15F0+32*k+2*(r*3+c));
        }
        int head=i32(b+0x40C);if(head<0 || head>=60)return 0;
        f->input=(t3_input_history_t){0};f->input.count=60;
        for(unsigned k=0;k<60;++k) {
            uint8_t value=u8(b+0x41C+(head+1+k)%60);
            if(value>=16u || (value&15u)>9u)return 0; /* seed requires unpressed neutral history */
            f->input.frames[k]=(t3_command_frame_t){value,0,0};
        }
        f->body.anchor=f->ground.anchor;f->body.root=f->ground.root;
        f->body.placed=(t3_vec3i_t){i32(b+0x12C),0,i32(b+0x130)};
        f->body.previous_root=(t3_vec3i_t){u16(previous+16*i+4),u16(previous+16*i+8),u16(previous+16*i+12)};
        f->body.ramp=i32(ramp+4*i);f->ramp_step=i32(ramp+12+4*i);
        f->body.active=u8(b+0xC3);f->body.invulnerable=u8(b+0xC2);f->body.no_push=u8(b+0xD9);
        f->body.contact=u8(b+0xD5);f->body.contact_with=(uint8_t)(u8(b+0xD6)|(u8(b+0xD7)<<1));
        f->body.push=(t3_vec3i_t){i32(b+0x3E8),i32(b+0x3EC),i32(b+0x3F0)};
    }
    return !bad;
}
static void compare(unsigned i,uint32_t sx,uint32_t sz) {
    const t3_psx_fighter_t* f=&game.fighter[i];uint32_t b=0x800A9228u+i*0x188Cu;
    if(original_round_flow) {
        const t3_psx_round_actor_t* a=&game.round_actor[i];
        check(i,"round_actor","wins",a->wins,i16(b+0x44));
        check(i,"round_actor","won",a->won,i16(b+0x46));
        check(i,"round_actor","win_pose",a->win_pose,i16(b+0x48));
        check(i,"round_actor","active",f->body.active,u8(b+0xC3));
        check(i,"round_actor","health_left",a->health_left,i16(b+0x400));
        check(i,"round_actor","carried_health",a->carried_health,i32(b+0x3FC));
        check(i,"round_actor","script_input",a->script_input,u16(b+0x408));
    }
#define CHECK(member,offset,kind) check(i,"ground",#member,f->ground.member,kind(b+offset));
    T3_GROUND_EVAL_FIELDS(CHECK)
#undef CHECK
    check(i,"ground","step_accum_x",f->ground.step_accum_x,i32(sx+4*i));
    check(i,"ground","step_accum_z",f->ground.step_accum_z,i32(sz+4*i));
    for(unsigned k=0;k<8;++k) {
        char name[48];uint32_t p=b+0x324+16*k;
        const t3_vec3i_t v=f->body.spheres[k].center;
        const int32_t xyz[3]={v.x,v.y,v.z};
        for(unsigned axis=0;axis<3;++axis) {
            snprintf(name,sizeof(name),"sphere%u.center%u",k,axis);
            check(i,"body",name,xyz[axis],i32(p+4*axis));
        }
        snprintf(name,sizeof(name),"sphere%u.radius",k);
        check(i,"body",name,f->body.spheres[k].radius,u16(p+12));
    }
    check(i,"body","push.x",f->body.push.x,i32(b+0x3E8));
    check(i,"body","push.y",f->body.push.y,i32(b+0x3EC));
    check(i,"body","push.z",f->body.push.z,i32(b+0x3F0));
#define VM(field,offset,kind) check(i,"move",#field,f->vm.field,kind(b+offset))
    VM(slot,0xA0,u16);VM(clock.pose_frame,0x58,i16);VM(clock.root_frame,0x50,i16);
    VM(clock.move_frame,0x5C,i16);VM(clock.hit_freeze,0x76,i16);VM(clock.hold_frames,0x6A,i16);
    VM(clock.frame_step,0x6E,i16);VM(clock.ballistic,0x92,i16);VM(current_transition,0xA2,u16);
#undef VM
    check(i,"hit","health_q16",f->health_q16,i32(b+0x3F4));
    check(i,"move","pending_slot",f->vm.pending_valid?(int)f->vm.pending_slot:-1,u32(b+0x1A4)?i16(b+0x1A0):-1);
    if(f->vm.pending_valid) {
        check(i,"move","queued.transition",f->vm.queued.transition,u16(b+0x19A));
        check(i,"move","queued.entry_frame",f->vm.queued.entry_frame,i16(b+0x198));
        check(i,"move","queued.branch_kind",f->vm.queued.branch_kind,u8(b+0x8B));
        check(i,"move","queued.branch_window",f->vm.queued.branch_window,i16(b+0x6C));
    }
    check(i,"hit","guarded",f->hit.guarded,u8(b+0xCF));
    check(i,"hit","hit_clean",f->hit.hit_clean,u8(b+0xD0));
    check(i,"hit","hit_done",f->hit_done,u8(b+0x83+1u-i));
    if(config.rsqrt.data) {
#define BLEND(field,off,kind) check(i,"blend",#field,f->blend.field,kind(b+off))
        BLEND(mode,0x181C,i32);BLEND(active,0xE5,u8);BLEND(frames,0x1870,i32);
        BLEND(counter,0x1878,i32);BLEND(weight,0x186E,i16);BLEND(last_frame,0x182C,i32);
        BLEND(previous_root[1],0x1812,i16);
        BLEND(src_frame,0x1884,u8);BLEND(dst_frame,0x1885,u8);
        for(unsigned k=0;k<3;++k) {
            char name[32];snprintf(name,sizeof(name),"root_delta%u",k);
            check(i,"blend",name,f->blend.root_delta[k],i16(b+0x1868+2*k));
        }
#undef BLEND
        for(unsigned k=0;k<17;++k)for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
            char name[48];snprintf(name,sizeof(name),"previous%u.%u%u",k+1,r,c);
            check(i,"blend_local",name,f->blend.previous[k].m[r][c],i16(b+0x13B0+32*k+2*(r*3+c)));
            snprintf(name,sizeof(name),"delta%u.%u%u",k+1,r,c);
            check(i,"blend_local",name,f->blend.delta[k].m[r][c],i16(b+0x15F0+32*k+2*(r*3+c)));
        }
    }
    for(unsigned k=0;k<18;++k) {
        char name[64];uint32_t p=b+0x8F4+0x44*k;
        const int32_t xyz[3]={f->world[k].origin.x,f->world[k].origin.y,f->world[k].origin.z};
        for(unsigned axis=0;axis<3;++axis) {
            snprintf(name,sizeof(name),"part%u.origin%u",k,axis);
            check(i,"joint_origin",name,xyz[axis],i32(p+0x14+4*axis));
        }
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c) {
            snprintf(name,sizeof(name),"part%u.rotation%u%u",k,r,c);
            check(i,"joint_rotation",name,f->world[k].rotation.m[r][c],i16(p+2*(3*r+c)));
        }
    }
}
int main(int argc,char** argv) {
    char line[4096];FILE* input;uint32_t sx,sz,prev,ramp;unsigned rows=0;
    if(argc!=7 && argc!=8)return 2;
    if(argc==8){if(strcmp(argv[7],"--original-round-flow"))return 2;original_round_flow=1;}
    sx=(uint32_t)strtoul(argv[3],NULL,0);sz=(uint32_t)strtoul(argv[4],NULL,0);
    prev=(uint32_t)strtoul(argv[5],NULL,0);ramp=(uint32_t)strtoul(argv[6],NULL,0);
    if(!seed(argv[1],sx,sz,prev,ramp)){fprintf(stderr,"invalid or unsupported neutral seed\n");return 2;}
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
        differences=0;for(unsigned i=0;i<2;++i)compare(i,sx,sz);
        if(original_round_flow) {
#define CHECK_FLOW(member,offset,kind) check(0,"round",#member,game.flow.member,kind(offset));
            T3_ROUND_FLOW_EVAL_FIELDS(CHECK_FLOW)
#undef CHECK_FLOW
            check(0,"round","replay_mode",game.replay_mode,i32(0x8009BCA8u));
        }
        printf("],\"passed\":%s}\n",ok && !differences && !bad?"true":"false");++rows;
        if(!ok || bad)break;
    }
    fclose(input);return rows?0:2;
}
