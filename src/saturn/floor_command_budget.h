#ifndef T3_FLOOR_COMMAND_BUDGET_H
#define T3_FLOOR_COMMAND_BUDGET_H
#include <stdint.h>
#include "saturn/scene3d_faces.h"

/* The SDK near clip emits at most four pieces, each screen clip at most six.
 * Tiled textures can additionally attempt N*N texture cells. Conservatively
 * budget 25 commands per tile for the clipped tiled fallback. */
static inline uint32_t t3_floor_cold_face_commands(const sat_scene3d_face_t* face) {
    if(!face)return UINT32_MAX;
    if(face->projected_safe)return 1;
    if(face->program_valid)return UINT32_MAX;
    if(face->material.kind==SAT_SCENE3D_INDEXED_TILED) {
        if(!face->material.tiled)return UINT32_MAX;
        unsigned n=face->material.tiled->grid;
        if(n==0)n=2;
        if(n!=2 && n!=4 && n!=8)return UINT32_MAX;
        return 25u*n*n;
    }
    if(face->material.kind==SAT_SCENE3D_RGB || face->material.kind==SAT_SCENE3D_INDEXED_SOLID ||
       face->material.kind==SAT_SCENE3D_INDEXED_TEXTURED)return 24u;
    return UINT32_MAX;
}

/* The floor is emitted before the scene flush; reserve for all queued faces,
 * including culled ones. A bad/unrecognized queue suppresses floor drawing. */
static inline uint32_t t3_floor_protected_commands(const sat_scene3d_faces_t* queue) {
    if(!queue || queue->count>queue->capacity)return UINT32_MAX;
    uint32_t total=queue->count;
    if(queue->projected_entries && !queue->cold_count)return total;
    for(unsigned i=0;i<queue->count;++i) {
        const sat_scene3d_face_t* face;
        if(queue->projected_entries) {
            unsigned physical=queue->projected_indices?queue->projected_indices[i]:i;
            if(physical>=queue->capacity)return UINT32_MAX;
            const sat_scene3d_projected_face_t* item=&queue->projected_entries[physical];
            if(item->kind==SAT_SCENE3D_PROJECTED_RGB || item->kind==SAT_SCENE3D_PROJECTED_PROGRAM)continue;
            if(item->kind!=SAT_SCENE3D_PROJECTED_COLD)return UINT32_MAX;
            face=item->cold;
        } else {
            if(!queue->entries)return UINT32_MAX;
            face=&queue->entries[i];
        }
        uint32_t count=t3_floor_cold_face_commands(face);
        if(count==UINT32_MAX || UINT32_MAX-total<count-1u)return UINT32_MAX;
        total+=count-1u;
    }
    return total;
}
#endif
