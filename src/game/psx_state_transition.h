#ifndef T3_PSX_STATE_TRANSITION_H
#define T3_PSX_STATE_TRANSITION_H
#include <stdint.h>
#include "psx_combat_pipeline.h"

/* Read-only follow-up to original HitTest -> HitApply. This is a strict
 * transition harness, NOT a reconstruction of PS1 frame timing. Durations,
 * phase classification, recovery slots and combo policy must be measured
 * from the user's original PlayStation oracle.
 */
enum {
    T3_PSX_PHASE_NEUTRAL=0,
    T3_PSX_PHASE_HITSTUN=1,
    T3_PSX_PHASE_BLOCKSTUN=2,
    T3_PSX_PHASE_KNOCKDOWN=3,
    T3_PSX_PHASE_RECOVERY=4,
    T3_PSX_PHASE_KO=5
};
typedef struct {
    uint8_t phase;
    uint16_t move_slot;
    uint16_t phase_frame;
    uint16_t frames_left;
    uint16_t combo_hits_received;
    int32_t health_q16;
} t3_psx_fighter_transition_state_t;

typedef struct {
    /* Required only if a selected hit reaction starts this frame. */
    uint8_t reaction_timing_known;
    uint8_t reaction_phase; /* HITSTUN, BLOCKSTUN, KNOCKDOWN */
    uint16_t reaction_frames;
    /* Needed when an existing stun/knockdown expires. */
    uint8_t recovery_known;
    uint16_t recovery_frames;
    uint16_t recovery_move_slot;
    /* Needed when an existing recovery expires. */
    uint8_t neutral_known;
    uint16_t neutral_move_slot;
    /* Explicit measured count/reset decision; never synthesize combos. */
    uint8_t combo_known;
    uint8_t increment_combo;
    uint8_t reset_combo_on_neutral;
} t3_psx_transition_facts_t;

typedef struct {
    t3_psx_combat_preview_t combat;
    t3_psx_fighter_transition_state_t fighter[2];
    uint8_t reaction_started[2];
    uint8_t entered_recovery[2];
    uint8_t returned_neutral[2];
} t3_psx_transition_preview_t;

/* Both fighters consume the SAME initial state and HitTest snapshot.
 * Output is published atomically on success. Missing original reaction
 * selection/timing or expiry destinations FAIL CLOSED, even if arithmetic
 * damage is already available. No input or live game state is mutated.
 * The resulting state is the NEXT observation tick; a freshly delivered
 * reaction starts at phase_frame 0 with its full supplied duration.
 */
int t3_psx_combat_transition_preview_pair(
    const t3_psx_combat_attack_t attacks[2],
    const t3_psx_hit_apply_frame_t defender_frames[2],
    const t3_psx_fighter_transition_state_t before[2],
    const t3_psx_transition_facts_t facts[2],
    t3_psx_transition_preview_t* out);
#endif
