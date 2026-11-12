/*
 * Two independent animated actor instances over one shared SKA1 asset, or
 * over one asset each when a second character mesh is built in.
 * Every pose, matrix, mutable mesh, and material owner is caller-owned;
 * LibSaturn supplies animation sampling, skin deformation, scene projection,
 * texture residency, and palette management.
 */
#include "characters.h"
#if defined(T3_FIGHTER_PACKS)
#include "fighter_pack.h"
#if defined(T3_COMPILED_RENDERER) || defined(T3_SHARE_ACTOR_RESOURCES)
#error "these paths read a fighter's pixels after boot or share one embedded mesh"
#endif
#define ACTOR_FACE_OT(actor) (t3_fighter_pack(T3_PACK_INDEX(actor))->face_ot)
#elif defined(T3_SECOND_MESH)
#define ACTOR_FACE_OT(actor) ((actor)?t3_kmd_face_ot1:t3_kmd_face_ot)
#else
#define ACTOR_FACE_OT(actor) t3_kmd_face_ot
#endif
#include "move_probe.h"
#include "original_hud.h"
#include "psx_skin_bridge.h"
#include "perf.h"
#include "placement.h"
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
#include "psx_viewport.h"
#endif
#if defined(T3_ORIGINAL_FIGHTER_OT)
#include "psx_fighter_ot.h"
#endif
#if defined(T3_FACE_COVERAGE_CENSUS)
#include "psx_face_coverage.h"
#endif
#if defined(T3_MIXED_OT_DIAGNOSTIC)
#include "psx_particle_tile.h"
#include "psx_scene_ot.h"
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
#include "particle_prepare.h"
#include "stage_vdp2.h"
#endif
#endif
#if defined(T3_FIGHTER_LIGHTING)
#include "fighter_light.h"
#endif
#if defined(T3_FIGHTER_HAND_VARIANTS)
#include "fighter_hands.h"
#endif
#if defined(T3_FIGHTER_MESH_SHADOWS)
#include "fighter_shadow.h"
#include "stage_vdp2.h"
#endif
#if defined(T3_COMPILED_RENDERER)
#include "compiled_renderer.h"
extern const t3_compiled_plan_t t3_compiled_plan;
#endif

#include "saturn/character3d.h"
#include "saturn/math3d.h"
#include "saturn/skeleton3d.h"
#include "saturn/skin3d.h"
#include "saturn/time.h"
#if defined(T3_PARALLEL_GEOMETRY) || defined(T3_PARALLEL_SKINNING)
#include "saturn/parallel.h"
#endif

#define DT_Q16 (SAT_FX16_ONE / T3_HZ)
#define FADE_Q16 (SAT_FX16_ONE / 10)
#ifndef T3_ACTOR_COUNT
#define T3_ACTOR_COUNT 2
#endif
enum { ACTOR_COUNT = T3_ACTOR_COUNT };

/* Force: actor 0 keeps its player pack; either enemy can bind either of the
 * stage's two resident variants. A boss reload shares pack 1 for hidden actor 2. */
#if defined(T3_FORCE_PROGRAM)
static uint8_t g_force_pack_index[3] = {0, 1, 1};
#define T3_PACK_INDEX(actor) g_force_pack_index[(actor)]
#else
#define T3_PACK_INDEX(actor) ((actor) & 1u)
#endif

#if defined(T3_FORCE_PROGRAM)
enum { ASSET_COUNT = 3, FORCE_ENEMY_TEXTURE_CAP = T3_TEXTURE_COUNT };
static uint16_t (*g_force_enemy_fallback)[FORCE_ENEMY_TEXTURE_CAP];
_Static_assert(2u*FORCE_ENEMY_TEXTURE_CAP*sizeof(uint16_t)<=T3_FORCE_PACK_SCRATCH_BYTES,
    "resident enemy fallback colours fit the third pack's scratch");
#if T3_FORCE_VARIANT_PROBE
static volatile uint32_t g_force_variant_binds[3];
#endif
#elif defined(T3_SECOND_MESH)
#if defined(T3_COMPILED_RENDERER) || defined(T3_SHARE_ACTOR_RESOURCES)
#error "the compiled renderer and shared actor resources cover one mesh"
#endif
enum { ASSET_COUNT = 2 };
#else
enum { ASSET_COUNT = 1 };
#endif
#if defined(T3_ASSET_TOPOLOGY)
volatile uint32_t g_t3_asset_topology_ready;
#endif
#if defined(T3_SHARE_ACTOR_RESOURCES)
enum { RESOURCE_COPIES=1 };
static sat_scene3d_material_program_t g_fallback_material_commands[T3_TEXTURE_COUNT]
    __attribute__((section(".wram_l")));
static sat_vdp1_texture_t g_fallback_texture_descriptors[T3_TEXTURE_COUNT]
    __attribute__((section(".wram_l")));
volatile uint32_t g_t3_shared_actor_resources;
#else
enum { RESOURCE_COPIES=ACTOR_COUNT };
#endif

typedef struct {
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
    sat_vec3_t vertices[T3_PROJECTED_VERTEX_COUNT];
    union {
        sat_projected_vertex_t projected[T3_PROJECTED_VERTEX_COUNT];
        t3_psx_projected_vertex_t original_raw[T3_PROJECTED_VERTEX_COUNT];
    };
#else
    sat_vec3_t vertices[T3_VERTEX_COUNT];
    sat_projected_vertex_t projected[T3_VERTEX_COUNT];
#endif
#if !defined(T3_VDP2_FLOOR)
    sat_vec3_t world_vertices[T3_VERTEX_COUNT];
#endif
} actor_geometry_t;

typedef struct {
    /* The decoded asset and counts this actor draws; buffers take the
     * largest asset's counts. */
    const sat_character_asset_t* asset;
    const sat_skeleton_clip_t* clips;
    uint16_t vertex_count, texture_count;
    sat_skeleton_instance_t skeleton;
    sat_transform3d_node_t nodes[T3_JOINT_COUNT];
    uint16_t scratch[T3_JOINT_COUNT];
    sat_skeleton_pose_joint_t pose[T3_JOINT_COUNT];
    sat_skeleton_pose_joint_t fade_from[T3_JOINT_COUNT];
    sat_pose_fade_t fade;
    sat_skel_clip_player_t clip_player;
#if defined(T3_COMPILED_RENDERER)
    union {
        sat_mat4_t joints[T3_JOINT_COUNT];
        t3_compiled_pose_t compiled_pose;
    };
    uint8_t compiled_active;
#else
    sat_mat4_t joints[T3_JOINT_COUNT];
#endif
    sat_mesh_t mesh;
    sat_vec3_t* vertices;
    uint16_t indices[T3_FACE_COUNT * 4u];
    uint16_t face_materials[T3_FACE_COUNT];
    sat_vdp1_texture_t* textures;
    sat_texture_t owners[T3_TEXTURE_COUNT];
    sat_scene3d_material_t materials[T3_TEXTURE_COUNT];
    sat_scene3d_material_program_t* material_commands;
    sat_scene3d_material_programs_t material_programs;
    sat_projected_vertex_t* projected;
    sat_vec3_t* world_vertices;
    sat_scene3d_instance_t instance;
    sat_scene3d_instance_binding_t binding;
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
    sat_mesh_t original_mesh;
    sat_scene3d_instance_binding_t original_binding;
    uint8_t original_active;
#endif
    sat_mat4_t world;
    uint8_t clip;
    int32_t last_x, last_z;
} actor_t;

static sat_character_asset_t g_assets[ASSET_COUNT];
static sat_skeleton_clip_t g_clips[ASSET_COUNT][T3_CLIP_COUNT];
#if defined(T3_VDP2_FLOOR)
/* Owned encounter already places vertices in world space. Its generic
 * transform fallback remains available in Low WRAM, freeing High WRAM
 * for the VDP2/CDFS code without slowing projected vertex storage. */
static sat_vec3_t g_stage_world_scratch[ACTOR_COUNT][T3_VERTEX_COUNT]
#if !defined(T3_ORIGINAL_FIGHTER_PROJECTION)
    __attribute__((section(".wram_l")));
#else
    ;
#endif
#if defined(T3_ORIGINAL_FIGHTER_OT)
_Static_assert(sizeof(g_stage_world_scratch[0]) >= T3_PROJECTED_VERTEX_COUNT*sizeof(t3_psx_projected_vertex_t),
    "fighter OT needs room for every original projected vertex");
#endif
#endif
#if defined(T3_FIGHTER_PACKS)
/* The texture table is only read while decoding, in the arena. */
static actor_t g_actors[ACTOR_COUNT] T3_HOT_WRAM;
/* One decode per resident asset, independent of which actor currently uses it. */
static uint8_t g_asset_loaded[ASSET_COUNT];
#elif defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES
/* Decoded/uploaded once; preserve High WRAM for simulation and stack. */
static sat_model_texture_asset_t g_texture_tables[ASSET_COUNT][T3_TEXTURE_COUNT] __attribute__((section(".wram_l")));
static actor_t g_actors[ACTOR_COUNT] T3_HOT_WRAM;
#else
static sat_model_texture_asset_t g_texture_tables[ASSET_COUNT][T3_TEXTURE_COUNT];
static actor_t g_actors[ACTOR_COUNT];
#endif
/* Keep independently mutable geometry together. Texture/material tables and
 * topology retain their own lifetime and bank; the placement option moves only
 * these hot arrays, with identical capacity and CPU publication rules. */
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES && !defined(T3_HIGH_GEOMETRY_SCRATCH) && !defined(T3_FIGHTER_PACKS)
static actor_geometry_t g_actor_geometry[ACTOR_COUNT] __attribute__((section(".wram_l")));
#else
static actor_geometry_t g_actor_geometry[ACTOR_COUNT];
#endif
/* Immutable after upload/compilation, independently placed from geometry
 * and from each other so native bank experiments isolate their read cost. */
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES && !defined(T3_HIGH_MATERIAL_PROGRAMS)
static sat_scene3d_material_program_t g_material_commands[RESOURCE_COPIES][T3_TEXTURE_COUNT]
    __attribute__((section(".wram_l")));
#else
static sat_scene3d_material_program_t g_material_commands[RESOURCE_COPIES][T3_TEXTURE_COUNT];
#endif
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES && !defined(T3_HIGH_TEXTURE_DESCRIPTORS)
static sat_vdp1_texture_t g_texture_descriptors[RESOURCE_COPIES][T3_TEXTURE_COUNT]
    __attribute__((section(".wram_l")));
#else
static sat_vdp1_texture_t g_texture_descriptors[RESOURCE_COPIES][T3_TEXTURE_COUNT];
#endif
static uint8_t g_initialized;
#if defined(T3_FIGHTER_LIGHTING)
/* Each image relights one actor's tables, the two in turn. */
static uint8_t g_light_turn;
#endif
static uint8_t g_skin_preview_applied[ACTOR_COUNT];
#if defined(T3_FORCE_PROGRAM)
/* Tekken Force publishes its owned match's original joint pose here. The
 * pose is rebased on the Force camera so the bridge's original-unit bounds
 * hold for a whole level (see characters.h). */
static t3_psx_joint_t g_force_pose[ACTOR_COUNT][T3_PSX_DRAW_PARTS];
static uint32_t g_force_parts_mask[ACTOR_COUNT];
static uint8_t g_force_shown[ACTOR_COUNT], g_force_pose_ready[ACTOR_COUNT];
static int32_t g_force_origin_x, g_force_origin_z;
#endif
static uint32_t g_texture_vram_bytes, g_skin_work_ms;
#if defined(T3_COMPILED_RENDERER)
static t3_compiled_renderer_t g_compiled_renderer;
volatile uint32_t g_t3_compiled_skin_frames;
volatile uint32_t g_t3_compiled_skin_fallbacks;
#if defined(T3_VERIFY_COMPILED_RENDERER)
static sat_mat4_t g_reference_joints[ACTOR_COUNT][T3_JOINT_COUNT] __attribute__((section(".wram_l")));
static sat_vec3_t g_reference_vertices[T3_VERTEX_COUNT] __attribute__((section(".wram_l")));
volatile uint32_t g_t3_compiled_skin_verified;
#endif
_Static_assert(sizeof(t3_compiled_pose_t)<=sizeof(sat_mat4_t)*T3_JOINT_COUNT,
    "compiled pose must fit the existing joint storage");
#endif
#define GEOMETRY_TIMEOUT_TICKS 60000u
#if defined(T3_PARALLEL_GEOMETRY)
#if !defined(T3_DIRECT_FACE_SPANS) || defined(T3_VERIFY_PARALLEL_GEOMETRY)
static sat_scene3d_face_t g_geometry_faces[T3_FACE_COUNT] __attribute__((section(".wram_l")));
#endif
static uint32_t g_geometry_keys[T3_FACE_COUNT] T3_HOT_WRAM;
static sat_scene3d_prepare_item_t g_geometry_item T3_HOT_WRAM;
static sat_scene3d_prepare_batch_t g_geometry_batch T3_HOT_WRAM;
static sat_scene3d_source_plan_t g_geometry_sources T3_HOT_WRAM;
/* Materials have no vertex-Gouraud pointers; descriptor ranges plus topology. */
static sat_scene3d_source_range_t g_geometry_source_ranges[T3_TEXTURE_COUNT+5u]
#if defined(T3_REFERENCE_REPLAY)
    /* The long trace occupies High WRAM. Descriptor validation ranges can
     * use spare Low WRAM in this diagnostic without reducing either stack. */
    __attribute__((section(".wram_l")));
#else
    T3_HOT_WRAM;
#endif
volatile uint32_t g_t3_geometry_pairs __attribute__((section(".wram_l")));
volatile uint32_t g_t3_geometry_serial __attribute__((section(".wram_l")));
#if defined(T3_VERIFY_PARALLEL_GEOMETRY)
volatile uint32_t g_t3_geometry_verified __attribute__((section(".wram_l")));
static int same_face(const sat_scene3d_face_t* a,const sat_scene3d_face_t* b) {
    if(a->projected_safe!=b->projected_safe || a->cached_projected!=b->cached_projected ||
       a->gouraud_valid!=b->gouraud_valid || a->program_valid!=b->program_valid)return 0;
    if(a->program_valid) {
        if(a->program!=b->program)return 0;
    } else if(a->material.kind!=b->material.kind ||
       a->material.rgb555!=b->material.rgb555 || a->material.texture!=b->material.texture ||
       a->material.tiled!=b->material.tiled || a->material.color_calc_slot!=b->material.color_calc_slot ||
       a->material.vertex_gouraud!=b->material.vertex_gouraud ||
       a->material.sprite_flags!=b->material.sprite_flags)return 0;
    for(unsigned c=0;c<4;++c) {
        if(a->projected.x[c]!=b->projected.x[c] || a->projected.y[c]!=b->projected.y[c] ||
           (a->gouraud_valid && a->gouraud[c]!=b->gouraud[c]))return 0;
        if(!a->projected_safe && (a->world.v[c].x!=b->world.v[c].x ||
           a->world.v[c].y!=b->world.v[c].y || a->world.v[c].z!=b->world.v[c].z))return 0;
    }
    return 1;
}
#endif
#endif

static uint8_t clip_for(const t3_fighter_t* fighter, int moving) {
#if T3_CLIP_COUNT == 1
    /* Real KMD geometry has only the static bind pose until DIVMOT import.
     * Never fabricate original character moves by relabeling other clips. */
    (void)fighter;
    (void)moving;
    return T3_CLIP_IDLE;
#else
    if (!fighter->health) return T3_CLIP_KNOCKDOWN;
    if (fighter->jump_frames) return T3_CLIP_JUMP;
    if (fighter->stun) return T3_CLIP_HIT;
    if (fighter->attack)
        return fighter->attack <= 2u ? T3_CLIP_PUNCH : T3_CLIP_KICK;
    if (fighter->guarding) return T3_CLIP_GUARD;
    return moving ? T3_CLIP_WALK : T3_CLIP_IDLE;
#endif
}

static sat_result_t select_clip(actor_t* actor, uint8_t next) {
    sat_result_t st;
    if (actor->clip == next) return SAT_OK;
    st = sat_pose_fade_begin(&actor->fade, actor->pose, FADE_Q16);
    if (st != SAT_OK) return st;
    st = sat_skel_clip_player_set_clip(&actor->clip_player, &actor->clips[next], 0);
    if (st == SAT_OK) actor->clip = next;
    return st;
}

