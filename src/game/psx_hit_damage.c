#include "psx_hit_damage.h"
#include <stdint.h>

/* Original PS1 zone percentages (feet -> thighs), in KMD hurt-zone order. */
static const uint8_t k_zone_percent[14] = {
    90u,90u,90u,90u,100u,100u,120u,120u,
    130u,100u,100u,140u,125u,125u
};
static int16_t w16(int64_t n) {
    uint16_t bits=(uint16_t)(uint64_t)n;
    return (int16_t)(bits<0x8000u?(int32_t)bits:(int32_t)bits-65536);
}
static int32_t toward_zero(int64_t numerator,int64_t denominator) {
    /* C signed division truncates to zero, matching MIPS signed DIV.
     * Denominator is guaranteed positive by all call sites. */
    return (int32_t)(numerator/denominator);
}
int t3_psx_hit_preview(const t3_move_record_t* move,
                       unsigned zone,const t3_psx_hit_facts_t* facts,
                       t3_psx_hit_candidate_t* out) {
    t3_psx_hit_candidate_t r={0};
    int32_t base,d,e,divisor;
    uint16_t reaction;
    if(!move || !facts || !out || zone>=14u ||
       (facts->defender_guard & (uint16_t)~0x18u) ||
       (facts->attacker_damage && move->close_reaction &&
        !facts->close_table_available) ||
       (facts->attacker_damage && move->close_reaction &&
        !facts->close_threshold))
        return 0;
    r.zone=(uint8_t)zone;
    r.base_damage=facts->attacker_damage;
    r.attack_class=(uint8_t)((move->damage_class>>14u)&3u);
    r.no_damage=(uint8_t)(facts->attacker_damage==0);
    /* Original HitClassify returns immediately for zero attacker damage.
     * It sets NO_DAMAGE alone: no guard, chip, close, hit, counter or air.
     * A guard-chip calculation must NOT invent +2 damage in this case. */
    if(!r.no_damage) {
    r.close=(uint8_t)(move->close_reaction!=0u &&
                      facts->fighter_distance<facts->close_threshold);
    r.guarded=(uint8_t)((facts->defender_guard & move->attack_word)!=0u &&
                        facts->defender_rel_angle<=0x3FFFu);
    r.chip=(uint8_t)(r.guarded &&
            (facts->chip_enabled || facts->attacker_power));
    r.hit=(uint8_t)(!r.guarded);
    r.counter=(uint8_t)(r.hit &&
            ((facts->defender_damage!=0 &&
               facts->defender_pose_frame<facts->defender_active_first) ||
             facts->defender_power || facts->attacker_power));
    r.airborne=(uint8_t)(r.hit &&
            (facts->defender_in_air || (facts->defender_state&0x400u)));
    }
    r.severity=(uint8_t)(r.no_damage+2*r.guarded+3*r.chip+
                        4*r.close+5*r.hit+6*r.counter+7*r.airborne);
    base=facts->attacker_damage;
    if(r.guarded) {
        d=r.chip?toward_zero(base,10)+2:0;
    } else {
        if(r.close) d=base+toward_zero(base,2);
        else d=w16(toward_zero((int64_t)base*k_zone_percent[zone],100));
        if(r.counter) {
            e=toward_zero((int64_t)(
                  facts->defender_damage<0?
                  -(int32_t)facts->defender_damage:facts->defender_damage)*3,10);
            if(!facts->defender_power && !facts->attacker_power) {
                divisor=facts->defender_active_first;
                if(!divisor || divisor<(int32_t)facts->defender_pose_frame)
                    divisor=facts->defender_pose_frame;
                /* Valid original MIPS states never divide by zero;
                 * fail closed on corrupt/missing startup facts. */
                if(divisor==0)return 0;
                e=toward_zero((int64_t)toward_zero((int64_t)w16(e)*3,10)*
                              facts->defender_pose_frame,divisor);
            }
            d=e+toward_zero((int64_t)w16(d)*12,10);
        }
        d=w16(d);
        if(r.airborne)
            d=facts->juggle_count?
              toward_zero(d,2):toward_zero((int64_t)d*80,100);
    }
    r.damage=w16(d);
    reaction=r.close?facts->close_reaction_word:move->reaction;
    r.reaction_word=reaction;
    r.reaction_kind=(uint8_t)(reaction<0x1000u?
        T3_HIT_REACTION_RECORD:
        (reaction<0x2000u?T3_HIT_REACTION_THROW_SLOT:
         T3_HIT_REACTION_UNSUPPORTED));
    *out=r;
    return 1;
}
int t3_psx_hit_best(const t3_psx_hit_candidate_t* first,
                     const t3_psx_hit_candidate_t* second) {
    if(!first || !second)return -1;
    return second->severity>first->severity ||
        (second->severity==first->severity &&
         second->damage>first->damage) ? 1:0;
}
