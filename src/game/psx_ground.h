#ifndef T3_PSX_GROUND_H
#define T3_PSX_GROUND_H
#include "move_vm.h"
#include "psx_pose.h"
#include "psx_reaction.h"

/* The private ground blob: atan (1025), push tables (0x8001075C, 0x58C bytes),
 * the launch push table (0x80019D2C, 8 frames) and the air kinds (0x8001A630).
 * The launch table directly follows the push tables, so push_size covers it. */
enum { T3_PSX_ATAN_BYTES=1025u, T3_PSX_PUSH_BYTES=0x58Cu, T3_PSX_LAUNCH_PUSH_BYTES=16u,
       T3_PSX_GROUND_BLOB_BYTES=1025u+0x58Cu+16u+24u };
typedef struct {
    t3_psx_trig_t trig;
    const uint8_t* atan;size_t atan_size;
    const uint8_t* push;size_t push_size;
    const uint8_t* air_kinds; /* T3_PSX_AIR_KIND_BYTES, or NULL: no launches */
} t3_psx_ground_tables_t;
/* Bind a T3_PSX_GROUND_BLOB_BYTES blob; returns 0 on a wrong size. */
int t3_psx_ground_tables_bind(t3_psx_trig_t trig,const uint8_t* blob,size_t size,
                              t3_psx_ground_tables_t* out);

/* Arena coordinates are PS1 integers. Angles wrap at one 65536-unit turn.
 * This is owned fighter state, not a view of guest RAM or demo Q8 values. */
typedef struct {
    t3_vec3i_t anchor,root,opponent_start;
    uint32_t state,distance;
    int32_t step_accum_x,step_accum_z;
    uint16_t facing,heading,target_dir,opp_to_self_dir,opp_heading;
    uint16_t heading_delta,opp_heading_delta,relative_angle,opp_relative_angle,aim_dir,opp_aim_dir;
    int16_t damage,damage_override,last_damage,root_dx,track_accum,turn_frames,turn_step;
    int16_t push_frames,push_speed,push_direction,push_repeat,push_table_frames;
    uint16_t push_cursor;
    int16_t attack_alert,hit_cooldown,recover_frames,recover_mash,power_timer,push_repeat_timer,push_repeat_transition;
    uint8_t facing_quadrant,opponent_quadrant,fixed_facing,fixed_facing_side;
    uint8_t track_mode,air_phase,anchor_dirty,step_kind,human_guard,in_air,is_cpu;
    uint8_t attack_segment_count,active_segments,was_hit_this_move,air_kind,state_class;
    uint8_t landed_a,landed_b,about_to_hit,launch_armed,hit_clean;
    uint8_t slide_state,slide_to_point;
    int32_t slide_step_x,slide_step_z; /* +0x78/+0x7C, per frame while slide_state is 2 */
    /* Launch and juggle flight (+0x96..+0x9E), the root velocity (+0x3C4,
     * previous root +0x3B8) and the attacker direction copied on a hit (+0x3DC). */
    int16_t ground_offset,air_speed,air_vel_x,air_vel_y,air_vel_z;
    t3_vec3i_t velocity,velocity_root,hit_dir;
    /* Results for the VM's clock, which the ground code does not own:
     * the entry frame LaunchTrajectory chose (transitions 30/36) and the end
     * of a ballistic flight this frame. */
    int16_t launch_entry;
    uint8_t flight_landed,landing_shake;
    int16_t throw_state,juggle_count,step_cooldown;
    uint8_t in_throw; /* +0xC4: paired by MoveStep, kept while throw_state != 0 */
    uint8_t attack_pending; /* +0xBD: the victim was attacking or had power on throw start */
    uint16_t guard;
} t3_psx_ground_state_t;

int t3_psx_atan2(const t3_psx_ground_tables_t* tables,int32_t dz,int32_t dx,uint16_t* out);
int t3_psx_polar(const t3_psx_ground_tables_t* tables,int32_t length,uint16_t angle,
                  int32_t* dx,int32_t* dz);
int t3_psx_ground_angles(t3_psx_ground_state_t* state,const t3_psx_ground_state_t* opponent,
                         const t3_psx_ground_tables_t* tables);
/* Physical side effects after a queued ground move starts. VM owns clocks;
 * caller supplies the previous move and the actual selected reaction. */
int t3_psx_ground_start(t3_psx_ground_state_t* state,const t3_psx_ground_state_t* opponent,
                        const t3_move_vm_t* vm,const t3_move_record_t* previous,
                        const t3_psx_ground_tables_t* tables,uint8_t branch_flags,
                        uint8_t apply_end_turn,t3_move_start_facts_t facts,
                        const t3_psx_reaction_pick_t* reaction,uint8_t segments);
/* Normal grounded Paul movement. Clock/move ownership remains in the VM.
 * Direction 0 is the original pre-first-input history state, distinct from
 * neutral 5. Launch/throw/slide-to-point policies are not synthesized. */
int t3_psx_ground_step(t3_psx_ground_state_t* state,const t3_psx_ground_state_t* opponent,
                       const t3_move_vm_t* vm,const t3_move_vm_t* opponent_vm,
                       const t3_psx_ground_tables_t* tables,
                       uint8_t input_direction,uint8_t pressed_buttons);
#endif
