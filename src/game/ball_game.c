/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Tekken Ball on the owned match (volley.ovl FUN_800B0E14, tools/research/ball_sim.py ball_frame). See ball_game.h. */
#include "ball_game.h"
#if defined(T3_PSX_BALL)
#include "psx_contact.h"

enum { HEAD_BYTES=6, OPTION_WORDS=12, HIT_POINTS=4, VERTICES=42, TRIANGLES=16, QUADS=32 };

static uint16_t be16(const uint8_t* p){return (uint16_t)(p[0]<<8|p[1]);}
static int16_t be16s(const uint8_t* p){return (int16_t)be16(p);}

int t3_ball_mesh(const uint8_t* blob,size_t size,unsigned option,t3_ball_mesh_t* out) {
    const size_t options=HEAD_BYTES,hit=options+3u*OPTION_WORDS*2u,verts=hit+HIT_POINTS*3u*2u,
                 normals=verts+VERTICES*6u,tris=normals+VERTICES*6u,quads=tris+TRIANGLES*3u,
                 colours=quads+QUADS*4u,end=colours+3u*16u*4u;
    if(!blob||!out||size!=end||option>2u||blob[0]!='T'||blob[1]!='3'||blob[2]!='B'||blob[3]!='L')return 0;
    out->vertices=blob+verts;out->normals=blob+normals;out->triangles=blob+tris;out->quads=blob+quads;
    out->colours=blob+colours+option*64u;out->style=blob+options+option*OPTION_WORDS*2u;
    return 1;
}

int t3_ball_game_init(t3_ball_game_t* g,const uint8_t* blob,size_t size,unsigned option,unsigned serve_side) {
    t3_ball_mesh_t mesh;
    unsigned t,i,j;
    if(!g||!t3_ball_mesh(blob,size,option,&mesh)||be16(blob+4)!=BALL_DATA_VERSION)return 0;
    __builtin_memset(g,0,sizeof *g);
    for(t=0;t<3u;++t)for(i=0;i<OPTION_WORDS;++i)g->options[t][i]=be16(blob+HEAD_BYTES+(t*OPTION_WORDS+i)*2u);
    for(j=0;j<HIT_POINTS;++j)for(i=0;i<3u;++i)
        g->world.hit_offsets[j][i]=be16s(blob+HEAD_BYTES+3u*OPTION_WORDS*2u+(j*3u+i)*2u);
    g->option=(uint8_t)option;g->world.option=(int32_t)option;g->world.serve_side=(int32_t)(serve_side&1u);
    g->world.first_round=1;g->world.stand_in.source=-1;
    g->world.lob_base=0;
    t3_ball_fight_start(&g->ball,&g->world,g->options[option]);
    g->active=1;g->mesh_ok=1;
    g->f[0].index=0;g->f[1].index=1;g->f[0].player=0;g->f[1].player=1;
    return 1;
}

/* The match's facts for fighter i, the way the original's record holds them (fighter.md). */
static int facts(t3_ball_fighter_t* o,t3_psx_match_t* m,unsigned i) {
    const t3_psx_fighter_t* f=&m->fighter[i];
    t3_hurt_cylinder_t hurt[T3_PSX_HURT_ZONES];
    unsigned k,s;
    o->index=(int32_t)i;o->player=(int32_t)i;
    o->x=f->ground.root.x;o->z=f->ground.root.z;
    o->head=f->world[1].origin.y;                       /* +0x950: the second joint block's y */
    o->health=f->health_q16;
    o->kind=f->vm.bank?(int32_t)f->vm.bank->bank_type:0;
    o->level=f->vm.move.attack_word;o->damage=f->ground.damage;o->guard=f->ground.guard;
    o->slot=f->vm.slot;
    o->airborne=f->ground.in_air!=0;
    o->busy3=f->body.active!=0;o->busy4=f->ground.in_throw!=0;
    o->projectile=0;
    o->seg_count=(int32_t)f->strike.segment_count;
    if(o->seg_count>2)o->seg_count=2;
    for(k=0;k<4u;++k)for(s=0;s<6u;++s)o->segs[k][s]=0;
    for(k=0;k<(unsigned)o->seg_count;++k) {
        const t3_world_segment_t* g=&f->strike.segments[k];
        o->segs[k][0]=g->start.x;o->segs[k][1]=g->start.y;o->segs[k][2]=g->start.z;
        o->segs[k][3]=g->end.x;o->segs[k][4]=g->end.y;o->segs[k][5]=g->end.z;
    }
    __builtin_memset(hurt,0,sizeof hurt);
    if(f->body.active&&!t3_psx_hurt_from_parts(f->world,f->parts_mask,t3_psx_hurt_profile(m->config->character[i]),hurt))return 0;
    for(k=0;k<T3_PSX_HURT_ZONES;++k) {
        o->cyl[k][0]=hurt[k].center.x;o->cyl[k][1]=hurt[k].center.y;o->cyl[k][2]=hurt[k].center.z;
        o->cyl[k][3]=hurt[k].radius;o->cyl[k][4]=(int32_t)hurt[k].radius*(int32_t)hurt[k].radius;
    }
    return 1;
}

