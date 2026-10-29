#ifndef T3_PSX_BLINK_H
#define T3_PSX_BLINK_H
#include <stdint.h>
/* FighterAnimate's eye state, fighter +0x128E/+0x1290. Texture copies and
 * Gon's jaw commands are presentation outputs, separate from this timer. */
typedef struct { int16_t timer; uint8_t closed; } t3_psx_blink_t;
void t3_psx_blink_step(t3_psx_blink_t* state,uint32_t move_flags,
                       uint32_t move_state,uint32_t* random_state);
#endif
