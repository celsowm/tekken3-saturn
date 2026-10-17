#include "move_probe.h"
#if defined(T3_LOCAL_DIVMOT_ENABLED)
#include "t3_local_divmot.h"
/* Read by a debugger after each simulation tick, not disk I/O/VDP1.
 * Epoch odd = write in progress; even and unchanged = stable pair. */
volatile uint32_t g_t3_pose_trace_epoch;
volatile uint8_t g_t3_pose_trace[2][T3_PSX_TRACE_BYTES] __attribute__((section(".wram_l")));
static uint32_t g_t3_pose_trace_tick;
#include "divmot.h"
#include "move_vm.h"
#include "attack_records.h"
#include "psx_move_events.h"
#include "psx_motion.h"
#include "psx_pose.h"
#include "psx_ik.h"
#include "psx_contact.h"
#include "psx_shadow_combat.h"
#include "psx_root.h"
#include "psx_match.h"
#include "t3_generated_character.h"

static t3_divmot_t g_bank, g_common;
#if !defined(T3_ENABLE_PSX_OWNED_MATCH)
static t3_motion_cache_t g_pose_cache[2],g_root_cache[2];
#endif
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
static t3_psx_match_t g_owned_match __attribute__((section(".wram_l")));
/* Reset assigns the entire object before match initialization. */
static t3_psx_match_config_t g_owned_config __attribute__((section(".wram_l")));
#if defined(T3_ENABLE_PSX_COMBAT)
static t3_psx_replay_storage_t g_replay_history __attribute__((section(".wram_l")));
#endif
#define VM(i) (g_owned_match.fighter[(i)].vm)
#else
static t3_move_vm_t g_preview_vms[2];
#define VM(i) (g_preview_vms[(i)])
#endif
static uint8_t g_active, g_round;
static unsigned g_start_slot;
static uint8_t g_segments[2];
static uint8_t g_move_event_status[2],g_move_extra_kind[2],g_move_extra_damage[2];
static uint16_t g_move_event_fired[2];
static uint32_t g_decode_errors[2];
static uint8_t g_pose_status[2],g_pose_frames[2];
static int16_t g_root_pose[2][3];
static uint32_t g_fk_mask[2],g_fk_parts[2],g_ik_mask[2];
static uint8_t g_hurt_ready[2];
static int32_t g_hurt_head_y[2];
#if defined(T3_LOCAL_KMD_BIND)
static t3_psx_joint_t g_world_pose[2][T3_PSX_DRAW_PARTS] __attribute__((section(".wram_l")));
#endif
static int16_t g_fk_root_xx[2];
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
/* Debugger-written external WORLD placement; never inferred from the
 * prototype's Q8 fighter coordinates. Writer: odd epoch, update roots
 * and valid_mask, even epoch. Reader accepts only unchanged even epoch.
 * SH2 structs and fields use target-native BIG endian. */
volatile uint32_t g_t3_shadow_root_epoch;
volatile uint32_t g_t3_shadow_root_valid_mask;
volatile t3_psx_root_t g_t3_shadow_external_roots[2];
static t3_psx_shadow_state_t g_shadow;
static t3_psx_shadow_result_t g_shadow_result[2];
static uint8_t g_shadow_valid;
static uint32_t g_shadow_last_injected_epoch;
#endif

sat_result_t t3_move_probe_init(void) {
    if (!t3_divmot_open(&g_bank,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES))
        return SAT_ERR_INVALID_ARG;
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
    if (!t3_divmot_open(&g_common,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES))
        return SAT_ERR_INVALID_ARG;
#endif
#if T3_LOCAL_COMMON_BRANCH_BYTES > 0
    if (!t3_divmot_set_common_branches(&g_bank,t3_local_common_branches,
                                       T3_LOCAL_COMMON_BRANCH_BYTES))
        return SAT_ERR_INVALID_ARG;
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
    if (!t3_divmot_set_common_branches(&g_common,t3_local_common_branches,
                                       T3_LOCAL_COMMON_BRANCH_BYTES))
        return SAT_ERR_INVALID_ARG;
#endif
#endif
    /* Slot 3 is the ordinary Tekken standing stance. Fallback is useful for
     * synthetic tests, but must not be mistaken for authentic startup. */
    g_start_slot=3u;
    if (t3_divmot_slot(&g_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
          &g_common,
#else
          NULL,
#endif
          g_start_slot,&VM(0).move)!=1) g_start_slot=0u;
    g_active=1;
#if defined(T3_ENABLE_PSX_COMBAT)
    /* Low WRAM is NOLOAD; only the first initialization clears storage.
     * Later Start resets invalidate the ring through its monotonic epoch. */
    __builtin_memset(&g_replay_history,0,sizeof(g_replay_history));
#endif
    t3_move_probe_reset(1u);
    return g_active?SAT_OK:SAT_ERR_INVALID_ARG;
}

