#include "floor_command_budget.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    sat_indexed_tiled_quad3_t tiled={0};
    sat_scene3d_face_t cold[4]={0};
    cold[0].projected_safe=1;
    cold[1].material.kind=SAT_SCENE3D_RGB;
    cold[2].material.kind=SAT_SCENE3D_INDEXED_TILED;
    cold[2].material.tiled=&tiled;
    cold[3].material.kind=SAT_SCENE3D_INDEXED_TILED;
    sat_indexed_tiled_quad3_t wide={.grid=8};cold[3].material.tiled=&wide;
    sat_scene3d_faces_t scene={.entries=cold,.capacity=4,.count=4};
    assert(t3_floor_protected_commands(&scene)==1+24+100+1600);
    cold[1].material.kind=SAT_SCENE3D_INDEXED_SOLID;
    assert(t3_floor_protected_commands(&scene)==1+24+100+1600);
    cold[1].material.kind=SAT_SCENE3D_INDEXED_TEXTURED;
    assert(t3_floor_protected_commands(&scene)==1+24+100+1600);
    cold[1].material.kind=SAT_SCENE3D_RGB;
    tiled.grid=4;
    assert(t3_floor_protected_commands(&scene)==1+24+400+1600);
    tiled.grid=0;
    sat_scene3d_projected_face_t hot[4]={0};uint16_t indices[4]={2,0,3,1};
    hot[0].kind=SAT_SCENE3D_PROJECTED_COLD;hot[0].cold=&cold[1];
    hot[1].kind=SAT_SCENE3D_PROJECTED_COLD;hot[1].cold=&cold[3];
    hot[2].kind=SAT_SCENE3D_PROJECTED_PROGRAM;
    hot[3].kind=SAT_SCENE3D_PROJECTED_COLD;hot[3].cold=&cold[2];
    scene.projected_entries=hot;scene.projected_indices=indices;scene.cold_count=3;
    assert(t3_floor_protected_commands(&scene)==1+24+100+1600);
    hot[0].cold=NULL;
    assert(t3_floor_protected_commands(&scene)==UINT32_MAX);
    hot[0].cold=&cold[1];wide.grid=3;
    assert(t3_floor_protected_commands(&scene)==UINT32_MAX);
    wide.grid=8;scene.count=5;
    assert(t3_floor_protected_commands(&scene)==UINT32_MAX);
    puts("floor command reserve: ok");
    return 0;
}
