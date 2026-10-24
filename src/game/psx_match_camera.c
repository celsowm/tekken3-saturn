/* The original fight camera inside the owned match: what the director reads
 * of each fighter, and where FightFrame runs it (RoundFlow, CameraDirector,
 * CameraShakeStep, CameraFrame, ..., FighterSkeletonUpdate's screen x, the
 * landing and move-event shakes).
 *
 * The call order and the fighter fields follow the rules documented in the
 * pinned Tekken3Remake research (docs/research/code/fight-frame.md,
 * camera.md, moves.md, fighter.md) and its remake/core/fight/
 * fight_simulation.gd and remake/core/fighter/fighter_animation.gd, adapted
 * to C. That project is PolyForm Noncommercial 1.0.0 (see NOTICE.md).
 * Required Notice: Copyright (c) 2026 n0madic. */
#include "psx_match.h"
#if defined(T3_MATCH_FAILURE_TRACE)
extern volatile uint32_t g_t3_match_failure_phase;
#define T3_CAMERA_PHASE(value) (g_t3_match_failure_phase=(value))
volatile int32_t g_t3_camera_failure_state[8];
#else
#define T3_CAMERA_PHASE(value) ((void)0)
#endif

enum { SCREEN_JOINT = 11, HEAD_JOINT = 2, START_SCREEN_X = 100, OVERHEAD_DEPTH = 0xA3A };

static int16_t event_left(const t3_psx_fighter_t* f,unsigned hi) {
    int16_t left=-1;
    if(!t3_psx_move_events_last(f->vm.active_bank,&f->vm.move,hi,f->vm.clock.pose_frame,&left))return -1;
    return left;
}

/* Fighter f as the director reads it (the record as the previous frame left it). */
static void fighter(const t3_psx_match_t* m,unsigned i,t3_psx_camera_fighter_t* o) {
    const t3_psx_fighter_t* f=&m->fighter[i];
    const t3_psx_fighter_t* a=&m->fighter[1u-i];
    const t3_psx_fight_camera_config_t* c=m->config->camera;
    const t3_divmot_t* bank=f->vm.active_bank;
    *o=(t3_psx_camera_fighter_t){0};
    o->pos_x=f->ground.anchor.x;o->pos_y=f->ground.anchor.y;o->pos_z=f->ground.anchor.z;
    o->root_x=f->ground.root.x;o->root_y=f->ground.root.y;o->root_z=f->ground.root.z;
    o->head_y=f->world[HEAD_JOINT].origin.y;
    o->screen_x=m->camera.screen_x[i];
    o->throw_state=f->ground.throw_state;
    o->pose_frame=(int16_t)f->vm.clock.pose_frame;
    o->cur_slot=(int16_t)f->vm.slot;
    o->bank_type=t3_psx_match_bank(m->config,i)->bank_type;
    o->move_flags=f->vm.move.flags;
    o->move_state=f->vm.move.state_word;
    /* The game compares row pointers; the slot's entry in its bank names the row. */
    o->move_id=bank?(uint32_t)(uintptr_t)(bank->data+bank->sections[2]+4u*f->vm.slot):0u;
    o->event_left_b=event_left(f,0x0Bu);
    o->event_left_c=event_left(f,0x0Cu);
    o->transition=f->vm.current_transition;
    o->heading=(int16_t)f->ground.heading;
    o->facing=(int16_t)f->ground.facing;
    o->health=f->health_q16;
    o->hit_clean=f->hit.hit_clean;o->counter_hit=f->hit.counter;
    o->close_hit=f->hit.close;o->ko=f->hit.ko_started;
    o->last_damage=f->ground.last_damage;
    /* The best hit slot holds the hitting segment's end point (HitTest). */
    if(a->hit_gate.status==T3_HIT_GATE_ACCEPT && a->hit_gate.segment>=0 &&
       a->hit_gate.segment<a->strike.segment_count) {
        const t3_vec3i_t p=a->strike.segments[a->hit_gate.segment].end;
        o->hit_point[0]=p.x;o->hit_point[1]=p.y;o->hit_point[2]=p.z;
    }
    const int second=i==1u && c->second_bank_choices;
    o->bank_choices=second?c->second_bank_choices:c->bank_choices;
    o->bank_choice_count=second?c->second_bank_choice_count:c->bank_choice_count;
    o->bank_streams=second?c->second_bank_streams:c->bank_streams;
    o->bank_spline=c->spline;
}

