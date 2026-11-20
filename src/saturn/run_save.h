#ifndef T3_SATURN_RUN_SAVE_H
#define T3_SATURN_RUN_SAVE_H
#include "run_records.h"
#include "saturn/core.h"
#include "saturn/save.h"
/* The Survival and Time Attack records in the internal backup, as force_save.h: only our own record is read or replaced,
 * never formats, and the Boot ROM's backup library runs from `workspace`. A missing record loads as no records. */
sat_result_t t3_run_save_load(t3_run_records_t* out,void* workspace,unsigned bytes);
sat_result_t t3_run_save_store(const t3_run_records_t* records,void* workspace,unsigned bytes);
#endif
