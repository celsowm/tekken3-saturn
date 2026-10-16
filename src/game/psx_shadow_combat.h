#ifndef T3_PSX_SHADOW_COMBAT_H
#define T3_PSX_SHADOW_COMBAT_H
#include <stdint.h>
#include "psx_contact.h"

/* Read-only PS1 combat geometry observer. No damage, guard, reaction,
 * hitstop, KO, or original move decision can be inferred here.
 * Actor roots MUST be supplied by a calibrated caller: this portable
 * core never derives PS1 heading/scale from the Saturn demo fighter.
 */
enum {
    T3_SHADOW_INACTIVE=0,
    T3_SHADOW_MISS=1,
    T3_SHADOW_CONTACT=2,
    T3_SHADOW_MISSING=3,
    T3_SHADOW_BAD=4
};
typedef struct {
    const t3_attack_sample_t* attack; /* NULL = no authored active attack */
    uint16_t move_slot;
    uint16_t attack_frame; /* index from active_first, NOT move-frame */
    uint8_t pose_available;
    uint8_t use_large_hurt;
    t3_psx_root_t root;
    const t3_psx_joint_t* parts; /* Original joint-slot matrices, not KMD mesh order. */
    uint32_t parts_mask;
} t3_psx_shadow_actor_t;
typedef struct {
    uint8_t status, new_contact;
    int8_t zone, segment;
} t3_psx_shadow_result_t;
typedef struct {
    t3_psx_sweep_history_t history[2];
    t3_psx_root_t last_root[2];
    uint16_t last_slot[2];
    uint8_t root_valid[2],contact_last[2];
    uint32_t contact_edges[2],contact_frames[2];
} t3_psx_shadow_state_t;

void t3_psx_shadow_reset(t3_psx_shadow_state_t* state);

/* Evaluates both strikes from an immutable same-tick snapshot. The
 * sweep history is advanced only after valid attacker transforms.
 * Incomplete opponent pose never produces a guessed contact.
 * Contact "edges" are observational rising edges and NOT Tekken hits:
 * one multi-frame strike may cover multiple distinct enemy zones.
 * 1 accepted actor records and updated state; 0 invalid arguments
 * (neither state nor results modified). No heap or libm dependencies.
 */
int t3_psx_shadow_step(t3_psx_shadow_state_t* state,
                       const t3_psx_shadow_actor_t actors[2],
                       t3_psx_shadow_result_t results[2]);
#endif
