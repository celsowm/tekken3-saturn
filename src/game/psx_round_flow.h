#ifndef T3_PSX_ROUND_FLOW_H
#define T3_PSX_ROUND_FLOW_H
#include <stdint.h>

enum {
    T3_ROUND_INTRO=0,T3_ROUND_FIGHT=1,T3_ROUND_RESULT=2,
    T3_ROUND_REPLAY=3,T3_ROUND_REPLAY_DONE=4,T3_ROUND_WIN_POSES=5,
    T3_ROUND_WIN_WAIT=6,T3_ROUND_AREA_CHANGE=7,T3_ROUND_STAGE_CLEAR=8,T3_ROUND_END=9
};
/* Normal VS RoundFlow only. Replay playback, camera progression, move starts
 * and the outer FightMain round reset are separate owners. Call this before
 * MoveStartAll, after ReplayUpdate. No hidden clocks or allocation. */
typedef struct {
    int32_t state,counter,hold,done,replay_request,frame,played,timer,result;
    int32_t input[2],freeze,no_damage,camera,timer_stopped;
    int32_t to_win,max_rounds,blend_hold,human_mask,tie_choice,draw_wins;
    int32_t match_result,match_winner,match_loser,win_done[2];
} t3_psx_round_flow_t;
typedef struct {
    int32_t health,maximum,wins,won,win_pose,active,pose,length;
    /* state_word is the pending row's state, or the resolved stance row's
     * state when there is no pending row (the original HoldInput fallback). */
    int32_t bank_type,state_word,script_input,is_cpu,costume,health_left,carried_health;
} t3_psx_round_actor_t;
typedef struct {
    int32_t replay_mode,replay_skip;
    uint32_t frame_counter;
    uint16_t physical[2],pressed[2]; /* Original PsyQ word, not port pad bits. */
    uint8_t team_round; /* g_teamRound: 1 in normal VS; suppresses loser camera. */
    /* Tekken Force (FUN_8003E608): the round watches the player alone while the enemies come, and the
     * first two records once the level has ended (its boss fight). An enemy record that is not on the
     * field has no health, move or pose to validate. */
    uint8_t force,force_level_ended;
    uint8_t force_tally_done; /* the level runner's tally has finished (STAGE CLEAR waits for Start after it) */
} t3_psx_round_flow_facts_t;
typedef struct {
    int16_t start_slot[2]; /* -1 means no move start. Resolve with original VM. */
    uint8_t grounded_mask,clear_blends,animate_before_camera,fight_clock_advance,stop_sounds;
} t3_psx_round_flow_actions_t;
/* Pair-atomic on invalid/unsupported input. Mode-specific team/arcade logic
 * and costume 0x26's VBlank-selected victory are outside normal Paul VS. */
int t3_psx_round_flow_step(t3_psx_round_flow_t* flow,t3_psx_round_actor_t actors[2],
                         const t3_psx_round_flow_facts_t* facts,
                         t3_psx_round_flow_actions_t* actions);
#endif