static void fighters(const t3_psx_match_t* m,t3_psx_camera_fighter_t f[2]) {
    fighter(m,0,&f[0]);fighter(m,1,&f[1]);
}

static t3_psx_camera_frame_t frame_of(t3_psx_match_t* m) {
    t3_psx_fight_camera_t* c=&m->camera;
    return (t3_psx_camera_frame_t){.fighter_distance=(int32_t)m->fighter[0].ground.distance,
        .rounds_played=m->flow.played,.rand=&c->rand,.camera_rand=&c->camera_rand,.ai_rand=&c->ai_rand};
}

/* The main loop steps the camera generator once per frame. */
static void camera_rand_step(t3_psx_fight_camera_t* c) {c->camera_rand=(c->camera_rand+1u)*0x10DCDu;}

/* CameraShakeStep and CameraFrame. */
static int view(t3_psx_match_t* m) {
    const t3_psx_camera_tables_t* t=m->config->camera->tables;
    t3_psx_camera_shake_step(&m->camera.camera,t);
    return t3_psx_camera_build_view(&m->camera.camera.view,t,&m->camera.matrix);
}

int t3_psx_match_camera_round(t3_psx_match_t* m,int first) {
    const t3_psx_fight_camera_config_t* config=m->config->camera;
    t3_psx_camera_fighter_t f[2];t3_psx_camera_frame_t fr;
    t3_psx_fight_camera_t* c=&m->camera;
    if(!config)return 1;
    if(first) {
        *c=(t3_psx_fight_camera_t){0};
        c->rand=c->camera_rand=c->ai_rand=1u;
        t3_psx_camera_reset(&c->camera);
        c->camera.faithful=1;
    }
    /* FUN_8002AB68: the fighters start on their own sides of the screen. */
    c->screen_x[0]=-START_SCREEN_X;c->screen_x[1]=START_SCREEN_X;
    c->camera.shake_script=-1;
    c->camera.overhead=0;   /* cleared when the next fight starts */
    fighters(m,f);fr=frame_of(m);
    if(!t3_psx_camera_round_start(&c->camera,config->tables,f,&fr))return 0;
    /* The first round starts at the fight phase (no intro in the owned flow). */
    c->phase=c->flow=first?T3_PSX_CAMERA_FIGHT_PHASE:T3_PSX_CAMERA_RESET;
    if(first)c->camera.intro_timer=0;
    c->started=1;
    if(!t3_psx_camera_build_view(&c->camera.view,config->tables,&c->matrix))return 0;
    /* StageBackgroundSetup notes the camera's yaw; every round start
     * (FUN_8006CB40) straightens the panorama. */
    if(first)t3_psx_backdrop_setup(&c->backdrop,&c->camera.view);
    c->backdrop.angle=0;
    return 1;
}

int t3_psx_match_camera_frame(t3_psx_match_t* m) {
    T3_CAMERA_PHASE(61);
    const t3_psx_fight_camera_config_t* config=m->config->camera;
    t3_psx_camera_fighter_t f[2];t3_psx_camera_frame_t fr;
    t3_psx_fight_camera_t* c=&m->camera;
    const int16_t flow=(int16_t)m->flow.camera;
    if(!config)return 1;
    if(!c->started)return 0;
    camera_rand_step(c);
    fighters(m,f);fr=frame_of(m);
    /* RoundFlow sets the phase before the director: a new round (RESET), the
     * winner camera (WINNER_0/1) and the loser camera with its set-up. The
     * director moves on by itself; the flow's copy changes only on events. */
    if(flow==T3_PSX_CAMERA_RESET && c->flow!=flow)c->phase=flow;
    else if((flow==T3_PSX_CAMERA_WINNER_0 || flow==T3_PSX_CAMERA_WINNER_1) && c->phase<T3_PSX_CAMERA_WINNER_0)
        c->phase=flow;
    else if((flow==T3_PSX_CAMERA_LOSER_0 || flow==T3_PSX_CAMERA_LOSER_1) && c->phase!=flow) {
        T3_CAMERA_PHASE(62);
        const t3_psx_camera_fighter_t* l=&f[flow==T3_PSX_CAMERA_LOSER_1];
        if(!t3_psx_camera_loser_start(&c->camera,config->tables,l,l->health!=0,&fr))return 0;
        c->phase=flow;
    }
    c->flow=flow;
    T3_CAMERA_PHASE(63);
#if defined(T3_MATCH_FAILURE_TRACE)
    g_t3_camera_failure_state[0]=flow;
    g_t3_camera_failure_state[1]=c->phase;
    g_t3_camera_failure_state[2]=c->camera.state;
    g_t3_camera_failure_state[3]=c->camera.script;
    g_t3_camera_failure_state[4]=c->camera.thrower;
    g_t3_camera_failure_state[5]=f[0].cur_slot;
    g_t3_camera_failure_state[6]=f[1].cur_slot;
#endif
    const int stepped=t3_psx_camera_step(&c->camera,config->tables,f,&fr,&c->phase);
#if defined(T3_MATCH_FAILURE_TRACE)
    g_t3_camera_failure_state[7]=stepped;
#endif
    if(stepped<=0)return 0;
    T3_CAMERA_PHASE(64);
    return view(m);
}