sat_result_t t3_characters_init(t3_character_load_progress_fn progress, void* context) {
    unsigned i;
    sat_result_t st;
    if (g_initialized) return SAT_OK;
#if defined(T3_FORCE_PROGRAM)
    g_force_pack_index[0]=0;g_force_pack_index[1]=1;
    g_force_pack_index[2]=(uint8_t)(t3_fighter_packs_count()==3u?2u:1u);
    g_force_enemy_fallback=t3_fighter_pack_force_scratch();
#endif
#if defined(T3_PARALLEL_GEOMETRY) || defined(T3_PARALLEL_SKINNING)
    const sat_parallel_config_t parallel={SAT_PARALLEL_AUTO,NULL,0,0,GEOMETRY_TIMEOUT_TICKS};
    if((st=sat_parallel_init(&parallel))!=SAT_OK)return st;
#endif
#if !defined(T3_FIGHTER_PACKS)
    {
        uint8_t* const archives[ASSET_COUNT]={t3_archive.bytes
#if defined(T3_SECOND_MESH)
            ,t3_archive1.bytes
#endif
        };
        const uint32_t sizes[ASSET_COUNT]={T3_ARCHIVE_BYTES
#if defined(T3_SECOND_MESH)
            ,T3_ARCHIVE1_BYTES
#endif
        };
        for (i = 0; i < ASSET_COUNT; ++i) {
            sat_character_asset_t* asset=&g_assets[i];
            if ((st = sat_character_asset_decode(archives[i], sizes[i], g_clips[i],
                    T3_CLIP_COUNT, g_texture_tables[i], T3_TEXTURE_COUNT, asset)) != SAT_OK)
                return st;
            if ((st = sat_skin_validate(&asset->skin)) != SAT_OK) return st;
            if ((st = sat_model_validate(&asset->model)) != SAT_OK) return st;
            if (asset->model.vertex_count > T3_VERTEX_COUNT ||
                asset->model.face_count > T3_FACE_COUNT ||
                asset->model.texture_count > T3_TEXTURE_COUNT) return SAT_ERR_CAPACITY;
        }
    }
#endif
    if (progress) progress(context, T3_CHARACTER_LOAD_MODEL_READY);

    int share_resources=0;
#if defined(T3_SHARE_ACTOR_RESOURCES)
    /* Both original actors use this same decoded model and palette bytes.
     * A single shared INDEX8 palette gives identical colors in either bank.
     * Multiple overlapping banks retain separate resources unless all faces
     * use LUT4, which never reads palette_base. */
    share_resources=g_assets[0].model.texture_count>0;
    for(unsigned t=0;t<g_assets[0].model.texture_count;++t)
        if(g_assets[0].model.palette_count>1 &&
           !(g_assets[0].model.textures[t].flags&SAT_MODEL_TEXTURE_LUT4))share_resources=0;
    g_t3_shared_actor_resources=0;
#endif

    for (i = 0; i < ACTOR_COUNT; ++i) {
        actor_t* a = &g_actors[i];
        /* Force preloads each regular stage's two enemy variants. A boss
         * reload uses two packs and shares its second asset for hidden actor 2. */
#if defined(T3_FORCE_PROGRAM)
        const unsigned asset_index = T3_PACK_INDEX(i);
#else
        const unsigned asset_index = i < ASSET_COUNT ? i : 0u;
#endif
#if defined(T3_FIGHTER_PACKS)
        /* The archive and its texture table borrow the arena until compacted
         * below; a loaded actor keeps only its store. */
        sat_character_asset_t* const owned = &g_assets[asset_index];
        if (!g_asset_loaded[asset_index]) {
            uint8_t* const arena=t3_fighter_pack_arena();
            const uint32_t bytes=t3_fighter_pack_section_bytes(asset_index,T3_PACK_ARCHIVE);
            sat_model_texture_asset_t* const tables=
                (sat_model_texture_asset_t*)(void*)(arena+((bytes+15u)&~15u));
            if(bytes>T3_ROSTER_ARCHIVE_BYTES)return SAT_ERR_CAPACITY;
#if defined(T3_FORCE_PROGRAM)
            if((uint32_t)((uint8_t*)(tables+T3_TEXTURE_COUNT)-arena)>
               T3_ROSTER_ARENA_BYTES-T3_ROSTER_STORE_BYTES)return SAT_ERR_CAPACITY;
#endif
            if((st=t3_fighter_pack_read(asset_index,T3_PACK_ARCHIVE,0,arena,bytes))!=SAT_OK)return st;
            if((st=sat_character_asset_decode(arena,bytes,g_clips[asset_index],T3_CLIP_COUNT,
                    tables,T3_TEXTURE_COUNT,owned))!=SAT_OK)return st;
            if((st=sat_skin_validate(&owned->skin))!=SAT_OK)return st;
            if((st=sat_model_validate(&owned->model))!=SAT_OK)return st;
            if(owned->model.vertex_count>T3_VERTEX_COUNT || owned->model.face_count>T3_FACE_COUNT ||
               owned->model.texture_count>T3_TEXTURE_COUNT)return SAT_ERR_CAPACITY;
            g_asset_loaded[asset_index]=1u;
        }
#endif
        const sat_character_asset_t* asset = &g_assets[asset_index];
        a->asset = asset;
        a->clips = g_clips[asset_index];
        a->vertex_count = asset->model.vertex_count;
        a->texture_count = asset->model.texture_count;
        a->vertices = g_actor_geometry[i].vertices;
        a->projected = g_actor_geometry[i].projected;
#if defined(T3_VDP2_FLOOR)
        a->world_vertices = g_stage_world_scratch[i];
#else
        a->world_vertices = g_actor_geometry[i].world_vertices;
#endif
#if defined(T3_SHARE_ACTOR_RESOURCES)
        a->textures=i && !share_resources?g_fallback_texture_descriptors:g_texture_descriptors[0];
        a->material_commands=i && !share_resources?g_fallback_material_commands:g_material_commands[0];
        if(i && share_resources)g_t3_shared_actor_resources=1;
#else
        a->textures = g_texture_descriptors[i];
        a->material_commands = g_material_commands[i];
#endif
        sat_model_asset_t model = asset->model;
#if defined(T3_FORCE_PROGRAM)
        if (asset_index != 0u) {
#else
        if (i == 1u) {
#endif
            /* The second actor gets separate CRAM banks after the first
             * actor's, never overriding its colors. */
#if defined(T3_SECOND_MESH)
            model.palette_base = (uint16_t)(g_assets[0].model.palette_base +
                (g_assets[0].model.palette_count ? g_assets[0].model.palette_count : 1u));
#else
#if !defined(T3_LOCAL_KMD_BIND) || !T3_ORIGINAL_TEXTURES
            model.palettes_rgb555 = t3_red_palette;
#endif
            model.palette_base = (uint16_t)(model.palette_base + 1u);
#endif
        }
#if defined(T3_FORCE_PROGRAM)
        /* Stage tiles own CRAM colours 0..1279. Sprite INDEX8 palettes
         * occupy banks 5 and 6; bank 7 is reserved for the text overlay.
         * Even LUT4 models upload a fallback palette through this API. */
        model.palette_base=(uint16_t)(5u+(asset_index?g_assets[0].model.palette_count:0u));
        if(model.palette_base+model.palette_count>7u)return SAT_ERR_CAPACITY;
#endif
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES
        /* These immutable model textures live for the application lifetime.
         * Use the public native model uploader; the logical texture registry
         * has 64 owner slots and is intended for individually managed assets. */
        int upload_textures=(!share_resources || !i);
#if defined(T3_FORCE_PROGRAM)
        /* Enemy 2 shares the enemy asset already compacted by actor 1.
         * Its texture pixels were transient arena data; reuse the resident
         * VDP1 descriptors instead of uploading those expired pointers. */
        if(i==2u && asset_index==1u) {
            __builtin_memcpy(a->textures,g_actors[1].textures,
                a->texture_count*sizeof(*a->textures));
            upload_textures=0;
        }
#endif
        if (upload_textures &&
            (st = sat_model_upload_textures(&model,a->textures,a->texture_count)) != SAT_OK)
            return st;
        if(share_resources && i) {
            /* Preserve the original second bank's CRAM side effect. Only
             * immutable VDP1 pixels/descriptors/programs are shared. */
            for(unsigned p=0;p<model.palette_count;++p)
                if((st=sat_palette_upload_indexed8(model.palettes_rgb555+p*256u,
                        (uint16_t)(model.palette_base+p)))!=SAT_OK)return st;
        }
        for (unsigned t=0;t<a->texture_count;++t) {
            sat_scene3d_material_t* material=&a->materials[t];
#if defined(T3_FORCE_PROGRAM)
            if(i==2u && asset_index==1u) {
                *material=g_actors[1].materials[t];
                material->texture=&a->textures[t];
                continue;
            }
#endif
            *material=(sat_scene3d_material_t){0};
            const sat_model_texture_asset_t* texture=&model.textures[t];
            material->kind=SAT_SCENE3D_INDEXED_TEXTURED;
            material->texture=&a->textures[t];
            material->color_calc_slot=SAT_INDEXED_SOLID_OPAQUE;
            /* The fallback colour of a near-clipped face: its middle texel. Native-density
             * patches shrink on most faces: high-speed shrink keeps their VDP1 cost near the
             * 8x8 patches' (docs/FIGHTER_FIDELITY.md). */
            if(texture->flags&SAT_MODEL_TEXTURE_LUT4) {
                material->rgb555=model.luts_rgb555[texture->palette_slot*16u+
                    (texture->pixels[texture->pixel_count/4u]>>4)];
                material->sprite_flags=SAT_SPRITE_FLAG_HIGH_SPEED_SHRINK;
            }
            else
                material->rgb555=model.palettes_rgb555[
                    texture->palette_slot*256u+texture->pixels[texture->pixel_count/2u]];
#if defined(T3_FORCE_PROGRAM)
            if(asset_index && t3_fighter_packs_count()==3u) {
                if(t>=FORCE_ENEMY_TEXTURE_CAP || !(texture->flags&SAT_MODEL_TEXTURE_LUT4))
                    return SAT_ERR_CAPACITY;
                g_force_enemy_fallback[asset_index-1u][t]=material->rgb555;
            }
#endif
        }
#else
        if ((st = sat_character_materials_upload(&model, a->owners,
                a->textures, a->materials, a->texture_count)) != SAT_OK)
            return st;
#endif
        if((!share_resources || !i)
#if defined(T3_FORCE_PROGRAM)
           && !(i==2u && asset_index==1u)
#endif
           )for (unsigned t=0;t<a->texture_count;++t)
            g_texture_vram_bytes+=(uint32_t)a->textures[t].width*a->textures[t].height/
                (a->textures[t].format==SAT_VDP1_TEXTURE_LUT4?2u:1u);
        if ((st = sat_mesh_init(&a->mesh, a->vertices, T3_VERTEX_COUNT,
                a->indices, T3_FACE_COUNT)) != SAT_OK) return st;
        if ((st = sat_model_copy_to_mesh(&asset->model, &a->mesh)) != SAT_OK)
            return st;
        if ((st = sat_skin_validate_mesh(&asset->skin, &a->mesh)) != SAT_OK)
            return st;
#if defined(T3_FIGHTER_PACKS)
        /* Compact after the last actor using this decode has copied its
         * mesh. Compaction removes the texture table; model_copy_to_mesh
         * validates that table even though the mesh itself has no pixels. */
#if defined(T3_FORCE_PROGRAM)
        if(i==0u || i==2u || t3_fighter_packs_count()==3u) {
#else
        if(asset_index==i) {
#endif
            uint32_t capacity,kept;
            void* const storage=t3_fighter_pack_asset_storage(asset_index,&capacity);
            if((st=sat_character_asset_compact(owned,storage,capacity,&kept))!=SAT_OK)return st;
        }
#endif
#if defined(T3_ASSET_TOPOLOGY)
        /* The application owns the mutable archive; decode has already made
         * its tables native-endian. After boot both actors only read topology.
         * Vertices stay independently mutable. Bind before validation and
         * source-plan creation, so their identities/publication remain exact. */
        a->mesh.indices=(uint16_t*)(uintptr_t)asset->model.indices;
#endif
        __builtin_memcpy(a->face_materials, asset->model.face_texture_indices,
               asset->model.face_count*sizeof(*a->face_materials));
        if ((st = sat_skeleton_instance_init(&a->skeleton,
                &asset->skeleton, a->nodes, T3_JOINT_COUNT,
                a->scratch, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_skeleton_rest_pose(&asset->skeleton,
                a->pose, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_pose_fade_init(&a->fade,
                a->fade_from, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_skel_clip_player_init(&a->clip_player,
                &a->clips[T3_CLIP_IDLE])) != SAT_OK) return st;
        a->instance.mesh = &a->mesh;
        a->instance.materials = a->materials;
        a->instance.material_count = a->texture_count;
        a->instance.face_materials = a->face_materials;
#if defined(T3_ASSET_TOPOLOGY)
        a->instance.face_materials=asset->model.face_texture_indices;
#endif
        a->instance.world = &a->world;
        /* Topology, texture residency and the face/material mapping remain
         * immutable after initialization. Pay their validation once; skinning
         * changes only vertex coordinates, which the binding permits. */
        if ((st = sat_scene3d_instance_binding_init(&a->binding, &a->mesh,
                a->materials, a->texture_count, a->instance.face_materials)) != SAT_OK)
            return st;
        if ((st = sat_scene3d_material_programs_init(&a->material_programs,
                a->materials, a->texture_count, a->material_commands,
                T3_TEXTURE_COUNT)) != SAT_OK) return st;
        a->binding.programs = &a->material_programs;
        a->instance.validated_binding = &a->binding;
#if defined(T3_FIGHTER_LIGHTING)
        a->instance.face_gouraud=t3_fighter_light_load(i);
#endif
#if defined(T3_FIGHTER_HAND_VARIANTS)
        (void)t3_fighter_hands_load(i,(sat_skin_asset_t*)(uintptr_t)&a->asset->skin);
#endif
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
        a->original_mesh=a->mesh;
        a->original_mesh.vertex_cap=T3_PROJECTED_VERTEX_COUNT;
#if defined(T3_FIGHTER_PACKS)
        a->original_mesh.vertex_count=(uint16_t)t3_fighter_pack(T3_PACK_INDEX(i))->projected_vertices;
        a->original_mesh.indices=(uint16_t*)(uintptr_t)t3_fighter_pack(T3_PACK_INDEX(i))->projected_faces;
#else
        a->original_mesh.vertex_count=(uint16_t)t3_kmd_projected_vertex_count;
        a->original_mesh.indices=(uint16_t*)(uintptr_t)t3_kmd_projected_faces;
#if defined(T3_SECOND_MESH)
        if(i==1u) {
            a->original_mesh.vertex_count=(uint16_t)t3_kmd_projected1_vertex_count;
            a->original_mesh.indices=(uint16_t*)(uintptr_t)t3_kmd_projected1_faces;
        }
#endif
#endif
        if((st=sat_scene3d_instance_binding_init(&a->original_binding,&a->original_mesh,
                a->materials,a->texture_count,a->instance.face_materials))!=SAT_OK)return st;
        a->original_binding.programs=&a->material_programs;
#endif
        /* Preserve the original per-face two-sided flag when culling. The
         * original culls by screen winding alone (NCLIP), so the world-normal
         * test, which reads every corner again, is left out. */
#if T3_CLIP_COUNT == 1
        a->instance.cull_backfaces = SAT_SCENE3D_CULL_SCREEN;
#if defined(T3_FIGHTER_PACKS)
        a->instance.double_sided_faces = t3_fighter_pack(T3_PACK_INDEX(i))->double_sided;
#else
        a->instance.double_sided_faces = t3_kmd_double_sided;
#if defined(T3_SECOND_MESH)
        if (i == 1u) a->instance.double_sided_faces = t3_kmd_double_sided1;
#endif
#endif
#else
        a->instance.cull_backfaces = 1u;
#endif
        if (progress) progress(context, i == 0u ?
            T3_CHARACTER_LOAD_PLAYER1_READY : T3_CHARACTER_LOAD_PLAYER2_READY);
    }
#if defined(T3_ASSET_TOPOLOGY)
    g_t3_asset_topology_ready=1;
#endif
#if defined(T3_COMPILED_RENDERER)
    /* An unsupported asset keeps the reference path. All metadata is checked
     * once against the decoded SKA, rather than trusted per frame. */
    (void)t3_compiled_renderer_init(&g_compiled_renderer,&t3_compiled_plan,
        &g_assets[0].skin,g_actors[0].indices,g_actors[0].mesh.face_count,t3_kmd_double_sided);
#endif
#if defined(T3_PARALLEL_GEOMETRY)
    /* Texture descriptors, materials and topology live for the application.
     * Pose vertices/world/masks remain dynamic in the SDK publication path. */
    if ((st=sat_scene3d_source_plan_init(&g_geometry_sources,&g_actors[1].instance,
            g_geometry_source_ranges,T3_TEXTURE_COUNT+5u))!=SAT_OK) return st;
#endif
    g_initialized = 1u;
    return SAT_OK;
}

#if defined(T3_FORCE_PROGRAM)
/* Rebind mutable actor state to one of the stage's two immutable, resident
 * enemy assets. No allocation, disc read or texture upload occurs here.
 * The compact assets retain topology, skin, skeleton and clips; their
 * transient texture tables have already been released. */
sat_result_t t3_characters_force_bind_enemy(unsigned actor,unsigned key) {
    if(actor<1u || actor>=ACTOR_COUNT || key>3u || !g_initialized ||
       t3_fighter_packs_count()!=3u)return SAT_ERR_INVALID_ARG;
    unsigned index=1u;
    while(index<3u && t3_fighter_pack(index)->id!=21u+key)++index;
    if(index==3u)return SAT_ERR_NOT_FOUND;
    if(g_force_pack_index[actor]==index)return SAT_OK;
    actor_t* a=&g_actors[actor];
    const sat_character_asset_t* asset=&g_assets[index];
    const t3_fighter_pack_t* pack=t3_fighter_pack(index);
    a->asset=asset;a->clips=g_clips[index];
    a->vertex_count=asset->model.vertex_count;a->texture_count=asset->model.texture_count;
    if(a->texture_count>FORCE_ENEMY_TEXTURE_CAP)return SAT_ERR_CAPACITY;
    a->textures=g_texture_descriptors[index];
    a->material_commands=g_material_commands[index];
    for(unsigned t=0;t<a->texture_count;++t)a->materials[t]=(sat_scene3d_material_t){
        .kind=SAT_SCENE3D_INDEXED_TEXTURED,.texture=&a->textures[t],
        .rgb555=g_force_enemy_fallback[index-1u][t],
        .color_calc_slot=SAT_INDEXED_SOLID_OPAQUE,.sprite_flags=SAT_SPRITE_FLAG_HIGH_SPEED_SHRINK};
    SAT_TRY(sat_mesh_init(&a->mesh,a->vertices,T3_VERTEX_COUNT,a->indices,T3_FACE_COUNT));
    __builtin_memcpy(a->vertices,asset->model.vertices,a->vertex_count*sizeof(*a->vertices));
    __builtin_memcpy(a->indices,asset->model.indices,asset->model.face_count*4u*sizeof(*a->indices));
    a->mesh.vertex_count=a->vertex_count;a->mesh.face_count=asset->model.face_count;
    SAT_TRY(sat_skin_validate_mesh(&asset->skin,&a->mesh));
    __builtin_memcpy(a->face_materials,asset->model.face_texture_indices,
        a->mesh.face_count*sizeof(*a->face_materials));
    SAT_TRY(sat_skeleton_instance_init(&a->skeleton,&asset->skeleton,a->nodes,
        T3_JOINT_COUNT,a->scratch,T3_JOINT_COUNT));
    SAT_TRY(sat_skeleton_rest_pose(&asset->skeleton,a->pose,T3_JOINT_COUNT));
    SAT_TRY(sat_pose_fade_init(&a->fade,a->fade_from,T3_JOINT_COUNT));
    SAT_TRY(sat_skel_clip_player_init(&a->clip_player,&a->clips[T3_CLIP_IDLE]));
    a->clip=T3_CLIP_IDLE;
    a->instance.mesh=&a->mesh;a->instance.materials=a->materials;
    a->instance.material_count=a->texture_count;a->instance.face_materials=a->face_materials;
    a->instance.double_sided_faces=pack->double_sided;
    SAT_TRY(sat_scene3d_instance_binding_init(&a->binding,&a->mesh,
        a->materials,a->texture_count,a->instance.face_materials));
    SAT_TRY(sat_scene3d_material_programs_init(&a->material_programs,a->materials,
        a->texture_count,a->material_commands,T3_TEXTURE_COUNT));
    a->binding.programs=&a->material_programs;
    a->instance.validated_binding=&a->binding;
    g_force_pack_index[actor]=(uint8_t)index;
#if T3_FORCE_VARIANT_PROBE
    ++g_force_variant_binds[actor];
#endif
    g_force_pose_ready[actor]=0u;
    return SAT_OK;
}

unsigned t3_characters_force_model(unsigned actor) {
    return actor<ACTOR_COUNT?t3_fighter_pack(g_force_pack_index[actor])->id:255u;
}

sat_result_t t3_characters_force_check_skin(uint32_t* checked) {
    sat_vec3_t expected[T3_VERTEX_COUNT];
    *checked=0;
    for(unsigned i=0;i<T3_DRAW_ACTORS;++i) {
        const actor_t* a=&g_actors[i];
        if(!g_force_shown[i] || !g_force_pose_ready[i])continue;
        SAT_TRY(sat_skin_deform(&a->asset->skin,a->joints,T3_JOINT_COUNT,expected,T3_VERTEX_COUNT));
        for(unsigned j=0;j<a->vertex_count;++j) {
            const sat_vec3_t* actual=&a->mesh.vertices[j];
            if(expected[j].x!=actual->x || expected[j].y!=actual->y || expected[j].z!=actual->z)
                return SAT_ERR_VERSION;
        }
        *checked+=a->vertex_count;
    }
    return SAT_OK;
}

sat_result_t t3_characters_force_reload(void) {
    g_initialized=0;
    __builtin_memset(g_asset_loaded,0,sizeof g_asset_loaded);
    __builtin_memset(g_force_shown,0,sizeof g_force_shown);
    __builtin_memset(g_force_pose_ready,0,sizeof g_force_pose_ready);
    g_texture_vram_bytes=0;
    return t3_characters_init(NULL,NULL);
}
#endif

sat_result_t t3_characters_reset(void) {
    unsigned i;
    sat_result_t st;
    if (!g_initialized) return SAT_ERR_BUSY;
    for (i = 0; i < ACTOR_COUNT; ++i) {
        actor_t* a = &g_actors[i];
        a->clip = T3_CLIP_IDLE;
        g_skin_preview_applied[i]=0u;
        a->last_x = i ? 360 : -360;
        a->last_z = 0;
        if ((st = sat_skel_clip_player_set_clip(&a->clip_player,
                &a->clips[T3_CLIP_IDLE], 0)) != SAT_OK) return st;
        if ((st = sat_skeleton_rest_pose(&a->asset->skeleton,
                a->pose, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_pose_fade_init(&a->fade,
                a->fade_from, T3_JOINT_COUNT)) != SAT_OK) return st;
    }
    return SAT_OK;
}

#if defined(T3_FORCE_PROGRAM)
void t3_characters_set_force_pose(unsigned actor,
        const t3_psx_joint_t world[T3_PSX_DRAW_PARTS], uint32_t parts_mask, int shown) {
    if (actor >= ACTOR_COUNT || !world) return;
    /* The owned match stores joints in original slot order; the KMD skin
     * wants part order (move_probe's publish_owned_pose does the same). */
    const t3_psx_part_t* parts = t3_fighter_pack(T3_PACK_INDEX(actor))->parts;
    for (unsigned p = 0; p < T3_PSX_DRAW_PARTS; ++p) {
        t3_psx_joint_t* out = &g_force_pose[actor][p];
        *out = world[parts[p].slot];
        out->origin.x -= g_force_origin_x;
        out->origin.z -= g_force_origin_z;
    }
    g_force_parts_mask[actor] = parts_mask;
    g_force_shown[actor] = (uint8_t)(shown != 0);
}

void t3_characters_set_force_origin(int32_t x, int32_t z) {
    g_force_origin_x = x;
    g_force_origin_z = z;
}

int t3_characters_force_pose_ready(unsigned actor) {
    return actor < ACTOR_COUNT ? g_force_pose_ready[actor] : 0;
}
#endif

sat_result_t t3_characters_tick(const t3_match_t* game) {
    unsigned i;
    sat_result_t st;
    if (!g_initialized || !game) return SAT_ERR_INVALID_ARG;
    for (i = 0; i < ACTOR_COUNT; ++i) {
#if defined(T3_FORCE_PROGRAM)
        if(!g_force_shown[i])continue;
#endif
        actor_t* a = &g_actors[i];
        const t3_fighter_t* fighter = &game->fighters[i];
        int moved = fighter->x != a->last_x || fighter->z != a->last_z;
        a->last_x = fighter->x;
        a->last_z = fighter->z;
        if ((st = select_clip(a, clip_for(fighter, moved))) != SAT_OK) return st;
        if ((st = sat_skel_clip_player_advance(&a->clip_player,
                DT_Q16, NULL, 0, NULL, NULL)) != SAT_OK) return st;
        sat_pose_fade_advance(&a->fade, DT_Q16);
    }
    return SAT_OK;
}

/* Skinning is presentation work: run once for the final pose submitted to
 * VDP1, rather than again for every catch-up simulation tick. */
static sat_result_t prepare_base_skeleton(actor_t* a) {
    sat_result_t st;
    if ((st = sat_skel_clip_player_sample(&a->clip_player,
            a->pose, T3_JOINT_COUNT)) != SAT_OK) return st;
    if ((st = sat_pose_fade_apply(&a->fade,a->pose)) != SAT_OK) return st;
    if ((st = sat_skeleton_evaluate(&a->skeleton,
            a->pose,T3_JOINT_COUNT)) != SAT_OK) return st;
    return sat_skeleton_world_matrices(&a->skeleton,a->joints,T3_JOINT_COUNT);
}

#if defined(T3_PARALLEL_SKINNING)
typedef struct {
    volatile sat_result_t range_result[2];
#if defined(T3_FORCE_PROGRAM)
    uint16_t vertices[T3_DRAW_ACTORS];
#endif
} skin_ranges_t;

static void skin_actors_range(void* opaque,uint32_t begin,uint32_t end) {
    skin_ranges_t* ranges=(skin_ranges_t*)opaque;
    const unsigned range_index=begin==0u?0u:1u;
    while(begin<end) {
#if defined(T3_FORCE_PROGRAM)
        unsigned actor_index=0;
        uint32_t first=begin;
        while(actor_index<T3_DRAW_ACTORS && first>=ranges->vertices[actor_index])
            first-=ranges->vertices[actor_index++];
        if(actor_index>=T3_DRAW_ACTORS) {
            ranges->range_result[range_index]=SAT_ERR_INVALID_ARG;return;
        }
        uint32_t count=ranges->vertices[actor_index]-first;
#else
        const unsigned actor_index=begin/T3_VERTEX_COUNT;
        const uint32_t first=begin%T3_VERTEX_COUNT;
        uint32_t count=T3_VERTEX_COUNT-first;
#endif
        if(count>end-begin)count=end-begin;
        if(actor_index>=T3_DRAW_ACTORS) {
            ranges->range_result[range_index]=SAT_ERR_INVALID_ARG;
            return;
        }
        actor_t* actor=&g_actors[actor_index];
        sat_result_t st=SAT_OK;
        /* Ranges stride by the largest mesh; a smaller one ends early. */
        const uint32_t used=first>=actor->vertex_count?0u:
            count<actor->vertex_count-first?count:actor->vertex_count-first;
        if(used) {
#if defined(T3_COMPILED_RENDERER)
        if(actor->compiled_active) {
            st=t3_compiled_deform_range(&g_compiled_renderer,&actor->compiled_pose,
                first,count,actor->mesh.vertices,actor->mesh.vertex_cap)?SAT_OK:SAT_ERR_INVALID_ARG;
        } else
#endif
        st=sat_skin_deform_range(&actor->asset->skin,actor->joints,
                T3_JOINT_COUNT,first,used,actor->mesh.vertices,actor->mesh.vertex_cap);
        }
        if(st!=SAT_OK) {
            ranges->range_result[range_index]=st;
            return;
        }
        begin+=count;
    }
}
#endif

#if defined(T3_VERIFY_COMPILED_RENDERER)
static sat_result_t verify_compiled_actors(void) {
    sat_result_t st;
    for(unsigned i=0;i<T3_DRAW_ACTORS;++i) {
        const actor_t* a=&g_actors[i];
        if(!a->compiled_active)continue;
        if((st=sat_skin_deform(&a->asset->skin,g_reference_joints[i],T3_JOINT_COUNT,
                g_reference_vertices,T3_VERTEX_COUNT))!=SAT_OK)return st;
        if(!t3_psx_skin_place(g_reference_vertices,T3_VERTEX_COUNT,
                t3_move_probe_world_center(0),t3_move_probe_world_center(2)))return SAT_ERR_INVALID_ARG;
        for(unsigned v=0;v<T3_VERTEX_COUNT;++v) {
            const sat_vec3_t ref=g_reference_vertices[v],got=a->mesh.vertices[v];
            if(ref.x!=got.x || ref.y!=got.y || ref.z!=got.z)return SAT_ERR_VERIFY_FAILED;
        }
        ++g_t3_compiled_skin_verified;
    }
    return SAT_OK;
}
#endif

static sat_result_t prepare_actors(int defer_skin) {
    sat_result_t st;
    const uint32_t started=sat_time_ms();
    for (unsigned i = 0; i < T3_DRAW_ACTORS; ++i) {
#if defined(T3_FORCE_PROGRAM)
        /* Released/off-screen enemy records retain their simulation pose,
         * but produce no geometry. Do not evaluate and skin a hidden actor. */
        g_force_pose_ready[i]=0u;
        if(!g_force_shown[i])continue;
#endif
        actor_t* a = &g_actors[i];
        T3_PERF_START(skeleton_started);
        g_skin_preview_applied[i]=0u;
#if defined(T3_COMPILED_RENDERER)
        a->compiled_active=0u;
#endif
#if defined(T3_ENABLE_PSX_SKIN_PREVIEW) && defined(T3_LOCAL_KMD_BIND)
        {
            t3_psx_joint_t originals[T3_PSX_DRAW_PARTS];
            uint32_t parts_mask=0u;
            int available=t3_move_probe_copy_world(i,originals,&parts_mask);
#if defined(T3_COMPILED_RENDERER)
            if(available && t3_move_probe_owned() &&
               (parts_mask&T3_PSX_VISIBLE_MASK)==T3_PSX_VISIBLE_MASK &&
               t3_compiled_pose_pack(&g_compiled_renderer,originals,parts_mask,
                   t3_move_probe_world_center(0),t3_move_probe_world_center(2),
                   &a->compiled_pose)) {
                a->compiled_active=1u;
                g_skin_preview_applied[i]=1u;
#if defined(T3_VERIFY_COMPILED_RENDERER)
                if(!t3_psx_skin_bridge(originals,parts_mask,g_reference_joints[i]))return SAT_ERR_INVALID_ARG;
#endif
            } else
#endif
            /* Skip the base skeleton only when the bridge can supply every
             * joint referenced by this immutable skin. Accessory joints keep
             * their generic evaluation; an invalid original pose keeps the
             * complete existing fallback path. */
            if(T3_CLIP_COUNT==1 && available && a->asset->skin.max_joint<T3_PSX_VISIBLE_PARTS &&
               t3_psx_skin_bridge(originals,parts_mask,a->joints)) {
                g_skin_preview_applied[i]=1u;
            } else {
                if((st=prepare_base_skeleton(a))!=SAT_OK)return st;
                if(available && t3_psx_skin_bridge(originals,parts_mask,a->joints))
                    g_skin_preview_applied[i]=1u;
            }
        }
#elif defined(T3_FORCE_PROGRAM)
        /* Every roster fighter keeps the original body pose. Costumes using
         * accessory joints also need their imported local rest transforms;
         * propagate those from the newly animated parents, rather than hiding
         * the whole actor when its skin references a joint above 17. */
        g_force_pose_ready[i]=0u;
        const int accessories=a->asset->skin.max_joint>=T3_PSX_VISIBLE_PARTS;
        if(accessories && (st=prepare_base_skeleton(a))!=SAT_OK)return st;
        if(T3_CLIP_COUNT==1 &&
           t3_psx_skin_bridge(g_force_pose[i],g_force_parts_mask[i],a->joints)) {
            if(accessories)for(unsigned joint=T3_PSX_VISIBLE_PARTS;joint<T3_JOINT_COUNT;++joint) {
                const uint16_t parent=a->asset->skeleton.joints[joint].parent;
                if(parent==SAT_TRANSFORM3D_ROOT)a->joints[joint]=a->nodes[joint].local_matrix;
                else if((st=sat_mat4_multiply(&a->joints[joint],&a->joints[parent],
                                             &a->nodes[joint].local_matrix))!=SAT_OK)return st;
            }
            g_skin_preview_applied[i]=1u;
            g_force_pose_ready[i]=1u;
        }

#else
        if((st=prepare_base_skeleton(a))!=SAT_OK)return st;
#endif
        T3_PERF_ADD(skeleton,skeleton_started);
#if !defined(T3_PARALLEL_SKINNING)
        if (!defer_skin) {
        T3_PERF_START(skin_started);
#if defined(T3_COMPILED_RENDERER)
        if(a->compiled_active) {
            if(!t3_compiled_deform_range(&g_compiled_renderer,&a->compiled_pose,
                    0,T3_VERTEX_COUNT,a->mesh.vertices,a->mesh.vertex_cap))return SAT_ERR_INVALID_ARG;
            a->mesh.vertex_count=T3_VERTEX_COUNT;
        } else
#endif
        if ((st = sat_skin_update_mesh(&a->asset->skin,
                a->joints, T3_JOINT_COUNT, &a->mesh, 0, NULL)) != SAT_OK)
            return st;
        T3_PERF_ADD(skin,skin_started);
        }
#endif
    }
#if defined(T3_PARALLEL_SKINNING)
    if (!defer_skin) {
        T3_PERF_START(skin_started);
        skin_ranges_t ranges={.range_result={SAT_OK,SAT_OK}};
#if defined(T3_FORCE_PROGRAM)
        uint32_t end=0;
        for(unsigned i=0;i<T3_DRAW_ACTORS;++i) {
            ranges.vertices[i]=(g_force_shown[i] && g_force_pose_ready[i])?g_actors[i].vertex_count:0u;
            end+=ranges.vertices[i];
        }
#else
        const uint32_t end=(uint32_t)T3_DRAW_ACTORS*T3_VERTEX_COUNT;
#endif
        if(end && (st=sat_parallel_for(0u,end,128u,skin_actors_range,&ranges,
                g_actor_geometry,(uint32_t)sizeof(g_actor_geometry)))!=SAT_OK)return st;
        if((st=sat_parallel_cache_sync_range(&ranges,sizeof(ranges)))!=SAT_OK)return st;
        if(ranges.range_result[0]!=SAT_OK)return ranges.range_result[0];
        if(ranges.range_result[1]!=SAT_OK)return ranges.range_result[1];
        for(unsigned i=0;i<T3_DRAW_ACTORS;++i)
            g_actors[i].mesh.vertex_count=g_actors[i].vertex_count;
        T3_PERF_ADD(skin,skin_started);
    }
#endif
#if defined(T3_COMPILED_RENDERER)
    unsigned active=0;
    for(unsigned i=0;i<T3_DRAW_ACTORS;++i)active+=g_actors[i].compiled_active;
    if(active==T3_DRAW_ACTORS)++g_t3_compiled_skin_frames;
    else ++g_t3_compiled_skin_fallbacks;
#if defined(T3_VERIFY_COMPILED_RENDERER)
    if(!defer_skin && (st=verify_compiled_actors())!=SAT_OK)return st;
#endif
#endif
    g_skin_work_ms=sat_time_ms()-started;
    return SAT_OK;
}

#if defined(T3_FUSED_ACTOR_PREPARATION)
typedef struct {
    sat_scene_t* scene;
    int32_t center_x,center_z;
    sat_result_t result[2];
    uint32_t geometry_started;
    /* Set through the uncached alias once a started worker's actor 1 is
     * complete; the worker's task then continues with other work. */
    volatile uint32_t worker_ready;
    /* Actor 1's leading vertices, which the Master skins after starting the
     * worker. Skinning to world, the worker skins the rest, then waits for
     * lead_ready (through the uncached alias) before projecting; skinning to
     * screen, it projects the rest and goes straight on to its ticks. */
    uint16_t lead;
    volatile uint32_t lead_ready;
    sat_result_t lead_result;
    /* Byte span of g_actor_geometry[1] the worker may have written this image
     * (note_worker_write, through the uncached alias): the join drops only
     * these lines from the Master's cache. Empty when first >= end. */
    uint32_t written_first,written_end;
    /* Read before the worker and its deferred simulation start. A hidden actor
     * is still skinned, but submits no faces. */
    uint8_t shown[2];
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
    t3_psx_camera_matrix_t original_view;
    t3_psx_joint_t original_joints[2][T3_PSX_DRAW_PARTS];
    uint32_t original_mask[2];
#if defined(T3_ORIGINAL_FIGHTER_OT)
    uint16_t original_first[2],original_after[2];
#endif
#endif
#if defined(T3_FIGHTER_LIGHTING)
    /* The actor whose tables this image relights from original_joints, or 2. */
    uint8_t relight;
#endif
#if defined(T3_FIGHTER_HAND_VARIANTS)
    /* Per actor the variant of each hand channel, and whether its CPU changed one. */
    uint8_t hand_variant[2][2],hands_changed[2];
#endif
#if defined(T3_NATIVE_PHASE_PROBE)
    t3_character_phase_profile_t phase;
#endif
} fused_actor_context_t;
static fused_actor_context_t g_fused_actors __attribute__((section(".wram_l")));
#if defined(T3_MIXED_OT_DIAGNOSTIC)
enum { MIXED_OT_CAP=T3_FACE_COUNT*2u+T3_PSX_EFFECT_CAPACITY };
_Static_assert(sizeof(g_stage_world_scratch[0])>=MIXED_OT_CAP*sizeof(t3_psx_scene_ot_entry_t),"mixed OT entries exceed scratch");
_Static_assert(sizeof(g_stage_world_scratch[1])>=MIXED_OT_CAP*2u*sizeof(uint16_t),"mixed OT order exceeds scratch");
#if defined(T3_FACE_PARTICLE_CENSUS)
static void census_particle_faces(const sat_scene_t* scene,const t3_psx_particle_draw_t* particles,unsigned count);
#endif
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
static sat_result_t ordered_particle_compose(const sat_scene_t* scene,const t3_psx_particle_draw_t* particles,
    unsigned count,const t3_psx_scene_ot_entry_t* entries,const uint16_t* order,unsigned sorted,unsigned first,
    const uint16_t* face_slots,unsigned face_entries);
volatile uint32_t g_t3_ordered_mask_images,g_t3_ordered_mask_fallbacks,g_t3_ordered_mask_particles;
volatile uint32_t g_t3_ordered_mask_drawn_images,g_t3_ordered_mask_drawn_particles;
volatile uint32_t g_t3_ordered_mask_face_failures,g_t3_ordered_mask_particle_failures;
volatile uint32_t g_t3_ordered_mask_dropped_particles;
#if !defined(T3_ORDERED_MASK_SELFTEST) || defined(T3_PARTICLE_80_SELFTEST)
static sat_result_t ordered_particle_result(sat_result_t status,unsigned* drawn) {
    if(status==SAT_OK){++g_t3_ordered_mask_particles;++*drawn;return SAT_OK;}
    if(status==SAT_ERR_CAPACITY || status==SAT_ERR_UNSUPPORTED) {
        /* Both are rejected before the canvas changes, so only this particle
         * is lost, not the image. */
        ++g_t3_ordered_mask_particle_failures;++g_t3_ordered_mask_dropped_particles;
        return SAT_OK;
    }
    return status;
}
#endif
#endif
volatile uint32_t g_t3_mixed_ot_frames,g_t3_mixed_ot_particles,g_t3_mixed_ot_shared,g_t3_mixed_ot_last;
volatile uint32_t g_t3_mixed_ot_verified,g_t3_mixed_ot_peak;
#if defined(T3_EFFECT_RING)
/* W3.2 of docs/POST_VRAM_FRAME_RATE_PLAN.md: the last image's mixed-OT
 * counts (live fighter faces, sorted entries) and the FRT split of the
 * three phases (coverage test, OT sort, compose). Read from a save state. */
volatile uint32_t g_t3_mixed_ot_faces_last,g_t3_mixed_ot_entries_last;
volatile uint32_t g_t3_mixed_ot_frt[3];
/* W3.2 split of the coverage phase: the particle bounds loop (the rest is the
 * face loop), ring field 18. Split of compose: the face masks, ring field 19
 * (the rest is the particle cells and the HUD occlusion). */
volatile uint32_t g_t3_mixed_ot_cover_particles_frt,g_t3_mixed_ot_compose_faces_frt;
/* The faces the mixed OT examined, and those that reached the coverage test
 * (in front of the farthest particle). The rest are dropped by depth alone. */
volatile uint32_t g_t3_mixed_ot_considered_last,g_t3_mixed_ot_depth_last;
#endif
#if defined(T3_MIXED_OT_LIVE_ENTRIES)
/* W3.3 of docs/POST_VRAM_FRAME_RATE_PLAN.md: only the faces that survive
 * the coverage test enter the OT, so the entry list is short. The slot of
 * every live face is kept: the compacted entry index is not the face index. */
static uint16_t g_mixed_ot_face_slots[MIXED_OT_CAP];
#endif
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
/* W3.4 of docs/POST_VRAM_FRAME_RATE_PLAN.md: the compacted entry list must
 * publish the same cells as the full list. Both lists are built, sorted
 * and composed in the same image, in the same field, and the published
 * cells are compared; the compacted image is the one that stays up. */
volatile uint32_t g_t3_live_entries_check_images,g_t3_live_entries_check_mismatch;
volatile uint32_t g_t3_live_entries_check_sum[2],g_t3_live_entries_check_entries[2];
volatile uint32_t g_t3_live_entries_check_faces[2],g_t3_live_entries_check_cells[2];
#endif
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE) && !defined(T3_ORDERED_MASK_SELFTEST) && !defined(T3_FACE_PARTICLE_CENSUS)
volatile uint32_t g_t3_mixed_ot_empty_frames;
#endif
#if defined(T3_PARTICLE_COMPOSE_CHECK)
volatile uint32_t g_t3_compose_check_images,g_t3_compose_check_mismatch;
#endif
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE) && !defined(T3_ORDERED_MASK_SELFTEST) && !defined(T3_FACE_PARTICLE_CENSUS)
enum { PARTICLE_COVER_ROWS=14 }; /* 16-pixel rows; 20 columns fit each word. */
/* Bits for columns 0..n; a table, as the SH-2 shifts a variable count slowly. */
static const uint32_t g_cover_through[20]={
    0x1u,0x3u,0x7u,0xfu,0x1fu,0x3fu,0x7fu,0xffu,0x1ffu,0x3ffu,
    0x7ffu,0xfffu,0x1fffu,0x3fffu,0x7fffu,0xffffu,0x1ffffu,0x3ffffu,0x7ffffu,0xfffffu};
static inline uint32_t cover_columns(int left,int right) {
    const unsigned first=(unsigned)left>>4,last=(unsigned)right>>4;
    return g_cover_through[last]&~(first?g_cover_through[first-1u]:0u);
}
static void particle_coverage_add(uint32_t coverage[PARTICLE_COVER_ROWS],const t3_psx_particle_quad_t* quad) {
    t3_particle_bounds_t b;
    if(!t3_particle_screen_bounds(quad,&b))return;
    const uint32_t columns=cover_columns(b.left,b.right);
    for(int row=b.top>>4;row<=b.bottom>>4;++row)coverage[row]|=columns;
}
#if defined(T3_MIXED_OT_OUTCODES)
/* W3.4 of docs/POST_VRAM_FRAME_RATE_PLAN.md: one outcode per projected
 * vertex against the rectangle of the covered cells. A face whose four
 * vertices share an outside side cannot reach a covered cell, which the full
 * test below would also find; only the other faces take the full test. */
enum { OUTCODE_LEFT=1,OUTCODE_RIGHT=2,OUTCODE_TOP=4,OUTCODE_BOTTOM=8 };
static uint8_t g_mixed_ot_outcodes[2][T3_PROJECTED_VERTEX_COUNT];
#if defined(T3_MIXED_OT_OUTCODES_CHECK)
/* Faces tested, faces the outcodes reject, and rejected faces that reach a
 * covered cell (must stay 0). */
volatile uint32_t g_t3_outcodes_check_faces,g_t3_outcodes_check_rejected,g_t3_outcodes_check_mismatch;
#endif
static void vertex_outcodes(const uint32_t coverage[PARTICLE_COVER_ROWS]) {
    uint32_t columns=0;int top=-1,bottom=-1;
    for(int row=0;row<PARTICLE_COVER_ROWS;++row)if(coverage[row]) {
        columns|=coverage[row];if(top<0)top=row;bottom=row;
    }
    int first=0,last=19;
    while(!(columns&(1u<<first)))++first;
    while(!(columns&(1u<<last)))--last;
    /* Screen pixels, with the +160/+112 centre folded into the bounds. */
    const int left=first*16-160,right=last*16+15-160,up=top*16-112,down=bottom*16+15-112;
    for(unsigned actor=0;actor<2u;++actor) {
        const sat_projected_vertex_t* p=g_actors[actor].projected;uint8_t* out=g_mixed_ot_outcodes[actor];
        for(unsigned v=0,n=g_actors[actor].original_mesh.vertex_count;v<n;++v) {
            const int x=p[v].x,y=p[v].y;
            out[v]=(uint8_t)((x<left?OUTCODE_LEFT:0)|(x>right?OUTCODE_RIGHT:0)|
                             (y<up?OUTCODE_TOP:0)|(y>down?OUTCODE_BOTTOM:0));
        }
    }
}
#endif
/* entry is the face's index in insertion order, as in the composition. */
static int face_reaches_particles(const uint32_t coverage[PARTICLE_COVER_ROWS],unsigned entry,unsigned split) {
    const unsigned actor=entry<split?0u:1u,f=entry<split?entry:entry-split;
    const actor_t* a=&g_actors[actor];const uint16_t* index=&a->original_mesh.indices[4*f];
#if defined(T3_MIXED_OT_OUTCODES)
    const uint8_t* oc=g_mixed_ot_outcodes[actor];
#if defined(T3_MIXED_OT_OUTCODES_CHECK)
    /* The full test still decides; a face the outcodes reject but the full
     * test lets through is a mismatch. */
    const int rejected=(oc[index[0]]&oc[index[1]]&oc[index[2]]&oc[index[3]])!=0;
    ++g_t3_outcodes_check_faces;g_t3_outcodes_check_rejected+=(uint32_t)rejected;
#else
    if(oc[index[0]]&oc[index[1]]&oc[index[2]]&oc[index[3]])return 0;
#endif
#endif
    const sat_projected_vertex_t* p=a->projected;
    int left=p[index[0]].x+160,right=left,top=p[index[0]].y+112,bottom=top;
    for(unsigned j=1;j<4;++j) {
        const int x=p[index[j]].x+160,y=p[index[j]].y+112;
        if(x<left)left=x;
        else if(x>right)right=x;
        if(y<top)top=y;
        else if(y>bottom)bottom=y;
    }
    if(right<0 || bottom<0 || left>319 || top>223)return 0;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>319)right=319;
    if(bottom>223)bottom=223;
    const uint32_t columns=cover_columns(left,right);
    for(int row=top>>4;row<=bottom>>4;++row)if(coverage[row]&columns) {
#if defined(T3_MIXED_OT_OUTCODES_CHECK)
        g_t3_outcodes_check_mismatch+=(uint32_t)rejected;
#endif
        return 1;
    }
    return 0;
}
#endif
sat_result_t t3_characters_mixed_ot(const sat_scene_t* scene,
    const t3_psx_particle_draw_t* particles,unsigned count) {
    if(!scene || (count && !particles) || count>T3_PSX_EFFECT_CAPACITY)return SAT_ERR_INVALID_ARG;
    if(!g_actors[0].original_active || !g_actors[1].original_active ||
       g_fused_actors.original_after[0]!=g_fused_actors.original_first[1]) {
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
        SAT_TRY(t3_stage_particle_multi_begin(g_t3_prepared_particles.frame_counter));
        t3_stage_particle_multi_end();++g_t3_ordered_mask_fallbacks;
#endif
#if defined(T3_EFFECT_RING)
        g_t3_mixed_ot_faces_last=0u;g_t3_mixed_ot_entries_last=0u;
        g_t3_mixed_ot_frt[0]=0u;g_t3_mixed_ot_frt[1]=0u;g_t3_mixed_ot_frt[2]=0u;
#endif
        return SAT_OK;
    }
    unsigned first=g_fused_actors.original_first[0],end=g_fused_actors.original_after[1];
    if(first>end || end>scene->faces.count || end-first+count>MIXED_OT_CAP)return SAT_ERR_INVALID_ARG;
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE) && !defined(T3_ORDERED_MASK_SELFTEST) && !defined(T3_FACE_PARTICLE_CENSUS)
    /* No visible particle can be occluded. Publish an empty image so that
     * the previous overlay is still cleared, without sorting fighter faces. */
    unsigned any_visible=0;
    for(unsigned i=0;i<count;++i)any_visible|=particles[i].quad.visible!=0;
    if(!any_visible) {
        SAT_TRY(t3_stage_particle_multi_begin(g_t3_prepared_particles.frame_counter));
        t3_stage_particle_multi_end();
        ++g_t3_ordered_mask_images;++g_t3_mixed_ot_frames;++g_t3_mixed_ot_empty_frames;
        g_t3_mixed_ot_last=0;
#if defined(T3_EFFECT_RING)
        g_t3_mixed_ot_faces_last=0u;g_t3_mixed_ot_entries_last=0u;
        g_t3_mixed_ot_frt[0]=0u;g_t3_mixed_ot_frt[1]=0u;g_t3_mixed_ot_frt[2]=0u;
#endif
        return SAT_OK;
    }
#endif
#if defined(T3_FACE_PARTICLE_CENSUS)
    census_particle_faces(scene,particles,count);
#endif
    t3_psx_scene_ot_entry_t* entries=(t3_psx_scene_ot_entry_t*)(void*)g_stage_world_scratch[0];
    uint16_t* order=(uint16_t*)(void*)g_stage_world_scratch[1];uint16_t* next=order+MIXED_OT_CAP;
    unsigned n=0,visible=0,expected=0,shared=0;
    unsigned considered=0,depth_live=0;
#if defined(T3_EFFECT_RING)
    const uint16_t coverage_started=sat_time_frc();
#endif
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE) && !defined(T3_ORDERED_MASK_SELFTEST) && !defined(T3_FACE_PARTICLE_CENSUS)
    /* Faces that cannot change a particle pixel stay out of the sort: those
     * entirely behind the farthest particle, which the composition skips, and
     * those that overlap no particle's bounds, at 16-pixel resolution. */
    uint32_t coverage[PARTICLE_COVER_ROWS]={0};int farthest=-1;
#if defined(T3_EFFECT_RING)
    const uint16_t cover_particles_started=sat_time_frc();
#endif
    for(unsigned i=0;i<count;++i)if(particles[i].quad.visible) {
        if(particles[i].quad.bucket>farthest)farthest=particles[i].quad.bucket;
        particle_coverage_add(coverage,&particles[i].quad);
    }
#if defined(T3_MIXED_OT_OUTCODES)
    /* any_visible holds, so a visible particle covered a cell unless all of
     * them are off screen; then no face can reach one. */
    int covered=0;
    for(int row=0;row<PARTICLE_COVER_ROWS;++row)covered|=coverage[row]!=0;
    if(covered)vertex_outcodes(coverage);
    else farthest=-1;
#endif
#if defined(T3_EFFECT_RING)
    g_t3_mixed_ot_cover_particles_frt=(uint16_t)(sat_time_frc()-cover_particles_started);
#endif
    const unsigned split_at=g_fused_actors.original_after[0]-first;
#endif
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK) || defined(T3_PARTICLE_COMPOSE_CHECK)
    /* Two passes over the same image, in the same field. The first is the
     * reference and the second the variant under test; the second is the one
     * that stays on screen, so the check does not change the published image.
     *  - T3_PARTICLE_COMPOSE_CHECK: the reference lets every face through, the
     *    second applies the coverage test.
     *  - T3_MIXED_OT_LIVE_ENTRIES_CHECK: the reference is the full entry list
     *    (every face, with its live flag), the second is the compacted list
     *    (only the live faces). */
    uint32_t reference_sum=0;
    for(int pass=0;pass<2;++pass) {
    const int filter_faces=
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
        1;
#else
        (pass==0)?0:1;
#endif
    const int compact=
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
        (pass!=0);
#else
        0;
#endif
    n=0;visible=0;expected=0;considered=0;depth_live=0;
#else
    const int filter_faces=1;
#if defined(T3_MIXED_OT_LIVE_ENTRIES)
    const int compact=1;
#else
    const int compact=0;
#endif
#endif
    (void)filter_faces;(void)compact;
    /* The fighter queue was reversed for the SDK; restore insertion order. */
    unsigned face_index=0;
    for(unsigned i=end;i-->first;++face_index) {
        const uint32_t key=scene->faces.keys[i];
        uint8_t live=(uint8_t)(key!=0xffffffffu);
        ++considered;
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE) && !defined(T3_ORDERED_MASK_SELFTEST) && !defined(T3_FACE_PARTICLE_CENSUS)
        if(filter_faces&&live) {
            if((int)(key&1023u)>farthest)live=0;
            else {
                ++depth_live;
                if(!face_reaches_particles(coverage,face_index,split_at))live=0;
            }
        }
#endif
#if defined(T3_MIXED_OT_LIVE_ENTRIES)
        /* Only the faces that can change a particle pixel enter the sort.
         * Their relative order is the insertion order, so the chain the
         * sort builds is the same one the full list would build. */
        if(compact) {
            if(live) {
                g_mixed_ot_face_slots[n]=(uint16_t)face_index;
                entries[n++]=(t3_psx_scene_ot_entry_t){(uint16_t)(key&1023u),1u,0u};
                ++expected;
            }
        } else
#endif
        {
            entries[n++]=(t3_psx_scene_ot_entry_t){(uint16_t)(key&1023u),live,0};
            expected+=live;
        }
    }
    const unsigned face_entries=n;
    for(unsigned i=0;i<count;++i) {
        const t3_psx_particle_quad_t* q=&particles[i].quad;
        if(q->visible && (q->bucket<0 || q->bucket>1023))return SAT_ERR_INVALID_ARG;
        entries[n++]=(t3_psx_scene_ot_entry_t){(uint16_t)q->bucket,q->visible,1};visible+=q->visible!=0;
    }
#if defined(T3_EFFECT_RING)
    /* The coverage phase ends here: the face loop and its per-face bounds. */
    g_t3_mixed_ot_frt[0]=(uint16_t)(sat_time_frc()-coverage_started);
    g_t3_mixed_ot_faces_last=expected;g_t3_mixed_ot_entries_last=n;
    g_t3_mixed_ot_considered_last=considered;g_t3_mixed_ot_depth_last=depth_live;
    const uint16_t sort_started=sat_time_frc();
#endif
    int sorted=t3_psx_scene_ot_order(entries,n,order,next);if(sorted<0)return SAT_ERR_INVALID_ARG;
#if defined(T3_EFFECT_RING)
    g_t3_mixed_ot_frt[1]=(uint16_t)(sat_time_frc()-sort_started);
#endif
    expected+=visible;if((unsigned)sorted!=expected)return SAT_ERR_VERIFY_FAILED;
    shared=0;
    for(int i=1;i<sorted;++i) {
        const t3_psx_scene_ot_entry_t* a=&entries[order[i-1]],*b=&entries[order[i]];
        if(a->bucket<b->bucket || (a->bucket==b->bucket && order[i-1]<=order[i]))return SAT_ERR_VERIFY_FAILED;
        if(a->bucket==b->bucket && a->kind!=b->kind)++shared;
    }
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
#if defined(T3_EFFECT_RING)
    const uint16_t compose_started=sat_time_frc();
#endif
    SAT_TRY(ordered_particle_compose(scene,particles,count,entries,order,(unsigned)sorted,first,
#if defined(T3_MIXED_OT_LIVE_ENTRIES)
        compact?g_mixed_ot_face_slots:NULL,
#else
        NULL,
#endif
        face_entries
        ));
#if defined(T3_EFFECT_RING)
    g_t3_mixed_ot_frt[2]=(uint16_t)(sat_time_frc()-compose_started);
#endif
#endif
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK) || defined(T3_PARTICLE_COMPOSE_CHECK)
    /* The checksum covers every cell's position and every pixel of it, in
     * list order: equal checksums mean the published cells are identical. */
    const uint32_t sum=t3_stage_particle_multi_checksum();
    if(pass==0) {
        reference_sum=sum;
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
        g_t3_live_entries_check_sum[0]=sum;g_t3_live_entries_check_entries[0]=n;
        g_t3_live_entries_check_faces[0]=expected-visible;
        g_t3_live_entries_check_cells[0]=t3_stage_particle_multi_cell_count();
#endif
    } else {
#if defined(T3_PARTICLE_COMPOSE_CHECK)
        ++g_t3_compose_check_images;
        if(sum!=reference_sum)++g_t3_compose_check_mismatch;
#endif
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
        ++g_t3_live_entries_check_images;
        g_t3_live_entries_check_sum[1]=sum;g_t3_live_entries_check_entries[1]=n;
        g_t3_live_entries_check_faces[1]=expected-visible;
        g_t3_live_entries_check_cells[1]=t3_stage_particle_multi_cell_count();
        if(sum!=reference_sum)++g_t3_live_entries_check_mismatch;
#endif
    }
    }
