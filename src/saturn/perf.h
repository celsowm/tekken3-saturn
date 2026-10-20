#ifndef T3_SATURN_PERF_H
#define T3_SATURN_PERF_H
#include <stdint.h>
#include "saturn/time.h"
#ifndef T3_PERF_MODE
#define T3_PERF_MODE 0
#endif
/* Modes 1/2 deliberately remove drawing work and are diagnostic only. */
#define T3_DRAW_ACTORS (T3_PERF_MODE == 2 ? 1u : 2u)
#if defined(T3_PERF_SAMPLES)
enum { T3_PERF_CAPACITY=1024, T3_PERF_VERSION=2 };
typedef struct {
    uint32_t field, ticks, elapsed, input, simulation, skeleton, skin,
             placement, geometry, flush, hud, present, work,
             camera, stage, diagnostics;
} t3_perf_sample_t;
typedef struct {
    uint32_t version, count, cursor;
    t3_perf_sample_t samples[T3_PERF_CAPACITY];
} t3_perf_capture_t;
extern t3_perf_sample_t g_t3_perf_current;
extern volatile t3_perf_capture_t g_t3_perf;
void t3_perf_init(void);
void t3_perf_begin(void);
void t3_perf_end(void);
#define T3_PERF_START(name) const uint32_t name=sat_time_ms()
#define T3_PERF_ADD(member,name) (g_t3_perf_current.member+=sat_time_ms()-(name))
#else
#define T3_PERF_START(name) ((void)0)
#define T3_PERF_ADD(member,name) ((void)0)
#define t3_perf_begin() ((void)0)
#define t3_perf_end() ((void)0)
#define t3_perf_init() ((void)0)
#endif
#endif
