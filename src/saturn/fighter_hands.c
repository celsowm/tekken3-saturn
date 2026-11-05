/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Hand variants: PartSelectVertexVariant's block swap as Tekken3Remake's
 * docs/research/formats/3dmk-models.md (vertex variants) documents it.
 * Changes: only the positions a variant moves are rewritten, in both the
 * original projection's blocks and the SDK skin records; Gon's palette copy
 * is not reproduced. NOTICE.md. */
#include "fighter_hands.h"
#include "fighter_pack.h"
#include "saturn/parallel.h"

enum { MAX_ROWS=4u, BASE=T3_HAND_VARIANTS };

static uint8_t g_on[2],g_shown[2][MAX_ROWS];

static int16_t* record_position(sat_skin_asset_t* skin,uint16_t ref) {
    const uint32_t record=ref>>2u,slot=ref&3u;
    if(record<skin->rigid_count)return slot?NULL:((sat_skin_rigid_t*)(uintptr_t)skin->rigid)[record].pos;
    if(record-skin->rigid_count>=skin->blend_count || slot>2u)return NULL;
    return ((sat_skin_blend_t*)(uintptr_t)skin->blend)[record-skin->rigid_count].pos[slot];
}

static int16_t (*block_positions(const t3_fighter_pack_t* pack,const t3_hand_variant_row_t* row))[3] {
    return (int16_t(*)[3])(uintptr_t)pack->projected[row->block].positions;
}

int t3_fighter_hands_load(unsigned actor,sat_skin_asset_t* skin) {
    const t3_fighter_pack_t* pack=t3_fighter_pack(actor);
    g_on[actor&1u]=0u;
    if(!pack->hand_row_count || pack->hand_row_count>MAX_ROWS)return 0;
    for(uint32_t r=0;r<pack->hand_row_count;++r) {
        const t3_hand_variant_row_t* row=&pack->hand_rows[r];
        const int16_t (*base)[3]=row->positions+BASE*row->record_count;
        for(uint32_t k=0;k<row->record_count;++k) {
            const int16_t* p=record_position(skin,row->records[k]);
            if(!p || p[0]!=base[k][0] || p[1]!=base[k][1] || p[2]!=base[k][2])return 0;
        }
        int16_t (*block)[3]=block_positions(pack,row);
        const int16_t (*coords)[3]=row->coords+BASE*row->vertex_count;
        for(uint32_t k=0;k<row->vertex_count;++k) {
            const uint16_t v=row->vertices[k];
            if(v>=pack->projected[row->block].own_count || block[v][0]!=coords[k][0] ||
               block[v][1]!=coords[k][1] || block[v][2]!=coords[k][2])return 0;
        }
        g_shown[actor&1u][r]=BASE;
    }
    g_on[actor&1u]=1u;
    return 1;
}

int t3_fighter_hands_apply(unsigned actor,sat_skin_asset_t* skin,const uint8_t variant[2]) {
    if(!g_on[actor&1u])return 0;
    const t3_fighter_pack_t* pack=t3_fighter_pack(actor);
    int changed=0;
    for(uint32_t r=0;r<pack->hand_row_count;++r) {
        const t3_hand_variant_row_t* row=&pack->hand_rows[r];
        const unsigned v=variant[row->channel&1u];
        /* A zero table entry keeps the block shown before. */
        if(v>=T3_HAND_VARIANTS || !((row->present>>v)&1u) || g_shown[actor&1u][r]==v)continue;
        g_shown[actor&1u][r]=(uint8_t)v;
        int16_t (*block)[3]=block_positions(pack,row);
        const int16_t (*coords)[3]=row->coords+v*row->vertex_count;
        for(uint32_t k=0;k<row->vertex_count;++k) {
            int16_t* p=block[row->vertices[k]];
            p[0]=coords[k][0];p[1]=coords[k][1];p[2]=coords[k][2];
        }
        const int16_t (*positions)[3]=row->positions+v*row->record_count;
        for(uint32_t k=0;k<row->record_count;++k) {
            int16_t* p=record_position(skin,row->records[k]);
            if(p){p[0]=positions[k][0];p[1]=positions[k][1];p[2]=positions[k][2];} /* load checked every one */
        }
        changed=1;
    }
    return changed;
}

sat_result_t t3_fighter_hands_sync(unsigned actor,sat_skin_asset_t* skin) {
    if(!g_on[actor&1u])return SAT_OK;
    const t3_fighter_pack_t* pack=t3_fighter_pack(actor);
    for(uint32_t r=0;r<pack->hand_row_count;++r) {
        const t3_hand_variant_row_t* row=&pack->hand_rows[r];
        uintptr_t first=UINTPTR_MAX,last=0u;
        for(uint32_t k=0;k<row->record_count;++k) {
            const uintptr_t p=(uintptr_t)record_position(skin,row->records[k]);
            if(p<first)first=p;
            if(p>last)last=p;
        }
        if(row->record_count)SAT_TRY(sat_parallel_cache_sync_range((const void*)first,(uint32_t)(last-first+6u)));
        SAT_TRY(sat_parallel_cache_sync_range(block_positions(pack,row),
            pack->projected[row->block].own_count*3u*sizeof(int16_t)));
    }
    return SAT_OK;
}
