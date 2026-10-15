#ifndef T3_PSX_REACTION_H
#define T3_PSX_REACTION_H
#include <stddef.h>
#include <stdint.h>
#include "psx_hit_damage.h"

/* Local, original EXE-derived reaction tables. The full normal table
 * contains 633 42-byte little-endian records. A throw-victim entry is
 * an s16 move slot. These inputs stay PRIVATE; no reference bytes in Git.
 */
enum { T3_PSX_REACTION_BYTES=42u, T3_PSX_REACTIONS_MAX=633u };
typedef struct {
    const uint8_t* records;
    size_t records_size;
    const uint8_t* throw_victim_slots;
    size_t throw_slots_size;
} t3_psx_reaction_tables_t;
typedef struct {
    uint16_t defender_state;
    uint8_t facing_quadrant; /* 0 front, 1 left, 2 back, 3 right */
    uint8_t forced_hit;      /* fixed EXE record is NOT imported */
} t3_psx_reaction_facts_t;
typedef struct {
    uint16_t move_slot, transition;
    int16_t push_angle, push_index;
    uint8_t selected, throw_victim;
} t3_psx_reaction_pick_t;

/* Return 1 when the result is genuinely known (including an explicitly
 * suppressed reaction for a damage-only move). Return 0 otherwise and
 * leave the caller's output unchanged.
 *
 * Airborne reactions use separate AirReactionKind tables and are not
 * invented here. The caller must supply the actual relative quadrant,
 * posture and original PRIVATE source table.
 *
 * This pure decoder does NOT cause a state transition or change health.
 */
int t3_psx_reaction_select(const t3_psx_hit_candidate_t* hit,
                           const t3_psx_reaction_facts_t* facts,
                           const t3_psx_reaction_tables_t* tables,
                           t3_psx_reaction_pick_t* out);
#endif