#endif
    ++g_t3_mixed_ot_frames;g_t3_mixed_ot_particles+=visible;g_t3_mixed_ot_shared+=shared;
    g_t3_mixed_ot_last=visible;++g_t3_mixed_ot_verified;
    if(visible>g_t3_mixed_ot_peak)g_t3_mixed_ot_peak=visible;
    return SAT_OK;
}
#endif
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
static sat_result_t ordered_particle_compose(const sat_scene_t* scene,const t3_psx_particle_draw_t* particles,
    unsigned count,const t3_psx_scene_ot_entry_t* entries,const uint16_t* order,unsigned sorted,unsigned first,
    const uint16_t* face_slots,unsigned face_entries) {
    (void)scene;
#if defined(T3_ORDERED_MASK_SELFTEST)
    (void)particles;(void)count;(void)entries;(void)order;(void)sorted;(void)first;
    (void)face_slots;(void)face_entries;
#if defined(T3_PARTICLE_80_SELFTEST)
    uint8_t pattern[128];uint16_t palette[16]={0};
    for(unsigned i=0;i<128;++i)pattern[i]=0x11u;
    palette[1]=0x8001u;
    SAT_TRY(t3_stage_particle_multi_begin(g_t3_prepared_particles.frame_counter));
    unsigned drawn=0;
    for(int i=0;i<81;++i) {
        int y=i/10,x=i%10;
        int left=0,right=0;
        while(t3_psx_viewport_x(left)<x*8)++left;
        while(t3_psx_viewport_x(right)<(x+1)*8)++right;
        t3_psx_particle_quad_t q={0};q.visible=1;
        q.xy[0][0]=q.xy[2][0]=(int16_t)left;q.xy[1][0]=q.xy[3][0]=(int16_t)right;
        q.xy[0][1]=q.xy[1][1]=(int16_t)(20+y*16);q.xy[2][1]=q.xy[3][1]=(int16_t)(36+y*16);
        SAT_TRY(ordered_particle_result(t3_stage_particle_multi_add(&q,pattern,palette),&drawn));
    }
    /* An overflow must not prevent later particles from using existing cells. */
    t3_psx_particle_quad_t reuse={0};reuse.visible=1;
    reuse.xy[0][0]=reuse.xy[2][0]=0;
    int right=0;while(t3_psx_viewport_x(right)<8)++right;
    reuse.xy[1][0]=reuse.xy[3][0]=(int16_t)right;
    reuse.xy[0][1]=reuse.xy[1][1]=20;reuse.xy[2][1]=reuse.xy[3][1]=36;
    palette[1]=0x8002u;
    SAT_TRY(ordered_particle_result(t3_stage_particle_multi_add(&reuse,pattern,palette),&drawn));
    t3_stage_particle_multi_end();++g_t3_ordered_mask_images;return SAT_OK;
#else
    uint8_t pattern[128],pixels[64],packed[32];uint16_t palette[16]={0};
    for(unsigned i=0;i<128;++i)pattern[i]=0x11u;
#if defined(T3_ORDERED_MASK_TRI_SELFTEST)
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)pixels[y*8+x]=(uint8_t)(x==3);
#elif defined(T3_ORDERED_MASK_QUAD_SELFTEST)
    static const uint8_t rows[8]={0xff,0x81,0xb9,0xa9,0xb9,0x81,0xa9,0xff};
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)pixels[y*8+x]=(uint8_t)((rows[y]>>(7-x))&1u);
#else
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)pixels[y*8+x]=(uint8_t)(((x+y)&1u)!=0);
#endif
    for(unsigned i=0;i<32;++i)packed[i]=(uint8_t)((pixels[2*i]<<4)|pixels[2*i+1]);
    const int lut4=(g_t3_ordered_mask_images&1u)!=0;
    const sat_model_texture_asset_t texture={lut4?packed:pixels,8,8,0,lut4?SAT_MODEL_TEXTURE_LUT4:0,64};
    t3_psx_particle_quad_t q={0};q.visible=1;
