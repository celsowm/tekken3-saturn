/*
 * Two independent animated actor instances over one shared SKA1 asset.
 * Every pose, matrix, mutable mesh, and material owner is caller-owned;
 * LibSaturn supplies animation sampling, skin deformation, scene projection,
 * texture residency, and palette management.
 */
#include "characters.h"

#include "saturn/character3d.h"
#include "saturn/math3d.h"
#include "saturn/skeleton3d.h"
#include "saturn/skin3d.h"

#define DT_Q16 (SAT_FX16_ONE / T3_HZ)
#define FADE_Q16 (SAT_FX16_ONE / 10)

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
    sat_vec3_t vertices[T3_VERTEX_COUNT];
    uint16_t indices[T3_FACE_COUNT * 4u];
    uint16_t face_materials[T3_FACE_COUNT];
    sat_vdp1_texture_t textures[T3_TEXTURE_COUNT];
    sat_texture_t owners[T3_TEXTURE_COUNT];
    sat_scene3d_material_t materials[T3_TEXTURE_COUNT];
    sat_projected_vertex_t projected[T3_VERTEX_COUNT];
    sat_vec3_t world_vertices[T3_VERTEX_COUNT];
    sat_scene3d_instance_t instance;
    sat_mat4_t world;
    uint8_t clip;
    int32_t last_x, last_z;
} actor_t;

static sat_character_asset_t g_asset;
static sat_skeleton_clip_t g_clips[T3_CLIP_COUNT];
static sat_model_texture_asset_t g_texture_tables[T3_TEXTURE_COUNT];
static actor_t g_actors[2];
static uint8_t g_initialized;

static uint8_t clip_for(const t3_fighter_t* fighter, int moving) {
    if (!fighter->health) return T3_CLIP_KNOCKDOWN;
    if (fighter->stun) return T3_CLIP_HIT;
    if (fighter->attack)
        return fighter->attack <= 2u ? T3_CLIP_PUNCH : T3_CLIP_KICK;
    if (fighter->guarding) return T3_CLIP_GUARD;
    return moving ? T3_CLIP_WALK : T3_CLIP_IDLE;
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

sat_result_t t3_characters_init(void) {
    unsigned i;
    sat_result_t st;
    if (g_initialized) return SAT_OK;
    if ((st = sat_character_asset_decode(
        t3_archive.bytes, T3_ARCHIVE_BYTES, g_clips, T3_CLIP_COUNT,
        g_texture_tables, T3_TEXTURE_COUNT, &g_asset)) != SAT_OK)
        return st;
    if ((st = sat_skin_validate(&g_asset.skin)) != SAT_OK) return st;
    if ((st = sat_model_validate(&g_asset.model)) != SAT_OK) return st;

    for (i = 0; i < 2u; ++i) {
        actor_t* a = &g_actors[i];
        sat_model_asset_t model = g_asset.model;
        if (i == 1u) {
            /* The second costume gets a separate CRAM bank, never overriding
             * the first actor's colors. Both share the same SKA1 geometry. */
            model.palettes_rgb555 = t3_red_palette;
            model.palette_base = (uint16_t)(model.palette_base + 1u);
        }
        if ((st = sat_character_materials_upload(&model, a->owners,
                a->textures, a->materials, T3_TEXTURE_COUNT)) != SAT_OK)
            return st;
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
        a->instance.cull_backfaces = 1u;
    }
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
        if ((st = sat_skel_clip_player_sample(&a->clip_player,
                a->pose, T3_JOINT_COUNT)) != SAT_OK) return st;
        sat_pose_fade_advance(&a->fade, DT_Q16);
        if ((st = sat_pose_fade_apply(&a->fade, a->pose)) != SAT_OK) return st;
        if ((st = sat_skeleton_evaluate(&a->skeleton,
                a->pose, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_skeleton_world_matrices(&a->skeleton,
                a->joints, T3_JOINT_COUNT)) != SAT_OK) return st;
        if ((st = sat_skin_update_mesh(&g_asset.skin,
                a->joints, T3_JOINT_COUNT, &a->mesh, 0, NULL)) != SAT_OK)
            return st;
    }
    return SAT_OK;
}

sat_result_t t3_characters_render(sat_scene_t* scene, const t3_match_t* game) {
    unsigned i;
    sat_result_t st;
    if (!scene || !game || !g_initialized) return SAT_ERR_INVALID_ARG;
    for (i = 0; i < 2u; ++i) {
        actor_t* a = &g_actors[i];
        const t3_fighter_t* fighter = &game->fighters[i];
        sat_model_transform3d_t transform;
        sat_model_transform3d_identity(&transform);
        transform.position.x = (sat_fx16_t)(fighter->x * (SAT_FX16_ONE / 256));
        transform.position.z = (sat_fx16_t)(fighter->z * (SAT_FX16_ONE / 256));
        /* Original example model faces +Z. Turn its front towards rival. */
        transform.rotation_deg.y = (fighter->facing > 0 ? 90 : -90) * SAT_FX16_ONE;
        if ((st = sat_model_transform3d_matrix(&transform, &a->world))
                != SAT_OK) return st;
        if ((st = sat_scene_submit_instance(scene, &a->instance,
                SAT_SCENE3D_SLOT_INHERIT, a->projected, a->world_vertices))
                != SAT_OK) return st;
    }
    return SAT_OK;
}
