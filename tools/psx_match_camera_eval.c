/* PRIVATE native run of the owned match with the original fight camera, set
 * up as the Saturn build sets it up, on scripted pads. It checks that every
 * step stays valid through rounds, KO replays and the round-end cameras, and
 * that a Saturn look-at camera made from the PS1 view (forward = row 2,
 * up = -row 1, through the Saturn axes x, -y, -z) puts each fighter's screen
 * joint where the GTE projection does. Generated data is a local build input,
 * never a public fixture.
 *
 *   t3_match_camera_eval [frames [cameras.jsonl [every]]]
 *       -> one JSON summary line; exit 0 on pass. With a path, every
 *          `every`-th frame's view (eye relative to the fighters' root
 *          midpoint, Q12 rows, H and the panorama turn) is written there
 *          for the backdrop checks. */
#include "psx_match.h"
#include "attack_records.h"
#include "t3_local_divmot.h"
#include "t3_local_camera.h"
#include "t3_generated_character.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { SCREEN_JOINT = 11, DEFAULT_FRAMES = 9000 };
/* The PS1 display: 368 x 480 around (192, 240); row 1 carries 0x1BD2/4096. */
#define PSX_HALF_W 184.0
#define PSX_HALF_H 240.0
#define PSX_ROW1 (7122.0/4096.0)

static t3_psx_match_t game;
static t3_psx_match_config_t config;
static t3_psx_fight_camera_config_t camera;
static t3_psx_camera_tables_t tables;
static t3_psx_trig_t trig;
static t3_divmot_t bank,common;
static t3_psx_replay_storage_t history;
static t3_motion_sample_cache_t samples;

static int setup(void) {
    if(!t3_divmot_open(&bank,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES) ||
       !t3_divmot_open(&common,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES) ||
       !t3_divmot_set_common_branches(&bank,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES) ||
       !t3_divmot_set_common_branches(&common,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES))return 0;
    /* The camera shares the build's trig, atan (ground) and square root (body) tables. */
    tables=t3_local_camera_tables;trig=(t3_psx_trig_t){t3_local_trig_blob,T3_LOCAL_TRIG_BYTES};
    tables.trig=&trig;tables.atan=t3_local_ground_blob;
    tables.sqrt=t3_local_body_blob;tables.sqrt_size=384u;
    camera=(t3_psx_fight_camera_config_t){
        .tables=&tables,
        .bank_choices=t3_local_camera_bank_choices,
        .bank_choice_count=T3_LOCAL_CAMERA_BANK_CHOICES,
        .bank_streams=&t3_local_camera_bank_streams};
    if(T3_LOCAL_COMBAT_ATTACK_BYTES==T3_BUILTIN_ATTACK_BYTES)
        bank.builtin_attacks=common.builtin_attacks=t3_local_combat_attacks;
    config.bank=&bank;config.common=&common;config.sample_cache=&samples;
    if(!t3_psx_ground_tables_bind(trig,t3_local_ground_blob,T3_LOCAL_GROUND_BYTES,&config.ground_tables))return 0;
    config.spline=(t3_motion_tables_t){t3_local_spline_blob,T3_LOCAL_SPLINE_BYTES};
    config.sequences=t3_local_sequences;config.sequence_count=T3_LOCAL_SEQUENCE_COUNT;
    config.enable_body=config.enable_combat=1;config.enable_rounds=2;
    config.replay_storage=&history;config.collapse_on_ko=1;
    config.reactions=t3_local_combat_reactions;config.reactions_size=T3_LOCAL_COMBAT_REACTION_BYTES;
    config.close_reactions=t3_local_combat_close;config.close_reactions_size=T3_LOCAL_COMBAT_CLOSE_BYTES;
    config.throw_victim_slots=t3_local_combat_throws;config.throw_victim_slots_size=T3_LOCAL_COMBAT_THROW_BYTES;
    config.parts[0]=t3_private_actor_parts[0];config.parts[1]=t3_private_actor_parts[1];
    config.ik=(t3_psx_ik_tables_t){{t3_local_trig_blob,T3_LOCAL_TRIG_BYTES},t3_local_ik_blob,T3_LOCAL_IK_BYTES};
    config.body_tables=t3_local_body_blob;config.body_tables_size=T3_LOCAL_BODY_BYTES;
#if defined(T3_LOCAL_RSQRT_BYTES) && T3_LOCAL_RSQRT_BYTES > 0
    config.rsqrt=(t3_psx_rsqrt_t){t3_local_rsqrt_blob,T3_LOCAL_RSQRT_BYTES};
#endif
    config.scale_percent=T3_KMD_SCALE_PERCENT;config.max_health_q16=140*65536;
    config.spawn[0]=(t3_psx_spawn_t){{-1000,0,-4},{-1000,-1027,-4},16384u,16384u};
    config.spawn[1]=(t3_psx_spawn_t){{1000,0,4},{1000,-1027,4},49151u,49151u};
    config.camera=&camera;
    return t3_psx_match_init(&game,&config);
}