#if defined(T3_ORDERED_MASK_TRI_SELFTEST)
    q.xy[0][0]=q.xy[2][0]=28;q.xy[1][0]=q.xy[3][0]=46;
    q.xy[0][1]=q.xy[1][1]=52;q.xy[2][1]=q.xy[3][1]=84;
    const int16_t x[4]={30,36,33,33},y[4]={22,18,29,29};
#elif defined(T3_ORDERED_MASK_QUAD_SELFTEST)
    q.xy[0][0]=q.xy[2][0]=28;q.xy[1][0]=q.xy[3][0]=64;
    q.xy[0][1]=q.xy[1][1]=160;q.xy[2][1]=q.xy[3][1]=220;
    const int16_t x[4]={20,52,56,24},y[4]={70,75,100,95};
#else
    q.xy[0][0]=q.xy[2][0]=92;q.xy[1][0]=q.xy[3][0]=101;
    q.xy[0][1]=q.xy[1][1]=148;q.xy[2][1]=q.xy[3][1]=164;
    const int16_t x[4]={80,87,87,80},y[4]={64,64,71,71};
#endif
    SAT_TRY(t3_stage_particle_multi_begin(g_t3_prepared_particles.frame_counter));
    palette[1]=0x8004u;SAT_TRY(t3_stage_particle_multi_add(&q,pattern,palette));
    SAT_TRY(t3_stage_particle_multi_face(&texture,x,y));
    palette[1]=0x8080u;SAT_TRY(t3_stage_particle_multi_add(&q,pattern,palette));
    t3_stage_particle_multi_end();++g_t3_ordered_mask_images;return SAT_OK;
