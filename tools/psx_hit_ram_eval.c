/* Offline adapter from a PRIVATE, pre-HitApply USA RAM snapshot to the SAME
 * portable HitApply implementation linked by Saturn. No guest code executes.
 * Addresses are JP->USA metadata from the pinned fingerprinted release map;
 * the Python caller must verify provenance before using retail observations. */
#include "psx_hit_apply.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define RAM_SIZE (2u*1024u*1024u)
#define FIGHTERS 0x800A9228u
static uint8_t ram[RAM_SIZE];
static int bad;
static const uint8_t* at(uint32_t address,unsigned n) {
    if(address<0x80000000u || n>RAM_SIZE ||
       address-0x80000000u>RAM_SIZE-n) {bad=1;return ram;}
    return ram+(address-0x80000000u);
}
static uint8_t u8(uint32_t a) {return *at(a,1u);}
static uint16_t u16(uint32_t a) {
    const uint8_t* p=at(a,2u);return (uint16_t)(p[0]|(p[1]<<8u));
}
static int16_t s16(uint32_t a) {return (int16_t)u16(a);}
static uint32_t u32(uint32_t a) {
    const uint8_t* p=at(a,4u);
    return (uint32_t)p[0]|((uint32_t)p[1]<<8u)|
           ((uint32_t)p[2]<<16u)|((uint32_t)p[3]<<24u);
}
static t3_psx_hit_extra_t extra(uint32_t f) {
    t3_psx_hit_extra_t e={u8(f+0xE4u),s16(f+0xECu),s16(f+0x5Eu),s16(f+0x74u)};
    return e;
}
int main(int argc,char** argv) {
    t3_psx_hit_apply_frame_t frame={0};
    t3_psx_hit_apply_result_t result;
    t3_move_record_t moves[2]={{0}};
    t3_psx_hit_facts_t facts[2]={{0}};
    t3_psx_reaction_tables_t tables={0};
    uint32_t d,opp,pose;
    unsigned index,i;
    FILE* file;
    if(argc!=3 || (argv[2][0]!='0' && argv[2][0]!='1') || argv[2][1])return 2;
    index=(unsigned)(argv[2][0]-'0');
    file=fopen(argv[1],"rb");
    if(!file)return 2;
    if(fread(ram,1,RAM_SIZE,file)!=RAM_SIZE || fgetc(file)!=EOF) {
        fclose(file);return 2;
    }
    fclose(file);
    d=FIGHTERS+index*0x188Cu;opp=FIGHTERS+(1u-index)*0x188Cu;
    if(u32(0x800AFA88u)!=1u || u16(d+0x18u)!=0u || u16(opp+0x18u)!=0u)
        return 2; /* explicitly bounded Paul mirror VS adapter */
    pose=u32(d+0x54u);
    frame.health_q16=(int32_t)u32(d+0x3F4u);
    frame.max_health_q16=(int32_t)u32(d+0x3F8u);
    frame.defender_extra=extra(d);frame.opponent_extra=extra(opp);
    frame.suppress_health_change=(uint8_t)(u32(0x8009547Cu)!=0u || u32(0x800954B4u)!=0u);
    tables.records=at(0x80010CE8u,633u*42u);
    tables.records_size=633u*42u;
    frame.reaction_tables=&tables;
    frame.reaction_facts_known=1u;
    frame.reaction_facts.defender_state=(uint16_t)u32(d+0x60u);
    frame.reaction_facts.facing_quadrant=(uint8_t)u16(d+0x32u);
    frame.reaction_facts.forced_hit=u8(d+0xE7u);
    for(i=0u;i<2u;++i) {
        const uint32_t slot=d+0x13Cu+i*0x2Cu;
        uint32_t attacker,row;
        t3_move_record_t* m=&moves[i];
        t3_psx_hit_facts_t* f=&facts[i];
        if(!u8(slot+0x21u))continue;
        if(u8(slot+0x21u)!=1u || s16(slot+0x18u)<0 || s16(slot+0x18u)>=14)
            return 2;
        frame.slots[i].used=1u;
        frame.slots[i].attacker=u8(slot+0x20u);
        if(frame.slots[i].attacker>1u || s16(slot+0x1Eu)!=0)return 2;
        attacker=FIGHTERS+frame.slots[i].attacker*0x188Cu;
        row=u32(attacker+0x54u);
        m->attack_word=u16(row+8u);m->damage_class=u16(row+0x14u);
        m->reaction=u16(row+0x32u);m->close_reaction=u16(row+0x34u);
        f->attacker_damage=s16(attacker+0x5Eu);f->defender_damage=s16(d+0x5Eu);
        f->defender_guard=u16(d+0x66u);f->defender_rel_angle=u16(d+0x30u);
        f->defender_pose_frame=u16(d+0x58u);f->defender_active_first=u8(pose+0x2Du);
        f->defender_state=(uint16_t)u32(pose+4u);f->juggle_count=u16(d+0x94u);
        f->fighter_distance=u32(0x800AFA3Cu);
        if(m->close_reaction) {
            const uint32_t close=0x800177DCu+4u*m->close_reaction;
            f->close_threshold=(uint32_t)(int32_t)s16(close+2u);
            f->close_reaction_word=u16(close);f->close_table_available=1u;
        }
        f->attacker_power=(uint8_t)(s16(attacker+0x122u)!=0);
        f->defender_power=(uint8_t)(s16(d+0x122u)!=0);
        f->chip_enabled=(uint8_t)(u32(0x800AFA58u)!=0u);
        f->defender_in_air=u8(d+0xDBu);
        frame.slots[i].zone=(uint8_t)s16(slot+0x18u);
        frame.slots[i].move=m;frame.slots[i].facts=f;
    }
    if(bad || !t3_psx_hit_apply_preview(&frame,&result))return 1;
    printf("{\"health_q16\":%ld,\"extra_damage\":%d,\"total_damage\":%d,"
           "\"ko_started\":%u,\"guarded\":%u,\"hit_clean\":%u,\"counter\":%u,"
           "\"reaction_known\":%u,\"reaction_slot\":%u,\"reaction_transition\":%u,\"slots\":[",
           (long)result.health_q16,result.extra_damage,result.total_damage,
           result.ko_started,result.guarded,result.hit_clean,result.counter,
           result.reaction_known,result.reaction.move_slot,result.reaction.transition);
    for(i=0;i<2u;++i) {
        const t3_psx_hit_candidate_t* h=&result.slots[i];
        printf("%s{\"used\":%u,\"damage\":%d,\"base_damage\":%d,\"no_damage\":%u,"
               "\"guarded\":%u,\"chip\":%u,\"close\":%u,\"hit\":%u,\"counter\":%u,"
               "\"airborne\":%u}",i?",":"",result.slot_used[i],h->damage,h->base_damage,
               h->no_damage,h->guarded,h->chip,h->close,h->hit,h->counter,h->airborne);
    }
    puts("]}");return 0;
}
