/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * HitSelectReaction order and AirReactionKind adapted from the pinned
 * Tekken3Remake remake/core/fighter/move_system.gd and checked against the
 * Rev.1 decompile (0x8002F0E8). Changes: typed facts, private sparse tables,
 * explicit refusal of unported forced hits.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_reaction.h"

static int16_t le16(const uint8_t* p) {
    uint16_t v=(uint16_t)(p[0]|((uint16_t)p[1]<<8u));
    return (int16_t)(v<0x8000u?(int32_t)v:(int32_t)v-65536);
}
static int valid_move(int16_t slot) {
    return slot>=0 && slot<T3_DIVMOT_MOVE_SLOTS;
}
unsigned t3_psx_air_reaction_kind(const t3_psx_reaction_facts_t* f) {
    const uint16_t slot=f->defender_slot;
    unsigned special=0;
    /* A hit during the down-bounce moves (0xD62, or 0xD63/0xD64 after frame
     * 0x12) keeps the fighter low: kind 2, or 0 when the root is high. */
    if(slot==0xD62u || ((slot==0xD63u || slot==0xD64u) && f->defender_pose_frame>0x12)) {
        if(slot!=0xD62u) {
            const int16_t h=(int16_t)f->defender_root_y;
            if(f->defender_root_y<0?-(int32_t)h>0x3FF:h>0x3FF)return 0;
        }
        return 2;
    }
    if(f->attacker_bank==9u && f->attacker_slot==0x241u)special=1;
    if(f->attacker_bank==0x10u && f->attacker_slot==0x14Au) {
        special=1;
        if(f->attacker_pending_slot>=0 && (uint16_t)(f->attacker_pending_slot-0x848)<2u)special=2;
    }
    if(special==1 || (special!=2 && f->hit_dir_y>0x6D))return 1;
    if(slot!=0xD58u) {
        if(f->facing_quadrant==1u)return 4;
        if(f->facing_quadrant==3u)return 3;
    }
    return 0;
}
int t3_psx_reaction_select(const t3_psx_hit_candidate_t* hit,
                           const t3_psx_reaction_facts_t* facts,
                           const t3_psx_reaction_tables_t* tables,
                           t3_psx_reaction_pick_t* out) {
    static const uint8_t normal_off[4]={0u,8u,16u,12u};
    static const uint16_t normal_branch[4]={0x2Au,0x27u,0x2Bu,0x28u};
    t3_psx_reaction_pick_t value={0};
    const uint8_t* p;
    size_t record_count,off,index;
    int16_t target;
    unsigned crouch,down;
    if(!hit || !facts || !out || !tables ||
       facts->facing_quadrant>3u || hit->zone>=14u ||
       (hit->guarded && hit->hit) || hit->attack_class>3u)
        return 0;
    /* MoveStep (0x800454E0) reacts to attack class 0, and to class 2 only
     * while the defender is neither in the air (+0xDB) nor down (state bit
     * 2); classes 1 and 3 deal damage with no move reaction. */
    down=(unsigned)((facts->defender_state&4u)!=0u ||
                    (facts->defender_state&0x200u)!=0u);
    if(hit->attack_class==1u || hit->attack_class==3u ||
       (hit->attack_class==2u && (facts->in_air || (facts->defender_state&4u)))) {
        *out=value;
        return 1;
    }
    if(facts->forced_hit) {
        /* Requires the fixed forced-hit record. */
        return 0;
    }
    crouch=(unsigned)((facts->defender_state&7u)==1u);
    if(hit->reaction_kind==T3_HIT_REACTION_THROW_SLOT) {
        if(hit->airborne)return 0;
        index=(size_t)(hit->reaction_word&0x0FFFu);
        if(!tables->throw_victim_slots ||
           tables->throw_slots_size%2u ||
           index>=tables->throw_slots_size/2u)return 0;
        target=le16(tables->throw_victim_slots+index*2u);
        if(!valid_move(target))return 0;
        value.selected=1u;
        value.throw_victim=1u;
        value.move_slot=(uint16_t)target;
        value.transition=0x22u;
        *out=value;
        return 1;
    }
    if(hit->reaction_kind!=T3_HIT_REACTION_RECORD ||
       !tables->records ||
       tables->records_size%T3_PSX_REACTION_BYTES)return 0;
    record_count=tables->records_size/T3_PSX_REACTION_BYTES;
    if(!record_count || record_count>T3_PSX_REACTIONS_MAX ||
       hit->reaction_word>=record_count)return 0;
    p=tables->records+(size_t)hit->reaction_word*T3_PSX_REACTION_BYTES;
    if(hit->guarded) {
        off=0x14u+2u*crouch;
        value.transition=0x23u;
        value.push_angle=le16(p+0x1Cu);
        value.push_index=le16(p+0x28u);
    } else if(hit->airborne) {
        unsigned kind;
        if(!tables->air_kinds)return 0;
        kind=t3_psx_air_reaction_kind(facts);
        target=le16(tables->air_kinds+kind*4u+2u);
        if(!valid_move(target))return 0;
        value.transition=0x24u;value.air=1u;value.air_kind=(uint8_t)kind;
        value.ground_offset=le16(tables->air_kinds+kind*4u);
        value.push_angle=le16(p+0x1Cu);
        value.move_slot=(uint16_t)target;value.selected=1u;
        *out=value;
        return 1;
    } else if(down) {
        off=(facts->defender_state&0x200u)?0x1Au:0x18u;
        value.transition=0x25u;
        value.push_angle=le16(p+0x24u);
        value.push_index=le16(p+0x26u);
    } else if(hit->counter) {
        off=4u+2u*crouch;
        value.transition=0x26u;
        value.push_angle=le16(p+0x1Cu);
        value.push_index=le16(p+0x22u);
    } else {
        off=normal_off[facts->facing_quadrant]+2u*crouch;
        value.transition=normal_branch[facts->facing_quadrant];
        value.push_angle=le16(p+0x1Cu);
        value.push_index=le16(p+0x1Eu);
    }
    target=le16(p+off);
    if(!valid_move(target))return 0;
    value.move_slot=(uint16_t)target;
    value.selected=1u;
    *out=value;
    return 1;
}

int t3_psx_reaction_select_sparse(const t3_psx_hit_candidate_t* hit,
                                  const t3_psx_reaction_facts_t* facts,
                                  const uint8_t* records,size_t size,
                                  const uint8_t* air_kinds,
                                  t3_psx_reaction_pick_t* out) {
    t3_psx_reaction_tables_t tables={0};t3_psx_hit_candidate_t local;
    tables.air_kinds=air_kinds;
    const uint8_t* found=NULL;int previous=-1;
    if(!hit || !facts || !out || !records || !size || size%44u || size/44u>T3_PSX_REACTIONS_MAX)return 0;
    for(size_t offset=0;offset<size;offset+=44u) {
        int index=le16(records+offset);
        if(index<=previous || index>=T3_PSX_REACTIONS_MAX)return 0;
        previous=index;if(index==hit->reaction_word)found=records+offset+2;
    }
    /* The attack-class outcomes without a reaction come first; a throw slot
     * reaches the full select without its table and is refused there. */
    if(hit->reaction_kind!=T3_HIT_REACTION_RECORD)return t3_psx_reaction_select(hit,facts,&tables,out);
    if(!found)return 0;
    local=*hit;local.reaction_word=0;
    tables.records=found;tables.records_size=T3_PSX_REACTION_BYTES;
    return t3_psx_reaction_select(&local,facts,&tables,out);
}
