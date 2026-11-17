/* ctypes entry point of the Tekken Ball ball for scripts/check_ball_flow_trace.py: one frame of the ball
 * (FUN_800B16A4 then FUN_800B2F14) over the flat int32 state of tests/test_ball_rules.c. */
#include "ball_rules.h"
#include "ball_hud.h"
#include <string.h>

typedef struct { t3_ball_t ball; t3_ball_world_t world; t3_ball_fighter_t f[2]; } shim_state_t;

int ball_run(int32_t* state,const uint8_t* trig,unsigned trig_size,const uint8_t* atan,unsigned atan_size,int s7,
             int32_t* events) {
    shim_state_t* s=(shim_state_t*)state;
    t3_ball_tables_t t;
    t3_ball_events_t ev;
    memset(&t,0,sizeof t);memset(&ev,0,sizeof ev);
    t.tables.ground.trig.data=trig;t.tables.ground.trig.size=trig_size;
    t.tables.ground.atan=atan;t.tables.ground.atan_size=atan_size;
    if(!t3_ball_update(&s->ball,&s->world,s->f,&t,s7,&ev))return 0;
    if(!t3_ball_after_update(&s->ball,&s->world,&t,&ev))return 0;
    events[0]=ev.count;
    for(int i=0;i<ev.count;++i) {
        events[1+6*i]=ev.e[i].kind;events[2+6*i]=ev.e[i].a;events[3+6*i]=ev.e[i].b;
        events[4+6*i]=ev.e[i].c;events[5+6*i]=ev.e[i].d;events[6+6*i]=ev.e[i].e;
    }
    return (int)(sizeof(shim_state_t)/4);
}

/* One gauge frame over {last power, target, fill, trail}. */
void ball_gauge(int32_t* g,int32_t power,int32_t maximum) {
    t3_ball_gauge_t x={(uint32_t)g[0],(int16_t)g[1],(int16_t)g[2],(int16_t)g[3]};
    t3_ball_gauge_step(&x,power,maximum);
    g[0]=(int32_t)x.last_power;g[1]=x.target;g[2]=x.fill;g[3]=x.trail;
}
