#ifndef T3_SATURN_BALL_SAVE_H
#define T3_SATURN_BALL_SAVE_H
#include "ball_progress.h"
#include "saturn/core.h"
#include "saturn/save.h"
/* Tekken Ball's internal-backup record, as force_save.h's: only our own record is read or replaced, never formats, and
 * the Boot ROM's backup library runs from `workspace`. A missing record loads as nothing (`out` unchanged). */
sat_result_t t3_ball_save_load(t3_ball_choice_t* out,void* workspace,unsigned bytes);
sat_result_t t3_ball_save_store(const t3_ball_choice_t* choice,void* workspace,unsigned bytes);
#endif