#endif
#else
    const unsigned faces=face_entries;
    const unsigned split=g_fused_actors.original_after[0]-first;
    unsigned drawn=0;
#if defined(T3_EFFECT_RING)
    uint32_t face_frt=0;g_t3_mixed_ot_compose_faces_frt=0;
#endif
    SAT_TRY(t3_stage_particle_multi_begin(g_t3_prepared_particles.frame_counter));
    for(unsigned k=0;k<sorted;++k) {
        const unsigned entry=order[k];sat_result_t st;
        if(entries[entry].kind) {
            if(entry<faces || entry-faces>=count)return SAT_ERR_INVALID_ARG;
            st=ordered_particle_result(t3_particle_prepare_add(&particles[entry-faces]),&drawn);
        } else {
            if(entry>=faces)return SAT_ERR_INVALID_ARG;
            /* Before the first particle, the freshly begun overlay has no
             * cells to occlude. Keep the sorted traversal but skip the face. */
            if(!drawn)continue;
            /* The compacted entry list keeps the face slot of every live
             * face: the entry index is not the face index. */
            const unsigned slot=face_slots?face_slots[entry]:entry;
            const unsigned actor=slot<split?0u:1u,f=slot<split?slot:slot-split;
            const actor_t* a=&g_actors[actor];const uint16_t* index=&a->original_mesh.indices[4*f];
            int16_t x[4],y[4];
            for(unsigned j=0;j<4;++j){x[j]=(int16_t)(a->projected[index[j]].x+160);y[j]=(int16_t)(a->projected[index[j]].y+112);}
            const unsigned material=a->instance.face_materials[f];
            const sat_model_texture_asset_t* texture=material<a->asset->model.texture_count?&a->asset->model.textures[material]:NULL;
#if defined(T3_EFFECT_RING)
            const uint16_t face_started=sat_time_frc();
#endif
            st=t3_stage_particle_multi_face(texture,x,y);
#if defined(T3_EFFECT_RING)
            face_frt+=(uint16_t)(sat_time_frc()-face_started);
#endif
        }
        if(st!=SAT_OK) {
            if(entries[entry].kind)++g_t3_ordered_mask_particle_failures;
            else ++g_t3_ordered_mask_face_failures;
            /* Never publish a partially composed overlay. A later frame may
             * become fully supported; reset the same free slot to blank. */
            SAT_TRY(t3_stage_particle_multi_begin(g_t3_prepared_particles.frame_counter));
            t3_stage_particle_multi_end();++g_t3_ordered_mask_fallbacks;return SAT_OK;
        }
    }
