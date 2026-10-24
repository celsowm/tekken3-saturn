/* Native grounded physics against PRIVATE compact CPU RAM range snapshots.
 * No retail bytes, guest code execution or emulator writes are implemented. */
#include "psx_ground.h"
#include "psx_ground_eval_fields.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psx_ram_ranges.h"
static uint8_t trig_bytes[0x2800],atan_bytes[1025],push_bytes[0x58C];
static void state_read(t3_psx_ground_state_t* n,unsigned index,uint32_t sx,uint32_t sz) {
    uint32_t base=0x800A9228u+index*0x188Cu,pointer;
    *n=(t3_psx_ground_state_t){0};
#define READ(member,offset,kind) n->member=kind(base+offset);
    T3_GROUND_EVAL_FIELDS(READ)
#undef READ
    n->step_accum_x=i32(sx+4u*index);n->step_accum_z=i32(sz+4u*index);
    pointer=u32(base+0x110u);
    if(pointer>=0x8001075Cu && pointer<=0x80010CE8u) {
        n->push_cursor=(uint16_t)(pointer-0x8001075Cu);
    } else if(n->push_table_frames>0){bad=1;return;}
}
static void move_read(t3_move_record_t* m,uint32_t row) {
    *m=(t3_move_record_t){0};
    m->state_word=u32(row+4u);m->flags=u32(row+36u);m->step=i16(row+22u);
    m->end_turn=i16(row+18u);m->damage_class=u16(row+20u);m->attack_word=u16(row+8u);
    m->length=u8(row+24u);m->air_start=u8(row+25u);m->air_end=u8(row+26u);
    m->hold_frame=u8(row+27u);m->active_first=u8(row+45u);m->active_last=u8(row+46u);
}
static void vm_read(t3_move_vm_t* v,unsigned index) {
    uint32_t b=0x800A9228u+index*0x188Cu,row=u32(b+0x54u);
    *v=(t3_move_vm_t){0};v->slot=u16(b+0xA0u);
    v->clock=(t3_move_clock_t){i16(b+0x58u),i16(b+0x50u),i16(b+0x5Cu),
        i16(b+0x6Eu),i16(b+0x76u),i16(b+0x6Au),(uint8_t)i16(b+0x92u),u8(b+0xDAu)};
    move_read(&v->move,row);
}
int main(int argc,char** argv) {
    uint32_t sx,sz;char line[8192];FILE *list,*out;int starts;
    t3_psx_ground_tables_t tables={{trig_bytes,sizeof(trig_bytes)},atan_bytes,sizeof(atan_bytes),push_bytes,sizeof(push_bytes),NULL};
    if(argc!=9)return 2;
    starts=strcmp(argv[8],"start-ranges-v1")==0;
    if(!read_blob(argv[1],trig_bytes,sizeof(trig_bytes)) || !read_blob(argv[2],atan_bytes,sizeof(atan_bytes)) ||
       !read_blob(argv[3],push_bytes,sizeof(push_bytes)))return 2;
    sx=(uint32_t)strtoul(argv[4],NULL,0);sz=(uint32_t)strtoul(argv[5],NULL,0);
    list=fopen(argv[6],"r");if(!list)return 2;
    out=fopen(argv[7],"w");if(!out){fclose(list);return 2;}
    if(!starts && strcmp(argv[8],"ranges-v1")!=0){fclose(list);fclose(out);return 2;}
    while(fgets(line,sizeof(line),list)) {
        char *before=strtok(line,"\t"),*after=strtok(NULL,"\t"),*player=strtok(NULL,"\t"),*id=strtok(NULL,"\n");
        unsigned index,call,failures=0;int supported;
        uint8_t direction,pressed;
        t3_psx_ground_state_t n,opponent,expected;
        t3_move_vm_t vm,opponent_vm;
        if(!before || !after || !player || !id)goto fail;
        index=(unsigned)strtoul(player,NULL,0);call=(unsigned)strtoul(id,NULL,0);if(index>1u)goto fail;
        if(!snapshot(before))goto fail;
        state_read(&n,index,sx,sz);state_read(&opponent,1u-index,sx,sz);
        vm_read(&vm,index);vm_read(&opponent_vm,1u-index);
        {
            const uint32_t b=0x800A9228u+index*0x188Cu;
            int32_t history_index=i32(b+0x40Cu);
            uint8_t history;
            if(history_index<0 || history_index>=60){bad=1;history_index=0;}
            history=u8(b+0x41Cu+(uint32_t)history_index);
            direction=history&15u;pressed=history>>4u;
        }
        if(bad)goto fail;
        if(starts) {
            uint32_t b=0x800A9228u+index*0x188Cu,pending=u32(b+0x1A4u);
            int ready=t3_move_clock_pending_ready(&vm.clock,i16(b+0x198u),vm.move.length);
            supported=ready>=0;
            if(!pending)goto fail;
            if(ready==1) {
                t3_move_record_t previous=vm.move;
                t3_move_start_facts_t facts={u8(b+0xCEu),(uint8_t)(i32(b+0x3F4u)!=0),i16(b+0xEEu)};
                t3_psx_reaction_pick_t reaction={0};
                uint8_t segments=0,flags=(uint8_t)((u16(b+0x19Cu)?128u:0u)|(u16(b+0x19Eu)?64u:0u));
                uint32_t r=u32(b+0x1A8u);
                move_read(&vm.move,pending);vm.current_transition=(uint8_t)u16(b+0x19Au);
                vm.current_branch_kind=u8(b+0x8Bu);
                if(vm.move.active_first) {
                    uint32_t desc=u32(pending+0x28u);
                    segments=u8(desc+2u)?2u:u8(desc)?1u:0u;
                }
                if(vm.current_transition>=35u) {
                    unsigned code=vm.current_transition;
                    if(!r)goto fail;
                    reaction.selected=1;reaction.transition=(uint16_t)code;
                    reaction.push_angle=i16(r+(code==37u?0x24u:0x1Cu));
                    reaction.push_index=i16(r+(code==35u?0x28u:code==37u?0x26u:code==38u?0x22u:0x1Eu));
                }
                supported=t3_psx_ground_start(&n,&opponent,&vm,&previous,&tables,flags,
                                              u8(b+0xC1u),facts,&reaction,segments);
            }
        } else supported=t3_psx_ground_step(&n,&opponent,&vm,&opponent_vm,&tables,direction,pressed);
        if(!snapshot(after))goto fail;
        state_read(&expected,index,sx,sz);if(bad)goto fail;
        fprintf(out,"{\"call\":%u,\"fighter\":%u,\"slot\":%u,\"supported\":%d,\"differences\":{",call,index,vm.slot,supported);
#define CHECK(member,offset,kind) do {if(n.member!=expected.member) { \
    fprintf(out,"%s\"%s\":[%lld,%lld]",failures?",":"",#member,(long long)n.member,(long long)expected.member);++failures;}}while(0);
        T3_GROUND_EVAL_FIELDS(CHECK)
        CHECK(step_accum_x,0,i32) CHECK(step_accum_z,0,i32) CHECK(push_cursor,0,u16)
#undef CHECK
        fprintf(out,"},\"passed\":%s}\n",supported && !failures?"true":"false");
    }
    if(ferror(list))goto fail;
    fclose(list);if(fclose(out)!=0){remove(argv[7]);return 1;}return 0;
fail:fclose(list);fclose(out);remove(argv[7]);return 1;
}