void t3_move_probe_reset(unsigned round) {
    unsigned i;
    if (!g_active) return;
    g_round=(uint8_t)round;
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    g_owned_config=(t3_psx_match_config_t){0};
    g_owned_config.bank=&g_bank;g_owned_config.common=&g_common;
    g_owned_config.ground_tables=(t3_psx_ground_tables_t){
        {t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},t3_local_ground_blob,1025u,
        t3_local_ground_blob+1025u,T3_LOCAL_GROUND_BYTES-1025u};
    g_owned_config.spline=(t3_motion_tables_t){t3_local_spline_blob,T3_LOCAL_SPLINE_BYTES};
    g_owned_config.sequences=t3_local_sequences;g_owned_config.sequence_count=T3_LOCAL_SEQUENCE_COUNT;
    g_owned_config.enable_body=1;
#if defined(T3_ENABLE_PSX_COMBAT)
    g_owned_config.enable_combat=1;
    g_owned_config.enable_rounds=2;
    g_owned_config.replay_storage=&g_replay_history;
    g_owned_config.collapse_on_ko=1; /* measured USA VS regional policy */
    g_owned_config.reactions=t3_local_combat_reactions;
    g_owned_config.reactions_size=T3_LOCAL_COMBAT_REACTION_BYTES;
#endif
    g_owned_config.parts[0]=t3_private_actor_parts[0];g_owned_config.parts[1]=t3_private_actor_parts[1];
    g_owned_config.ik=(t3_psx_ik_tables_t){{t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},t3_local_ik_blob,T3_LOCAL_IK_BYTES};
    g_owned_config.body_tables=t3_local_body_blob;g_owned_config.body_tables_size=T3_LOCAL_BODY_BYTES;
#if defined(T3_LOCAL_RSQRT_BYTES) && T3_LOCAL_RSQRT_BYTES > 0
    g_owned_config.rsqrt=(t3_psx_rsqrt_t){t3_local_rsqrt_blob,T3_LOCAL_RSQRT_BYTES};
#endif
    g_owned_config.scale_percent=T3_KMD_SCALE_PERCENT;g_owned_config.max_health_q16=140*65536;
    /* Measured USA Paul mirror round placement; render conversion is separate. */
    g_owned_config.spawn[0]=(t3_psx_spawn_t){{-1000,0,-4},{-1000,-1027,-4},16384u,16384u};
    g_owned_config.spawn[1]=(t3_psx_spawn_t){{1000,0,4},{1000,-1027,4},49151u,49151u};
    if(!t3_psx_match_init(&g_owned_match,&g_owned_config)){g_active=0u;return;}
#endif
    for (i=0;i<2u;++i) {
#if !defined(T3_ENABLE_PSX_OWNED_MATCH)
        g_pose_cache[i]=(t3_motion_cache_t){0};
        g_root_cache[i]=(t3_motion_cache_t){0};
#endif
        g_segments[i]=0u;
        g_move_event_status[i]=0u;
        g_move_extra_kind[i]=0u;
        g_move_extra_damage[i]=0u;
        g_move_event_fired[i]=0u;
        g_decode_errors[i]=0u;
        g_pose_status[i]=0u;
        g_pose_frames[i]=0u;
        g_fk_mask[i]=0u;
        g_fk_parts[i]=0u;
        g_hurt_ready[i]=0u;
        g_hurt_head_y[i]=0;
        g_ik_mask[i]=0u;
        g_fk_root_xx[i]=0;
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
        g_shadow_result[i].status=T3_SHADOW_INACTIVE;
        g_shadow_result[i].new_contact=0u;
        g_shadow_result[i].zone=-1;
        g_shadow_result[i].segment=-1;
#endif
        g_root_pose[i][0]=g_root_pose[i][1]=g_root_pose[i][2]=0;
#if !defined(T3_ENABLE_PSX_OWNED_MATCH)
#if defined(T3_ENABLE_PSX_MOVE_LIFECYCLE)
        if (!t3_move_vm_start_phased(&VM(i),&g_bank,
#else
        if (!t3_move_vm_start(&VM(i),&g_bank,
#endif
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
                   &g_common,
#else
                   NULL,
#endif
                   g_start_slot)) g_active=0u;
#endif
    }
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    t3_psx_shadow_reset(&g_shadow);
    g_shadow_valid=0u;
    g_t3_shadow_root_valid_mask=0u; /* debug writer must supply fresh data */
    g_shadow_last_injected_epoch=g_t3_shadow_root_epoch;
#endif
}

void t3_move_probe_tick(const t3_match_t* match) {
    unsigned i;
    if (!g_active || !match) return;
    if (g_round!=match->round) t3_move_probe_reset(match->round);
#if !defined(T3_ENABLE_PSX_COMBAT)
    if (match->round_done || match->match_done || !g_active) return;
#endif
    for (i=0;i<2u;++i) {
        t3_attack_sample_t sampled;
        int decoded;
        {
            unsigned before=VM(i).clock.pose_frame;
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
            before=g_owned_match.fighter[i].previous_pose_frame;
            int transitioned=g_owned_match.fighter[i].started;
#elif defined(T3_ENABLE_PSX_MOVE_LIFECYCLE)
            /* Visual lifecycle preview. The enclosing match still owns
             * provisional HP/physics, so never fabricate original hit facts. */
            const t3_move_start_facts_t no_hit={0u,1u,0};
            int transitioned=t3_move_vm_begin(&VM(i),no_hit);
#else
            int transitioned=t3_move_vm_tick_ex(
                &VM(i),&match->input_histories[i],
                NULL,t3_local_sequences,T3_LOCAL_SEQUENCE_COUNT);
#endif
            t3_psx_move_events_t events;
#if defined(T3_ENABLE_PSX_COMBAT)
            int rc=1;events=g_owned_match.fighter[i].events;
            (void)transitioned;(void)before;
#else
            int rc=transitioned<0?-1:
                t3_psx_move_events_sample(VM(i).active_bank,
                    &VM(i).move,transitioned==1?0u:before,
                    VM(i).clock.pose_frame,&events);
#endif
            g_move_event_status[i]=rc==1?1u:(rc==0?2u:3u);
            g_move_extra_kind[i]=rc==1?events.extra_kind:0u;
            g_move_extra_damage[i]=rc==1?events.extra_damage:0u;
            g_move_event_fired[i]=rc==1?events.fired:0u;
            if(rc<0)++g_decode_errors[i];
        }
        decoded=t3_attack_sample(VM(i).active_bank,&VM(i).move,
                                 VM(i).clock.pose_frame,0,&sampled);
        g_segments[i]=decoded==1?sampled.segment_count:0u;
        if(decoded<0) ++g_decode_errors[i];
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
        {
            const t3_psx_fighter_t* f=&g_owned_match.fighter[i];
            t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES];
            g_pose_status[i]=1;g_pose_frames[i]=f->pose_frames;
            g_fk_mask[i]=f->fk_mask;g_fk_parts[i]=f->parts_mask;
            g_ik_mask[i]=f->ik_mask;g_fk_root_xx[i]=f->fk_root_xx;
            for(unsigned axis=0;axis<3;++axis)g_root_pose[i][axis]=f->pose[axis];
            /* Skin geometry is KMD mesh order. Collision state is original
             * joint-slot order; use the model's explicit map at this boundary. */
            for(unsigned part=0;part<T3_PSX_DRAW_PARTS;++part)
                g_world_pose[i][part]=f->world[t3_private_kmd_parts[part].slot];
            g_hurt_ready[i]=(uint8_t)t3_psx_hurt_from_parts(f->world,f->parts_mask,0,zones);
            g_hurt_head_y[i]=g_hurt_ready[i]?zones[0].center.y:0;
        }
#else
        {
            const t3_motion_stream_t* motion;
            int opened=t3_motion_open_cached(&g_pose_cache[i],VM(i).active_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
                &g_common,
#else
                NULL,
#endif
                VM(i).move.animation_offset,&motion);
            g_pose_status[i]=0u;
            g_pose_frames[i]=0u;
            g_fk_mask[i]=0u;
            g_fk_parts[i]=0u;
            g_hurt_ready[i]=0u;
            g_hurt_head_y[i]=0;
            g_ik_mask[i]=0u;
            g_fk_root_xx[i]=0;
            if(opened==1) {
                int16_t pose[T3_PSX_POSE_CHANNELS];
                int16_t root[3];
                int full, simple;
#if T3_LOCAL_SPLINE_BYTES > 0
                const t3_motion_tables_t tables={
                    t3_local_spline_blob,T3_LOCAL_SPLINE_BYTES};
                const t3_motion_tables_t* weights=&tables;
#else
                const t3_motion_tables_t* weights=NULL;
#endif
                g_pose_frames[i]=motion->frames;
                full=t3_motion_sample(motion,
                    (unsigned)VM(i).clock.pose_frame-(VM(i).phased?1u:0u),weights,pose);
                if(full==1) {
#if defined(T3_ENABLE_PSX_MOVE_LIFECYCLE)
                    const t3_motion_stream_t* root_motion;
                    if(t3_motion_open_cached(&g_root_cache[i],VM(i).root_bank,VM(i).common,
                            VM(i).root_move.animation_offset,&root_motion)!=1 ||
                       t3_motion_sample_root(root_motion,
                            (unsigned)VM(i).clock.root_frame-1u,weights,root)!=1) {
                        g_pose_status[i]=4u;++g_decode_errors[i];continue;
                    }
                    /* A held transition can keep the previous root stream
                     * while the new move drives the limbs. */
                    pose[0]=root[0];pose[1]=root[1];pose[2]=root[2];
#endif
                    g_pose_status[i]=1u; /* all 49 channels available */
                    g_root_pose[i][0]=pose[0];
                    g_root_pose[i][1]=pose[1];
                    g_root_pose[i][2]=pose[2];
#if T3_LOCAL_TRIG_BYTES > 0
                    {
                        const t3_psx_trig_t trig={
                            t3_local_trig_blob,T3_LOCAL_TRIG_BYTES};
                        t3_psx_matrix_t slots[T3_PSX_ROTATION_SLOTS];
                        uint32_t valid=0u;
                        if(t3_psx_pose_fk(pose,&trig,slots,&valid)) {
                            g_fk_mask[i]=valid;
                            g_fk_root_xx[i]=slots[0].m[0][0];
#if T3_LOCAL_IK_BYTES > 0
                            {
                                const t3_psx_ik_tables_t ik_tables={
                                    {t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},
                                    t3_local_ik_blob,T3_LOCAL_IK_BYTES};
                                t3_psx_matrix_t complete[T3_PSX_ROTATION_SLOTS];
                                uint32_t complete_mask=0u;
                                if(t3_psx_pose_ik(pose,&ik_tables,slots,valid,
                                                  complete,&complete_mask)==1) {
                                    unsigned k;
                                    for(k=0u;k<T3_PSX_ROTATION_SLOTS;++k)
                                        slots[k]=complete[k];
                                    g_ik_mask[i]=complete_mask & ~valid;
                                    valid=complete_mask;
                                }
                            }
#endif
#if defined(T3_LOCAL_KMD_BIND)
                            {
                                t3_psx_joint_t world[T3_PSX_DRAW_PARTS];
                                t3_psx_joint_t root_frame={0};
                                uint32_t partmask=0u;
                                /* Preview poses are model-local. Owned mode
                                 * uses the authoritative original arena frame;
                                 * renderer centering is a separate transform. */
                                t3_vec3i_t zero={0};
                                uint16_t facing=0x8000u,heading=0x8000u;
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
                                zero=g_owned_match.fighter[i].ground.anchor;
                                facing=g_owned_match.fighter[i].ground.facing;
                                heading=g_owned_match.fighter[i].ground.heading;
#endif
                                int root_ok=t3_psx_pose_world_root(pose,
                                    T3_KMD_SCALE_PERCENT,zero,
                                    facing,heading,&trig,&root_frame);
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
                                root_frame.origin=g_owned_match.fighter[i].ground.root;
#endif
                                if(root_ok && t3_psx_compose_parts(t3_private_kmd_parts,
                                    slots,valid,&root_frame,world,&partmask)) {
                                    g_fk_parts[i]=partmask;
                                    if((partmask & 0x3FFFFu)==0x3FFFFu) {
                                        t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES];
                                        __builtin_memcpy(g_world_pose[i],world,
                                                         sizeof(world));
                                        /* Basis follows this mode's pose:
                                         * model-local preview or owned world.
                                         * Contact is not activated here. */
                                        if(t3_psx_parts_to_slots(t3_private_kmd_parts,world,partmask,world,&partmask) &&
                                           t3_psx_hurt_from_parts(world,partmask,
                                                                  0,zones)) {
                                            g_hurt_ready[i]=1u;
                                            g_hurt_head_y[i]=zones[0].center.y;
                                        }
                                    }
                                }
                            }
#endif
                        }
                    }
#endif
                } else {
                    simple=t3_motion_sample_root(motion,
                        (unsigned)VM(i).clock.pose_frame-(VM(i).phased?1u:0u),
                                                  weights,root);
                    if(simple==1) {
                        g_pose_status[i]=2u; /* only scalar XYZ root available */
                        g_root_pose[i][0]=root[0];
                        g_root_pose[i][1]=root[1];
                        g_root_pose[i][2]=root[2];
                    } else {
                        g_pose_status[i]=(full<0 || simple<0)?4u:3u;
                        /* 3 = missing original interpolation tables,
                         * 4 = corrupt or unsupported authored stream. */
                    }
                }
            } else if(opened<0) {
                g_pose_status[i]=4u;
            }
        }
#endif
    }
#if defined(T3_ENABLE_PSX_MOVE_LIFECYCLE) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    /* End-of-step input decisions become PENDING, never the rendered move
     * until a later begin phase satisfies their entry-frame condition. */
    for(i=0;i<2u;++i) {
        if(t3_move_vm_queue_input(&VM(i),&match->input_histories[i],NULL,
                t3_local_sequences,T3_LOCAL_SEQUENCE_COUNT)<0)++g_decode_errors[i];
    }
#endif

#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    {
        t3_psx_shadow_actor_t actors[2]={{0}};
        t3_psx_joint_t collision_joints[2][T3_PSX_DRAW_PARTS];
        t3_attack_sample_t attacks[2];
        t3_psx_root_t roots[2];
        uint32_t begin=g_t3_shadow_root_epoch, end;
        uint32_t valid;
        unsigned fighter;
        if(!(begin&1u)) {
            valid=g_t3_shadow_root_valid_mask;
            for(fighter=0u;fighter<2u;++fighter) {
                const volatile t3_psx_root_t* p=&g_t3_shadow_external_roots[fighter];
                roots[fighter].origin.x=p->origin.x;
                roots[fighter].origin.y=p->origin.y;
                roots[fighter].origin.z=p->origin.z;
                roots[fighter].sin_q12=p->sin_q12;
                roots[fighter].cos_q12=p->cos_q12;
            }
            end=g_t3_shadow_root_epoch;
            /* Reusing last tick's roots is not permitted: every sim
             * step requires a newly published, consistent PS1 frame.
             * This also fails closed if multiple sim ticks occur during
             * one VBlank without a matching fresh debugger update. */
            if(begin==end && !(end&1u) && (valid&3u)==3u &&
               end!=g_shadow_last_injected_epoch) {
                g_shadow_last_injected_epoch=end;
                for(fighter=0u;fighter<2u;++fighter) {
                    const t3_move_vm_t* vm=&VM(fighter);
                    int decoded=t3_attack_sample(vm->active_bank,&vm->move,
                                                 vm->clock.pose_frame,0,&attacks[fighter]);
                    actors[fighter].root=roots[fighter];
                    uint32_t collision_mask=0;
                    int remapped=t3_psx_parts_to_slots(t3_private_kmd_parts,g_world_pose[fighter],
                                g_fk_parts[fighter],collision_joints[fighter],&collision_mask);
                    actors[fighter].parts=collision_joints[fighter];
                    actors[fighter].pose_available=(uint8_t)
                        (remapped && (collision_mask&T3_PSX_BODY_PART_MASK)==
                           T3_PSX_BODY_PART_MASK);
                    actors[fighter].parts_mask=collision_mask;
                    actors[fighter].move_slot=vm->slot;
                    /* t3_psx_sweep_sample expects ACTIVE-WINDOW-relative time,
                     * not the raw DIVMOT move frame. */
                    actors[fighter].attack_frame=
                        vm->clock.pose_frame>=vm->move.active_first?
                        (uint16_t)(vm->clock.pose_frame-vm->move.active_first):0u;
                    if(decoded==1 && attacks[fighter].segment_count)
                        actors[fighter].attack=&attacks[fighter];
                    else if(decoded<0)
                        actors[fighter].attack=NULL; /* no fabricated segment */
                }
                g_shadow_valid=(uint8_t)t3_psx_shadow_step(
                    &g_shadow,actors,g_shadow_result);
            } else g_shadow_valid=0u;
        } else g_shadow_valid=0u;
        if(!g_shadow_valid) {
            /* Missing or unsynchronized injected roots invalidate historical
             * endpoints. Do not reuse geometry from the last valid packet. */
            t3_psx_shadow_reset(&g_shadow);
            for(fighter=0u;fighter<2u;++fighter) {
                g_shadow_result[fighter].status=T3_SHADOW_MISSING;
                g_shadow_result[fighter].new_contact=0u;
                g_shadow_result[fighter].zone=-1;
                g_shadow_result[fighter].segment=-1;
            }
        }
    }
