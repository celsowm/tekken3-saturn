#ifndef T3_SIM_PROFILE_H
#define T3_SIM_PROFILE_H
#include <stdint.h>
enum {
    T3_SIM_TICK, T3_SIM_MATCH, T3_SIM_COPY, T3_SIM_MOVE_BEGIN,
    T3_SIM_GROUND, T3_SIM_ROOT, T3_SIM_MOTION, T3_SIM_FK, T3_SIM_IK,
    T3_SIM_BLEND, T3_SIM_HEAD, T3_SIM_PARTS, T3_SIM_COMPOSE, T3_SIM_BODIES,
    T3_SIM_CONTACT, T3_SIM_HITS, T3_SIM_BRANCH, T3_SIM_BRANCH_DECODE,
    T3_SIM_COMMAND, T3_SIM_BRIDGE, T3_SIM_CACHE_HIT, T3_SIM_CACHE_MISS, T3_SIM_STAGES
};
#if defined(T3_SIM_PROFILE)
uint32_t t3_sim_enter(unsigned stage);
void t3_sim_leave(uint32_t token);
unsigned t3_sim_actor(unsigned actor);
void t3_sim_context(uint32_t state,uint32_t replay,uint32_t active);
void t3_sim_count(unsigned stage);
void t3_sim_tick_begin(uint32_t field,uint32_t tick);
void t3_sim_tick_end(void);
void t3_sim_init(void);
#define T3_SIM_IMPL(name) name##_profile_impl
#define T3_SIM_WRAP(type,name,stage,params,args) \
    type name params { \
        uint32_t token=t3_sim_enter(stage); \
        int result=name##_profile_impl args; \
        t3_sim_leave(token);return result; \
    }
#define T3_SIM_WRAP_ACTOR(type,name,stage,params,args,actor) \
    type name params { \
        unsigned previous=t3_sim_actor(actor); \
        uint32_t token=t3_sim_enter(stage); \
        int result=name##_profile_impl args; \
        t3_sim_leave(token);t3_sim_actor(previous);return result; \
    }
#define T3_SIM_START(token,stage) uint32_t token=t3_sim_enter(stage)
#define T3_SIM_END(token) t3_sim_leave(token)
#else
#define T3_SIM_IMPL(name) name
#define T3_SIM_WRAP(type,name,stage,params,args)
#define T3_SIM_WRAP_ACTOR(type,name,stage,params,args,actor)
#define T3_SIM_START(token,stage) ((void)0)
#define T3_SIM_END(token) ((void)0)
#define t3_sim_init() ((void)0)
#define t3_sim_tick_begin(field,tick) ((void)0)
#define t3_sim_tick_end() ((void)0)
#define t3_sim_context(state,replay,active) ((void)0)
#define t3_sim_count(stage) ((void)0)
#endif
#endif
