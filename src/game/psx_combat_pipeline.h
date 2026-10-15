#ifndef T3_PSX_COMBAT_PIPELINE_H
#define T3_PSX_COMBAT_PIPELINE_H
#include "psx_hit_test_gate.h"
#include "psx_hit_apply.h"

/* Read-only original PS1 HitTest -> HitClassify/HitDamage -> HitApply ->
 * optional reaction selection, evaluated simultaneously for both fighters.
 *
 * attacks[0] is player 0 attacking defender 1; attacks[1] is the reverse.
 * defender_frames[0] is player 0's pre-hit state, and [1] player 1's.
 * Existing occupied HitSlots are caller-provided original PS1 facts.
 * No state, memory-mapped register, health or MoveVM is ever modified.
 */
typedef struct {
    t3_psx_hit_test_facts_t test;
    const t3_move_record_t* move;
    const t3_psx_hit_facts_t* hit_facts;
} t3_psx_combat_attack_t;
typedef struct {
    t3_psx_hit_test_gate_t gate[2];
    t3_psx_hit_apply_result_t defend[2]; /* defender index */
} t3_psx_combat_preview_t;

/* Returns 1 if all supplied original facts are sufficient to evaluate,
 * 0 if any prerequisite is incomplete, inconsistent or unsupported.
 * Atomic publication: *out remains unchanged on failure. Even on
 * success, unsupported reaction tables remain reaction_known==0.
 */
int t3_psx_combat_preview_pair(
    const t3_psx_combat_attack_t attacks[2],
    const t3_psx_hit_apply_frame_t defender_frames[2],
    t3_psx_combat_preview_t* out);
#endif
