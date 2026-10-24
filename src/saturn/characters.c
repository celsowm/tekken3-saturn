/*
 * Two independent animated actor instances over one shared SKA1 asset, or
 * over one asset each when a second character mesh is built in.
 * Every pose, matrix, mutable mesh, and material owner is caller-owned;
 * LibSaturn supplies animation sampling, skin deformation, scene projection,
 * texture residency, and palette management.
 */
#include "characters.h"
#include "move_probe.h"
#include "psx_skin_bridge.h"
#include "perf.h"
#include "placement.h"
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
enum { ACTOR_COUNT = 2 };
#if defined(T3_SECOND_MESH)
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
    sat_vec3_t vertices[T3_VERTEX_COUNT];
    sat_projected_vertex_t projected[T3_VERTEX_COUNT];
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
    __attribute__((section(".wram_l")));
#endif
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES
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
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES && !defined(T3_HIGH_GEOMETRY_SCRATCH)
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
static uint8_t g_skin_preview_applied[ACTOR_COUNT];
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
    T3_HOT_WRAM;
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
#if defined(T3_PARALLEL_GEOMETRY) || defined(T3_PARALLEL_SKINNING)
    const sat_parallel_config_t parallel={SAT_PARALLEL_AUTO,NULL,0,0,GEOMETRY_TIMEOUT_TICKS};
    if((st=sat_parallel_init(&parallel))!=SAT_OK)return st;
