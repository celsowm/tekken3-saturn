#ifndef T3_SATURN_FIGHTER_PACK_H
#define T3_SATURN_FIGHTER_PACK_H
/* One program, one disc file per selectable fighter (scripts/build_fighter_packs.py).
 * The fight phase opens its chosen packs on a clean machine; each loader
 * below reads its own section. Boot-time Master code only. */
#include <stddef.h>
#include <stdint.h>
#include "psx_camera.h"
#include "psx_pose.h"
#include "psx_projected_mesh.h"
#include "saturn/core.h"
#include "t3_roster.h"
#if defined(T3_FORCE_PROGRAM)
#include "t3_force_boss_limits.h"
sat_result_t t3_fighter_packs_open_boss(unsigned player,unsigned character,unsigned costume);
/* Preload the two distinct original enemy models used by a regular level. */
sat_result_t t3_fighter_packs_open_force(unsigned player,unsigned first_key,unsigned second_key);
unsigned t3_fighter_packs_count(void);
/* Regular-stage scratch in the unused tail of the third asset's store. */
void* t3_fighter_pack_force_scratch(void);
/* After bank/camera loading: unused arena, excluding resident asset stores.
 * Invalidated by opening/reloading packs. No caller may retain it across that. */
void* t3_fighter_pack_unused_arena(uint32_t* bytes);
#define T3_FORCE_PACK_SCRATCH_BYTES 4096u
#endif

enum {
    T3_PACK_HOT=0,T3_PACK_ARCHIVE,T3_PACK_BANK,T3_PACK_CAMERA,
    T3_PACK_FLIPBOOK,T3_PACK_VOICES,T3_PACK_PCM,T3_PACK_NAMES,T3_PACK_SECTIONS
};

/* One lit normal: a Q12 unit vector in its joint's basis (scripts/build_fighter_packs.py,
 * lighting_data). */
typedef struct {
    int16_t x,y,z;
    uint16_t joint;
} t3_light_normal_t;

/* A face's lighting word: the normal of corner A, its tone class, and whether that one
 * normal lights the whole face; otherwise light_corners holds corners B, C and D. */
#define T3_LIGHT_FACE_NORMAL(word) ((word)&0x3FFu)
#define T3_LIGHT_FACE_TONE(word) (((word)>>10)&15u)
#define T3_LIGHT_FACE_FLAT 0x8000u
#define T3_LIGHT_TONES 16u

/* A row with hand (or jaw) vertex variants (scripts/build_fighter_packs.py,
 * hand_variant_rows): the vertices of projected block `block` and the skin
 * records (record << 2 | influence) that its variants move, then per variant
 * 0..T3_HAND_VARIANTS-1 and the base (index T3_HAND_VARIANTS) their positions;
 * a variant whose bit in `present` is clear keeps the current block. */
#define T3_HAND_VARIANTS 10u
typedef struct {
    uint8_t channel,block;
    uint16_t vertex_count,record_count,present;
    const uint16_t* vertices;
    const uint16_t* records;
    const int16_t (*coords)[3];    /* [T3_HAND_VARIANTS + 1][vertex_count] */
    const int16_t (*positions)[3]; /* [T3_HAND_VARIANTS + 1][record_count] */
} t3_hand_variant_row_t;

typedef struct {
    uint32_t id,bank_type,voice_set,scale_percent,name_width;
    uint32_t vertices,faces,textures,palettes,voice_count,compact_bytes,bank_fnv1a;
    const char* name;
    const t3_psx_part_t* parts;
    const int16_t* radii; /* eight body spheres */
    const t3_psx_camera_choice_t* choices;
    uint32_t choice_count;
    t3_psx_camera_streams_t streams;
    const uint8_t* double_sided;
    const t3_psx_projected_part_t* projected;
    uint32_t projected_count,projected_vertices;
    const uint16_t (*projected_faces)[4];
    const uint8_t (*face_ot)[2];
    uint32_t light_normal_count; /* 0 without lighting data */
    const t3_light_normal_t* light_normals;
    const uint16_t* light_faces;
    const uint16_t* light_corners;
    const uint16_t* light_tones; /* T3_LIGHT_TONES mean texel colours, RGB555 */
    const t3_hand_variant_row_t* hand_rows;
    uint32_t hand_row_count;
    const uint8_t* shadow; /* the TK3psSDW block (build_fighter_packs.py, shadow_mesh) */
    uint32_t shadow_faces; /* 0 without one */
    const uint8_t* body_shapes; /* 48 bytes: body joint spheres for collision (build_fighter_packs.py) */
    const uint8_t* bank; /* in the arena, after t3_fighter_packs_load_banks */
    uint32_t bank_bytes;
} t3_fighter_pack_t;

/* The display name of a roster id, for the select screen. */
const char* t3_fighter_name(unsigned id);
/* Reads both packs' headers and hot blocks. */
sat_result_t t3_fighter_packs_open(unsigned first,unsigned second);
const t3_fighter_pack_t* t3_fighter_pack(unsigned actor);
/* Both fighters play one bank (a mirror match, or Mokujin against Jin). */
int t3_fighter_packs_share_bank(void);
uint32_t t3_fighter_pack_section_bytes(unsigned actor,unsigned section);
sat_result_t t3_fighter_pack_read(unsigned actor,unsigned section,uint32_t at,void* out,uint32_t bytes);
/* Low WRAM: the logic banks and camera streams once loaded. Until then the
 * mesh loader borrows it for an archive and its texture table. */
uint8_t* t3_fighter_pack_arena(void);
_Static_assert(T3_ROSTER_ARENA_BYTES>=T3_ROSTER_ARCHIVE_BYTES+T3_ROSTER_TEXTURES*16u+16u,
    "the arena must also hold a decoding mesh");
/* Where sat_character_asset_compact keeps an actor's asset for the match. */
void* t3_fighter_pack_asset_storage(unsigned actor,uint32_t* capacity);
/* Loads and checks each bank (one when shared) and both camera streams. */
sat_result_t t3_fighter_packs_load_banks(void);

#endif
