/* PRIVATE CPU range observations -> exact native geometry/gate checks. */
#include "psx_exact_contact.h"
#include "psx_contact.h"
#include "psx_hit_test_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx_ram_ranges.h"
static uint8_t trig_bytes[0x2800];
static unsigned diffs;
static int16_t signed16(uint32_t u){u&=65535u;return (int16_t)(u<32768u?(int32_t)u:(int32_t)u-65536);}
static t3_vec3i_t vec(uint32_t p){return (t3_vec3i_t){i32(p),i32(p+4),i32(p+8)};}
static t3_world_attack_t segments(uint32_t b) {
    t3_world_attack_t a={0};a.segment_count=2;
    for(unsigned k=0;k<2;++k) {
        uint32_t p=b+0x1ACu+24*k;a.segments[k].start=vec(p);a.segments[k].end=vec(p+12);
    }
    return a;
}
static void check(const char* name,int32_t actual,int32_t expected) {
    if(actual!=expected) {
        if(diffs++)putchar(',');
        printf("{\"field\":\"%s\",\"actual\":%d,\"expected\":%d}",name,actual,expected);
    }
}
static void vector_check(const char* label,t3_vec3i_t a,t3_vec3i_t e) {
    char text[80];snprintf(text,sizeof(text),"%s.x",label);check(text,a.x,e.x);
    snprintf(text,sizeof(text),"%s.y",label);check(text,a.y,e.y);
    snprintf(text,sizeof(text),"%s.z",label);check(text,a.z,e.z);
}
static int shapes(uint32_t b,unsigned index,uint32_t anchor,uint32_t end0,uint32_t end1,uint32_t mode,
                  t3_world_attack_t* attack,t3_hurt_cylinder_t hurt[14]) {
    uint32_t row=u32(b+0x54),desc=u32(row+40);unsigned count=u8(b+0xB4);
    t3_psx_joint_t parts[T3_PSX_DRAW_PARTS]={0};t3_move_record_t move={0};
    t3_psx_trig_t trig={trig_bytes,sizeof(trig_bytes)};
    *attack=segments(b);
    for(unsigned k=0;k<18;++k)parts[k].origin=vec(b+0x8F4+0x44*k+0x14);
    if(!t3_psx_hurt_from_parts(parts,0x3FFFFu,0,hurt))return 0;
    move.active_first=u8(row+45);move.active_last=u8(row+46);move.length=u8(row+24);
    if(count) {
        int frame=i16(b+0x58);
        t3_attack_sample_t sample;
        t3_divmot_t bank={0};
        t3_world_attack_t next,previous={0};
        size_t size=70u+20u*move.length;
        if(desc<0x80000000u || !at(desc,(unsigned)size) || bad)return 0;
        bank.data=ram;bank.size=sizeof(ram);bank.sections[9]=desc-0x80000000u;
        bank.sections[10]=bank.sections[9]+(uint32_t)size;move.attack_descriptor=4;
        if(frame>=move.active_first && frame<=move.active_last && u8(desc)<=23u) {
            previous.segment_count=2;
            previous.segments[0].end=vec(end0+12*index);previous.segments[1].end=vec(end1+12*index);
            if(t3_attack_sample(&bank,&move,(unsigned)frame,i32(mode+4*index),&sample)!=1 ||
               !t3_psx_attack_place(&sample,(unsigned)(frame-move.active_first),vec(b),vec(anchor+12*index),
                     vec(b+0xF68),u16(b+0x2C),u16(b+0xE),&trig,&previous,&next))return 0;
            for(unsigned k=0;k<next.segment_count;++k)attack->segments[k]=next.segments[k];
        } else {
            if(count>2u)return 0;
            for(unsigned k=0;k<count;++k) {
                unsigned a=u8(desc+2*k),j=u8(desc+2*k+1);
                if(a>=18u || j>=18u)return 0;
                attack->segments[k].end=parts[a].origin;
                if(j)attack->segments[k].start=parts[j].origin;
            }
        }
    }
    return !bad;
}
int main(int argc,char** argv) {
    char before[2048],after[2048],kind[16];unsigned index,call;FILE* list;
    uint32_t anchor,end0,end1,mode;
    if(argc!=7 || !read_blob(argv[1],trig_bytes,sizeof(trig_bytes)))return 2;
    anchor=(uint32_t)strtoul(argv[2],NULL,0);end0=(uint32_t)strtoul(argv[3],NULL,0);
    end1=(uint32_t)strtoul(argv[4],NULL,0);mode=(uint32_t)strtoul(argv[5],NULL,0);
    list=fopen(argv[6],"r");if(!list)return 2;
    while(fscanf(list,"%2047s\t%2047s\t%15s\t%u\t%u",before,after,kind,&index,&call)==5) {
        uint32_t b=0x800A9228u+index*0x188Cu;int ok=1;unsigned accepted=0;
        if(index>1 || !snapshot(before)){fclose(list);return 2;}
        printf("{\"call\":%u,\"kind\":\"%s\",\"diffs\":[",call,kind);diffs=0;
        if(!strcmp(kind,"shape")) {
            t3_world_attack_t actual,expected;t3_hurt_cylinder_t hurt[14];
            ok=shapes(b,index,anchor,end0,end1,mode,&actual,hurt);
            if(!ok || bad || !snapshot(after)){fclose(list);return 2;}
            expected=segments(b);
            for(unsigned k=0;k<2;++k) {
                char label[40];snprintf(label,sizeof(label),"segment%u.start",k);
                vector_check(label,actual.segments[k].start,expected.segments[k].start);
                snprintf(label,sizeof(label),"segment%u.end",k);
                vector_check(label,actual.segments[k].end,expected.segments[k].end);
            }
            for(unsigned k=0;k<14;++k) {
                char label[40];uint32_t p=b+0x20C+20*k;
                snprintf(label,sizeof(label),"hurt%u.center",k);vector_check(label,hurt[k].center,vec(p));
                check("hurt.radius",hurt[k].radius,i32(p+12));
                check("hurt.radius_squared",(int32_t)hurt[k].radius*hurt[k].radius,i32(p+16));
            }
        } else if(!strcmp(kind,"contact")) {
            uint32_t d=0x800A9228u+(1u-index)*0x188Cu,ar=u32(b+0x54),dr=u32(d+0x54);
            t3_psx_hit_test_facts_t facts={0};t3_psx_hit_test_gate_t gate;
            t3_world_attack_t strike=segments(b);t3_hurt_cylinder_t hurt[14];
            unsigned hit_segment=0;int zone;
            int16_t freeze=i16(d+0xEE),cooldown=i16(d+0x104);
            unsigned got=u8(d+0xCE),done=u8(b+0x83+1u-index),contact=u8(b+0x88),own_contact=u8(b+0xD1);
            unsigned was_hit=u8(d+0x87),whiff=u8(b+0xD2);uint8_t used[2];
            strike.segment_count=u8(b+0xE3);if(strike.segment_count>2){fclose(list);return 2;}
            for(unsigned k=0;k<14;++k) {
                uint32_t p=d+0x20C+20*k;hurt[k]=(t3_hurt_cylinder_t){vec(p),u16(p+12)};
                if(i32(p+16)!=(int32_t)hurt[k].radius*hurt[k].radius){fclose(list);return 2;}
            }
            zone=t3_psx_attack_contact(&strike,hurt,&hit_segment);
            if(zone< -1){fclose(list);return 2;}
            facts.geometry=(t3_psx_shadow_result_t){strike.segment_count?(zone>=0?T3_SHADOW_CONTACT:T3_SHADOW_MISS):T3_SHADOW_INACTIVE,
                                                    0,(int8_t)zone,(int8_t)hit_segment};
            facts.attack_word=u16(b+0x64);facts.attacker_move_slot=u16(b+0xA0);
            facts.defender_state=u16(d+0x60);facts.defender_pose_frame=u16(d+0x58);
            facts.attacker_damage=i16(b+0x5E);facts.attacker_freeze=u8(ar+44);
            facts.defender_air_start=u8(dr+25);facts.defender_air_end=u8(dr+26);
            facts.defender_state_class=u8(d+0x69);facts.defender_step_kind=u8(d+0xB6);
            facts.game_mode=(uint8_t)u32(0x800AFA88u);
            for(unsigned k=0;k<2;++k) {
                used[k]=u8(d+0x13C+44*k+0x21);
                if(!used[k])facts.free_slots_mask|=(uint8_t)(1u<<k);
            }
            facts.attacker_invulnerable=u8(b+0xC2)!=0;facts.defender_invulnerable=u8(d+0xC2)!=0;
            facts.attacker_active=u8(b+0xC3)!=0;facts.defender_active=u8(d+0xC3)!=0;
            facts.hit_done=done!=0;facts.defender_hit_cooldown=cooldown!=0;
            facts.defender_in_air=u8(d+0xDB)!=0;facts.defender_in_throw=u8(d+0xC4)!=0;
            facts.attacker_forced_hit=u8(b+0xE6)!=0;facts.defender_is_cpu=u8(d+0xC5)!=0;
            facts.forced_contact_flag=u8(d+0xD6+index)!=0;
            if(bad || !t3_psx_hit_test_gate(&facts,&gate)){fclose(list);return 2;}
            if(gate.status==T3_HIT_GATE_ACCEPT) {
                got=done=contact=own_contact=was_hit=1;freeze=gate.hit_freeze_in;
                cooldown=gate.hit_cooldown;used[(unsigned)gate.hit_slot]=1;accepted=1;
            }
            if(u8(ar+46)==u16(b+0x58) && !contact)whiff=1;
            if(!snapshot(after)){fclose(list);return 2;}
            check("got_hit",got,u8(d+0xCE));check("hit_done",done,u8(b+0x83+1u-index));
            check("contact_this_move",contact,u8(b+0x88));check("contact",own_contact,u8(b+0xD1));
            check("was_hit",was_hit,u8(d+0x87));check("freeze",freeze,i16(d+0xEE));
            check("cooldown",cooldown,i16(d+0x104));check("whiff",whiff,u8(b+0xD2));
            for(unsigned k=0;k<2;++k)check("slot_used",used[k],u8(d+0x13C+44*k+0x21));
            if(accepted) {
                uint32_t slot=d+0x13C+44u*(unsigned)gate.hit_slot;
                const t3_world_segment_t* seg=&strike.segments[(unsigned)gate.segment];
                check("zone",gate.zone,i16(slot+0x18));check("attacker",(int32_t)index,u8(slot+0x20));
                vector_check("hit_end",seg->end,vec(slot));
                check("hit_dir.x",signed16((uint32_t)seg->end.x-(uint32_t)seg->start.x),i16(slot+0x10));
                check("hit_dir.y",signed16((uint32_t)seg->end.y-(uint32_t)seg->start.y),i16(slot+0x12));
                check("hit_dir.z",signed16((uint32_t)seg->end.z-(uint32_t)seg->start.z),i16(slot+0x14));
            }
        } else {fclose(list);return 2;}
        if(bad){fclose(list);return 2;}
        printf("],\"accepted\":%s,\"passed\":%s}\n",accepted?"true":"false",diffs?"false":"true");
    }
    fclose(list);return 0;
}