#endif
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
        const sat_character_asset_t* asset = &g_assets[i < ASSET_COUNT ? i : 0u];
        a->asset = asset;
        a->clips = g_clips[i < ASSET_COUNT ? i : 0u];
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
        if (i == 1u) {
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
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES
        /* These immutable model textures live for the application lifetime.
         * Use the public native model uploader; the logical texture registry
         * has 64 owner slots and is intended for individually managed assets. */
        if ((!share_resources || !i) &&
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
            const sat_model_texture_asset_t* texture=&model.textures[t];
            material->kind=SAT_SCENE3D_INDEXED_TEXTURED;
            material->texture=&a->textures[t];
            material->color_calc_slot=SAT_INDEXED_SOLID_OPAQUE;
            material->rgb555=model.palettes_rgb555[
                texture->palette_slot*256u+texture->pixels[texture->pixel_count/2u]];
        }
#else
        if ((st = sat_character_materials_upload(&model, a->owners,
                a->textures, a->materials, a->texture_count)) != SAT_OK)
            return st;
#endif
        if(!share_resources || !i)for (unsigned t=0;t<a->texture_count;++t)
            g_texture_vram_bytes+=(uint32_t)a->textures[t].width*a->textures[t].height/
                (a->textures[t].format==SAT_VDP1_TEXTURE_LUT4?2u:1u);
        if ((st = sat_mesh_init(&a->mesh, a->vertices, T3_VERTEX_COUNT,
                a->indices, T3_FACE_COUNT)) != SAT_OK) return st;
        if ((st = sat_model_copy_to_mesh(&asset->model, &a->mesh)) != SAT_OK)
            return st;
        if ((st = sat_skin_validate_mesh(&asset->skin, &a->mesh)) != SAT_OK)
            return st;
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
        /* Preserve the original per-face two-sided flag when culling. The
         * original culls by screen winding alone (NCLIP), so the world-normal
         * test, which reads every corner again, is left out. */
#if T3_CLIP_COUNT == 1
        a->instance.cull_backfaces = SAT_SCENE3D_CULL_SCREEN;
        a->instance.double_sided_faces = t3_kmd_double_sided;
#if defined(T3_SECOND_MESH)
        if (i == 1u) a->instance.double_sided_faces = t3_kmd_double_sided1;
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

sat_result_t t3_characters_tick(const t3_match_t* game) {
    unsigned i;
    sat_result_t st;
    if (!g_initialized || !game) return SAT_ERR_INVALID_ARG;
    for (i = 0; i < ACTOR_COUNT; ++i) {
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
} skin_ranges_t;

static void skin_actors_range(void* opaque,uint32_t begin,uint32_t end) {
    skin_ranges_t* ranges=(skin_ranges_t*)opaque;
    const unsigned range_index=begin==0u?0u:1u;
    while(begin<end) {
        const unsigned actor_index=begin/T3_VERTEX_COUNT;
        const uint32_t first=begin%T3_VERTEX_COUNT;
        uint32_t count=T3_VERTEX_COUNT-first;
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
        if(used)
#if defined(T3_COMPILED_RENDERER)
        if(actor->compiled_active) {
            st=t3_compiled_deform_range(&g_compiled_renderer,&actor->compiled_pose,
                first,count,actor->mesh.vertices,actor->mesh.vertex_cap)?SAT_OK:SAT_ERR_INVALID_ARG;
        } else
#endif
        st=sat_skin_deform_range(&actor->asset->skin,actor->joints,
                T3_JOINT_COUNT,first,used,actor->mesh.vertices,actor->mesh.vertex_cap);
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
        skin_ranges_t ranges={{SAT_OK,SAT_OK}};
        const uint32_t end=(uint32_t)T3_DRAW_ACTORS*T3_VERTEX_COUNT;
        if((st=sat_parallel_for(0u,end,128u,skin_actors_range,&ranges,
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
     * worker. The worker skins the rest, then waits for lead_ready (through
     * the uncached alias) before projecting. */
    uint16_t lead;
    volatile uint32_t lead_ready;
    sat_result_t lead_result;
#if defined(T3_NATIVE_PHASE_PROBE)
    t3_character_phase_profile_t phase;
#endif
} fused_actor_context_t;
static fused_actor_context_t g_fused_actors __attribute__((section(".wram_l")));
volatile uint32_t g_t3_fused_actor_frames;
#if defined(T3_NATIVE_PHASE_PROBE)
static t3_character_phase_profile_t g_completed_actor_phase;
uint32_t g_t3_phase_sums[sizeof(t3_character_phase_profile_t)/sizeof(uint16_t)],g_t3_phase_samples;
#endif

#if defined(T3_SKIN_TO_SCREEN)
#if T3_MASTER_SKIN_PERCENT
#error "T3_SKIN_TO_SCREEN projects each actor on one CPU; keep T3_MASTER_SKIN_PERCENT at 0"
#endif
#if defined(T3_COMPILED_RENDERER)
#error "T3_SKIN_TO_SCREEN skins through the SDK; the compiled renderer writes world vertices"
#endif
/* Each actor's joints times the placement and the camera, written and read by
 * the CPU that prepares the actor. */
static sat_mat4_t g_clip_joints[ACTOR_COUNT][T3_JOINT_COUNT];
volatile uint32_t g_t3_skin_screen_fallbacks;
#endif

/* Skins vertices [first, end) of the actor (a compiled mesh is skinned whole).
 * With T3_SKIN_TO_SCREEN and instance.pre_projected set, it writes projected
 * vertices instead, as the original's RTPT does with each joint and the
 * camera concatenated; if the concatenation is refused it clears
 * pre_projected and skins to world. */
static sat_result_t deform_actor(const fused_actor_context_t* context,actor_t* actor,
        uint32_t first,uint32_t end) {
#if defined(T3_COMPILED_RENDERER)
    if(actor->compiled_active)
        return t3_compiled_deform_range(&g_compiled_renderer,&actor->compiled_pose,
            0,T3_VERTEX_COUNT,actor->mesh.vertices,actor->mesh.vertex_cap)?SAT_OK:SAT_ERR_INVALID_ARG;
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
    return sat_skin_deform_range_placed(&actor->asset->skin,actor->joints,T3_JOINT_COUNT,first,
        end-first,actor->mesh.vertices,actor->mesh.vertex_cap,&placement);
}

/* Off the compiled fast path a pre-projected actor is refused with nothing
 * appended; skin it to world and let the caller submit again. */
static int skin_to_world_after(sat_result_t st,const fused_actor_context_t* context,actor_t* actor) {
#if defined(T3_SKIN_TO_SCREEN)
    if(st!=SAT_ERR_UNSUPPORTED || !actor->instance.pre_projected)return 0;
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
        sat_result_t st=deform_actor(context,actor,lead,actor->vertex_count);
        if(st==SAT_OK && lead) {
            const fused_actor_context_t* shared=sat_parallel_uncached_address(context);
            for(uint32_t spins=0;!shared->lead_ready;)
                if(++spins>=4000000u){st=SAT_ERR_TIMEOUT;break;}
            if(st==SAT_OK)st=shared->lead_result;
            if(st==SAT_OK)st=sat_parallel_cache_sync_range(actor->mesh.vertices,
                lead*sizeof(*actor->mesh.vertices));
        }
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
            st=sat_scene_submit_instance(context->scene,&actor->instance,
                SAT_SCENE3D_SLOT_INHERIT,actor->projected,actor->world_vertices);
            if(skin_to_world_after(st,context,actor))
                st=sat_scene_submit_instance(context->scene,&actor->instance,
                    SAT_SCENE3D_SLOT_INHERIT,actor->projected,actor->world_vertices);
        }
#if !defined(T3_MASTER_WORKER_FACES)
        if(st==SAT_OK && i==1) {
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
static sat_result_t prepare_with_worker(void) {
    actor_t* worker=&g_actors[1];
#if defined(T3_COMPILED_RENDERER)
    const uint32_t lead=worker->compiled_active?0u:worker->vertex_count*T3_MASTER_SKIN_PERCENT/100u;
#else
    const uint32_t lead=worker->vertex_count*T3_MASTER_SKIN_PERCENT/100u;
#endif
    g_fused_actors.lead=(uint16_t)lead;
    const sat_result_t st=g_worker_start();
    if(st!=SAT_OK)return st;
    if(lead) {
        fused_actor_context_t* shared=sat_parallel_uncached_address(&g_fused_actors);
        shared->lead_result=deform_actor(&g_fused_actors,worker,0,lead);
        shared->lead_ready=1u;
    }
    prepare_actor_range(&g_fused_actors,0,1);
    const fused_actor_context_t* shared=sat_parallel_uncached_address(&g_fused_actors);
    for(uint32_t spins=0;!shared->worker_ready;)
        if(++spins>=4000000u)return SAT_ERR_TIMEOUT;
    return sat_parallel_cache_sync_range(&g_actor_geometry[1],sizeof(g_actor_geometry[1]));
}

static sat_result_t prepare_fused_actors(sat_scene_t* scene) {
    sat_result_t st;
#if defined(T3_NATIVE_PHASE_PROBE)
    const uint16_t setup_started=sat_time_frc();
#endif
    actor_t* slave=&g_actors[1];
    for(unsigned i=0;i<2;++i) {
        g_actors[i].mesh.vertex_count=g_actors[i].vertex_count;
        g_actors[i].instance.world=NULL;
#if defined(T3_SKIN_TO_SCREEN)
        g_actors[i].instance.pre_projected=1u;
#endif
    }
    g_geometry_item=(sat_scene3d_prepare_item_t){
        .instance=&slave->instance,.screen_scratch=slave->projected,
        .world_scratch=slave->world_vertices,.color_calc_slot=SAT_SCENE3D_SLOT_INHERIT,
        .source_plan=&g_geometry_sources};
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
    g_fused_actors=(fused_actor_context_t){.scene=scene,
        .center_x=t3_move_probe_world_center(0),
        .center_z=t3_move_probe_world_center(2),
        .result={SAT_OK,SAT_OK},.worker_ready=0u};
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
#if defined(T3_MASTER_WORKER_FACES)
    /* The worker only skinned actor 1 (its geometry is already synced); it
     * may have cleared pre_projected. Its faces are submitted here, so the
     * worker goes on to the deferred ticks a face pass earlier. */
    if(g_fused_actors.result[0]!=SAT_OK)return g_fused_actors.result[0];
    if(g_fused_actors.result[1]!=SAT_OK)return g_fused_actors.result[1];
    if((st=sat_parallel_cache_sync_range(&slave->instance,sizeof(slave->instance)))!=SAT_OK)return st;
    st=sat_scene_submit_instance(scene,&slave->instance,SAT_SCENE3D_SLOT_INHERIT,
        slave->projected,slave->world_vertices);
    if(skin_to_world_after(st,&g_fused_actors,slave))
        st=sat_scene_submit_instance(scene,&slave->instance,SAT_SCENE3D_SLOT_INHERIT,
            slave->projected,slave->world_vertices);
    if(st!=SAT_OK)return st;
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
    if ((st = prepare_actors(fused)) != SAT_OK) return st;
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
    if(T3_DRAW_ACTORS==2 && sat_parallel_slave_available()) {
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
        if((st=sat_scene_submit_instance(scene,&a->instance,
            SAT_SCENE3D_SLOT_INHERIT,a->projected,a->world_vertices))!=SAT_OK)return st;
    }
    T3_PERF_ADD(geometry,geometry_started);
    return SAT_OK;
}

uint8_t t3_characters_skin_preview_applied(unsigned fighter) {
    return fighter<ACTOR_COUNT?g_skin_preview_applied[fighter]:0u;
}

uint32_t t3_characters_texture_vram_bytes(void) {return g_texture_vram_bytes;}
uint32_t t3_characters_skin_work_ms(void) {return g_skin_work_ms;}
