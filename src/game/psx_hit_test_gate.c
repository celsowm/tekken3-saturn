#include "psx_hit_test_gate.h"

static int valid_bit(unsigned x) {return x<=1u;}
int t3_psx_hit_test_gate(const t3_psx_hit_test_facts_t* f,
                         t3_psx_hit_test_gate_t* out) {
    t3_psx_hit_test_gate_t value={0};
    unsigned forced,posture;
    if(!f || !out || f->free_slots_mask>3u ||
       f->geometry.status>T3_SHADOW_BAD ||
       !valid_bit(f->attacker_invulnerable) ||
       !valid_bit(f->defender_invulnerable) ||
       !valid_bit(f->attacker_active) ||
       !valid_bit(f->defender_active) ||
       !valid_bit(f->hit_done) ||
       !valid_bit(f->defender_in_air) ||
       !valid_bit(f->defender_in_throw) ||
       !valid_bit(f->attacker_forced_hit) ||
       !valid_bit(f->defender_is_cpu) ||
       !valid_bit(f->forced_contact_flag) ||
       f->defender_state_class>31u ||
       (f->geometry.status==T3_SHADOW_CONTACT &&
         (f->geometry.zone<0 || f->geometry.zone>=14 ||
          f->geometry.segment<0 || f->geometry.segment>=2)))
        return 0;
    value.hit_slot=-1;value.zone=-1;value.segment=-1;
    value.status=T3_HIT_GATE_NO_ATTACK;
    if(f->geometry.status==T3_SHADOW_MISSING ||
       f->geometry.status==T3_SHADOW_BAD) {
        value.status=T3_HIT_GATE_UNAVAILABLE;
    } else if(f->geometry.status==T3_SHADOW_INACTIVE) {
        value.status=T3_HIT_GATE_NO_ATTACK;
    } else if(!f->free_slots_mask) {
        value.status=T3_HIT_GATE_NO_SLOT;
    } else if(f->attacker_invulnerable||f->defender_invulnerable) {
        value.status=T3_HIT_GATE_INVULNERABLE;
    } else if(!f->attacker_active || !f->defender_active) {
        value.status=T3_HIT_GATE_INACTIVE;
    } else if(f->hit_done) {
        value.status=T3_HIT_GATE_ALREADY_HIT;
    } else if(f->defender_hit_cooldown) {
        value.status=T3_HIT_GATE_COOLDOWN;
    } else if(f->attack_word==0x10Fu &&
              f->defender_state_class==12u &&
              (f->defender_in_air ||
              (f->defender_air_end>=5u &&
               f->defender_pose_frame>=f->defender_air_start &&
               f->defender_pose_frame<=
                   (unsigned)(f->defender_air_end-5u)))) {
        value.status=T3_HIT_GATE_AIR_EVADE;
    } else if((f->game_mode==7u || f->game_mode==8u) &&
              f->defender_in_throw) {
        value.status=T3_HIT_GATE_THROW_LOCK;
    } else {
        forced=(unsigned)(f->attacker_forced_hit &&
              f->defender_is_cpu && f->forced_contact_flag);
        if(forced) {
            value.status=T3_HIT_GATE_FORCED_UNSUPPORTED;
        } else if(f->geometry.status!=T3_SHADOW_CONTACT) {
            value.status=T3_HIT_GATE_MISS;
        } else {
            posture=(unsigned)(f->attack_word |
                (f->defender_step_kind==2u?1u:0u)) &
                (unsigned)f->defender_state & 7u;
            if(!posture) {
                value.status=T3_HIT_GATE_POSTURE;
            } else {
                value.status=T3_HIT_GATE_ACCEPT;
                /* The original chooses the LAST empty hit slot. */
                value.hit_slot=(int8_t)
                    ((f->free_slots_mask&2u)?1:0);
                value.zone=f->geometry.zone;
                value.segment=f->geometry.segment;
                value.hit_freeze_in=(uint8_t)
                    (f->attacker_move_slot==0x898u?30u:
                      f->attacker_freeze);
                value.hit_cooldown=(uint8_t)
                    (f->attacker_damage || f->defender_in_air?4u:0u);
                value.set_attacker_hit_done=1u;
                value.set_attacker_contact=1u;
                value.set_defender_got_hit=1u;
            }
        }
    }
    *out=value;
    return 1;
}
int t3_psx_hit_test_pair(const t3_psx_hit_test_facts_t facts[2],
                         t3_psx_hit_test_gate_t out[2]) {
    t3_psx_hit_test_gate_t temp[2];
    if(!facts || !out ||
       !t3_psx_hit_test_gate(&facts[0],&temp[0]) ||
       !t3_psx_hit_test_gate(&facts[1],&temp[1]))
        return 0;
    out[0]=temp[0];out[1]=temp[1];
    return 1;
}
int t3_psx_hit_whiff(unsigned pose_frame,unsigned authored_whiff_frame,
                      int contact_this_move) {
    if(pose_frame>65535u || authored_whiff_frame>255u ||
       !valid_bit((unsigned)contact_this_move))return -1;
    return (pose_frame==authored_whiff_frame && !contact_this_move)?1:0;
}
