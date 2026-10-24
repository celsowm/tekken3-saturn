/* Independent implementation from the documented PS1 condition table.
 * Not copied from the Noncommercial Godot remake. */
#include "branch_rules.h"

#define HAS(c, bit) ((c) != 0 && (((c)->known & (bit)) == (bit)))

int t3_branch_restriction_matches(uint8_t restriction, uint8_t own_bank,
                                  const t3_branch_context_t* f) {
    if (restriction == 0) return 1;
    /* These two ranges can be evaluated from the loaded bank alone. */
    if (restriction <= 0x17u) return own_bank == restriction-1u;
    if (restriction <= 0x2Eu) return own_bank != restriction-0x18u;
    if (!HAS(f,T3_FACT_IDENTITY)) return 0;
    if (restriction <= 0x45u) return f->opponent_bank == restriction-0x2Fu;
    if (restriction <= 0x5Cu) return f->own_character == restriction-0x46u;
    return f->own_character != restriction-0x5Du;
}

int t3_branch_condition_matches(uint8_t type, uint16_t p,
                                const t3_branch_context_t* f) {
    if (type == 0) return 1;
    if (type >= 0x45u) return 0; /* separate reversal walker, not this VM */
    if (type >= 2u && type <= 0x17u) {
        if (!HAS(f,T3_FACT_THROW) || !HAS(f,T3_FACT_DISTANCE)) return 0;
        if (type <= 0x13u) return f->throw_situation == (int16_t)type &&
                                     f->adjusted_distance <= (int32_t)p;
        return (f->throw_situation == 2 + (int16_t)type - 0x14 ||
                f->throw_situation == 6 + (int16_t)type - 0x14) &&
               f->adjusted_distance <= (int32_t)p;
    }
    switch (type) {
    case 1: return HAS(f,T3_FACT_CONTACT) && f->contact;
    case 0x18: return HAS(f,T3_FACT_DISTANCE) && f->distance <= (int32_t)p;
    case 0x19: return HAS(f,T3_FACT_DISTANCE) && f->distance >= (int32_t)p;
    case 0x1A: return HAS(f,T3_FACT_CONTACT|T3_FACT_OPPONENT) && f->contact &&
                      !(f->own_attack&f->opponent_guard);
    case 0x1B: return HAS(f,T3_FACT_CONTACT) && f->guarded;
    case 0x1C: return HAS(f,T3_FACT_CONTACT) && f->whiffed;
    case 0x1D: return HAS(f,T3_FACT_OPPONENT) && f->opponent_transition!=0x21u;
    case 0x1E: return HAS(f,T3_FACT_OPPONENT) && f->opponent_damage!=0;
    case 0x1F: return HAS(f,T3_FACT_OPPONENT) && f->opponent_damage!=0 &&
                               (f->opponent_state & 7u)==2u;
    case 0x20: return HAS(f,T3_FACT_OPPONENT) && f->opponent_attack!=0 &&
                               (f->opponent_state & 7u)==1u;
    case 0x21: return HAS(f,T3_FACT_OPPONENT) && f->opponent_damage==0;
    case 0x22: return HAS(f,T3_FACT_OPPONENT) && (f->opponent_state&0x407u)==2u;
    case 0x23: return HAS(f,T3_FACT_OPPONENT) && (f->opponent_state&0x407u)==1u;
    case 0x24: return HAS(f,T3_FACT_ORIENTATION) && f->relative_angle>0x4000u;
    case 0x25: return HAS(f,T3_FACT_ORIENTATION) &&
                      f->heading_delta>=0x4E38u && f->heading_delta<0x8000u;
    case 0x26: return HAS(f,T3_FACT_ORIENTATION) &&
                      f->heading_delta>=0x8000u && f->heading_delta<=0xB1C6u;
    case 0x27: case 0x28: case 0x29: case 0x2A: {
        static const uint8_t quad[4]={0,1,3,2};
        return HAS(f,T3_FACT_ORIENTATION) && f->facing_quadrant==quad[type-0x27u];
    }
    case 0x2B: case 0x2C: case 0x2D: case 0x2E: {
        static const uint8_t quad[4]={0,1,3,2};
        return HAS(f,T3_FACT_ORIENTATION) && f->opponent_quadrant==quad[type-0x2Bu];
    }
    case 0x2F: return 0;
    case 0x30: return HAS(f,T3_FACT_DISTANCE|T3_FACT_OPPONENT) &&
                      f->opponent_attack==0x412u &&
                      f->opponent_frame<=f->opponent_active_last &&
                      f->distance<0x700;
    case 0x31: return HAS(f,T3_FACT_RECOVERY) &&
                      !f->recover_mash && !f->recover_frames;
    case 0x32: return HAS(f,T3_FACT_RECOVERY) &&
                      (f->recover_mash || f->recover_frames);
    case 0x33: return HAS(f,T3_FACT_COLLISION) && f->body_contact;
    case 0x34: return HAS(f,T3_FACT_COLLISION|T3_FACT_OPPONENT) &&
                      f->body_contact && !(f->opponent_state&0x404u);
    case 0x35: return HAS(f,T3_FACT_OPPONENT) && (f->opponent_state&4u)!=0;
    case 0x36: return HAS(f,T3_FACT_OPPONENT) && (f->opponent_state&4u)==0;
    case 0x37: return HAS(f,T3_FACT_OPPONENT|T3_FACT_DISTANCE) &&
                      (f->opponent_state&4u)!=0 &&
                      !f->opponent_air_start && !f->opponent_air_end &&
                      f->distance<=(int32_t)p;
    case 0x38: return HAS(f,T3_FACT_OPPONENT|T3_FACT_ORIENTATION) &&
                      (f->opponent_state&4u)!=0 && f->relative_angle>=0x4000u;
    case 0x39: return HAS(f,T3_FACT_CONTACT|T3_FACT_OPPONENT) &&
                      f->counter_hit && !f->opponent_in_air;
    case 0x3A: return HAS(f,T3_FACT_BUTTON_TAPS) && f->tap_lp;
    case 0x3B: return HAS(f,T3_FACT_BUTTON_TAPS) && f->tap_rp;
    case 0x3C: return HAS(f,T3_FACT_BUTTON_TAPS) && f->tap_both;
    case 0x3D: return HAS(f,T3_FACT_HEALTH) && !f->own_health;
    case 0x3E: return HAS(f,T3_FACT_SIDE) && !f->side_flag;
    case 0x3F: return HAS(f,T3_FACT_SIDE) && f->side_flag;
    case 0x40: return HAS(f,T3_FACT_SIDE|T3_FACT_ORIENTATION) &&
                      !f->side_flag && f->relative_angle>0x4000u;
    case 0x41: return HAS(f,T3_FACT_SIDE|T3_FACT_ORIENTATION) &&
                      f->side_flag && f->relative_angle>0x4000u;
    case 0x42: return HAS(f,T3_FACT_LAUNCH|T3_FACT_HEALTH) &&
                      !f->launch_armed && f->own_health;
    case 0x43: return HAS(f,T3_FACT_POWER) && f->own_power_timer;
    case 0x44: return HAS(f,T3_FACT_POWER) && !f->own_power_timer;
    default: break;
    }
    /* Conditions needing motion/recovery data not yet available are not
     * guessed; an unsupported type always returns false. */
    return 0;
}
