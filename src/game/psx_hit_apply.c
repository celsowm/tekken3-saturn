#include "psx_hit_apply.h"
#include <stdint.h>

static int16_t signed16(uint16_t x) {
    return (int16_t)(x < 0x8000u ? (int32_t)x : (int32_t)x - 65536);
}
static int32_t signed32(uint32_t x) {
    return x < 0x80000000u ? (int32_t)x :
           (int32_t)((int64_t)x - 4294967296LL);
}
static int16_t wrap16(int64_t n) {
    return signed16((uint16_t)(uint64_t)n);
}
static int32_t wrap32(int64_t n) {
    return signed32((uint32_t)(uint64_t)n);
}
static int valid_extra(const t3_psx_hit_extra_t* v) {
    return v && v->kind<=3u;
}
int t3_psx_hit_extra_damage(const t3_psx_hit_extra_t* defender,
                            const t3_psx_hit_extra_t* partner,
                            int16_t* out) {
    int64_t extra=0;
    int16_t own,opp;
    if(!out || !valid_extra(defender) || !valid_extra(partner))
        return 0;
    own=defender->extra_damage ?
        defender->extra_damage : defender->damage;
    if(defender->kind==2u)extra=own;
    if(defender->kind==3u)extra=-(int64_t)own;
    if(defender->throw_state<0 && partner->kind==1u) {
        opp=partner->extra_damage ?
            partner->extra_damage : partner->damage;
        extra+=opp;
    }
    *out=wrap16(extra);
    return 1;
}

int t3_psx_hit_apply_preview(const t3_psx_hit_apply_frame_t* frame,
                             t3_psx_hit_apply_result_t* out) {
    t3_psx_hit_apply_result_t result={0};
    int16_t extra;
    int64_t total;
    int32_t health;
    unsigned i;
    if(!frame || !out || frame->max_health_q16<=0 ||
       frame->health_q16<0 || frame->health_q16>frame->max_health_q16 ||
       frame->suppress_health_change>1u ||
       frame->reaction_facts_known>1u ||
       !t3_psx_hit_extra_damage(&frame->defender_extra,
                                &frame->opponent_extra,&extra))
        return 0;
    result.best_slot=-1;
    result.last_attacker=-1;
    result.extra_damage=extra;
    total=extra;
    for(i=0u;i<T3_PSX_HIT_SLOTS;++i) {
        const t3_psx_hit_slot_input_t* s=&frame->slots[i];
        if(s->used>1u)return 0;
        if(!s->used)continue;
        if(s->attacker>1u || !s->move || !s->facts ||
           !t3_psx_hit_preview(s->move,s->zone,s->facts,
                               &result.slots[i]))return 0;
        result.slot_used[i]=1u;
        total+=result.slots[i].damage;
        if(result.best_slot<0)
            result.best_slot=(int8_t)i;
        else if(t3_psx_hit_best(&result.slots[(unsigned)result.best_slot],
                                &result.slots[i])==1)
            result.best_slot=(int8_t)i;
    }
    result.total_damage=wrap16(total);
    result.slot_damage=wrap16(total-extra);
    /* HitApply operates in signed 32-bit MIPS registers, not C overflow UB.
     * Original subtract wraps BEFORE signed clamp to [0, healthMax]. */
    if(frame->suppress_health_change)
        health=frame->health_q16;
    else
        health=wrap32((int64_t)frame->health_q16-total*65536);
    if(health<0)health=0;
    if(health>frame->max_health_q16)health=frame->max_health_q16;
    result.health_q16=health;
    result.ko_started=(uint8_t)(health==0 && frame->health_q16!=0);
    if(result.best_slot>=0) {
        const unsigned best=(unsigned)result.best_slot;
        const t3_psx_hit_candidate_t* b=&result.slots[best];
        result.last_attacker=(int8_t)frame->slots[best].attacker;
        result.hit_clean=b->hit;
        result.guarded=b->guarded;
        result.close=b->close;
        result.counter=b->counter;
        if(result.ko_started && b->chip) {
            /* Original KO by guard chip reclassifies the effect as clean. */
            result.guarded=0;
            result.hit_clean=1;
        }
        if(frame->reaction_facts_known && frame->reaction_tables &&
           t3_psx_reaction_select(b,&frame->reaction_facts,
                                  frame->reaction_tables,&result.reaction))
            result.reaction_known=1u;
    }
    *out=result;
    return 1;
}
