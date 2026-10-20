/*
 * Two independent animated actor instances over one shared SKA1 asset.
 * Every pose, matrix, mutable mesh, and material owner is caller-owned;
 * LibSaturn supplies animation sampling, skin deformation, scene projection,
 * texture residency, and palette management.
 */
#include "characters.h"
#include "move_probe.h"
#include "psx_skin_bridge.h"
#include "perf.h"

#include "saturn/character3d.h"
#include "saturn/math3d.h"
#include "saturn/skeleton3d.h"
#include "saturn/skin3d.h"
#include "saturn/time.h"
#if defined(T3_PARALLEL_GEOMETRY)
#include "saturn/parallel.h"
#endif

#define DT_Q16 (SAT_FX16_ONE / T3_HZ)
#define FADE_Q16 (SAT_FX16_ONE / 10)

typedef struct {
    sat_vec3_t vertices[T3_VERTEX_COUNT];
    sat_projected_vertex_t projected[T3_VERTEX_COUNT];
    sat_vec3_t world_vertices[T3_VERTEX_COUNT];
} actor_geometry_t;

typedef struct {
    sat_skeleton_instance_t skeleton;
    sat_transform3d_node_t nodes[T3_JOINT_COUNT];
    uint16_t scratch[T3_JOINT_COUNT];
    sat_skeleton_pose_joint_t pose[T3_JOINT_COUNT];
    sat_skeleton_pose_joint_t fade_from[T3_JOINT_COUNT];
    sat_pose_fade_t fade;
    sat_skel_clip_player_t clip_player;
    sat_mat4_t joints[T3_JOINT_COUNT];
    sat_mesh_t mesh;
    sat_vec3_t* vertices;
    uint16_t indices[T3_FACE_COUNT * 4u];
    uint16_t face_materials[T3_FACE_COUNT];
    sat_vdp1_texture_t textures[T3_TEXTURE_COUNT];
    sat_texture_t owners[T3_TEXTURE_COUNT];
    sat_scene3d_material_t materials[T3_TEXTURE_COUNT];
    sat_scene3d_material_program_t material_commands[T3_TEXTURE_COUNT];
    sat_scene3d_material_programs_t material_programs;
    sat_projected_vertex_t* projected;
    sat_vec3_t* world_vertices;
    sat_scene3d_instance_t instance;
    sat_scene3d_instance_binding_t binding;
    sat_mat4_t world;
    uint8_t clip;
    int32_t last_x, last_z;
} actor_t;

static sat_character_asset_t g_asset;
static sat_skeleton_clip_t g_clips[T3_CLIP_COUNT];
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES
/* Decoded/uploaded once; preserve High WRAM for simulation and stack. */
static sat_model_texture_asset_t g_texture_tables[T3_TEXTURE_COUNT] __attribute__((section(".wram_l")));
static actor_t g_actors[2] __attribute__((section(".wram_l")));
#else
static sat_model_texture_asset_t g_texture_tables[T3_TEXTURE_COUNT];
static actor_t g_actors[2];
#endif
/* Keep independently mutable geometry together. Texture/material tables and
 * topology retain their own lifetime and bank; the placement option moves only
 * these hot arrays, with identical capacity and CPU publication rules. */
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES && !defined(T3_HIGH_GEOMETRY_SCRATCH)
static actor_geometry_t g_actor_geometry[2] __attribute__((section(".wram_l")));
#else
static actor_geometry_t g_actor_geometry[2];
#endif
static uint8_t g_initialized;
static uint8_t g_skin_preview_applied[2];
static uint32_t g_texture_vram_bytes, g_skin_work_ms;
#if defined(T3_PARALLEL_GEOMETRY)
static sat_scene3d_face_t g_geometry_faces[T3_FACE_COUNT] __attribute__((section(".wram_l")));
static uint32_t g_geometry_keys[T3_FACE_COUNT] __attribute__((section(".wram_l")));
static sat_scene3d_prepare_item_t g_geometry_item __attribute__((section(".wram_l")));
static sat_scene3d_prepare_batch_t g_geometry_batch __attribute__((section(".wram_l")));
static sat_scene3d_source_plan_t g_geometry_sources __attribute__((section(".wram_l")));
/* Materials have no vertex-Gouraud pointers; descriptor ranges plus topology. */
static sat_scene3d_source_range_t g_geometry_source_ranges[T3_TEXTURE_COUNT+5u]
    __attribute__((section(".wram_l")));
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
#define GEOMETRY_TIMEOUT_TICKS 60000u
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
    st = sat_skel_clip_player_set_clip(&actor->clip_player, &g_clips[next], 0);
    if (st == SAT_OK) actor->clip = next;
    return st;
}

