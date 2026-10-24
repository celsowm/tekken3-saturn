#include "sim_profile.h"
#if defined(T3_SIM_PROFILE)
#include <saturn/profile.h>
#include <saturn/time.h>
#if !defined(T3_SIM_PROFILE_CAPACITY)
#define T3_SIM_PROFILE_CAPACITY 128
#endif
/* Retained rows; a build short of Low Work RAM lowers it (totals are kept). */
enum { ACTORS=3, CAPACITY=T3_SIM_PROFILE_CAPACITY, COUNTERS=ACTORS*T3_SIM_STAGES, DEPTH=24 };
typedef struct {
    uint32_t field,tick,state,replay,active,errors,depth;
    sat_profile_counter_t counters[COUNTERS];
} sample_t;
typedef struct {
    uint32_t magic,version,frequency,count,cursor,epoch,after_field;
    uint32_t stages,actors,capacity,errors;
    sat_profile_counter_t totals[COUNTERS];
    sample_t samples[CAPACITY];
} capture_t;
_Static_assert(sizeof(sat_profile_counter_t)==12,"profile counter wire layout");
_Static_assert(sizeof(sample_t)==28+12*COUNTERS,"profile sample wire layout");
_Static_assert(sizeof(capture_t)==44+12*COUNTERS+CAPACITY*sizeof(sample_t),
               "profile capture wire layout");
/* The full capture needs Low Work RAM; a few rows fit with the rest of .bss. */
#if T3_SIM_PROFILE_CAPACITY>=16
volatile capture_t g_t3_sim_profile __attribute__((section(".wram_l")));
#else
volatile capture_t g_t3_sim_profile;
#endif
static sample_t current;
static sat_profile_t profile;
static sat_profile_span_t spans[DEPTH];
static uint32_t actor=2,enabled;
void t3_sim_init(void) {
    g_t3_sim_profile.magic=0x54335350u;g_t3_sim_profile.version=1;
    g_t3_sim_profile.frequency=sat_time_frequency_hz();
    g_t3_sim_profile.count=g_t3_sim_profile.cursor=g_t3_sim_profile.epoch=0;
    g_t3_sim_profile.after_field=300;g_t3_sim_profile.stages=T3_SIM_STAGES;
    g_t3_sim_profile.actors=ACTORS;g_t3_sim_profile.capacity=CAPACITY;
    g_t3_sim_profile.errors=0;
    for(unsigned i=0;i<COUNTERS;++i)
        g_t3_sim_profile.totals[i]=(sat_profile_counter_t){0,0,0};
    enabled=0;actor=2;
}
uint32_t t3_sim_enter(unsigned stage) {
    if(!enabled)return UINT32_MAX;
    uint32_t token=actor*T3_SIM_STAGES+stage;
    sat_profile_enter(&profile,token,(uint32_t)sat_time_ticks());return token;
}
void t3_sim_leave(uint32_t token) {
    if(token!=UINT32_MAX)
        sat_profile_leave(&profile,token,(uint32_t)sat_time_ticks());
}
unsigned t3_sim_actor(unsigned value) {
    unsigned previous=actor;actor=value<2u?value:2u;return previous;
}
void t3_sim_count(unsigned stage) {
    if(!enabled)return;
    if(stage>=T3_SIM_STAGES){profile.errors|=SAT_PROFILE_INVALID;return;}
    sat_profile_counter_t* c=&current.counters[actor*T3_SIM_STAGES+stage];
    if(c->calls==UINT32_MAX)profile.errors|=SAT_PROFILE_OVERFLOW;
    else ++c->calls;
}
void t3_sim_context(uint32_t state,uint32_t replay,uint32_t active) {
    if(enabled){current.state=state;current.replay=replay;current.active=active;}
}
void t3_sim_tick_begin(uint32_t field,uint32_t tick) {
    enabled=field>=g_t3_sim_profile.after_field;
    if(!enabled)return;
    current.field=field;current.tick=tick;current.state=current.replay=current.active=0;
    current.errors=current.depth=0;actor=2;
    sat_profile_init(&profile,current.counters,COUNTERS,spans,DEPTH);
    t3_sim_enter(T3_SIM_TICK);
}
void t3_sim_tick_end(void) {
    if(!enabled)return;
    t3_sim_leave(2*T3_SIM_STAGES+T3_SIM_TICK);
    current.errors=profile.errors;current.depth=profile.depth;
    if(!sat_profile_complete(&profile))current.errors|=SAT_PROFILE_ORDER;
    ++g_t3_sim_profile.epoch;
    g_t3_sim_profile.samples[g_t3_sim_profile.cursor]=current;
    for(unsigned i=0;i<COUNTERS;++i) {
        sat_profile_counter_t old=g_t3_sim_profile.totals[i],add=current.counters[i];
        if(UINT32_MAX-old.inclusive<add.inclusive ||
           UINT32_MAX-old.exclusive<add.exclusive || UINT32_MAX-old.calls<add.calls)
            g_t3_sim_profile.errors|=SAT_PROFILE_OVERFLOW;
        else {
            old.inclusive+=add.inclusive;old.exclusive+=add.exclusive;old.calls+=add.calls;
            g_t3_sim_profile.totals[i]=old;
        }
    }
    g_t3_sim_profile.errors|=current.errors;
    g_t3_sim_profile.cursor=(g_t3_sim_profile.cursor+1u)%CAPACITY;
    ++g_t3_sim_profile.count;++g_t3_sim_profile.epoch;enabled=0;
}
#endif
