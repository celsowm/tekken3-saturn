#ifndef T3_STAGE_CDDA_H
#define T3_STAGE_CDDA_H
#include <saturn/core.h>
#if defined(T3_STAGE_CDDA)
sat_result_t t3_stage_cdda_start(void);
#if defined(T3_SELECT_CDDA_SECTORS)
sat_result_t t3_select_cdda_start(void);
#endif
void t3_stage_cdda_mix(void);
/* CD Block Get Status: status, then control/address and track, index and FAD. */
sat_result_t t3_stage_cdda_status(uint16_t response[4]);
#else
static inline sat_result_t t3_stage_cdda_start(void) { return SAT_OK; }
static inline void t3_stage_cdda_mix(void) {}
static inline sat_result_t t3_stage_cdda_status(uint16_t response[4]) { (void)response;return SAT_ERR_UNSUPPORTED; }
#endif
#endif