#if defined(T3_EFFECT_RING)
    g_t3_mixed_ot_compose_faces_frt=face_frt;
#endif
    if(drawn)SAT_TRY(t3_original_hud_occlude_particles());
    t3_stage_particle_multi_end();++g_t3_ordered_mask_images;
    if(drawn){++g_t3_ordered_mask_drawn_images;g_t3_ordered_mask_drawn_particles+=drawn;}
    return SAT_OK;
#endif
}
#endif
volatile uint32_t g_t3_fused_actor_frames;
#if defined(T3_NATIVE_PHASE_PROBE)
static t3_character_phase_profile_t g_completed_actor_phase;
uint32_t g_t3_phase_sums[sizeof(t3_character_phase_profile_t)/sizeof(uint16_t)],g_t3_phase_samples;
#endif

#if defined(T3_SKIN_TO_SCREEN)
#if T3_MASTER_SKIN_PERCENT && !defined(T3_MASTER_WORKER_FACES)
#error "a Master share of actor 1's screen projection needs T3_MASTER_WORKER_FACES (the Master submits its faces)"
#endif
#if defined(T3_COMPILED_RENDERER)
#error "T3_SKIN_TO_SCREEN skins through the SDK; the compiled renderer writes world vertices"
#endif
/* Each actor's joints times the placement and the camera, written and read by
 * the CPU that prepares the actor. */
static sat_mat4_t g_clip_joints[ACTOR_COUNT][T3_JOINT_COUNT]
#if defined(T3_ORIGINAL_FIGHTER_OT) && defined(T3_PARTICLE_PREPARE)
    __attribute__((section(".wram_l")))
#endif
    ;
volatile uint32_t g_t3_skin_screen_fallbacks;
#endif

#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
volatile uint32_t g_t3_original_projected[2],g_t3_original_projected_fallbacks[2];
#if defined(T3_ORIGINAL_FIGHTER_OT)
volatile uint32_t g_t3_original_ot_frames,g_t3_original_ot_faces,g_t3_original_ot_culled;
#if defined(T3_FACE_COVERAGE_CENSUS)
/* Per actor: visible faces, supported rectangles, supported triangles, other. */
volatile uint32_t g_t3_face_coverage_census[2][4] __attribute__((section(".wram_l")));
volatile uint32_t g_t3_face_parallelograms[2] __attribute__((section(".wram_l")));
volatile uint32_t g_t3_face_general_quads[2] __attribute__((section(".wram_l")));
static void census_coverage_face(unsigned actor,const actor_t* a,const uint16_t* index,uint8_t group) {
    const sat_projected_vertex_t* p=a->projected;
    int x[4],y[4];for(unsigned i=0;i<4;++i){x[i]=p[index[i]].x;y[i]=p[index[i]].y;}
    unsigned cls=3;int height=y[2]-y[0];
    if(x[0]==x[3] && x[1]==x[2] && y[0]==y[1] && y[2]==y[3] && x[0]<=x[1] && height>=0)cls=1;
    else if(!(group&1u) && y[0]==y[1] && x[2]==x[3] && y[2]==y[3] && x[0]<=x[1] && height>=0) {
        int left=x[2]-x[0],right=x[2]-x[1];
        if(left>=-height && left<=height && right>=-height && right<=height)cls=2;
    }
    if(cls==3) {
        int across=x[1]-x[0],slope=y[1]-y[0],edge_x=x[3]-x[0],edge_y=y[3]-y[0];
        int major=edge_y<0?-edge_y:edge_y;
        if(x[2]-x[1]==edge_x && y[2]-y[1]==edge_y && across>=0 && across<=64 &&
           slope>=0 && slope<=across && edge_y>=0 && major<=64 && edge_x>=-major && edge_x<=major)
            ++g_t3_face_parallelograms[actor];
        else {
            int16_t sx[4],sy[4];
            for(unsigned i=0;i<4;++i){sx[i]=(int16_t)(x[i]+160);sy[i]=(int16_t)(y[i]+112);}
            if(t3_face_quad_supported(sx,sy))++g_t3_face_general_quads[actor];
        }
    }
    ++g_t3_face_coverage_census[actor][0];++g_t3_face_coverage_census[actor][cls];
 }
#if defined(T3_FACE_PARTICLE_CENSUS)
volatile uint32_t g_t3_particle_census_visible[2],g_t3_particle_census_candidates[2];
volatile uint32_t g_t3_particle_census_frames;
/* First encountered overlapping faces, in VDP1 perimeter order. The final
 * two values identify actor and source material group. Diagnostic only. */
volatile int16_t g_t3_particle_face_examples[32][10];
volatile uint32_t g_t3_particle_face_example_count;
static void census_particle_faces(const sat_scene_t* scene,const t3_psx_particle_draw_t* particles,unsigned count) {
    t3_particle_bounds_t bounds[T3_PSX_EFFECT_CAPACITY];unsigned n=0;
    for(unsigned i=0;i<count;++i)
        if(t3_particle_screen_bounds(&particles[i].quad,&bounds[n]))++n;
    if(!n)return;
    ++g_t3_particle_census_frames;
    const unsigned first=g_fused_actors.original_first[0],end=g_fused_actors.original_after[1];
    for(unsigned actor=0;actor<2;++actor) {
        const actor_t* a=&g_actors[actor];const uint16_t* indices=a->original_mesh.indices;
        const uint8_t (*meta)[2]=ACTOR_FACE_OT(actor);
        for(unsigned f=0;f<a->original_mesh.face_count;++f) {
            unsigned at=end-1u-(g_fused_actors.original_first[actor]+f-first);
            if(scene->faces.keys[at]==0xffffffffu)continue;
            ++g_t3_particle_census_visible[actor];
            int left=32767,right=-32768,top=32767,bottom=-32768;
            for(unsigned i=0;i<4;++i) {
                const sat_projected_vertex_t* p=&a->projected[indices[4*f+i]];
                int x=p->x+160,y=p->y+112;
                if(x<left)left=x;
                if(x>right)right=x;
                if(y<top)top=y;
                if(y>bottom)bottom=y;
            }
            if(right<0 || bottom<0 || left>319 || top>223)continue;
            if(left<0)left=0;
            if(right>319)right=319;
            if(top<0)top=0;
            if(bottom>223)bottom=223;
            t3_particle_bounds_t face={(int16_t)left,(int16_t)top,(int16_t)right,(int16_t)bottom};
            for(unsigned i=0;i<n;++i)if(t3_particle_bounds_intersect(&face,&bounds[i])) {
                ++g_t3_particle_census_candidates[actor];
                if(g_t3_particle_face_example_count<32u) {
                    unsigned sample=g_t3_particle_face_example_count++;
                    for(unsigned v=0;v<4;++v) {
                        const sat_projected_vertex_t* p=&a->projected[indices[4*f+v]];
                        g_t3_particle_face_examples[sample][2*v]=(int16_t)(p->x+160);
                        g_t3_particle_face_examples[sample][2*v+1]=(int16_t)(p->y+112);
                    }
                    g_t3_particle_face_examples[sample][8]=(int16_t)actor;
                    g_t3_particle_face_examples[sample][9]=(int16_t)meta[f][0];
                }
                census_coverage_face(actor,a,&indices[4*f],meta[f][0]);break;
            }
        }
    }
}
#endif
#endif
static int apply_original_fighter_ot(sat_scene_t* scene,const fused_actor_context_t* context) {
    sat_scene3d_faces_t* queue=&scene->faces;
    if(!queue->projected_indices || !queue->keys)return 0;
    for(unsigned actor=0;actor<2;++actor) {
        const actor_t* a=&g_actors[actor];
        if(a->original_active && (unsigned)(context->original_after[actor]-context->original_first[actor])!=a->original_mesh.face_count)
            return 0;
    }
    /* The SDK sorts unsigned keys high to low. Convert non-fighter depths
     * into the same approximately scaled 0..1023 range as the PS1 OT. */
    for(unsigned i=0;i<queue->count;++i) {
        uint32_t key=queue->keys[i];if(key==0xffffffffu)continue;
        uint32_t depth=(key&0xffffffu)/192u;if(depth>1023u)depth=1023u;
        queue->keys[i]=(key&0xff000000u)|depth;
    }
    for(unsigned actor=0;actor<2;++actor) {
        const actor_t* a=&g_actors[actor];if(!a->original_active)continue;
        const t3_psx_projected_vertex_t* raw=(const t3_psx_projected_vertex_t*)(const void*)a->world_vertices;
        const uint8_t (*meta)[2]=ACTOR_FACE_OT(actor);
        const uint16_t* indices=a->original_mesh.indices;
        for(unsigned f=0;f<a->original_mesh.face_count;++f) {
            const int32_t nclip=t3_psx_fighter_native_indexed_nclip(raw,&indices[4*f]);
            const unsigned at=context->original_first[actor]+f;
            if(!(meta[f][1]&1u) && nclip<=0) {
                queue->keys[at]=0xffffffffu;++g_t3_original_ot_culled;
            } else {
                const uint16_t bucket=t3_psx_fighter_ot_bucket((uint16_t)raw[indices[4*f]].z,
                    (uint16_t)raw[indices[4*f+2]].z,(uint32_t)meta[f][1]<<24,nclip);
                queue->keys[at]=(queue->keys[at]&0xff000000u)|bucket;
                ++g_t3_original_ot_faces;
#if defined(T3_FACE_COVERAGE_CENSUS) && !defined(T3_FACE_PARTICLE_CENSUS)
                census_coverage_face(actor,a,&indices[4*f],meta[f][0]);
#endif
            }
        }
    }
    /* Original OT insertion is LIFO. Reverse both consecutive fighter runs,
     * retaining the logical-to-physical map and leaving earlier stage faces. */
    if(g_actors[0].original_active && g_actors[1].original_active &&
       context->original_after[0]==context->original_first[1]) {
        unsigned lo=context->original_first[0],hi=context->original_after[1];
        while(lo<hi && lo<--hi) {
            uint32_t key=queue->keys[lo];queue->keys[lo]=queue->keys[hi];queue->keys[hi]=key;
            uint16_t index=queue->projected_indices[lo];queue->projected_indices[lo]=queue->projected_indices[hi];queue->projected_indices[hi]=index;
            ++lo;
        }
    } else for(unsigned actor=0;actor<2;++actor)if(g_actors[actor].original_active) {
        unsigned lo=context->original_first[actor],hi=context->original_after[actor];
        while(lo<hi && lo<--hi) {
            uint32_t key=queue->keys[lo];queue->keys[lo]=queue->keys[hi];queue->keys[hi]=key;
            uint16_t index=queue->projected_indices[lo];queue->projected_indices[lo]=queue->projected_indices[hi];queue->projected_indices[hi]=index;
            ++lo;
        }
    }
    ++g_t3_original_ot_frames;return 1;
}
#endif
static void original_projection_disable(actor_t* actor) {
    actor->original_active=0u;actor->instance.mesh=&actor->mesh;
    actor->instance.validated_binding=&actor->binding;
    actor->projected=g_actor_geometry[actor-g_actors].projected;
#if defined(T3_ORIGINAL_FIGHTER_OT)
    actor->instance.cull_backfaces=SAT_SCENE3D_CULL_SCREEN;
#endif
}
/* Before a write by actor 1's worker into g_actor_geometry[1], widens the span
 * the join invalidates. Writes outside that array were never in the join's
 * sync, and other actors' writes stay on their own CPU. */
static void note_worker_write(const fused_actor_context_t* context,const actor_t* actor,
        const void* p,uint32_t bytes) {
    if(actor!=&g_actors[1])return;
    const uintptr_t base=(uintptr_t)&g_actor_geometry[1],at=(uintptr_t)p;
    if(at<base || at-base>=sizeof(g_actor_geometry[1]))return;
    const uint32_t first=(uint32_t)(at-base);
    const uint32_t end=bytes<sizeof(g_actor_geometry[1])-first?first+bytes:(uint32_t)sizeof(g_actor_geometry[1]);
    volatile fused_actor_context_t* shared=sat_parallel_uncached_address((void*)(uintptr_t)context);
    if(first<shared->written_first)shared->written_first=first;
    if(end>shared->written_end)shared->written_end=end;
}

static int project_original(const fused_actor_context_t* context,actor_t* actor) {
    const unsigned n=(unsigned)(actor-g_actors);
    /* The worker can retain cached data from the previous image. Read the
     * published camera and joint snapshot through the shared alias. */
    context=sat_parallel_uncached_address(context);
#if defined(T3_FIGHTER_PACKS)
    const t3_psx_projected_part_t* parts=t3_fighter_pack(T3_PACK_INDEX(n))->projected;
    unsigned count=t3_fighter_pack(T3_PACK_INDEX(n))->projected_count;
#else
    const t3_psx_projected_part_t* parts=t3_kmd_projected;
    unsigned count=t3_kmd_projected_count;
#if defined(T3_SECOND_MESH)
    if(n==1u){parts=t3_kmd_projected1;count=t3_kmd_projected1_count;}
#endif
#endif
    actor_geometry_t* geometry=&g_actor_geometry[n];
#if defined(T3_ORIGINAL_FIGHTER_OT)
    t3_psx_projected_vertex_t* raw=(t3_psx_projected_vertex_t*)(void*)actor->world_vertices;
#else
    t3_psx_projected_vertex_t* raw=geometry->original_raw;
#endif
    note_worker_write(context,actor,raw,actor->original_mesh.vertex_count*(uint32_t)sizeof(*raw));
    note_worker_write(context,actor,geometry->projected,actor->original_mesh.vertex_count*(uint32_t)sizeof(*geometry->projected));
    if(!t3_psx_projected_mesh_build(parts,count,&context->original_view,context->original_joints[n],
            context->original_mask[n],raw,actor->original_mesh.vertex_count))return 0;
    for(unsigned i=0;i<actor->original_mesh.vertex_count;++i) {
        const t3_psx_projected_vertex_t* p=&raw[i];
        const int32_t x=(int16_t)(p->xy&0xffffu),y=(int16_t)(p->xy>>16);
        const int32_t z=p->z;
        geometry->projected[i].x=(int16_t)(t3_psx_viewport_x(x)-160);
        geometry->projected[i].y=(int16_t)(t3_psx_viewport_y(y)-112);
        geometry->projected[i].w=z*3072;
    }
    volatile uint32_t* counters=sat_parallel_uncached_address((void*)g_t3_original_projected);
    ++counters[n];
    return 1;
}
#endif

/* Skins vertices [first, end) of the actor (a compiled mesh is skinned whole).
 * With T3_SKIN_TO_SCREEN and instance.pre_projected set, it writes projected
 * vertices instead, as the original's RTPT does with each joint and the
 * camera concatenated; if the concatenation is refused it clears
 * pre_projected and skins to world. */
static sat_result_t deform_actor(const fused_actor_context_t* context,actor_t* actor,
        uint32_t first,uint32_t end) {
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
    if(actor->original_active) {
        if(first==0 && end==actor->vertex_count && project_original(context,actor))return SAT_OK;
        volatile uint32_t* counters=sat_parallel_uncached_address((void*)g_t3_original_projected_fallbacks);
        ++counters[actor-g_actors];
        original_projection_disable(actor);
    }
#endif
#if defined(T3_COMPILED_RENDERER)
    if(actor->compiled_active) {
        note_worker_write(context,actor,actor->mesh.vertices,T3_VERTEX_COUNT*(uint32_t)sizeof(*actor->mesh.vertices));
        return t3_compiled_deform_range(&g_compiled_renderer,&actor->compiled_pose,
            0,T3_VERTEX_COUNT,actor->mesh.vertices,actor->mesh.vertex_cap)?SAT_OK:SAT_ERR_INVALID_ARG;
    }
#endif
    if(first>=end)return SAT_OK;
    /* Placement is applied as each vertex is stored, not in a
     * second pass over the whole mesh. */
    sat_skin_placement_t placement;
    if(!t3_psx_skin_placement(&placement,context->center_x,context->center_z))
        return SAT_ERR_INVALID_ARG;
#if defined(T3_SKIN_TO_SCREEN)
    if(actor->instance.pre_projected) {
        sat_mat4_t* clip=g_clip_joints[actor-g_actors];
        note_worker_write(context,actor,actor->projected+first,(end-first)*(uint32_t)sizeof(*actor->projected));
        if(sat_skin_clip_joints(actor->joints,T3_JOINT_COUNT,&placement,
                &g_geometry_batch.view_proj,clip)==SAT_OK &&
           sat_skin_project_range(&actor->asset->skin,clip,T3_JOINT_COUNT,first,end-first,
                (int16_t)g_geometry_batch.width,(int16_t)g_geometry_batch.height,
                actor->projected,T3_VERTEX_COUNT)==SAT_OK)
            return SAT_OK;
        actor->instance.pre_projected=0u;
        ++g_t3_skin_screen_fallbacks;
    }
#endif
    note_worker_write(context,actor,actor->mesh.vertices+first,(end-first)*(uint32_t)sizeof(*actor->mesh.vertices));
    return sat_skin_deform_range_placed(&actor->asset->skin,actor->joints,T3_JOINT_COUNT,first,
        end-first,actor->mesh.vertices,actor->mesh.vertex_cap,&placement);
}

