#ifndef T3_SATURN_MOVE_PROBE_H
#define T3_SATURN_MOVE_PROBE_H
#include "fight.h"
#include "saturn/core.h"

/* Optional PS1 DIVMOT logic interpreter, isolated from combat gameplay.
 * Only source files emitted into the private build/ tree may be loaded. */
sat_result_t t3_move_probe_init(void);
void t3_move_probe_reset(unsigned round);
void t3_move_probe_tick(const t3_match_t* match);
uint8_t t3_move_probe_active(void);
uint16_t t3_move_probe_slot(unsigned fighter);
uint16_t t3_move_probe_frame(unsigned fighter);
uint16_t t3_move_probe_attack_word(unsigned fighter);
uint32_t t3_move_probe_transitions(unsigned fighter);
uint8_t t3_move_probe_segments(unsigned fighter);
uint32_t t3_move_probe_decode_errors(unsigned fighter);
#endif
