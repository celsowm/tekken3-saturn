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
/* AirReactionKind rows (0x8001A630): six (s16 ground offset, s16 move slot). */
enum { T3_PSX_AIR_KINDS=6u, T3_PSX_AIR_KIND_BYTES=24u };
typedef struct {
    const uint8_t* records;
    size_t records_size;
    const uint8_t* throw_victim_slots;
    size_t throw_slots_size;
    const uint8_t* air_kinds; /* T3_PSX_AIR_KIND_BYTES, needed for airborne hits */
} t3_psx_reaction_tables_t;
typedef struct {
    uint16_t defender_state;
    uint8_t facing_quadrant; /* 0 front, 1 left, 2 back, 3 right */
    uint8_t forced_hit;      /* fixed EXE record is NOT imported */
    uint8_t in_air;          /* defender +0xDB when MoveStep runs */
    /* AirReactionKind inputs: the defender's running slot, pose frame, root
     * height and hit direction (+0x3E0); the attacker's bank type (+0x16),
     * running slot and pending slot (-1 when none). */
    uint16_t defender_slot;
    int16_t defender_pose_frame;
    int32_t defender_root_y,hit_dir_y;
    uint16_t attacker_bank,attacker_slot;
    int32_t attacker_pending_slot;
} t3_psx_reaction_facts_t;
typedef struct {
    uint16_t move_slot, transition;
    int16_t push_angle, push_index;
    uint8_t selected, throw_victim;
    /* Airborne hits (transition 0x24): the air kind and its ground offset. */
    uint8_t air, air_kind;
    int16_t ground_offset;
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
/* Private compact records: repeated LE u16 original index + 42-byte row.
 * Missing indices fail; no zero-filled replacement record is fabricated. */
int t3_psx_reaction_select_sparse(const t3_psx_hit_candidate_t* hit,
                                  const t3_psx_reaction_facts_t* facts,
                                  const uint8_t* records,size_t size,
                                  const uint8_t* air_kinds,
                                  t3_psx_reaction_pick_t* out);
/* AirReactionKind (0x8002F0E8): the air kind row (0-4) of a juggle hit. */
unsigned t3_psx_air_reaction_kind(const t3_psx_reaction_facts_t* facts);
#endif
