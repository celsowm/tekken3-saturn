#ifndef T3_INPUT_TAPE_H
#define T3_INPUT_TAPE_H
#include "fight.h"
void t3_input_tape_reset(void);
void t3_input_tape_step(uint32_t tick,t3_input_t* first,t3_input_t* second);
#endif