#endif
    {
        unsigned fighter, byte;
        ++g_t3_pose_trace_epoch; /* start write; odd */
        for(fighter=0u;fighter<2u;++fighter) {
            uint8_t encoded[T3_PSX_TRACE_BYTES];
            uint32_t mask=0u;
            const t3_psx_joint_t* joints=NULL;
#if defined(T3_LOCAL_KMD_BIND)
            if((g_fk_parts[fighter]&T3_PSX_TRACE_BODY_MASK)==
                            T3_PSX_TRACE_BODY_MASK) {
                mask=T3_PSX_TRACE_BODY_MASK;
                joints=g_world_pose[fighter];
            }
#endif
            if(!t3_psx_trace_encode(g_t3_pose_trace_tick,fighter,
                    VM(fighter).slot,VM(fighter).clock.pose_frame,
                    joints,mask,encoded,sizeof(encoded))) {
                /* Unexpected invalid part coordinates: never expose stale
                 * valid joints to the oracle harness. */
                (void)t3_psx_trace_encode(g_t3_pose_trace_tick,fighter,
                    VM(fighter).slot,VM(fighter).clock.pose_frame,
                    NULL,0u,encoded,sizeof(encoded));
            }
            for(byte=0u;byte<T3_PSX_TRACE_BYTES;++byte)
                g_t3_pose_trace[fighter][byte]=encoded[byte];
        }
        ++g_t3_pose_trace_epoch; /* release stable paired records */
        ++g_t3_pose_trace_tick;
    }
}

