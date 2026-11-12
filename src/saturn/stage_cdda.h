#ifndef T3_STAGE_CDDA_H
#define T3_STAGE_CDDA_H
#include <saturn/core.h>
#include <saturn/cdda.h>
#if defined(T3_STAGE_CDDA)
/* `track` is the original's music track, 7..27 for the arranged fighter themes. */
sat_result_t t3_stage_cdda_start(unsigned track);
#if defined(T3_SELECT_CDDA_SECTORS)
sat_result_t t3_select_cdda_start(void);
#endif
#if defined(T3_RESULT_CDDA_SECTORS)
/* The result screens' music (the original's track 6). */
sat_result_t t3_result_cdda_start(void);
#endif
void t3_stage_cdda_mix(void);
sat_result_t t3_stage_cdda_status(sat_cd_status_t* status);
#else
static inline sat_result_t t3_stage_cdda_start(unsigned track) { (void)track;return SAT_OK; }
static inline void t3_stage_cdda_mix(void) {}
static inline sat_result_t t3_stage_cdda_status(sat_cd_status_t* status) { (void)status;return SAT_ERR_UNSUPPORTED; }
#endif
#endif
