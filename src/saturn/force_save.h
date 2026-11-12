#ifndef T3_SATURN_FORCE_SAVE_H
#define T3_SATURN_FORCE_SAVE_H
#include "force_progress.h"
#include "saturn/core.h"
/* Only our own internal-backup record is read or replaced. Never formats. */
sat_result_t t3_force_save_load(t3_force_progress_t* out);
sat_result_t t3_force_save_store(const t3_force_progress_t* progress);
#endif
