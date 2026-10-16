#include "psx_reaction.h"

static int16_t le16(const uint8_t* p) {
    uint16_t v=(uint16_t)(p[0]|((uint16_t)p[1]<<8u));
    return (int16_t)(v<0x8000u?(int32_t)v:(int32_t)v-65536);
}
static int valid_move(int16_t slot) {
    return slot>=0 && slot<T3_DIVMOT_MOVE_SLOTS;
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
    /* Original attack class 3 = damage with no move reaction.
     * Class 2 reacts only for grounded, non-down defenders. */
    down=(unsigned)((facts->defender_state&4u)!=0u ||
                    (facts->defender_state&0x200u)!=0u);
    if(hit->attack_class==3u ||
       (hit->attack_class==2u && (down || hit->airborne))) {
        *out=value;
        return 1;
    }
    if(facts->forced_hit || hit->airborne) {
        /* Requires fixed forced-hit record or AirReactionKind map. */
        return 0;
    }
    crouch=(unsigned)((facts->defender_state&7u)==1u);
    if(hit->reaction_kind==T3_HIT_REACTION_THROW_SLOT) {
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
                                  t3_psx_reaction_pick_t* out) {
    t3_psx_reaction_tables_t tables={0};t3_psx_hit_candidate_t local;
    const uint8_t* found=NULL;int previous=-1;
    if(!hit || !facts || !out || !records || !size || size%44u || size/44u>T3_PSX_REACTIONS_MAX)return 0;
    for(size_t offset=0;offset<size;offset+=44u) {
        int index=le16(records+offset);
        if(index<=previous || index>=T3_PSX_REACTIONS_MAX)return 0;
        previous=index;if(index==hit->reaction_word)found=records+offset+2;
    }
    if(hit->reaction_kind!=T3_HIT_REACTION_RECORD)return 0;
    if(!found)return 0;
    local=*hit;local.reaction_word=0;
    tables.records=found;tables.records_size=T3_PSX_REACTION_BYTES;
    return t3_psx_reaction_select(&local,facts,&tables,out);
}
