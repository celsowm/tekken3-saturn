#ifndef T3_STAGE_CDDA_H
#define T3_STAGE_CDDA_H
#include <saturn/core.h>
#if defined(T3_STAGE_CDDA)
sat_result_t t3_stage_cdda_start(void);
void t3_stage_cdda_mix(void);
#else
static inline sat_result_t t3_stage_cdda_start(void) { return SAT_OK; }
static inline void t3_stage_cdda_mix(void) {}
#endif
#endif