#if defined(T3_BALL_TEST_CHARGED)
#define T3_BALL_TEST_CHARGED_AT 600u
#ifndef T3_BALL_TEST_CHARGED_FRAMES
#define T3_BALL_TEST_CHARGED_FRAMES 8u
#endif
#ifndef T3_BALL_TEST_CHARGE
#define T3_BALL_TEST_CHARGE 50
#endif
volatile int32_t g_t3_charged_log[T3_BALL_TEST_CHARGED_FRAMES][11];
/* the stand-in's first attack segment at the touch (start x,y,z, end x,y,z): the hit direction of the charged hit (set_hit_dir) */
volatile int32_t g_t3_charged_seg[6];
#endif
int t3_ball_game_tick(t3_ball_game_t* g,t3_psx_match_t* m,const t3_ball_tables_t* tables) {
    unsigned i;
    if(!g||!m||!tables||!g->active)return 0;
    g->events.count=0;
    if(m->flow.played!=(int32_t)g->played) {
        /* FUN_800B0C90 runs at every round start: a new ball served by the last round's loser, empty gauges and no texts.
         * The first round's ball was started by t3_ball_game_init. */
        if(g->played) {
            g->world.serve_side=m->flow.serve_side&1;g->world.first_round=m->flow.played==1;
            t3_ball_fight_start(&g->ball,&g->world,g->options[g->option]);
            __builtin_memset(g->gauge,0,sizeof g->gauge);__builtin_memset(g->popup,0,sizeof g->popup);
            g->last_kind=0;
        }
        g->played=(uint8_t)m->flow.played;
    }
    for(i=0;i<2u;++i) {
        const t3_psx_fighter_t* f=&m->fighter[i];
        const int16_t slot=(int16_t)f->vm.slot,pose=(int16_t)f->vm.clock.pose_frame;
        if(!facts(&g->f[i],m,i))return 0;
        /* +0x85: this move already hit the ball; a new move releases it */
        if(slot!=g->last_slot[i]||pose<g->last_pose[i])g->f[i].locked=0;
        g->last_slot[i]=slot;g->last_pose[i]=pose;
    }
    g->world.lob_base=(int32_t)m->fighter_distance;
    g->world.round_state=m->flow.state;
    g->world.round_clock=(int32_t)m->fight_clock;
    g->world.no_score=0;
    g->world.no_winner_mark=0;
    for(i=0;i<2u;++i){t3_ball_move_tuning(&g->f[i]);g->f[i].f87=0;}
    if(g->world.touch_sound_wait>0)--g->world.touch_sound_wait;
#if defined(T3_BALL_TEST_CHARGED)
    /* test only (the PS1 probe's shot, scripts/pcsx_redux_ball_hit.lua with T3_HIT_BODY=0): at tick T3_BALL_TEST_CHARGED_AT the ball,
     * charged by fighter 1, touches fighter 0's body, and fighter 0 (the touched one) takes the charge: the victim's reaction is compared */
#if defined(T3_BALL_TEST_UNGUARDED)
    /* test only: fighter 0's powerTimer (120 frames) runs from the tick before the touch, so the fighter update of the touch tick clears its
     * guard (combat.md: "cleared while powerTimer runs") and the charged touch and its HitApply see an unguarded fighter */
    if(g->ticks==T3_BALL_TEST_CHARGED_AT-1u)m->fighter[0].ground.power_timer=120;
#endif
    if(g->ticks==T3_BALL_TEST_CHARGED_AT) {
        /* the owner (the charger) is fighter 1; a touch by the owner is ignored, and a touch overwrites the charge in the same tick (_charged_touch) */
        g->ball.owner=1;g->ball.target=1;g->ball.charge=T3_BALL_TEST_CHARGE;g->ball.state=BALL_FLY;
        g->ball.pos[0]=g->f[0].x;g->ball.pos[1]=g->f[0].head-0x200;g->ball.pos[2]=g->f[0].z;
        for(i=0;i<3;++i){g->ball.prev[i]=g->ball.pos[i];g->ball.vel[i]=0;}
        g->ball.speed=0;
        g->stand_in=m->fighter[1];   /* the owner's strike copies the charger's record (attack_hit); the forced touch has none, so the copy is made here */
        g->world.stand_in.level=0xFFFF;   /* and its attack word (attack_hit): the guard test of the charged touch reads it */
        g_t3_charged_seg[0]=g->stand_in.strike.segments[0].start.x;g_t3_charged_seg[1]=g->stand_in.strike.segments[0].start.y;
        g_t3_charged_seg[2]=g->stand_in.strike.segments[0].start.z;g_t3_charged_seg[3]=g->stand_in.strike.segments[0].end.x;
        g_t3_charged_seg[4]=g->stand_in.strike.segments[0].end.y;g_t3_charged_seg[5]=g->stand_in.strike.segments[0].end.z;
        /* the PS1 charger had no attack segment at this tick (its record reads zero there, so the hit direction is zero): the forced
         * stand-in has none either, so the slide is not steered by a stale segment of the charger's last move */
        g->stand_in.strike.segments[0].start.x=g->stand_in.strike.segments[0].start.y=g->stand_in.strike.segments[0].start.z=0;
        g->stand_in.strike.segments[0].end.x=g->stand_in.strike.segments[0].end.y=g->stand_in.strike.segments[0].end.z=0;
        /* attack_touch runs before body_touch: a fighter's strike segments still lying in its body would strike the ball as a set-up
         * normal hit (owner 0, charge 0), so this tick's touch has no strike segments and only the body test decides it */
        g->f[0].seg_count=0;g->f[1].seg_count=0;
        /* body_touch reads the swept volume the last tick left (hit_volume), not pos: place it at the forced pos too, and clear touch_wait */
        {
            unsigned j,c;
            for(j=0;j<4u;++j)for(c=0;c<3u;++c)
                g->ball.volume[j][c]=g->ball.volume[j][3+c]=g->ball.pos[c]+(int32_t)(int16_t)g->world.hit_offsets[j][c];
        }
        g->ball.touch_wait=0;
    }
#endif
    /* the ball waits while the fight is frozen (a hit's freeze, a pause); the replay records it, or plays it back instead of
     * running it (replay_state.gd ball: the playback frames are the ones ReplayUpdate returned non-zero for) */
    if(!m->flow.freeze) {
        const int mode=m->replay.mode;
        if(mode==T3_REPLAY_RECORDING) {
            t3_ball_replay_frame_t* r=&g->replay[(unsigned)m->replay.record%BALL_REPLAY_FRAMES];
            for(i=0;i<3u;++i){r->pos[i]=g->ball.pos[i];r->squash_axis[i]=(int16_t)g->ball.squash_axis[i];}
            r->spin[0]=(int16_t)g->ball.spin[0];r->spin[1]=(int16_t)g->ball.spin[1];
            r->kind=(uint8_t)g->ball.kind;r->timer=(uint8_t)g->ball.timer;r->squash=(int16_t)g->ball.squash;
        } else if(mode==T3_REPLAY_PLAYING||mode==T3_REPLAY_FIRST) {
            const t3_ball_replay_frame_t* r=&g->replay[(unsigned)(m->replay.play>>1)%BALL_REPLAY_FRAMES];
            for(i=0;i<3u;++i){g->ball.pos[i]=r->pos[i];g->ball.squash_axis[i]=r->squash_axis[i];}
            g->ball.spin[0]=r->spin[0];g->ball.spin[1]=r->spin[1];
            g->ball.kind=r->kind;g->ball.timer=r->timer;g->ball.squash=r->squash;
        }
        if(mode!=T3_REPLAY_PLAYING&&mode!=T3_REPLAY_FIRST&&mode!=T3_REPLAY_ENDING) {
            if(!t3_ball_update(&g->ball,&g->world,g->f,tables,0,&g->events))return 0;
            if(!t3_ball_after_update(&g->ball,&g->world,tables,&g->events))return 0;
        } else if(g->ball.kind==2&&g->ball.timer==5&&g->events.count<BALL_EV_MAX) {
            /* the replayed point's flash replays its bounce */
            t3_ball_event_t* x=&g->events.e[g->events.count++];
            x->kind=BALL_EV_SOUND;x->a=0x4E6E;x->b=x->c=x->d=x->e=0;
        }
    }
    m->ball_shows=(uint8_t)g->world.result;
    /* SoundPlayFighter(0, code, 0): the ball's sounds are raw sound codes, not sound-table ids (0x704E is not table entry 0x4E) */
    for(i=0;i<(unsigned)g->events.count;++i)
        if(g->events.e[i].kind==BALL_EV_SOUND&&m->config->sound_sink)
            m->config->sound_sink(m->config->sound_sink_user,T3_SOUND_CODE,0u,(uint16_t)g->events.e[i].a);
    for(i=0;i<(unsigned)g->events.count;++i)   /* a hard landing shakes the camera (CameraShakeStart, scripts 0 and 1 as the fighters' landings) */
        if(g->events.e[i].kind==BALL_EV_SHAKE&&(unsigned)g->events.e[i].a<2u)t3_psx_camera_shake_start(&m->camera.camera,g->events.e[i].a);
    for(i=0;i<(unsigned)g->events.count;++i)   /* the original copies the hitter's whole record into the stand-in */
        if(g->events.e[i].kind==BALL_EV_STAND_IN&&(unsigned)g->events.e[i].a<2u)g->stand_in=m->fighter[g->events.e[i].a];
    for(i=0;i<BALL_POPUPS;++i)if(g->popup[i].used&&++g->popup[i].age>=BALL_POPUP_FRAMES)g->popup[i].used=0;
    for(i=0;i<(unsigned)g->events.count;++i) {
        const t3_ball_event_t* e=&g->events.e[i];
        unsigned k,slot=0;
        if(e->kind!=BALL_EV_POPUP)continue;
        for(k=0;k<BALL_POPUPS;++k){if(!g->popup[k].used){slot=k;break;}if(g->popup[k].age>g->popup[slot].age)slot=k;}
        g->popup[slot]=(t3_ball_popup_t){{e->c,e->d,e->e},(int16_t)e->a,0,(uint8_t)e->b,1};
    }
    for(i=0;i<2u;++i) {
        t3_psx_fighter_t* f=&m->fighter[i];
        int32_t health=g->f[i].health;
        /* A charged ball that reached this player: the next HitApply takes it as a hit from the other fighter with the charge as damage
         * (it decides guarding and the reaction); the point's damage (health) is the ball's own. */
        /* f87 is set on the hitter, the fighter whose body the ball touched, and that fighter takes the charge: the owner is the opponent
         * (modes.md, "Charged ball hits the opponent"; the PS1 probe with the ball on one body) */
        if(g->f[i].f87) {m->ball_hit.pending=1;m->ball_hit.victim=(uint8_t)i;m->ball_hit.zone=(uint8_t)g->f[i].contact_cyl;m->ball_hit.charge=(int16_t)g->world.stand_in.damage;m->ball_hit.stand_in=&g->stand_in;}
        /* a point as the ball left it, or a charged hit: the health never goes below 0 */
        f->health_q16=health<0?0:health;
        t3_ball_gauge_step(&g->gauge[i],g->ball.power[i],BALL_POWER_MAX);
    }
    /* a point's flash (kind 2, k = 5) throws 16 sparks (effect type 1) around the ball (ball_sim.sparks: the position -0xFF + 9 random bits
     * per axis, pitch 0x400 and a yaw of 0 or 0x100); the original's random numbers are the AI's, these come from the tick count */
#if defined(T3_BALL_TEST_POINT)
    if(g->ticks%300u>=60u && g->ticks%300u<120u)g->ball.kind=6;   /* test only: the charged ball's glows (kinds 6, 7 and 9) */
    else if(g->ticks%300u>=120u && g->ticks%300u<180u)g->ball.kind=7;
    else if(g->ticks%300u>=180u && g->ticks%300u<240u)g->ball.kind=9;
    if(g->ticks%300u==0u) {   /* test only: a point's flash and a burn of fighter 1 every five seconds */
        g->ball.kind=2;g->ball.timer=5;
        if(g->events.count<BALL_EV_MAX){t3_ball_event_t* x=&g->events.e[g->events.count++];x->kind=BALL_EV_BURN;x->a=0;x->b=x->c=x->d=x->e=0;}
        if(m->config->effects) {   /* and the fire, flame, floor fire, cloud and gas objects in a row between the fighters */
            static const uint8_t kinds[5]={6,8,9,10,20};
            for(i=0;i<5u;++i) {
                t3_psx_spark_t* o=t3_psx_effect_alloc(m->config->effects,kinds[i]);
                if(!o)break;
                o->x=(g->f[0].x+g->f[1].x)/2+((int32_t)i-2)*900;o->y=-400;o->z=g->f[0].z;
                o->pitch=0;o->yaw=0x400;o->speed=0x300;
            }
        }
    }
#endif
    if(g->ball.kind==2&&g->last_kind!=2&&g->ball.timer==5&&m->config->effects) {
        uint32_t seed=g->ticks*2654435761u+0x9E3779B9u;
        for(i=0;i<16u;++i) {
            t3_psx_spark_t* o=t3_psx_effect_alloc(m->config->effects,1);
            if(!o)break;
            seed=seed*1664525u+1013904223u;
            o->x=g->ball.pos[0]-0xFF+(int32_t)(seed>>23);
            o->y=g->ball.pos[1]-0xFF+(int32_t)((seed>>14)&0x1FFu);
            o->z=g->ball.pos[2]-0xFF+(int32_t)((seed>>5)&0x1FFu);
            o->pitch=0x400;o->yaw=(int16_t)((seed>>3)&0x100u);
        }
    }
    g->last_kind=(uint8_t)g->ball.kind;
    /* the fight camera's third target (FUN_800B4948): the ball as this tick left it, seen by the next frame's camera */
    for(i=0;i<3u;++i)m->camera.camera.ball[i]=g->ball.pos[i];
    m->camera.camera.ball_on=1;
#if defined(T3_BALL_TEST_CHARGED)
    /* test only: T3_BALL_TEST_CHARGED_FRAMES ticks from the forced touch, eleven words a tick: ticks since, fighter 0's health (16.16,
     * the touched fighter that takes the charge), its slot, guard, in_air and got_hit (the hit transaction's fact that tick), its root z
     * (the ball world's), fighter 1's (the charger's) health, slot and got_hit, to see which fighter takes the hit reaction and when the
     * damage lands, then the ball's state (5: the lethal hit's pop-up) */
    if(g->ticks>=T3_BALL_TEST_CHARGED_AT && g->ticks<T3_BALL_TEST_CHARGED_AT+T3_BALL_TEST_CHARGED_FRAMES) {
        volatile int32_t* row=g_t3_charged_log[g->ticks-T3_BALL_TEST_CHARGED_AT];
        row[0]=(int32_t)(g->ticks-T3_BALL_TEST_CHARGED_AT);row[1]=m->fighter[0].health_q16;row[2]=(int32_t)m->fighter[0].vm.slot;
        row[3]=m->fighter[0].ground.guard;row[4]=m->fighter[0].ground.in_air;row[5]=m->fighter[0].got_hit;row[6]=g->f[0].z;
        row[7]=m->fighter[1].health_q16;row[8]=(int32_t)m->fighter[1].vm.slot;row[9]=m->fighter[1].got_hit;row[10]=g->ball.state;
    }
#endif
    ++g->ticks;
    return 1;
}

void t3_ball_game_view(const t3_ball_game_t* g,t3_ball_view_t* out) {
    if(!g||!out)return;
    out->ball=g->ball;out->gauge[0]=g->gauge[0];out->gauge[1]=g->gauge[1];
    out->fighter_z[0]=g->f[0].z;out->fighter_z[1]=g->f[1].z;out->round_state=g->world.round_state;
    for(unsigned k=0;k<BALL_POPUPS;++k)out->popup[k]=g->popup[k];
    out->option=g->option;out->active=g->active;
}

#endif /* T3_PSX_BALL */
