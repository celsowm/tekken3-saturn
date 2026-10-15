#include "move_probe.h"
#if defined(T3_LOCAL_DIVMOT_ENABLED)
#include "t3_local_divmot.h"
/* Read by a debugger after each simulation tick, not disk I/O/VDP1.
 * Epoch odd = write in progress; even and unchanged = stable pair. */
volatile uint32_t g_t3_pose_trace_epoch;
volatile uint8_t g_t3_pose_trace[2][T3_PSX_TRACE_BYTES];
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
#include "t3_generated_character.h"

static t3_divmot_t g_bank, g_common;
static t3_move_vm_t g_vms[2];
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
static t3_psx_joint_t g_world_pose[2][T3_PSX_DRAW_PARTS];
#endif
static int16_t g_fk_root_xx[2];
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND)
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
    /* Slot 3 is the ordinary Tekken standing stance. Fallback is useful for
     * synthetic tests, but must not be mistaken for authentic startup. */
    g_start_slot=3u;
    if (t3_divmot_slot(&g_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
          &g_common,
#else
          NULL,
#endif
          g_start_slot,&g_vms[0].move)!=1) g_start_slot=0u;
    g_active=1;
    t3_move_probe_reset(1u);
    return SAT_OK;
}

void t3_move_probe_reset(unsigned round) {
    unsigned i;
    if (!g_active) return;
    g_round=(uint8_t)round;
    for (i=0;i<2u;++i) {
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
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND)
        g_shadow_result[i].status=T3_SHADOW_INACTIVE;
        g_shadow_result[i].new_contact=0u;
        g_shadow_result[i].zone=-1;
        g_shadow_result[i].segment=-1;
#endif
        g_root_pose[i][0]=g_root_pose[i][1]=g_root_pose[i][2]=0;
        if (!t3_move_vm_start(&g_vms[i],&g_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
                   &g_common,
#else
                   NULL,
#endif
                   g_start_slot)) g_active=0u;
    }
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND)
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
    if (match->round_done || match->match_done || !g_active) return;
    for (i=0;i<2u;++i) {
        t3_attack_sample_t sampled;
        int decoded;
        {
            unsigned before=g_vms[i].frame;
            int transitioned=t3_move_vm_tick_ex(
                &g_vms[i],&match->input_histories[i],
                NULL,t3_local_sequences,T3_LOCAL_SEQUENCE_COUNT);
            t3_psx_move_events_t events;
            int rc=transitioned<0?-1:
                t3_psx_move_events_sample(g_vms[i].active_bank,
                    &g_vms[i].move,transitioned==1?0u:before,
                    g_vms[i].frame,&events);
            g_move_event_status[i]=rc==1?1u:(rc==0?2u:3u);
            g_move_extra_kind[i]=rc==1?events.extra_kind:0u;
            g_move_extra_damage[i]=rc==1?events.extra_damage:0u;
            g_move_event_fired[i]=rc==1?events.fired:0u;
            if(rc<0)++g_decode_errors[i];
        }
        decoded=t3_attack_sample(g_vms[i].active_bank,&g_vms[i].move,
                                 g_vms[i].frame,0,&sampled);
        g_segments[i]=decoded==1?sampled.segment_count:0u;
        if(decoded<0) ++g_decode_errors[i];
        {
            t3_motion_stream_t motion;
            int opened=t3_motion_open(g_vms[i].active_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
                &g_common,
#else
                NULL,
#endif
                g_vms[i].move.animation_offset,&motion);
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
                g_pose_frames[i]=motion.frames;
                full=t3_motion_sample(&motion,g_vms[i].frame,weights,pose);
                if(full==1) {
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
                                /* The original root displacement uses KMD
                                 * scale and the sampled 49-channel pose.
                                 * A model-local zero arena anchor with neutral
                                 * facing is intentional for VISUAL RESEARCH.
                                 * Never promote it to a real opponent hit. */
                                const t3_vec3i_t zero={0};
                                if(t3_psx_pose_world_root(pose,
                                    T3_KMD_SCALE_PERCENT,zero,
                                    0x8000u,0x8000u,&trig,&root_frame) &&
                                   t3_psx_compose_parts(t3_private_kmd_parts,
                                    slots,valid,&root_frame,world,&partmask)) {
                                    g_fk_parts[i]=partmask;
                                    if((partmask & 0x3FFFFu)==0x3FFFFu) {
                                        t3_hurt_cylinder_t zones[T3_PSX_HURT_ZONES];
                                        __builtin_memcpy(g_world_pose[i],world,
                                                         sizeof(world));
                                        /* These are MODEL-LOCAL zones only.
                                         * Do not present overlap between two
                                         * unplaced fighters as a real PS1 hit. */
                                        if(t3_psx_hurt_from_parts(world,partmask,
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
                    simple=t3_motion_sample_root(&motion,g_vms[i].frame,
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
    }

#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND)
    {
        t3_psx_shadow_actor_t actors[2]={{0}};
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
                    const t3_move_vm_t* vm=&g_vms[fighter];
                    int decoded=t3_attack_sample(vm->active_bank,&vm->move,
                                                 vm->frame,0,&attacks[fighter]);
                    actors[fighter].root=roots[fighter];
                    actors[fighter].parts=g_world_pose[fighter];
                    actors[fighter].pose_available=(uint8_t)
                        ((g_fk_parts[fighter]&T3_PSX_BODY_PART_MASK)==
                           T3_PSX_BODY_PART_MASK);
                    actors[fighter].parts_mask=g_fk_parts[fighter];
                    actors[fighter].move_slot=vm->slot;
                    /* t3_psx_sweep_sample expects ACTIVE-WINDOW-relative time,
                     * not the raw DIVMOT move frame. */
                    actors[fighter].attack_frame=
                        vm->frame>=vm->move.active_first?
                        (uint16_t)(vm->frame-vm->move.active_first):0u;
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
                    g_vms[fighter].slot,g_vms[fighter].frame,
                    joints,mask,encoded,sizeof(encoded))) {
                /* Unexpected invalid part coordinates: never expose stale
                 * valid joints to the oracle harness. */
                (void)t3_psx_trace_encode(g_t3_pose_trace_tick,fighter,
                    g_vms[fighter].slot,g_vms[fighter].frame,
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
uint16_t t3_move_probe_slot(unsigned i) {return i<2u && g_active?g_vms[i].slot:0;}
uint16_t t3_move_probe_frame(unsigned i) {return i<2u && g_active?g_vms[i].frame:0;}
uint16_t t3_move_probe_attack_word(unsigned i) {return i<2u && g_active?g_vms[i].move.attack_word:0;}
uint32_t t3_move_probe_transitions(unsigned i) {return i<2u && g_active?g_vms[i].transitions:0;}
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
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND)
    return i<2u && g_active?g_shadow_result[i].status:0u;
#else
    (void)i;return 0u;
#endif
}
int8_t t3_move_probe_shadow_zone(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND)
    return i<2u && g_active?g_shadow_result[i].zone:-1;
#else
    (void)i;return -1;
#endif
}
uint32_t t3_move_probe_shadow_edges(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND)
    return i<2u && g_active?g_shadow.contact_edges[i]:0u;
#else
    (void)i;return 0u;
#endif
}
uint32_t t3_move_probe_shadow_frames(unsigned i) {
#if defined(T3_ENABLE_PSX_SHADOW_CONTACT) && defined(T3_LOCAL_KMD_BIND)
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