/* Scripted pads: player 1 walks in and attacks, player 2 mixes guard and attacks. */
static uint32_t pad_rand=12345u;
static t3_input_t pad(unsigned i,uint16_t* last) {
    static const uint16_t moves[8]={T3_IN_FORWARD,T3_IN_LP,T3_IN_RP,T3_IN_LK,T3_IN_RK,
        T3_IN_FORWARD|T3_IN_RP,T3_IN_CROUCH|T3_IN_RK,T3_IN_BACK|T3_IN_GUARD};
    uint16_t held;
    pad_rand=pad_rand*1103515245u+12345u;
    held=moves[(pad_rand>>16)%(i?8u:7u)];
    if((pad_rand>>8)&1u)held=last[i];
    t3_input_t r={held,(uint16_t)(held&~last[i])};
    last[i]=held;
    return r;
}

typedef struct { double x,y,z; } v3;
static v3 sub(v3 a,v3 b){return (v3){a.x-b.x,a.y-b.y,a.z-b.z};}
static double dot(v3 a,v3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
static v3 cross(v3 a,v3 b){return (v3){a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
static v3 unit(v3 a){double n=sqrt(dot(a,a));return (v3){a.x/n,a.y/n,a.z/n};}
static v3 saturn(v3 a){return (v3){a.x,-a.y,-a.z};}

/* The PS1 point through the Saturn look-at camera, as the PS1 screen offset
 * it stands for (sx, sy in PS1 pixels from the centre); 0 behind the eye. */
static int saturn_view(const t3_psx_camera_matrix_t* m,const t3_psx_camera_view_t* v,
                       v3 p,double* sx,double* sy) {
    v3 r1={m->m[1][0],m->m[1][1],m->m[1][2]},r2={m->m[2][0],m->m[2][1],m->m[2][2]};
    v3 eye=saturn((v3){v->x,v->y,v->z}),f=unit(saturn(r2)),up=unit(saturn(r1));
    v3 s,u,d;
    up=(v3){-up.x,-up.y,-up.z};
    s=unit(cross(f,up));u=cross(s,f);d=sub(saturn(p),eye);
    if(dot(d,f)<=0)return 0;
    /* NDC times the PS1 half extents: tan = half / H across, half / (H 0x1BD2/4096) down. */
    *sx=dot(d,s)/dot(d,f)*m->h;
    *sy=-dot(d,u)/dot(d,f)*m->h*PSX_ROW1;
    return 1;
}

int main(int argc,char** argv) {
    unsigned frames=argc>1?(unsigned)strtoul(argv[1],0,10):DEFAULT_FRAMES;
    unsigned phases=0,cinematics=0,shakes=0,replays=0,projected=0,overhead=0,run=0;
    unsigned every=argc>3?(unsigned)strtoul(argv[3],0,10):15u;
    double worst=0,roll=0,low=1e9,high=-1e9,height_sum=0,pitch_lo=1e9,pitch_hi=-1e9;
    int32_t h_lo=1<<30,h_hi=0;uint16_t last[2]={0,0};
    FILE* dump=argc>2?fopen(argv[2],"w"):0;
    if(argc>2 && !dump){printf("{\"error\":\"dump\"}\n");return 1;}
    if(!every)every=1;
    if(!setup()){printf("{\"error\":\"setup\"}\n");return 1;}
    for(run=0;run<frames;++run) {
        t3_input_t p0=pad(0,last),p1=pad(1,last);
        t3_psx_match_frame_t fr={.frame_counter=game.ticks+1};
        const t3_psx_fight_camera_t* c=&game.camera;
        if(game.flow.state==T3_ROUND_END && game.flow.match_result)break;
        if(!t3_psx_match_step_original_in_place(&game,p0,p1,&fr)) {
            printf("{\"error\":\"step\",\"frame\":%u,\"phase\":%d,\"flow\":%d,\"state\":%d,\"gap\":%u,\"replay\":%d}\n",
                   run,c->phase,game.flow.camera,(int)game.flow.state,(unsigned)game.round_gap,game.replay.mode);
            return 1;
        }
        phases|=1u<<(c->phase&15);cinematics|=1u<<(c->camera.cinematic&15);
        /* A start late in the frame leaves its script at position 0. */
        shakes+=c->camera.shake_script>=0 && c->camera.shake_pos==0;
        replays+=game.replay.mode==T3_REPLAY_FIRST;overhead+=c->camera.overhead;
        /* The VDP2 floor needs a level right axis (row 0) and the eye above the floor. */
        roll=fmax(roll,fabs(c->matrix.m[0][1]/4096.0));
        if(-c->camera.view.y<low)low=-c->camera.view.y;
        if(-c->camera.view.y>high)high=-c->camera.view.y;
        height_sum+=-c->camera.view.y;
        if(c->matrix.h<h_lo)h_lo=c->matrix.h;
        if(c->matrix.h>h_hi)h_hi=c->matrix.h;
        {
            /* Pitch of the forward axis (row 2), degrees up. */
            const double p=atan2(-c->matrix.m[2][1],hypot(c->matrix.m[2][0],c->matrix.m[2][2]))*180/M_PI;
            pitch_lo=fmin(pitch_lo,p);pitch_hi=fmax(pitch_hi,p);
        }
        if(dump && run%every==0) {
            const int32_t mx=(game.fighter[0].ground.root.x+game.fighter[1].ground.root.x)/2;
            const int32_t mz=(game.fighter[0].ground.root.z+game.fighter[1].ground.root.z)/2;
            const int16_t (*r)[3]=c->matrix.m;
            fprintf(dump,"{\"frame\":%u,\"eye\":[%d,%d,%d],\"rows\":[[%d,%d,%d],[%d,%d,%d],[%d,%d,%d]],"
                    "\"h\":%d,\"turn\":%d,\"phase\":%d,\"overhead\":%d}\n",run,
                    (int)(c->camera.view.x-mx),(int)c->camera.view.y,(int)(c->camera.view.z-mz),
                    r[0][0],r[0][1],r[0][2],r[1][0],r[1][1],r[1][2],r[2][0],r[2][1],r[2][2],
                    (int)c->matrix.h,(int)c->backdrop.angle,c->phase,c->camera.overhead);
        }
        for(unsigned i=0;i<2;++i) {
            const t3_psx_joint_t* j=&game.fighter[i].world[SCREEN_JOINT];
            const int32_t p[3]={j->origin.x,j->origin.y,j->origin.z};
            int32_t s[3];double sx,sy,e;
            t3_psx_camera_matrix_t view=c->matrix;
            if(!game.fighter[i].body.active || c->camera.overhead)continue;
            t3_psx_camera_project(&view,p,s);
            if(s[2]<=0 || s[0]<=-0x400 || s[0]>=0x3FF || s[1]<=-0x400 || s[1]>=0x3FF)continue;
            if(!saturn_view(&view,&c->camera.view,(v3){p[0],p[1],p[2]},&sx,&sy))continue;
            e=fmax(fabs(sx-(s[0]-192)),fabs(sy-(s[1]-240)));
            if(e>worst)worst=e;
            ++projected;
        }
    }
    printf("{\"frames\":%u,\"rounds\":%d,\"result\":%d,\"phases\":\"0x%X\",\"cinematics\":\"0x%X\","
           "\"shakes\":%u,\"replays\":%u,\"overhead_frames\":%u,\"projected\":%u,\"worst_px\":%.3f,"
           "\"max_right_y\":%.4f,\"lowest_eye\":%.0f,\"highest_eye\":%.0f,\"mean_eye\":%.0f,"
           "\"h\":[%d,%d],\"pitch_deg\":[%.1f,%.1f]}\n",
           run,game.flow.played,game.flow.match_result,phases,cinematics,shakes,replays,overhead,projected,worst,
           roll,low,high,run?height_sum/run:0.0,(int)h_lo,(int)h_hi,pitch_lo,pitch_hi);
    if(dump)fclose(dump);
    /* The GTE rounds its division; the look-at camera should land within 2 px. */
    return worst<=2.0?0:1;
}
