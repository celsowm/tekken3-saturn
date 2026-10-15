#ifndef T3_PSX_MOVEVM_BRIDGE_H
#define T3_PSX_MOVEVM_BRIDGE_H
#include "move_vm.h"
#include "psx_state_transition.h"

/* Oracle-observed NEXT pose frame. A combat phase's phase_frame is NOT
 * necessarily its animation frame: freeze/cancel/ukemi timing remains unknown.
 * The bridge never extrapolates animation frames from stun duration.
 */
typedef struct {
    uint8_t pose_frame_known;
    uint16_t pose_frame;
} t3_psx_pose_frame_fact_t;

typedef struct {
    t3_psx_transition_preview_t transition;
    t3_move_vm_t vm[2]; /* two COPIES; no live MoveVM ever changed */
    uint8_t slot_changed[2];
    uint8_t pose_frame_changed[2];
} t3_psx_movevm_bridge_preview_t;

/* Resolve a supplied slot and frame against the ORIGINAL local/common DIVMOT.
 * Reject missing or invalid slots, stale starting VM rows, unsupported pose
 * frames or counter overflow. *out is unchanged on failure.
 * This is a projection; it does NOT run branches or execute gameplay.
 */
int t3_psx_movevm_project(const t3_move_vm_t* before,
                          unsigned next_slot, unsigned next_pose_frame,
                          t3_move_vm_t* out);

/* Pair-atomic read-only integration of HitTest -> HitApply -> reaction state
 * -> original DIVMOT MoveVM snapshots. Each NEXT animation frame is supplied
 * from an independent PS1 capture. No fabricated time-to-pose mapping, no
 * health/MoveVM writes and no fallback to the prototype moveset.
 *
 * A KO has no verified victim move slot yet and is rejected, rather than
 * silently continuing the pre-hit animation.
 */
int t3_psx_movevm_bridge_preview_pair(
    const t3_psx_combat_attack_t attacks[2],
    const t3_psx_hit_apply_frame_t defender_frames[2],
    const t3_psx_fighter_transition_state_t before[2],
    const t3_psx_transition_facts_t transition_facts[2],
    const t3_move_vm_t vms[2],
    const t3_psx_pose_frame_fact_t pose_after[2],
    t3_psx_movevm_bridge_preview_t* out);
#endif
