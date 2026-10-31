#ifndef T3_SATURN_FIGHTER_PACK_H
#define T3_SATURN_FIGHTER_PACK_H
/* One program, one disc file per selectable fighter (scripts/build_fighter_packs.py).
 * The fight phase opens the two chosen packs on a clean machine; each loader
 * below reads its own section. Boot-time Master code only. */
#include <stddef.h>
#include <stdint.h>
#include "psx_camera.h"
#include "psx_pose.h"
#include "psx_projected_mesh.h"
#include "saturn/core.h"
#include "t3_roster.h"

enum {
    T3_PACK_HOT=0,T3_PACK_ARCHIVE,T3_PACK_BANK,T3_PACK_CAMERA,
    T3_PACK_FLIPBOOK,T3_PACK_VOICES,T3_PACK_PCM,T3_PACK_NAMES,T3_PACK_SECTIONS
};

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
