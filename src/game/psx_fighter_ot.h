#ifndef T3_PSX_FIGHTER_OT_H
#define T3_PSX_FIGHTER_OT_H
#include <stdint.h>
#include "psx_projected_mesh.h"
/* DrawSkinnedPart's OT bucket from the first and last selected vertex SZ/32,
 * packed face flags, and signed GTE NCLIP result. Caller chooses the correct
 * vertex indices and handles back-face culling. */
uint16_t t3_psx_fighter_ot_bucket(uint16_t first,uint16_t last,uint32_t flags,int32_t nclip);
typedef struct { uint8_t slots[4],group; uint32_t flags; uint16_t bucket; } t3_psx_fighter_face_ot_t;
/* Decode the original FT3/FT4/GT3/GT4 face record (groups 0..3) and use
 * the per-part SZ/32 scratch slots. Triangle slot 3 repeats slot 2. Does not
 * perform NCLIP or determine whether the face is culled. */
int t3_psx_fighter_face_ot(t3_psx_fighter_face_ot_t* out,unsigned group,
                           uint32_t word0,uint32_t word1,const uint16_t depth[128],int32_t nclip);
/* Projected vertices in original GPU order. flags_high is the descriptor's
 * upper byte; quad NCLIP uses vertices 0, 1 and 3. */
uint16_t t3_psx_fighter_projected_bucket(const t3_psx_projected_vertex_t vertices[4],
    unsigned group,uint8_t flags_high,int32_t* nclip);
/* As above, but corners are in Saturn perimeter order: quad vertices 2 and
 * 3 are swapped from the original PS1 primitive order. */
uint16_t t3_psx_fighter_native_bucket(const t3_psx_projected_vertex_t vertices[4],
    unsigned group,uint8_t flags_high,int32_t* nclip);
/* Same native order, read directly from a shared vertex array. The caller
 * must provide three valid indices. */
uint16_t t3_psx_fighter_native_indexed_bucket(const t3_psx_projected_vertex_t* vertices,
    const uint16_t indices[3],uint8_t flags_high,int32_t* nclip);
#endif
