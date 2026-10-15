#ifndef T3_PSX_HIT_TEST_GATE_H
#define T3_PSX_HIT_TEST_GATE_H
#include <stdint.h>
#include "psx_shadow_combat.h"

/* HitTest / HitTestSegments eligibility gates from Tekken 3 PS1.
 * Geometry is provided separately by the validated shadow observer.
 * No input is inferred from the temporary Saturn fighter state.
 * A geometric overlap is NOT a hit until all gates have passed.
 */
enum {
    T3_HIT_GATE_NO_ATTACK=0,
    T3_HIT_GATE_MISS=1,
    T3_HIT_GATE_ACCEPT=2,
    T3_HIT_GATE_UNAVAILABLE=3,
    T3_HIT_GATE_NO_SLOT=4,
    T3_HIT_GATE_INVULNERABLE=5,
    T3_HIT_GATE_INACTIVE=6,
    T3_HIT_GATE_ALREADY_HIT=7,
    T3_HIT_GATE_COOLDOWN=8,
    T3_HIT_GATE_AIR_EVADE=9,
    T3_HIT_GATE_THROW_LOCK=10,
    T3_HIT_GATE_POSTURE=11,
    T3_HIT_GATE_FORCED_UNSUPPORTED=12
};
typedef struct {
    t3_psx_shadow_result_t geometry;
    uint16_t attack_word;
    uint16_t attacker_move_slot; /* PS1 slot 0x898 sets 30 freeze frames */
    uint16_t defender_state;
    uint16_t defender_pose_frame;
    int16_t attacker_damage;     /* live, not DIVMOT's original low 14 bits */
    uint8_t attacker_freeze;     /* original attacker poseMove +0x2C */
    uint8_t defender_air_start; /* original defender poseMove +0x19 */
    uint8_t defender_air_end;   /* original defender poseMove +0x1A */
    uint8_t defender_state_class;
    uint8_t defender_step_kind;
    uint8_t game_mode;
    uint8_t free_slots_mask; /* original two defender HitSlots: bit=1 empty */
    uint8_t attacker_invulnerable, defender_invulnerable;
    uint8_t attacker_active, defender_active;
    uint8_t hit_done, defender_hit_cooldown;
    uint8_t defender_in_air, defender_in_throw;
    uint8_t attacker_forced_hit, defender_is_cpu, forced_contact_flag;
} t3_psx_hit_test_facts_t;
typedef struct {
    uint8_t status;
    int8_t hit_slot; /* 0/1 only if ACCEPT, else -1 */
    int8_t zone;     /* 0..13 on ACCEPT, else -1 */
    int8_t segment;  /* 0..1 on ACCEPT, else -1 */
    uint8_t hit_freeze_in, hit_cooldown;
    uint8_t set_attacker_hit_done, set_attacker_contact;
    uint8_t set_defender_got_hit;
} t3_psx_hit_test_gate_t;

/* 1 = known decision (including unavailable geometry), 0 = malformed
 * source facts; no output modification on error.
 * Forced hits use special non-geometric segment creation and are
 * deliberately marked unsupported until original mode-8/Force oracle.
 */
int t3_psx_hit_test_gate(const t3_psx_hit_test_facts_t* facts,
                         t3_psx_hit_test_gate_t* out);
/* Two attacks evaluated from the SAME pre-HitTest snapshot, no state
 * mutation or order-dependent hit-slot reservation. */
int t3_psx_hit_test_pair(const t3_psx_hit_test_facts_t facts[2],
                         t3_psx_hit_test_gate_t out[2]);

/* HitTest whiff flag: authored move pose +0x2E is exactly current pose
 * frame and no prior contact was registered this move. */
int t3_psx_hit_whiff(unsigned pose_frame, unsigned authored_whiff_frame,
                      int contact_this_move);
#endif