uint8_t t3_move_probe_active(void) {return g_active;}
uint16_t t3_move_probe_slot(unsigned i) {return i<2u && g_active?VM(i).slot:0;}
uint16_t t3_move_probe_frame(unsigned i) {return i<2u && g_active?VM(i).clock.pose_frame:0;}
uint8_t t3_move_probe_phased(void) {return g_active?VM(0).phased:0u;}
int16_t t3_move_probe_pending_slot(unsigned i) {
    return i<2u && g_active && VM(i).pending_valid?(int16_t)VM(i).pending_slot:-1;
}
int16_t t3_move_probe_root_frame(unsigned i) {return i<2u && g_active?VM(i).clock.root_frame:0;}
int16_t t3_move_probe_move_age(unsigned i) {return i<2u && g_active?VM(i).clock.move_frame:0;}
int16_t t3_move_probe_freeze(unsigned i) {return i<2u && g_active?VM(i).clock.hit_freeze:0;}
int16_t t3_move_probe_hold_frames(unsigned i) {return i<2u && g_active?VM(i).clock.hold_frames:0;}
uint16_t t3_move_probe_attack_word(unsigned i) {return i<2u && g_active?VM(i).move.attack_word:0;}
uint32_t t3_move_probe_transitions(unsigned i) {return i<2u && g_active?VM(i).transitions:0;}
uint8_t t3_move_probe_segments(unsigned i) {return i<2u && g_active?g_segments[i]:0u;}
uint8_t t3_move_probe_event_status(unsigned i) {
    return i<2u && g_active?g_move_event_status[i]:0u;
}
uint8_t t3_move_probe_extra_kind(unsigned i) {
    return i<2u && g_active?g_move_extra_kind[i]:0u;
}
uint8_t t3_move_probe_extra_damage(unsigned i) {
    return i<2u && g_active?g_move_extra_damage[i]:0u;
}
uint16_t t3_move_probe_event_fired(unsigned i) {
    return i<2u && g_active?g_move_event_fired[i]:0u;
}
uint32_t t3_move_probe_decode_errors(unsigned i) {return i<2u && g_active?g_decode_errors[i]:0u;}
uint8_t t3_move_probe_pose_status(unsigned i) {return i<2u && g_active?g_pose_status[i]:0u;}
uint8_t t3_move_probe_pose_frames(unsigned i) {return i<2u && g_active?g_pose_frames[i]:0u;}
int16_t t3_move_probe_root_pose(unsigned i,unsigned axis) {
    return i<2u && axis<3u && g_active?g_root_pose[i][axis]:0;
}
uint32_t t3_move_probe_fk_mask(unsigned i) {return i<2u && g_active?g_fk_mask[i]:0u;}
uint32_t t3_move_probe_fk_parts(unsigned i) {return i<2u && g_active?g_fk_parts[i]:0u;}
int t3_move_probe_copy_world(unsigned i, t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                             uint32_t* mask) {
#if defined(T3_LOCAL_KMD_BIND)
    if(i>=2u || !g_active || !out || !mask ||
       (g_fk_parts[i]&0x3FFFFu)!=0x3FFFFu)return 0;
    __builtin_memcpy(out,g_world_pose[i],sizeof(g_world_pose[i]));
    *mask=g_fk_parts[i];
    return 1;
#else
    (void)i;(void)out;(void)mask;
    return 0;
#endif
}
uint32_t t3_move_probe_ik_mask(unsigned i) {return i<2u && g_active?g_ik_mask[i]:0u;}
uint8_t t3_move_probe_shadow_status(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    return i<2u && g_active?g_shadow_result[i].status:0u;
#else
    (void)i;return 0u;
#endif
}
int8_t t3_move_probe_shadow_zone(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    return i<2u && g_active?g_shadow_result[i].zone:-1;
#else
    (void)i;return -1;
#endif
}
uint32_t t3_move_probe_shadow_edges(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    return i<2u && g_active?g_shadow.contact_edges[i]:0u;
#else
    (void)i;return 0u;
#endif
}
uint32_t t3_move_probe_shadow_frames(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND) && !defined(T3_ENABLE_PSX_OWNED_MATCH)
    return i<2u && g_active?g_shadow.contact_frames[i]:0u;