#if defined(T3_SKIN_TO_SCREEN)
/* The Master's share of actor 1: vertices [0, end) straight to screen, while
 * the worker projects the rest. Its clip joints go in actor 0's slot, which
 * actor 0 rewrites next. It never clears pre_projected, which the worker owns:
 * a refusal on either CPU is settled after the join (settle_lead). */
static sat_result_t project_lead(const fused_actor_context_t* context,actor_t* actor,uint32_t end) {
    sat_skin_placement_t placement;
    sat_mat4_t* clip=g_clip_joints[0];
    if(!t3_psx_skin_placement(&placement,context->center_x,context->center_z) ||
       sat_skin_clip_joints(actor->joints,T3_JOINT_COUNT,&placement,&g_geometry_batch.view_proj,clip)!=SAT_OK)
        return SAT_ERR_UNSUPPORTED;
    return sat_skin_project_range(&actor->asset->skin,clip,T3_JOINT_COUNT,0,end,
        (int16_t)g_geometry_batch.width,(int16_t)g_geometry_batch.height,actor->projected,T3_VERTEX_COUNT);
}
#endif

/* Off the compiled fast path a pre-projected actor is refused with nothing
 * appended; skin it to world and let the caller submit again. */
static int skin_to_world_after(sat_result_t st,const fused_actor_context_t* context,actor_t* actor) {
#if defined(T3_SKIN_TO_SCREEN)
    if(st!=SAT_ERR_UNSUPPORTED || !actor->instance.pre_projected)return 0;
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
    if(actor->original_active) {
        volatile uint32_t* counters=sat_parallel_uncached_address((void*)g_t3_original_projected_fallbacks);
        ++counters[actor-g_actors];
        original_projection_disable(actor);
    }
#endif
    actor->instance.pre_projected=0u;
    ++g_t3_skin_screen_fallbacks;
    return deform_actor(context,actor,0,actor->vertex_count)==SAT_OK;
#else
    (void)st;(void)context;(void)actor;
    return 0;
#endif
}

/* The range executor purges stale Slave inputs once. Each CPU owns one
 * complete vertex/projected span; only Master changes the canonical queue.
 * No authoritative state, hardware access or nested dispatch on Slave. */
static void prepare_actor_range(void* opaque,uint32_t begin,uint32_t end) {
    fused_actor_context_t* context=(fused_actor_context_t*)opaque;
    for(uint32_t i=begin;i<end;++i) {
        actor_t* actor=&g_actors[i];
        uint32_t started=0;
        if(i==0)started=sat_time_ms();
#if defined(T3_NATIVE_PHASE_PROBE)
        const uint16_t skin_started=sat_time_frc();
#endif
        const uint32_t lead=i==1u?context->lead:0u;
#if defined(T3_FIGHTER_HAND_VARIANTS)
        {
            fused_actor_context_t* shared=sat_parallel_uncached_address(context);
            shared->hands_changed[i]=(uint8_t)t3_fighter_hands_apply(i,
                (sat_skin_asset_t*)(uintptr_t)&actor->asset->skin,shared->hand_variant[i]);
        }
#endif
        sat_result_t st=deform_actor(context,actor,lead,actor->vertex_count);
#if defined(T3_FIGHTER_LIGHTING)
        {
            const fused_actor_context_t* shared=sat_parallel_uncached_address(context);
            if(shared->relight==i)t3_fighter_light_relight(i,shared->original_joints[i]);
        }
#endif
#if !defined(T3_SKIN_TO_SCREEN)
        /* Screen projection: the Master submits actor 1's faces, so the
         * worker never reads the Master's share (see settle_lead). */
        if(st==SAT_OK && lead) {
            const fused_actor_context_t* shared=sat_parallel_uncached_address(context);
            for(uint32_t spins=0;!shared->lead_ready;)
                if(++spins>=4000000u){st=SAT_ERR_TIMEOUT;break;}
            if(st==SAT_OK)st=shared->lead_result;
            if(st==SAT_OK)st=sat_parallel_cache_sync_range(actor->mesh.vertices,
                lead*sizeof(*actor->mesh.vertices));
        }
#endif
#if defined(T3_NATIVE_PHASE_PROBE)
        const uint16_t placement_started=sat_time_frc();
#endif
        if(i==0) {
            g_skin_work_ms+=sat_time_ms()-started;
            T3_PERF_ADD(skin,started);
#if defined(T3_PERF_SAMPLES)
            started=sat_time_ms();
#endif
        }
#if defined(T3_NATIVE_PHASE_PROBE)
        const uint16_t geometry_started=sat_time_frc();
#endif
        if(i==0) {
            T3_PERF_ADD(placement,started);
#if defined(T3_PERF_SAMPLES)
            context->geometry_started=sat_time_ms();
#endif
        }
        if(st==SAT_OK && i==0) {
#if defined(T3_ORIGINAL_FIGHTER_OT)
            context->original_first[0]=context->scene->faces.count;
#endif
            if(context->shown[0]) {
                st=sat_scene_submit_instance(context->scene,&actor->instance,
                    SAT_SCENE3D_SLOT_INHERIT,actor->projected,actor->world_vertices);
                if(skin_to_world_after(st,context,actor))
                    st=sat_scene_submit_instance(context->scene,&actor->instance,
                        SAT_SCENE3D_SLOT_INHERIT,actor->projected,actor->world_vertices);
            }
#if defined(T3_ORIGINAL_FIGHTER_OT)
            context->original_after[0]=context->scene->faces.count;
#endif
        }
#if !defined(T3_MASTER_WORKER_FACES)
        if(st==SAT_OK && i==1 && ((const fused_actor_context_t*)sat_parallel_uncached_address(context))->shown[1]) {
            sat_scene3d_prepare_batch_t worker=g_geometry_batch;
            worker.projected_faces=sat_parallel_uncached_address(worker.projected_faces);
            worker.faces=sat_parallel_uncached_address(worker.faces);
            worker.keys=sat_parallel_uncached_address(worker.keys);
            st=sat_scene3d_prepare_batch_execute(&worker);
            if(skin_to_world_after(st,context,actor))
                st=sat_scene3d_prepare_batch_execute(&worker);
            sat_scene3d_prepare_batch_t* shared=sat_parallel_uncached_address(&g_geometry_batch);
            shared->metrics=worker.metrics;
#if defined(T3_NATIVE_PHASE_PROBE) && defined(SAT_SCENE3D_INSTANCE_PROFILE_AVAILABLE)
            shared->instance_profile=worker.instance_profile;
#endif
        }
#endif
        fused_actor_context_t* shared=sat_parallel_uncached_address(context);
#if defined(T3_NATIVE_PHASE_PROBE)
        const uint16_t finished=sat_time_frc();
        shared->phase.skin[i]=(uint16_t)(placement_started-skin_started);
        shared->phase.placement[i]=(uint16_t)(geometry_started-placement_started);
        shared->phase.geometry[i]=(uint16_t)(finished-geometry_started);
#endif
        shared->result[i]=st;
    }
}

static t3_characters_worker_start_t g_worker_start;

void t3_characters_set_worker_start(t3_characters_worker_start_t start) {
    g_worker_start=start;
}

void t3_characters_prepare_worker(void) {
    prepare_actor_range(&g_fused_actors,1,2);
    fused_actor_context_t* shared=sat_parallel_uncached_address(&g_fused_actors);
    shared->worker_ready=1u;
}

/* Actor 0 here, actor 1 by the started worker; returns once both are done.
 * Right after starting the worker, the Master skins T3_MASTER_SKIN_PERCENT of
 * actor 1's vertices alongside it, which shortens the Slave's chain (actor 1,
 * then the deferred ticks). */
#if !defined(T3_MASTER_SKIN_PERCENT)
#define T3_MASTER_SKIN_PERCENT 0u
#endif
#if defined(T3_FIGHTER_HAND_VARIANTS) && T3_MASTER_SKIN_PERCENT
#error "hand variants rewrite actor 1's skin on the Slave alone: no Master share"
#endif
static uint32_t g_master_skin_percent=T3_MASTER_SKIN_PERCENT;
void t3_characters_set_master_skin_percent(unsigned percent) {
    /* No share at all keeps the build's choice (hand variants need none). */
    if(T3_MASTER_SKIN_PERCENT)g_master_skin_percent=percent>100u?100u:percent;
}
#if defined(T3_SKIN_TO_SCREEN)
/* Both shares must be on screen; otherwise the whole actor goes to world, as
 * a refusal on one CPU does (skin_to_world_after). */
static sat_result_t settle_lead(actor_t* worker,sat_result_t lead_result) {
    sat_result_t st=sat_parallel_cache_sync_range(&worker->instance,sizeof(worker->instance));
    if(st!=SAT_OK || (lead_result==SAT_OK && worker->instance.pre_projected))return st;
    worker->instance.pre_projected=0u;
    ++g_t3_skin_screen_fallbacks;
    return deform_actor(&g_fused_actors,worker,0,worker->vertex_count);
}
#endif

#if defined(T3_SKIN_TO_SCREEN) && defined(T3_MASTER_SKIN_CHECK)
/* T3_MASTER_SKIN_CHECK: after a split image, the Master projects all of
 * actor 1 again and counts the vertices that differ from the two CPUs'
 * result. Zero means the split cannot change a pixel. Diagnostic only. */
volatile uint32_t g_t3_master_skin_check[3]; /* images checked, vertices differing, images differing */
static sat_projected_vertex_t g_split_projected[T3_VERTEX_COUNT];
static sat_result_t check_split(actor_t* worker) {
    sat_result_t st=sat_parallel_cache_sync_range(worker->projected,worker->vertex_count*sizeof(*worker->projected));
    if(st!=SAT_OK)return st;
    __builtin_memcpy(g_split_projected,worker->projected,worker->vertex_count*sizeof(*worker->projected));
    if((st=project_lead(&g_fused_actors,worker,worker->vertex_count))!=SAT_OK)return st;
    uint32_t differing=0;
    for(uint32_t v=0;v<worker->vertex_count;++v) {
        const sat_projected_vertex_t* a=&g_split_projected[v];
        const sat_projected_vertex_t* b=&worker->projected[v];
        differing+=a->x!=b->x || a->y!=b->y || a->w!=b->w;
    }
    ++g_t3_master_skin_check[0];
    g_t3_master_skin_check[1]+=differing;
    g_t3_master_skin_check[2]+=differing!=0;
    return SAT_OK;
}
#endif

static sat_result_t prepare_with_worker(void) {
    actor_t* worker=&g_actors[1];
#if defined(T3_COMPILED_RENDERER)
    const uint32_t lead=worker->compiled_active?0u:worker->vertex_count*g_master_skin_percent/100u;
#elif defined(T3_SKIN_TO_SCREEN)
    /* The original projection builds the whole mesh on one CPU. */
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
    const int whole=worker->original_active || !worker->instance.pre_projected;
#else
    const int whole=!worker->instance.pre_projected;
#endif
    const uint32_t lead=whole?0u:worker->vertex_count*g_master_skin_percent/100u;
#else
    const uint32_t lead=worker->vertex_count*g_master_skin_percent/100u;
#endif
    g_fused_actors.lead=(uint16_t)lead;
    g_fused_actors.written_first=UINT32_MAX;
    g_fused_actors.written_end=0u;
    sat_result_t st=g_worker_start();
    if(st!=SAT_OK)return st;
    sat_result_t lead_result=SAT_OK;
    if(lead) {
#if defined(T3_SKIN_TO_SCREEN)
        lead_result=project_lead(&g_fused_actors,worker,lead);
#else
        fused_actor_context_t* shared=sat_parallel_uncached_address(&g_fused_actors);
        shared->lead_result=deform_actor(&g_fused_actors,worker,0,lead);
        shared->lead_ready=1u;
#endif
    }
    prepare_actor_range(&g_fused_actors,0,1);
    const fused_actor_context_t* shared=sat_parallel_uncached_address(&g_fused_actors);
    for(uint32_t spins=0;!shared->worker_ready;)
        if(++spins>=4000000u)return SAT_ERR_TIMEOUT;
    /* Only the lines the worker wrote can be stale here: the Master's own
     * writes went through its write-through cache. */
    st=shared->written_first<shared->written_end?
        sat_parallel_cache_sync_range((const uint8_t*)&g_actor_geometry[1]+shared->written_first,
            shared->written_end-shared->written_first):SAT_OK;
#if defined(T3_SKIN_TO_SCREEN)
    if(st==SAT_OK && lead && shared->result[1]==SAT_OK)st=settle_lead(worker,lead_result);
#if defined(T3_MASTER_SKIN_CHECK)
    if(st==SAT_OK && lead && lead<worker->vertex_count && worker->instance.pre_projected)st=check_split(worker);
#endif
#endif
    (void)lead_result;
    return st;
}

static sat_result_t prepare_fused_actors(sat_scene_t* scene) {
    sat_result_t st;
#if defined(T3_NATIVE_PHASE_PROBE)
    const uint16_t setup_started=sat_time_frc();
#endif
    actor_t* slave=&g_actors[1];
    g_fused_actors=(fused_actor_context_t){.scene=scene,
        .center_x=t3_move_probe_world_center(0),
        .center_z=t3_move_probe_world_center(2),
        .shown={(uint8_t)t3_move_probe_fighter_shown(0),(uint8_t)t3_move_probe_fighter_shown(1)},
        .result={SAT_OK,SAT_OK},.worker_ready=0u};
#if defined(T3_FIGHTER_LIGHTING)
    g_fused_actors.relight=2u;
    ++g_light_turn;
#endif
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
#if defined(T3_PARTICLE_GATED_PROJECTION)
    /* The original projection and OT exist to order particles against the
     * fighters; without a live effect the cheaper SDK path draws the image. */
    const int original_view_ready=g_t3_particle_image.sprites.count>0u &&
        t3_move_probe_camera_matrix(&g_fused_actors.original_view);
#else
    const int original_view_ready=t3_move_probe_camera_matrix(&g_fused_actors.original_view);
#endif
#endif
    for(unsigned i=0;i<2;++i) {
        g_actors[i].mesh.vertex_count=g_actors[i].vertex_count;
        g_actors[i].instance.world=NULL;
#if defined(T3_FIGHTER_HAND_VARIANTS)
        for(unsigned c=0;c<2u;++c)g_fused_actors.hand_variant[i][c]=(uint8_t)t3_move_probe_hand_variant(i,c);
#endif
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
        original_projection_disable(&g_actors[i]);
#if defined(T3_FIGHTER_LIGHTING)
        /* Lighting reads the PS1 rotations on every path; the original
         * projection still runs only when its view is ready. */
        const int joints_ready=g_fused_actors.shown[i] && t3_move_probe_copy_world(i,
            g_fused_actors.original_joints[i],&g_fused_actors.original_mask[i]);
        if(joints_ready && i==(g_light_turn&1u))g_fused_actors.relight=(uint8_t)i;
        if(original_view_ready && joints_ready) {
#else
        if(original_view_ready && g_fused_actors.shown[i] && t3_move_probe_copy_world(i,g_fused_actors.original_joints[i],
                &g_fused_actors.original_mask[i])) {
#endif
            g_actors[i].original_active=1u;
            g_actors[i].instance.mesh=&g_actors[i].original_mesh;
            g_actors[i].instance.validated_binding=&g_actors[i].original_binding;
            g_actors[i].projected=g_actor_geometry[i].projected;
#if defined(T3_ORIGINAL_FIGHTER_OT)
            g_actors[i].instance.cull_backfaces=0u;
#endif
        }
#endif
#if defined(T3_SKIN_TO_SCREEN)
        g_actors[i].instance.pre_projected=1u;
#endif
    }
    g_geometry_item=(sat_scene3d_prepare_item_t){
        .instance=&slave->instance,.screen_scratch=slave->projected,
        .world_scratch=slave->world_vertices,.color_calc_slot=SAT_SCENE3D_SLOT_INHERIT,
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
        .source_plan=slave->original_active?NULL:&g_geometry_sources};
#else
        .source_plan=&g_geometry_sources};
#endif
    if((st=sat_scene3d_prepare_batch_init(&g_geometry_batch,&g_geometry_item,1,
            scene->faces.entries,g_geometry_keys,T3_FACE_COUNT))!=SAT_OK)return st;
#if !defined(T3_MASTER_WORKER_FACES)
    g_geometry_batch.cold_capacity=T3_ACTOR_COLD_FACES;
    if((st=sat_scene3d_faces_reserve_batch(&scene->faces,&g_geometry_batch))!=SAT_OK)return st;
#endif
    g_geometry_batch.view_proj=scene->faces.view_proj;
    g_geometry_batch.eye=scene->faces.eye;
    g_geometry_batch.forward=scene->faces.forward;
    g_geometry_batch.near_depth=scene->faces.near_depth;
    g_geometry_batch.width=scene->faces.width;
    g_geometry_batch.height=scene->faces.height;
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION)
    if((st=sat_parallel_cache_sync_range(&g_fused_actors,sizeof(g_fused_actors)))!=SAT_OK)return st;
