#ifndef T3_PSX_HIT_APPLY_H
#define T3_PSX_HIT_APPLY_H

#include <stdint.h>
#include "psx_hit_damage.h"
#include "psx_reaction.h"

/* PS1 HitExtraDamage + HitApply (normal two-fighter rules), preview only.
 * Inputs are VERIFIED ORIGINAL GAME FACTS, not inferred from the prototype
 * arena, dummy damage tables or geometry-only shadow contact edges.
 */
enum { T3_PSX_HIT_SLOTS = 2 };

typedef struct {
    uint8_t kind; /* original MoveEvents command 11/12/13 => kind 1/2/3 */
    int16_t extra_damage;
    int16_t damage;      /* live fighter damage, not static DIVMOT amount */
    int16_t throw_state; /* negative => original throw victim */
} t3_psx_hit_extra_t;

typedef struct {
    uint8_t used;        /* HitTest's actual .used flag; NOT a shadow overlap */
    uint8_t attacker;    /* 0/1 */
    uint8_t zone;        /* exact original hurt zone index */
    const t3_move_record_t* move;
    const t3_psx_hit_facts_t* facts;
} t3_psx_hit_slot_input_t;

typedef struct {
    t3_psx_hit_slot_input_t slots[T3_PSX_HIT_SLOTS];
    t3_psx_hit_extra_t defender_extra;
    t3_psx_hit_extra_t opponent_extra;
    int32_t health_q16, max_health_q16; /* PS1 signed 16.16, not demo HP */
    uint8_t suppress_health_change; /* debug no-damage/practice/mode flags */
    uint8_t reaction_facts_known;  /* optional, do not invent missing state */
    t3_psx_reaction_facts_t reaction_facts;
    const t3_psx_reaction_tables_t* reaction_tables;
} t3_psx_hit_apply_frame_t;

typedef struct {
    t3_psx_hit_candidate_t slots[T3_PSX_HIT_SLOTS];
    uint8_t slot_used[T3_PSX_HIT_SLOTS];
    int8_t best_slot;              /* -1 if no authored HitTest slot */
    int8_t last_attacker;          /* -1 when no authored HitTest slot */
    int32_t health_q16;
    int16_t extra_damage, slot_damage, total_damage;
    uint8_t ko_started, hit_clean, guarded, close, counter;
    uint8_t reaction_known;       /* false until original table/facts match */
    t3_psx_reaction_pick_t reaction;
} t3_psx_hit_apply_result_t;

/* Implements HitExtraDamage, including throw-victim transfer from partner
 * kind-1 events. Returns signed 16-bit wrapped MIPS event accumulator.
 * Invalid event kinds fail closed and leave *out unchanged. */
int t3_psx_hit_extra_damage(const t3_psx_hit_extra_t* defender,
                            const t3_psx_hit_extra_t* partner,
                            int16_t* out);

/* Read-only normal-mode PS1 two-hit-slot HitApply calculation.
 * Uses original candidate classifier/damage and best-slot selector.
 * Computes original signed 32-bit wrapping 16.16 health and KO/chip flip.
 * Reaction selection can be unknown even when damage is known; no guessed
 * victim slot is published. Returns 1 on fully known arithmetic or 0 on
 * malformed/missing required damage facts, with *out UNCHANGED on failure.
 * This function never changes the caller's data or gameplay state. */
int t3_psx_hit_apply_preview(const t3_psx_hit_apply_frame_t* frame,
                             t3_psx_hit_apply_result_t* out);

#endif