sat_result_t t3_characters_init(t3_character_load_progress_fn progress, void* context) {
    unsigned i;
    sat_result_t st;
    if (g_initialized) return SAT_OK;
#if defined(T3_PARALLEL_GEOMETRY)
    const sat_parallel_config_t parallel={SAT_PARALLEL_AUTO,NULL,0,0,GEOMETRY_TIMEOUT_TICKS};
    if((st=sat_parallel_init(&parallel))!=SAT_OK)return st;
#endif
    if ((st = sat_character_asset_decode(
        t3_archive.bytes, T3_ARCHIVE_BYTES, g_clips, T3_CLIP_COUNT,
        g_texture_tables, T3_TEXTURE_COUNT, &g_asset)) != SAT_OK)
        return st;
    if ((st = sat_skin_validate(&g_asset.skin)) != SAT_OK) return st;
    if ((st = sat_model_validate(&g_asset.model)) != SAT_OK) return st;
    if (progress) progress(context, T3_CHARACTER_LOAD_MODEL_READY);

    for (i = 0; i < 2u; ++i) {
        actor_t* a = &g_actors[i];
        a->vertices = g_actor_geometry[i].vertices;
        a->projected = g_actor_geometry[i].projected;
        a->world_vertices = g_actor_geometry[i].world_vertices;
        sat_model_asset_t model = g_asset.model;
        if (i == 1u) {
            /* The second costume gets a separate CRAM bank, never overriding
             * the first actor's colors. Both share the same SKA1 geometry. */
#if !defined(T3_LOCAL_KMD_BIND) || !T3_ORIGINAL_TEXTURES
            model.palettes_rgb555 = t3_red_palette;
#endif
            model.palette_base = (uint16_t)(model.palette_base + 1u);
        }
#if defined(T3_LOCAL_KMD_BIND) && T3_ORIGINAL_TEXTURES
        /* These immutable model textures live for the application lifetime.
         * Use the public native model uploader; the logical texture registry
         * has 64 owner slots and is intended for individually managed assets. */
        if ((st = sat_model_upload_textures(&model,a->textures,T3_TEXTURE_COUNT)) != SAT_OK)
            return st;
        for (unsigned t=0;t<T3_TEXTURE_COUNT;++t) {
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
                a->textures, a->materials, T3_TEXTURE_COUNT)) != SAT_OK)
            return st;