#endif
#if defined(T3_NATIVE_PHASE_PROBE)
    const uint16_t dispatch_started=sat_time_frc();
#endif
    /* The Slave writes only actor 1's geometry; the Master's cache already
     * holds its own writes for actor 0, so drop stale lines for actor 1 only. */
    st=g_worker_start?prepare_with_worker():
        sat_parallel_for(0,2,1,prepare_actor_range,&g_fused_actors,
            &g_actor_geometry[1],sizeof(g_actor_geometry[1]));
    if(st!=SAT_OK)return st; /* Do not reclaim a potentially running worker. */
#if defined(T3_NATIVE_PHASE_PROBE)
    const uint16_t merge_started=sat_time_frc();
#endif
    if((st=sat_parallel_cache_sync_range(&g_fused_actors,sizeof(g_fused_actors)))!=SAT_OK)return st;
#if defined(T3_FIGHTER_HAND_VARIANTS)
    /* The Slave rewrote actor 1's hands; the Master may read them in a fallback. */
    if(g_fused_actors.hands_changed[1] &&
       (st=t3_fighter_hands_sync(1u,(sat_skin_asset_t*)(uintptr_t)&slave->asset->skin))!=SAT_OK)return st;
#endif
#if defined(T3_MASTER_WORKER_FACES)
    /* The worker only skinned actor 1 (its geometry is already synced); it
     * may have cleared pre_projected. Its faces are submitted here, so the
     * worker goes on to the deferred ticks a face pass earlier. */
    if(g_fused_actors.result[0]!=SAT_OK)return g_fused_actors.result[0];
    if(g_fused_actors.result[1]!=SAT_OK)return g_fused_actors.result[1];
    if((st=sat_parallel_cache_sync_range(&slave->instance,sizeof(slave->instance)))!=SAT_OK)return st;
#if defined(T3_ORIGINAL_FIGHTER_OT)
    g_fused_actors.original_first[1]=scene->faces.count;
#endif
    if(g_fused_actors.shown[1]) {
        st=sat_scene_submit_instance(scene,&slave->instance,SAT_SCENE3D_SLOT_INHERIT,
            slave->projected,slave->world_vertices);
        if(skin_to_world_after(st,&g_fused_actors,slave))
            st=sat_scene_submit_instance(scene,&slave->instance,SAT_SCENE3D_SLOT_INHERIT,
                slave->projected,slave->world_vertices);
        if(st!=SAT_OK)return st;
    }
#if defined(T3_ORIGINAL_FIGHTER_OT)
    g_fused_actors.original_after[1]=scene->faces.count;
    if(slave->original_active && (st=sat_parallel_cache_sync_range(slave->world_vertices,
            slave->original_mesh.vertex_count*sizeof(t3_psx_projected_vertex_t)))!=SAT_OK)return st;
    if(!apply_original_fighter_ot(scene,&g_fused_actors))return SAT_ERR_INVALID_ARG;
#endif
#if defined(T3_FIGHTER_MESH_SHADOWS)
    for(unsigned i=0;i<2u;++i)if(g_actors[i].original_active)
        (void)t3_fighter_shadow_submit(scene,i,&g_fused_actors.original_view,
                                       g_fused_actors.original_joints[i],t3_stage_vdp2_shadow());
#endif
#else
    if((st=sat_parallel_cache_sync_range(&g_geometry_batch,sizeof(g_geometry_batch)))!=SAT_OK)return st;
    const uint32_t prepared=g_geometry_batch.metrics.prepared_faces;
    if((st=sat_parallel_cache_sync_range(g_geometry_keys,
            prepared*sizeof(*g_geometry_keys)))!=SAT_OK)return st;
    if((st=sat_parallel_cache_sync_range(g_geometry_batch.projected_faces,
            prepared*sizeof(*g_geometry_batch.projected_faces)))!=SAT_OK)return st;
    /* Only clipping fallbacks read cold records. The ordinary projected
     * subset writes none of them, so there is nothing to consume there. */
    if((g_geometry_batch.metrics.clipped_faces || !slave->material_programs.all_compiled) &&
       (st=sat_parallel_cache_sync_range(g_geometry_batch.faces,
            g_geometry_batch.cold_capacity*sizeof(*g_geometry_batch.faces)))!=SAT_OK)return st;
    if(g_fused_actors.result[0]!=SAT_OK)return g_fused_actors.result[0];
    if(g_fused_actors.result[1]!=SAT_OK)return g_fused_actors.result[1];
#if defined(T3_VERIFY_COMPILED_RENDERER)
    if((st=verify_compiled_actors())!=SAT_OK)return st;
#endif
    if((st=sat_scene3d_faces_merge_prepared(&scene->faces,&g_geometry_batch))!=SAT_OK)return st;
    scene->submitted_faces+=g_geometry_batch.metrics.prepared_faces;
#endif
#if defined(T3_NATIVE_PHASE_PROBE)
    const uint16_t finished=sat_time_frc();
    g_completed_actor_phase=g_fused_actors.phase;
#if defined(SAT_SCENE3D_INSTANCE_PROFILE_AVAILABLE)
    g_completed_actor_phase.projection[0]=(uint16_t)scene->faces.instance_profile.projection_ticks;
    g_completed_actor_phase.face_prep[0]=(uint16_t)scene->faces.instance_profile.preparation_ticks;
    g_completed_actor_phase.projection[1]=(uint16_t)g_geometry_batch.instance_profile.projection_ticks;
    g_completed_actor_phase.face_prep[1]=(uint16_t)g_geometry_batch.instance_profile.preparation_ticks;
#endif
    g_completed_actor_phase.setup=(uint16_t)(dispatch_started-setup_started);
    g_completed_actor_phase.dispatch=(uint16_t)(merge_started-dispatch_started);
    g_completed_actor_phase.merge=(uint16_t)(finished-merge_started);
    {
        /* Whole-run sums for reading from a save state. */
        const uint16_t* fields=(const uint16_t*)&g_completed_actor_phase;
        for(unsigned k=0;k<sizeof(g_completed_actor_phase)/sizeof(uint16_t);++k)
            g_t3_phase_sums[k]+=fields[k];
        ++g_t3_phase_samples;
    }
#endif
    scene->culled_faces=scene->faces.culled_faces;
    scene->clipped_faces=scene->faces.clipped_faces;
    T3_PERF_ADD(geometry,g_fused_actors.geometry_started);
    ++g_t3_geometry_pairs;
    ++g_t3_fused_actor_frames;
    return SAT_OK;
}
#endif

const t3_character_phase_profile_t* t3_characters_phase_profile(void) {
#if defined(T3_NATIVE_PHASE_PROBE)
    return &g_completed_actor_phase;
#else
    return NULL;
#endif
}

sat_result_t t3_characters_render(sat_scene_t* scene, const t3_match_t* game) {
    unsigned i;
    sat_result_t st;
    if (!scene || !game || !g_initialized) return SAT_ERR_INVALID_ARG;
    int fused=0;
#if defined(T3_FUSED_ACTOR_PREPARATION)
    fused=t3_move_probe_owned() && T3_DRAW_ACTORS==2;
#endif
    if ((st = prepare_actors(fused)) != SAT_OK) {
#if defined(T3_FORCE_PROGRAM)
        SAT_PANIC_IF_ERROR(st);
#endif
        return st;
    }
#if defined(T3_FUSED_ACTOR_PREPARATION)
    if(fused)return prepare_fused_actors(scene);
#endif
    T3_PERF_START(placement_started);
    for (i = 0; i < T3_DRAW_ACTORS; ++i) {
        actor_t* a = &g_actors[i];
        const t3_fighter_t* fighter = &game->fighters[i];
        a->instance.pre_projected = 0u; /* only fused preparation projects */
        sat_model_transform3d_t transform;
        sat_model_transform3d_identity(&transform);
#if defined(T3_LOCAL_KMD_BIND)
        /* Visual world-unit conversion only; original simulation/collision
         * remains in PS1 units. Paul is about 1400 units tall in idle. */
        transform.scale.x = transform.scale.y = transform.scale.z = 6 * SAT_FX16_ONE;
#endif
        transform.position.x = (sat_fx16_t)(fighter->x * (SAT_FX16_ONE / 256));
        transform.position.z = (sat_fx16_t)(fighter->z * (SAT_FX16_ONE / 256));
#if T3_CLIP_COUNT > 1
        if (fighter->jump_frames) {
            int32_t height = fighter->jump_frames;
            if (height > 18) height = 36 - height;
            transform.position.y = (sat_fx16_t)(height * (SAT_FX16_ONE / 36));
        }
#endif
        /* Original example model faces +Z. Turn its front towards rival. */
        transform.rotation_deg.y = (fighter->facing > 0 ? 90 : -90) * SAT_FX16_ONE;
#if defined(T3_FORCE_PROGRAM)
        /* The published original pose is already camera-rebased and carries
         * the fighter's own facing; only the sixfold visual scale applies. */
        transform.position.x = transform.position.z = 0;
        transform.rotation_deg.y = 0;
#endif
        if(t3_move_probe_owned()) {
            /* The owned world matrix is diagonal scale six + translation.
             * Apply that exact transform in place after skinning rather than
             * paying the generic matrix-vector path for every visible vertex.
             * prepare_actors rewrites these vertices before every submission. */
#if defined(T3_COMPILED_RENDERER)
            if(!a->compiled_active)
#endif
            if(!t3_psx_skin_place(a->mesh.vertices,a->mesh.vertex_count,
                    t3_move_probe_world_center(0),t3_move_probe_world_center(2)))
                return SAT_ERR_INVALID_ARG;
            a->instance.world=NULL;
        } else {
            if ((st = sat_model_transform3d_matrix(&transform, &a->world))
                    != SAT_OK) return st;
            a->instance.world=&a->world;
        }
    }
    T3_PERF_ADD(placement,placement_started);
    T3_PERF_START(geometry_started);
#if defined(T3_PARALLEL_GEOMETRY)
    if(T3_DRAW_ACTORS==2 && sat_parallel_slave_available() &&
       t3_move_probe_fighter_shown(0) && t3_move_probe_fighter_shown(1)) {
        actor_t* slave=&g_actors[1];
        g_geometry_item=(sat_scene3d_prepare_item_t){
            .instance=&slave->instance,
            .screen_scratch=slave->projected,
            .world_scratch=slave->world_vertices,
            .color_calc_slot=SAT_SCENE3D_SLOT_INHERIT,
            .source_plan=&g_geometry_sources};
        if((st=sat_scene3d_prepare_batch_init(&g_geometry_batch,&g_geometry_item,1,
#if defined(T3_DIRECT_FACE_SPANS)
                scene->faces.entries,
#else
                g_geometry_faces,
#endif
                g_geometry_keys,T3_FACE_COUNT))!=SAT_OK)return st;
#if defined(T3_DIRECT_FACE_SPANS)
        g_geometry_batch.cold_capacity=T3_ACTOR_COLD_FACES;
        if((st=sat_scene3d_faces_reserve_batch(&scene->faces,&g_geometry_batch))!=SAT_OK)return st;
#endif
        g_geometry_batch.dispatch=SAT_SCENE3D_PREPARE_DISPATCH_RUNTIME;
        sat_parallel_handle_t handle=0;
        if((st=sat_scene_prepare_batch_async(scene,&g_geometry_batch,&handle))!=SAT_OK) {
#if defined(T3_DIRECT_FACE_SPANS)
            (void)sat_scene3d_faces_cancel_batch(&scene->faces,&g_geometry_batch);
#endif
            return st;
        }
        actor_t* master=&g_actors[0];
        const sat_result_t master_result=sat_scene_submit_instance(scene,&master->instance,
            SAT_SCENE3D_SLOT_INHERIT,master->projected,master->world_vertices);
        st=sat_parallel_wait(handle,GEOMETRY_TIMEOUT_TICKS);
        if(st!=SAT_OK || master_result!=SAT_OK) {
            if(st!=SAT_ERR_TIMEOUT)(void)sat_scene_prepare_batch_release(scene,&g_geometry_batch,handle);
            return st!=SAT_OK?st:master_result;
        }
#if defined(T3_VERIFY_PARALLEL_GEOMETRY)
        const sat_scene3d_faces_t prefix=scene->faces;
#endif
        if((st=sat_scene_merge_prepared_batch(scene,&g_geometry_batch,handle))!=SAT_OK)return st;
#if defined(T3_VERIFY_PARALLEL_GEOMETRY)
        /* Independent ordinary reference owns separate face storage, while
         * the direct worker span remains in the canonical queue. */
        sat_scene3d_faces_t reference=prefix;
        reference.entries=g_geometry_faces;reference.keys=g_geometry_keys;
        reference.projected_entries=NULL;reference.projected_indices=NULL;
        reference.projected_gap=0;reference.reserved_batch=NULL;
        reference.count=reference.cold_count=0;reference.capacity=T3_FACE_COUNT;
        reference.culled_faces=reference.clipped_faces=0;
        if((st=sat_scene3d_faces_submit_instance(&reference,&slave->instance,
                SAT_SCENE3D_SLOT_INHERIT,slave->projected,slave->world_vertices))!=SAT_OK)return st;
        if(reference.count!=g_geometry_batch.metrics.prepared_faces ||
           reference.culled_faces!=g_geometry_batch.metrics.culled_faces ||
           reference.clipped_faces!=g_geometry_batch.metrics.clipped_faces)
            return SAT_ERR_VERIFY_FAILED;
        for(unsigned f=0;f<g_geometry_batch.metrics.prepared_faces;++f) {
            sat_scene3d_face_t queued;
            if((st=sat_scene3d_faces_get(&scene->faces,(uint16_t)(prefix.count+f),&queued))!=SAT_OK)return st;
            if(scene->faces.keys[prefix.count+f]!=g_geometry_keys[f] ||
               !same_face(&queued,&g_geometry_faces[f]))return SAT_ERR_VERIFY_FAILED;
        }
        ++g_t3_geometry_verified;
#endif
        if((st=sat_scene_prepare_batch_release(scene,&g_geometry_batch,handle))!=SAT_OK)return st;
        ++g_t3_geometry_pairs;
        T3_PERF_ADD(geometry,geometry_started);
        return SAT_OK;
    }
    ++g_t3_geometry_serial;
#endif
    for(i=0;i<T3_DRAW_ACTORS;++i) {
        actor_t* a=&g_actors[i];
#if defined(T3_FORCE_PROGRAM)
        /* A hidden actor, or one whose original pose is incomplete this
         * tick, has no image (the level runner hides released and wiped-out
         * enemies; an unbridged pose would be a stale one). */
        if(!g_force_shown[i] || !g_force_pose_ready[i])continue;
#endif
        if(!t3_move_probe_fighter_shown(i))continue;
        if((st=sat_scene_submit_instance(scene,&a->instance,
            SAT_SCENE3D_SLOT_INHERIT,a->projected,a->world_vertices))!=SAT_OK) {
#if defined(T3_FORCE_PROGRAM)
            SAT_PANIC_IF_ERROR(st);
#endif
            return st;
        }
    }
    T3_PERF_ADD(geometry,geometry_started);
    return SAT_OK;
}

uint8_t t3_characters_skin_preview_applied(unsigned fighter) {
    return fighter<ACTOR_COUNT?g_skin_preview_applied[fighter]:0u;
}

uint32_t t3_characters_texture_vram_bytes(void) {return g_texture_vram_bytes;}
uint32_t t3_characters_skin_work_ms(void) {return g_skin_work_ms;}
