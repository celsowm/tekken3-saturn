#ifndef T3_BRANCH_RULES_H
#define T3_BRANCH_RULES_H

#include <stdint.h>
#include "divmot.h"

/* Every group in 'known' is independent. Unknown original-game state MUST NOT
 * become a false positive in a commercial bank's branch list. A headless
 * oracle or a faithful fighter implementation explicitly supplies the facts. */
enum {
    T3_FACT_IDENTITY     = 1u << 0, /* enemy bank, own character id */
    T3_FACT_DISTANCE     = 1u << 1, /* original PS1 distance and adjusted distance units */
    T3_FACT_CONTACT      = 1u << 2, /* contact, whiff, guard, counter hit */
    T3_FACT_OPPONENT     = 1u << 3, /* opponent state, attack, damage, transition */
    T3_FACT_ORIENTATION  = 1u << 4, /* angle units: a turn = 0x10000 */
    T3_FACT_RECOVERY     = 1u << 5, /* recover mash/frame counters */
    T3_FACT_COLLISION    = 1u << 6, /* body contact flag */
    T3_FACT_BUTTON_TAPS  = 1u << 7, /* original latch state, not merely raw presses */
    T3_FACT_HEALTH       = 1u << 8,
    T3_FACT_POWER        = 1u << 9,
    T3_FACT_THROW        = 1u << 10,/* situation code + source attack contact */
    T3_FACT_SIDE         = 1u << 11,
    T3_FACT_LAUNCH       = 1u << 12
};

typedef struct t3_branch_context {
    uint32_t known;
    uint32_t own_attack;
    uint32_t opponent_attack;
    uint32_t opponent_state;
    uint16_t own_character;
    uint16_t opponent_damage;
    uint16_t opponent_transition;
    uint16_t own_health;
    uint16_t own_power_timer;
    uint16_t relative_angle;
    uint16_t heading_delta;
    uint16_t recover_mash, recover_frames;
    uint16_t opponent_active_last, opponent_frame;
    uint8_t opponent_air_start, opponent_air_end;
    int32_t distance, adjusted_distance;
    int16_t throw_situation; /* -1 = none */
    uint8_t opponent_bank;
    uint8_t facing_quadrant, opponent_quadrant, side_flag;
    /* contact/whiffed are the fighter's own (+0xD1/+0xD2); guarded and
     * counter_hit are the opponent's last applied hit (+0xCF/+0xD3). */
    uint8_t contact, guarded, whiffed, counter_hit;
    uint8_t opponent_in_air;
    uint16_t opponent_guard; /* +0x9C guard bits, matched against own_attack */
    uint8_t body_contact, tap_lp, tap_rp, tap_both;
    uint8_t launch_armed;
    int16_t step_cooldown; /* +0x128: rows flagged 0x1C are skipped while non-zero */
} t3_branch_context_t;

int t3_branch_restriction_matches(uint8_t restriction, uint8_t own_bank,
                                  const t3_branch_context_t* facts);
int t3_branch_condition_matches(uint8_t condition, uint16_t parameter,
                                const t3_branch_context_t* facts);

#endif /* T3_BRANCH_RULES_H */