#endif
        for (unsigned t=0;t<T3_TEXTURE_COUNT;++t)
            g_texture_vram_bytes+=(uint32_t)a->textures[t].width*a->textures[t].height/
                (a->textures[t].format==SAT_VDP1_TEXTURE_LUT4?2u:1u);
        if ((st = sat_mesh_init(&a->mesh, a->vertices, T3_VERTEX_COUNT,
                a->indices, T3_FACE_COUNT)) != SAT_OK) return st;
        if ((st = sat_model_copy_to_mesh(&g_asset.model, &a->mesh)) != SAT_OK)
            return st;
        if ((st = sat_skin_validate_mesh(&g_asset.skin, &a->mesh)) != SAT_OK)
            return st;
        __builtin_memcpy(a->face_materials, g_asset.model.face_texture_indices,
               sizeof(a->face_materials));
        if ((st = sat_skeleton_instance_init(&a->skeleton,
                &g_asset.skeleton, a->nodes, T3_JOINT_COUNT,
                a->scratch, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_skeleton_rest_pose(&g_asset.skeleton,
                a->pose, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_pose_fade_init(&a->fade,
                a->fade_from, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_skel_clip_player_init(&a->clip_player,
                &g_clips[T3_CLIP_IDLE])) != SAT_OK) return st;
        a->instance.mesh = &a->mesh;
        a->instance.materials = a->materials;
        a->instance.material_count = T3_TEXTURE_COUNT;
        a->instance.face_materials = a->face_materials;
        a->instance.world = &a->world;
        /* Topology, texture residency and the face/material mapping remain
         * immutable after initialization. Pay their validation once; skinning
         * changes only vertex coordinates, which the binding permits. */
        if ((st = sat_scene3d_instance_binding_init(&a->binding, &a->mesh,
                a->materials, T3_TEXTURE_COUNT, a->face_materials)) != SAT_OK)
            return st;
        if ((st = sat_scene3d_material_programs_init(&a->material_programs,
                a->materials, T3_TEXTURE_COUNT, a->material_commands,
                T3_TEXTURE_COUNT)) != SAT_OK) return st;
        a->binding.programs = &a->material_programs;
        a->instance.validated_binding = &a->binding;
        /* Preserve the original per-face two-sided flag when culling. */
#if T3_CLIP_COUNT == 1
        a->instance.cull_backfaces = 1u;
        a->instance.double_sided_faces = t3_kmd_double_sided;
#else
        a->instance.cull_backfaces = 1u;
#endif
        if (progress) progress(context, i == 0u ?
            T3_CHARACTER_LOAD_PLAYER1_READY : T3_CHARACTER_LOAD_PLAYER2_READY);
    }
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
    for (i = 0; i < 2u; ++i) {
        actor_t* a = &g_actors[i];
        a->clip = T3_CLIP_IDLE;
        g_skin_preview_applied[i]=0u;
        a->last_x = i ? 360 : -360;
        a->last_z = 0;
        if ((st = sat_skel_clip_player_set_clip(&a->clip_player,
                &g_clips[T3_CLIP_IDLE], 0)) != SAT_OK) return st;
        if ((st = sat_skeleton_rest_pose(&g_asset.skeleton,
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
    for (i = 0; i < 2u; ++i) {
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
static sat_result_t prepare_actors(void) {
    sat_result_t st;
    const uint32_t started=sat_time_ms();
    for (unsigned i = 0; i < T3_DRAW_ACTORS; ++i) {
        actor_t* a = &g_actors[i];
        T3_PERF_START(skeleton_started);
        g_skin_preview_applied[i]=0u;
#if defined(T3_ENABLE_PSX_SKIN_PREVIEW) && defined(T3_LOCAL_KMD_BIND)
        {
            t3_psx_joint_t originals[T3_PSX_DRAW_PARTS];
            uint32_t parts_mask=0u;
            int available=t3_move_probe_copy_world(i,originals,&parts_mask);
            /* Skip the base skeleton only when the bridge can supply every
             * joint referenced by this immutable skin. Accessory joints keep
             * their generic evaluation; an invalid original pose keeps the
             * complete existing fallback path. */
            if(T3_CLIP_COUNT==1 && available && g_asset.skin.max_joint<T3_PSX_VISIBLE_PARTS &&
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
        T3_PERF_START(skin_started);
        if ((st = sat_skin_update_mesh(&g_asset.skin,
                a->joints, T3_JOINT_COUNT, &a->mesh, 0, NULL)) != SAT_OK)
            return st;
        T3_PERF_ADD(skin,skin_started);
    }
    g_skin_work_ms=sat_time_ms()-started;
    return SAT_OK;
}

sat_result_t t3_characters_render(sat_scene_t* scene, const t3_match_t* game) {
    unsigned i;
    sat_result_t st;
    if (!scene || !game || !g_initialized) return SAT_ERR_INVALID_ARG;
    if ((st = prepare_actors()) != SAT_OK) return st;
    T3_PERF_START(placement_started);
    for (i = 0; i < T3_DRAW_ACTORS; ++i) {
        actor_t* a = &g_actors[i];
        const t3_fighter_t* fighter = &game->fighters[i];
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
                g_geometry_faces,g_geometry_keys,T3_FACE_COUNT))!=SAT_OK)return st;
        g_geometry_batch.dispatch=SAT_SCENE3D_PREPARE_DISPATCH_RUNTIME;
        sat_parallel_handle_t handle=0;
        if((st=sat_scene_prepare_batch_async(scene,&g_geometry_batch,&handle))!=SAT_OK)return st;
        actor_t* master=&g_actors[0];
        const sat_result_t master_result=sat_scene_submit_instance(scene,&master->instance,
            SAT_SCENE3D_SLOT_INHERIT,master->projected,master->world_vertices);
        if((st=sat_parallel_wait(handle,GEOMETRY_TIMEOUT_TICKS))!=SAT_OK)return st;
        if(master_result!=SAT_OK)return master_result;
#if defined(T3_VERIFY_PARALLEL_GEOMETRY)
        const sat_scene3d_faces_t prefix=scene->faces;
#endif
        if((st=sat_scene_merge_prepared_batch(scene,&g_geometry_batch,handle))!=SAT_OK)return st;
#if defined(T3_VERIFY_PARALLEL_GEOMETRY)
        /* Join/merge consume worker output and keys before comparison. Reuse
         * canonical queue storage for the reference; no extra face buffer.
         * Compare only fields that merge/emission consume, never padding or
         * the unspecified world coordinates of projected-safe faces. */
        sat_scene3d_faces_t reference=prefix;
        if((st=sat_scene3d_faces_submit_instance(&reference,&slave->instance,
                SAT_SCENE3D_SLOT_INHERIT,slave->projected,slave->world_vertices))!=SAT_OK)return st;
        if(reference.count-prefix.count!=g_geometry_batch.metrics.prepared_faces ||
           reference.culled_faces-prefix.culled_faces!=g_geometry_batch.metrics.culled_faces ||
           reference.clipped_faces-prefix.clipped_faces!=g_geometry_batch.metrics.clipped_faces)
            return SAT_ERR_VERIFY_FAILED;
        for(unsigned f=0;f<g_geometry_batch.metrics.prepared_faces;++f) {
            if(scene->faces.keys[prefix.count+f]!=g_geometry_keys[f] ||
               !same_face(&scene->faces.entries[prefix.count+f],&g_geometry_faces[f]))return SAT_ERR_VERIFY_FAILED;
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
    return fighter<2u?g_skin_preview_applied[fighter]:0u;
}

uint32_t t3_characters_texture_vram_bytes(void) {return g_texture_vram_bytes;}
uint32_t t3_characters_skin_work_ms(void) {return g_skin_work_ms;}