void t3_psx_match_camera_capture(const t3_psx_match_t* m,t3_psx_camera_fighter_t before[2]) {
    if(m->config->camera)fighters(m,before);
}

int t3_psx_match_camera_replay(t3_psx_match_t* m,t3_psx_camera_fighter_t f[2],int first) {
    const t3_psx_fight_camera_config_t* config=m->config->camera;
    t3_psx_fight_camera_t* c=&m->camera;
    if(!config)return 1;
    if(!c->started)return 0;
    camera_rand_step(c);
    if(first) {
        /* The replay's first frame puts the recorded roots in place before
         * its camera starts (one step), then runs the frame's own step. */
        t3_psx_camera_frame_t fr=frame_of(m);
        for(unsigned i=0;i<2;++i) {
            f[i].root_x=m->fighter[i].ground.root.x;f[i].root_y=m->fighter[i].ground.root.y;
            f[i].root_z=m->fighter[i].ground.root.z;
        }
        if(!t3_psx_camera_replay_start(&c->camera,config->tables,f,&fr))return 0;
    }
    t3_psx_camera_replay_step(&c->camera,config->tables,f);
    if(!view(m))return 0;
    return t3_psx_match_camera_after(m,0);
}

void t3_psx_match_fighter_view(const t3_psx_fight_camera_t* c,t3_psx_camera_matrix_t* out) {
    /* FUN_80036254: the overhead KO view draws the fighters closer. */
    *out=c->matrix;
    if(c->camera.overhead)out->t[2]=(int32_t)((uint32_t)out->t[2]-OVERHEAD_DEPTH);
}

void t3_psx_match_draw_screen(const t3_psx_match_t* m,unsigned i,
                              const t3_psx_camera_matrix_t* view,int16_t* screen_x) {
    const t3_psx_fighter_t* f=&m->fighter[i];
    if(f->body.active && (f->parts_mask&(1u<<SCREEN_JOINT))) {
        const int32_t p[3]={f->world[SCREEN_JOINT].origin.x,f->world[SCREEN_JOINT].origin.y,
                            f->world[SCREEN_JOINT].origin.z};
        int32_t s[3];
        t3_psx_camera_project(view,p,s);
        *screen_x=(int16_t)s[0];
    }
}

int t3_psx_match_camera_after(t3_psx_match_t* m,int live) {
    t3_psx_fight_camera_t* c=&m->camera;
    t3_psx_camera_matrix_t fighter_view;
    if(!m->config->camera)return 1;
    t3_psx_match_fighter_view(c,&fighter_view);
    for(unsigned i=0;i<2;++i)t3_psx_match_draw_screen(m,i,&fighter_view,&c->screen_x[i]);
    /* FUN_8006DAB4 draws every fight frame, replays too: the panorama turns
     * only while a round runs (RoundFlow state 0x80097350 non-zero). */
    if(!t3_psx_backdrop_step(&c->backdrop,&c->camera.view,m->config->camera->tables,m->flow.state==0))
        return 0;
    if(!live)return 1;
    /* FighterMovePhysics: a knocked-down landing starts shake 0; MoveEvents
     * 1-3 later start their scripts. */
    for(unsigned i=0;i<2;++i) {
        if(!m->fighter[i].body.active)continue;
        /* A ballistic landing of air kind 1 shakes with script 1. */
        if(m->fighter[i].ground.landing_shake)t3_psx_camera_shake_start(&c->camera,1);
        if(m->fighter[i].ground.landed_b)t3_psx_camera_shake_start(&c->camera,0);
    }
    for(unsigned i=0;i<2;++i)
        if(m->fighter[i].events.shake)t3_psx_camera_shake_start(&c->camera,m->fighter[i].events.shake-1);
    return 1;
}
