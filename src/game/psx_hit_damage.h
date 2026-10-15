#ifndef T3_PSX_HIT_DAMAGE_H
#define T3_PSX_HIT_DAMAGE_H
#include <stdint.h>
#include "divmot.h"

/* Read-only, PS1-native HitClassify/HitDamage preview. It does NOT
 * mutate the match, grant/restrict hits, resolve throws, reactions,
 * guard transitions, combos, or original 16.16 health.
 *
 * ALL mutable facts are provided by the caller's verified PS1 state.
 * In particular, the move's +0x14 bits contain the attack class and
 * original base-damage field, but live attacker_damage can be changed
 * by game events, so it is supplied explicitly.
 */
enum {
    T3_HIT_REACTION_RECORD=0,
    T3_HIT_REACTION_THROW_SLOT=1,
    T3_HIT_REACTION_UNSUPPORTED=2
};
typedef struct {
    int16_t attacker_damage, defender_damage;
    uint16_t defender_guard, defender_rel_angle;
    uint16_t defender_pose_frame, defender_active_first;
    uint16_t defender_state, juggle_count;
    uint32_t fighter_distance, close_threshold;
    uint16_t close_reaction_word;
    uint8_t close_table_available;
    uint8_t attacker_power, defender_power, chip_enabled, defender_in_air;
} t3_psx_hit_facts_t;
typedef struct {
    int16_t base_damage, damage;
    uint16_t reaction_word;
    uint8_t reaction_kind, attack_class, severity, zone;
    uint8_t no_damage, guarded, chip, close, hit, counter, airborne;
} t3_psx_hit_candidate_t;

/* zone is 0..13 from t3_psx_contact_probe.
 * 1 complete computed candidate, 0 unavailable facts/bad arguments;
 * output remains unchanged on failure.
 * This routine assumes that a separately verified geometric contact
 * already occurred; it never decides whether a hit connects.
 */
int t3_psx_hit_preview(const t3_move_record_t* move,
                       unsigned zone, const t3_psx_hit_facts_t* facts,
                       t3_psx_hit_candidate_t* out);

/* Original HitBestSlot ordering for two already classified contacts.
 * The second wins on higher severity, or equal severity with larger
 * signed damage. 0/1 on success, -1 malformed inputs.
 */
int t3_psx_hit_best(const t3_psx_hit_candidate_t* first,
                     const t3_psx_hit_candidate_t* second);
#endif