#else
    (void)i;return 0u;
#endif
}
uint8_t t3_move_probe_hurt_ready(unsigned i) {
    return i<2u && g_active ? g_hurt_ready[i] : 0u;
}
int32_t t3_move_probe_hurt_head_y(unsigned i) {
    return i<2u && g_active ? g_hurt_head_y[i] : 0;
}
int16_t t3_move_probe_fk_root_xx(unsigned i) {return i<2u && g_active?g_fk_root_xx[i]:0;}

#else
sat_result_t t3_move_probe_init(void) {return SAT_OK;}
void t3_move_probe_reset(unsigned round) {(void)round;}
void t3_move_probe_tick(const t3_match_t* match) {(void)match;}
uint8_t t3_move_probe_active(void) {return 0u;}
uint16_t t3_move_probe_slot(unsigned i) {(void)i;return 0u;}
uint16_t t3_move_probe_frame(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_phased(void) {return 0u;}
int16_t t3_move_probe_pending_slot(unsigned i) {(void)i;return -1;}
int16_t t3_move_probe_root_frame(unsigned i) {(void)i;return 0;}
int16_t t3_move_probe_move_age(unsigned i) {(void)i;return 0;}
int16_t t3_move_probe_freeze(unsigned i) {(void)i;return 0;}
int16_t t3_move_probe_hold_frames(unsigned i) {(void)i;return 0;}
uint16_t t3_move_probe_attack_word(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_transitions(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_segments(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_event_status(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_extra_kind(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_extra_damage(unsigned i) {(void)i;return 0u;}
uint16_t t3_move_probe_event_fired(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_decode_errors(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_pose_status(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_pose_frames(unsigned i) {(void)i;return 0u;}
int16_t t3_move_probe_root_pose(unsigned i,unsigned axis) {(void)i;(void)axis;return 0;}
uint32_t t3_move_probe_fk_mask(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_fk_parts(unsigned i) {(void)i;return 0u;}
int t3_move_probe_copy_world(unsigned i, t3_psx_joint_t out[T3_PSX_DRAW_PARTS],
                             uint32_t* mask) {
    (void)i;(void)out;(void)mask;return 0;
}
uint32_t t3_move_probe_ik_mask(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_shadow_status(unsigned i) {(void)i;return 0u;}
int8_t t3_move_probe_shadow_zone(unsigned i) {(void)i;return -1;}
uint32_t t3_move_probe_shadow_edges(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_shadow_frames(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_hurt_ready(unsigned i) {(void)i;return 0u;}
int32_t t3_move_probe_hurt_head_y(unsigned i) {(void)i;return 0;}

int16_t t3_move_probe_fk_root_xx(unsigned i) {(void)i;return 0;}

#endif

/* Available in baseline builds too, so the main loop has one explicit switch. */
uint8_t t3_move_probe_owned(void) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    return 1u;
#else
    return 0u;
#endif
}
uint8_t t3_move_probe_combat(void) {
#if defined(T3_ENABLE_PSX_COMBAT)
    return 1;
#else
    return 0;
#endif
}
int32_t t3_move_probe_owned_value(unsigned i,unsigned field) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    if(i<2u && g_active) {
        const t3_psx_fighter_t* f=&g_owned_match.fighter[i];
        switch(field) {
        case 0:return f->health_q16;
        case 1:return f->ground.root.x;
        case 2:return f->ground.root.y;
        case 3:return f->ground.root.z;
        case 4:return f->ground.anchor.x;
        case 5:return f->ground.anchor.y;
        case 6:return f->ground.anchor.z;
        case 7:return (int32_t)f->body_contacts;
        case 8:return f->body.push.x;
        case 9:return f->body.push.y;
        case 10:return f->body.push.z;
        case 11:return f->geometry.status;
        case 12:return f->geometry.zone;
        case 13:return f->hit_gate.status;
        case 14:return (int32_t)f->contact_frames;
        case 15:return (int32_t)f->gate_frames;
        case 16:return (int32_t)f->registered_hits;
        case 17:return (int32_t)f->registered_blocks;
        case 18:return (int32_t)f->unsupported_moves;
        case 19:return f->hit.total_damage;
        case 20:return f->reaction.selected?f->reaction.move_slot:-1;
        case 21:return f->reaction.selected?f->reaction.transition:-1;
        case 22:return f->hit_done;
        case 23:return f->hit.guarded;
        case 24:return f->hit.ko_started;
        case 25:return f->ko_collapse;
        }
    }
#else
    (void)i;(void)field;
#endif
    return 0;
}
int32_t t3_move_probe_world_center(unsigned axis) {
    return (int32_t)(((int64_t)t3_move_probe_owned_value(0,axis+1u)+
                      t3_move_probe_owned_value(1,axis+1u))/2);
}
sat_result_t t3_move_probe_owned_step(t3_input_t p0,t3_input_t p1,t3_match_t* view) {
#if defined(T3_ENABLE_PSX_OWNED_MATCH)
    if(!g_active || !view)return SAT_ERR_INVALID_ARG;
#if defined(T3_ENABLE_PSX_COMBAT)
    t3_psx_match_frame_t frame={.frame_counter=g_owned_match.ticks+1};
    const t3_input_t pads[2]={p0,p1};
    for(unsigned i=0;i<2;++i) {
        /* Physical face buttons choose the win pose. Direction words are
         * already mapped relative to the rival at the controller boundary. */
        const unsigned bits[4]={T3_IN_LP,T3_IN_RP,T3_IN_LK,T3_IN_RK};
        const unsigned original[4]={0x80,0x10,0x40,0x20};
        for(unsigned b=0;b<4;++b) {
            if(pads[i].held&bits[b])frame.physical[i]|=(uint16_t)original[b];
            if(pads[i].pressed&bits[b])frame.pressed[i]|=(uint16_t)original[b];
        }
    }
    if(!t3_psx_match_step_original(&g_owned_match,p0,p1,&frame))return SAT_ERR_INVALID_ARG;
#else
    if(!t3_psx_match_step(&g_owned_match,p0,p1))return SAT_ERR_INVALID_ARG;
#endif
    view->total_ticks=g_owned_match.ticks;
    view->hits=(uint16_t)(g_owned_match.fighter[0].registered_hits+g_owned_match.fighter[1].registered_hits);
    view->blocks=(uint16_t)(g_owned_match.fighter[0].registered_blocks+g_owned_match.fighter[1].registered_blocks);
#if defined(T3_ENABLE_PSX_COMBAT)
    const t3_psx_round_flow_t* flow=&g_owned_match.flow;
    view->timer=(uint16_t)flow->timer;
    view->round=(uint8_t)flow->played;g_round=view->round;
    view->round_done=(uint8_t)(flow->state>=T3_ROUND_RESULT);view->match_done=(uint8_t)(flow->match_result!=0);
    view->winner=flow->match_result==3?-1:flow->match_result?(int8_t)flow->match_winner:
        g_owned_match.round_actor[0].won>0?0:g_owned_match.round_actor[1].won>0?1:-1;
    view->round_pause=(uint16_t)flow->hold;
    view->wins[0]=(uint8_t)g_owned_match.round_actor[0].wins;
    view->wins[1]=(uint8_t)g_owned_match.round_actor[1].wins;
#else
    /* Query-only presentation timer. */
    view->timer=g_owned_match.ticks<T3_ROUND_TICKS?
        (uint16_t)(T3_ROUND_TICKS-g_owned_match.ticks):0u;
#endif
    for(unsigned i=0;i<2u;++i) {
        const t3_psx_fighter_t* f=&g_owned_match.fighter[i];
        t3_fighter_t* v=&view->fighters[i];
        *v=(t3_fighter_t){0};
        v->x=(int32_t)(((int64_t)f->ground.root.x-t3_move_probe_world_center(0))*3/8);
        v->z=(int32_t)(-((int64_t)f->ground.root.z-t3_move_probe_world_center(2))*3/8);
        v->health=(int16_t)((int64_t)f->health_q16*T3_MAX_HEALTH/g_owned_config.max_health_q16);
        v->facing=f->ground.root.x<=g_owned_match.fighter[1u-i].ground.root.x?1:-1;
        v->guarding=f->ground.guard!=0u;v->crouching=(f->ground.state&7u)==1u;
        v->stun=f->ground.recover_frames;
        view->input_histories[i]=f->input;
    }
    return SAT_OK;
#else
    (void)p0;(void)p1;(void)view;return SAT_ERR_INVALID_ARG;
#endif
}
int32_t t3_move_probe_round_value(unsigned field) {
#if defined(T3_ENABLE_PSX_COMBAT)
    switch(field) {
    case 0:return 1;
    case 1:return g_owned_match.flow.state;
    case 2:return g_owned_match.flow.counter;
    case 3:return g_owned_match.flow.hold;
    case 4:return g_owned_match.replay.mode;
    case 5:return g_owned_match.replay.record;
    case 6:return g_owned_match.replay.play;
    case 7:return g_owned_match.replay.mark;
    case 8:return g_owned_match.replay.recorded;
    case 9:return (int32_t)g_owned_match.replay.epoch;
    }
#else
    (void)field;
#endif
    return 0;
}
