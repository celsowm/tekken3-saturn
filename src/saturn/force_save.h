#ifndef T3_SATURN_FORCE_SAVE_H
#define T3_SATURN_FORCE_SAVE_H
#include "force_progress.h"
#include "saturn/core.h"
#include "saturn/save.h"
/* Only our own internal-backup record is read or replaced. Never formats. The Boot ROM's backup library runs from
 * `workspace` (4-byte aligned Work RAM of at least SAT_SAVE_WORKSPACE_BYTES, free for the call; sat_save_init_in), so a
 * program that lends a buffer it has free does not keep 24 KiB for it. */
sat_result_t t3_force_save_load(t3_force_progress_t* out,void* workspace,unsigned bytes);
sat_result_t t3_force_save_store(const t3_force_progress_t* progress,void* workspace,unsigned bytes);
#endif
