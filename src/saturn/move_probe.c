#include "move_probe.h"
#if defined(T3_LOCAL_DIVMOT_ENABLED)
#include "t3_local_divmot.h"
#include "divmot.h"
#include "move_vm.h"
#include "attack_records.h"
#include "psx_motion.h"
#include "psx_pose.h"
#include "psx_ik.h"
#include "t3_generated_character.h"

static t3_divmot_t g_bank, g_common;
static t3_move_vm_t g_vms[2];
static uint8_t g_active, g_round;
static unsigned g_start_slot;
static uint8_t g_segments[2];
static uint32_t g_decode_errors[2];
static uint8_t g_pose_status[2],g_pose_frames[2];
static int16_t g_root_pose[2][3];
static uint32_t g_fk_mask[2],g_fk_parts[2],g_ik_mask[2];
static int16_t g_fk_root_xx[2];

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
        g_decode_errors[i]=0u;
        g_pose_status[i]=0u;
        g_pose_frames[i]=0u;
        g_fk_mask[i]=0u;
        g_fk_parts[i]=0u;
        g_ik_mask[i]=0u;
        g_fk_root_xx[i]=0;
        g_root_pose[i][0]=g_root_pose[i][1]=g_root_pose[i][2]=0;
        if (!t3_move_vm_start(&g_vms[i],&g_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
                   &g_common,
#else
                   NULL,
#endif
                   g_start_slot)) g_active=0u;
    }
}

void t3_move_probe_tick(const t3_match_t* match) {
    unsigned i;
    if (!g_active || !match) return;
    if (g_round!=match->round) t3_move_probe_reset(match->round);
    if (match->round_done || match->match_done || !g_active) return;
    for (i=0;i<2u;++i) {
        t3_attack_sample_t sampled;
        int decoded;
        (void)t3_move_vm_tick_ex(&g_vms[i],&match->input_histories[i],
                                NULL,t3_local_sequences,
                                T3_LOCAL_SEQUENCE_COUNT);
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
                                /* Model-local diagnostic only: unknown PS1
                                 * fighter heading/root and IK are not guessed. */
                                root_frame.rotation.m[0][0]=4096;
                                root_frame.rotation.m[1][1]=4096;
                                root_frame.rotation.m[2][2]=4096;
                                if(t3_psx_compose_parts(t3_private_kmd_parts,
                                    slots,valid,&root_frame,world,&partmask))
                                    g_fk_parts[i]=partmask;
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
}

uint8_t t3_move_probe_active(void) {return g_active;}
uint16_t t3_move_probe_slot(unsigned i) {return i<2u && g_active?g_vms[i].slot:0;}
uint16_t t3_move_probe_frame(unsigned i) {return i<2u && g_active?g_vms[i].frame:0;}
uint16_t t3_move_probe_attack_word(unsigned i) {return i<2u && g_active?g_vms[i].move.attack_word:0;}
uint32_t t3_move_probe_transitions(unsigned i) {return i<2u && g_active?g_vms[i].transitions:0;}
uint8_t t3_move_probe_segments(unsigned i) {return i<2u && g_active?g_segments[i]:0u;}
uint32_t t3_move_probe_decode_errors(unsigned i) {return i<2u && g_active?g_decode_errors[i]:0u;}
uint8_t t3_move_probe_pose_status(unsigned i) {return i<2u && g_active?g_pose_status[i]:0u;}
uint8_t t3_move_probe_pose_frames(unsigned i) {return i<2u && g_active?g_pose_frames[i]:0u;}
int16_t t3_move_probe_root_pose(unsigned i,unsigned axis) {
    return i<2u && axis<3u && g_active?g_root_pose[i][axis]:0;
}
uint32_t t3_move_probe_fk_mask(unsigned i) {return i<2u && g_active?g_fk_mask[i]:0u;}
uint32_t t3_move_probe_fk_parts(unsigned i) {return i<2u && g_active?g_fk_parts[i]:0u;}
uint32_t t3_move_probe_ik_mask(unsigned i) {return i<2u && g_active?g_ik_mask[i]:0u;}
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
uint32_t t3_move_probe_decode_errors(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_pose_status(unsigned i) {(void)i;return 0u;}
uint8_t t3_move_probe_pose_frames(unsigned i) {(void)i;return 0u;}
int16_t t3_move_probe_root_pose(unsigned i,unsigned axis) {(void)i;(void)axis;return 0;}
uint32_t t3_move_probe_fk_mask(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_fk_parts(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_ik_mask(unsigned i) {(void)i;return 0u;}
int16_t t3_move_probe_fk_root_xx(unsigned i) {(void)i;return 0;}

#endif
