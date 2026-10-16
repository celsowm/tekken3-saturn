/* PRIVATE full pre-HitApply RAM -> the same owned transaction as Saturn. */
#include "psx_match.h"
#include "psx_ground_eval_fields.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx_ram_ranges.h"
static uint8_t trig[0x2800],atan_bytes[1025],push[0x58C],sparse[132];
static void* load(const char* path,size_t* size) {
    FILE* f=fopen(path,"rb");void* p;long n;
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END) || (n=ftell(f))<=0 || n>600000 || fseek(f,0,SEEK_SET)){fclose(f);return NULL;}
    p=malloc((size_t)n);if(!p){fclose(f);return NULL;}
    if(fread(p,1,(size_t)n,f)!=(size_t)n){free(p);fclose(f);return NULL;}
    fclose(f);*size=(size_t)n;return p;
}
int main(int argc,char** argv) {
    t3_psx_match_t match={0};t3_psx_match_config_t config={0};t3_divmot_t bank,common;
    size_t bank_size,common_size;uint8_t *bank_data,*common_data;unsigned target;
    if(argc!=9 || strlen(argv[2])!=1 || argv[2][0]<'0' || argv[2][0]>'1')return 2;
    target=(unsigned)(argv[2][0]-'0');
    if(!snapshot_full(argv[1]) || !read_blob(argv[5],trig,sizeof(trig)) ||
       !read_blob(argv[6],atan_bytes,sizeof(atan_bytes)) || !read_blob(argv[7],push,sizeof(push)) ||
       !read_blob(argv[8],sparse,sizeof(sparse)))return 2;
    bank_data=load(argv[3],&bank_size);common_data=load(argv[4],&common_size);
    if(!bank_data || !common_data)return 2;
    if(!t3_divmot_open(&bank,bank_data,bank_size) || !t3_divmot_open(&common,common_data,common_size))return 2;
    config.bank=&bank;config.common=&common;config.enable_combat=1;
    config.max_health_q16=i32(0x800A9228u+0x3F8u);config.chip_enabled=u32(0x800AFA58u)!=0;
    config.collapse_on_ko=u8(0x800AFA93u);config.scale_percent=100;
    config.ground_tables=(t3_psx_ground_tables_t){{trig,sizeof(trig)},atan_bytes,sizeof(atan_bytes),push,sizeof(push)};
    config.reactions=sparse;config.reactions_size=sizeof(sparse);match.config=&config;
    for(unsigned i=0;i<2;++i) {
        uint32_t b=0x800A9228u+i*0x188Cu;
        t3_psx_fighter_t* f=&match.fighter[i];
        if(!t3_move_vm_start_phased(&f->vm,&bank,&common,u16(b+0xA0)))return 2;
#define READ(member,offset,kind) f->ground.member=kind(b+offset);
        T3_GROUND_EVAL_FIELDS(READ)
#undef READ
        f->vm.clock.pose_frame=i16(b+0x58);f->vm.clock.root_frame=i16(b+0x50);
        f->vm.clock.move_frame=i16(b+0x5C);f->vm.clock.frame_step=i16(b+0x6E);
        f->vm.clock.hit_freeze=i16(b+0x76);f->vm.clock.hold_frames=i16(b+0x6A);
        f->vm.clock.ballistic=(uint8_t)i16(b+0x92);f->vm.clock.move_changed=u8(b+0xDA);
        f->health_q16=i32(b+0x3F4);f->hit_done=u8(b+0x83+1u-i);
        f->contact=u8(b+0xD1);f->contact_this_move=u8(b+0x88);f->got_hit=u8(b+0xCE);
        f->hit_freeze_in=i16(b+0xEE);f->events.extra_kind=u8(b+0xE4);f->events.extra_damage=(uint8_t)i16(b+0xEC);
        f->hit_gate.hit_slot=-1;
    }
    for(unsigned defender=0;defender<2;++defender) {
        uint32_t b=0x800A9228u+defender*0x188Cu;
        for(unsigned slot=0;slot<2;++slot)if(u8(b+0x13C+44*slot+0x21)) {
            uint32_t p=b+0x13C+44*slot;unsigned attacker=u8(p+0x20);
            if(attacker>1 || attacker==defender)return 2;
            t3_psx_hit_test_gate_t* gate=&match.fighter[attacker].hit_gate;
            gate->status=T3_HIT_GATE_ACCEPT;gate->hit_slot=(int8_t)slot;
            gate->zone=(int8_t)i16(p+0x18);gate->hit_freeze_in=(uint8_t)i16(b+0xEE);
            gate->hit_cooldown=(uint8_t)i16(b+0x104);
        }
    }
    if(bad || !t3_psx_match_apply_hits(&match))return 1;
    {
        const t3_psx_fighter_t* f=&match.fighter[target];
        printf("{\"health_q16\":%d,\"last_damage\":%d,\"guarded\":%u,\"hit_clean\":%u,\"counter\":%u,"
               "\"ko_started\":%u,\"ko_collapse\":%u,\"slot\":%u,\"pose\":%d,\"root_frame\":%d,"
               "\"move_frame\":%d,\"freeze\":%d,\"pending_slot\":%d,\"transition\":%u,\"entry_frame\":%d,\"branch_kind\":%u}\n",
               f->health_q16,f->ground.last_damage,f->hit.guarded,f->hit.hit_clean,f->hit.counter,
               f->hit.ko_started,f->ko_collapse,f->vm.slot,f->vm.clock.pose_frame,f->vm.clock.root_frame,
               f->vm.clock.move_frame,f->vm.clock.hit_freeze,f->vm.pending_valid?(int)f->vm.pending_slot:-1,
               f->vm.pending_valid?f->vm.queued.transition:f->vm.current_transition,
               f->vm.pending_valid?f->vm.queued.entry_frame:f->vm.last_entry_frame,
               f->vm.pending_valid?f->vm.queued.branch_kind:f->vm.current_branch_kind);
    }
    free(bank_data);free(common_data);return 0;
}
