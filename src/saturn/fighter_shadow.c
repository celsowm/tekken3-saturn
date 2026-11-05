/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Fighter mesh shadows: a C adaptation of the drop shadow that pinned
 * Tekken3Remake documents in docs/research/formats/pssdw-member.md and
 * docs/research/code/stages.md (Lighting): each joint's section of the
 * TK3psSDW mesh goes through camera x floor shear (0x800AE310) x joint world
 * matrix (ShadowProjectSection 0x80037CF0, RTPS), and every face is one flat
 * polygon in the stage's shadow colour (ShadowBuildPrims 0x80037D7C).
 * Changes: the faces are queued behind the whole scene instead of in the OT. */
#include "fighter_shadow.h"
#include "fighter_pack.h"
#include "psx_viewport.h"
#include "saturn/view_cache.h"

enum { SECTIONS=18u, MAX_SLOTS=256u, FAR_DEPTH=0x3FFFFF };

static const int16_t k_shears[][2]=T3_ROSTER_SHADOW_SHEARS;
static int16_t g_shear[2];
volatile int32_t g_t3_shadow_probe[8];

void t3_fighter_shadow_stage(unsigned stage) {
    const unsigned count=sizeof k_shears/sizeof k_shears[0];
    g_shear[0]=k_shears[stage<count?stage:0u][0];
    g_shear[1]=k_shears[stage<count?stage:0u][1];
}

int t3_fighter_shadow_submit(sat_scene_t* scene,unsigned actor,const t3_psx_camera_matrix_t* view,
                             const t3_psx_joint_t joints[T3_PSX_DRAW_PARTS],uint16_t colour) {
    const t3_fighter_pack_t* pack=t3_fighter_pack(actor);
    ++g_t3_shadow_probe[0];
    g_t3_shadow_probe[7]=1;
    if(!pack->shadow_faces || pack->shadow_faces>T3_SHADOW_MAX_FACES)return 0;
    /* The pack's shadow block (build_fighter_packs.py, shadow_mesh). */
    const uint16_t* head=(const uint16_t*)(const void*)pack->shadow;
    const uint32_t slots=head[0];
    const uint16_t* first=head+1;
    const int16_t (*coords)[3]=(const int16_t(*)[3])(const void*)(head+2u+SECTIONS);
    const uint8_t (*faces)[4]=(const uint8_t(*)[4])(const void*)(coords+slots);
    g_t3_shadow_probe[7]=2;
    if(slots>MAX_SLOTS || first[SECTIONS]!=slots)return 0;
    /* The floor shear has m11 = 0: every point lands on the floor plane. */
    const t3_psx_matrix_t shear={{{4096,g_shear[0],0},{0,0,0},{0,g_shear[1],4096}}};
    t3_psx_matrix_t rotation,flat;
    for(unsigned i=0;i<3u;++i)for(unsigned j=0;j<3u;++j)rotation.m[i][j]=view->m[i][j];
    if(!t3_psx_mul_rot(&rotation,&shear,&flat))return 0;
    t3_psx_camera_matrix_t shadow_view=*view;
    for(unsigned i=0;i<3u;++i)for(unsigned j=0;j<3u;++j)shadow_view.m[i][j]=flat.m[i][j];
    int16_t sx[MAX_SLOTS],sy[MAX_SLOTS];
    uint8_t behind[MAX_SLOTS];
    for(unsigned s=0;s<SECTIONS;++s) {
        t3_psx_camera_matrix_t m;
        if(first[s]>first[s+1u])return 0;
        if(first[s]==first[s+1u])continue;
        g_t3_shadow_probe[7]=10+(int32_t)s;
        if(!t3_psx_camera_joint_matrix(&shadow_view,&joints[s],&m))return 0;
        for(unsigned k=first[s];k<first[s+1u];++k) {
            int32_t out[3];
            t3_psx_camera_project_s16(&m,coords[k],out);
            sx[k]=(int16_t)(t3_psx_viewport_x(out[0])-160);
            sy[k]=(int16_t)(t3_psx_viewport_y(out[1])-112);
            behind[k]=out[2]<=0;
        }
    }
    g_t3_shadow_probe[4]=sx[0];g_t3_shadow_probe[5]=sy[0];
    sat_view_cache_item_t item={0};
    item.color=colour;
    for(uint32_t f=0;f<pack->shadow_faces;++f) {
        const uint8_t* v=faces[f];
        if(behind[v[0]] || behind[v[1]] || behind[v[2]] || behind[v[3]]){++g_t3_shadow_probe[3];continue;}
        for(unsigned c=0;c<4u;++c){item.quad.x[c]=sx[v[c]];item.quad.y[c]=sy[v[c]];}
        const sat_result_t st=sat_scene_queue_view_item(scene,&item,FAR_DEPTH,0);
        if(st!=SAT_OK){g_t3_shadow_probe[7]=100-(int32_t)st;return 0;}
        ++g_t3_shadow_probe[2];
    }
    ++g_t3_shadow_probe[1];g_t3_shadow_probe[7]=0;
    return 1;
}
