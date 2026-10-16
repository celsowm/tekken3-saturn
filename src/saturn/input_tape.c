/* Private input recording/replay at the controller-to-simulation boundary.
 * This fixture supplies inputs only; combat state remains owned by psx_match.
 * Normal builds have neither option enabled. */
#include "input_tape.h"
#if defined(T3_CAPTURE_INPUT_TAPE)
volatile uint32_t g_t3_input_tape_count;
volatile uint32_t g_t3_input_tape_overflow;
volatile uint32_t g_t3_input_tape[512][5] __attribute__((section(".wram_l")));
#endif
#if defined(T3_REPLAY_INPUT_TAPE)
#include "t3_private_input_tape.h"
static uint32_t g_cursor;
#endif
void t3_input_tape_reset(void) {
#if defined(T3_CAPTURE_INPUT_TAPE)
    g_t3_input_tape_count=0;g_t3_input_tape_overflow=0;
#endif
#if defined(T3_REPLAY_INPUT_TAPE)
    g_cursor=0;
#endif
}
void t3_input_tape_step(uint32_t tick,t3_input_t* p0,t3_input_t* p1) {
#if defined(T3_REPLAY_INPUT_TAPE)
    while(g_cursor+1u<T3_PRIVATE_INPUT_EVENTS && t3_private_input_tape[g_cursor+1u][0]<=tick)++g_cursor;
    const uint16_t* r=t3_private_input_tape[g_cursor];
    *p0=(t3_input_t){r[1],r[2]};*p1=(t3_input_t){r[3],r[4]};
#endif
#if defined(T3_CAPTURE_INPUT_TAPE)
    uint32_t count=g_t3_input_tape_count;
    if(!count || g_t3_input_tape[count-1u][1]!=p0->held || g_t3_input_tape[count-1u][2]!=p0->pressed ||
       g_t3_input_tape[count-1u][3]!=p1->held || g_t3_input_tape[count-1u][4]!=p1->pressed) {
        if(count>=512u){g_t3_input_tape_overflow=1;return;}
        g_t3_input_tape[count][0]=tick;
        g_t3_input_tape[count][1]=p0->held;g_t3_input_tape[count][2]=p0->pressed;
        g_t3_input_tape[count][3]=p1->held;g_t3_input_tape[count][4]=p1->pressed;
        g_t3_input_tape_count=count+1u;
    }
#endif
    (void)tick;(void)p0;(void)p1;
}
