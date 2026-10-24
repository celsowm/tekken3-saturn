#include "combat_events.h"
#include "move_probe.h"

static uint32_t g_seen_hits[2], g_seen_blocks[2];
static uint8_t g_swinging[2];

void t3_combat_events_reset(void) {
    for(unsigned i=0;i<2u;++i)g_seen_hits[i]=g_seen_blocks[i]=g_swinging[i]=0u;
}

uint8_t t3_combat_events_poll(void) {
    uint8_t events=0u;
    for(unsigned i=0;i<2u;++i) {
        const uint32_t hits=(uint32_t)t3_move_probe_owned_value(i,16u);
        const uint32_t blocks=(uint32_t)t3_move_probe_owned_value(i,17u);
        /* A reset starts the counters again from zero. */
        if(hits<g_seen_hits[i] || blocks<g_seen_blocks[i]){g_seen_hits[i]=hits;g_seen_blocks[i]=blocks;}
        if(blocks>g_seen_blocks[i])events|=(uint8_t)(T3_EVENT_BLOCK<<(T3_EVENT_BITS*i));
        else if(hits>g_seen_hits[i])events|=(uint8_t)(T3_EVENT_HIT<<(T3_EVENT_BITS*i));
        g_seen_hits[i]=hits;g_seen_blocks[i]=blocks;
        const uint8_t swinging=t3_move_probe_owned_value(i,30u)!=0;
        if(swinging && !g_swinging[i])events|=(uint8_t)(T3_EVENT_SWING<<(T3_EVENT_BITS*i));
        g_swinging[i]=swinging;
    }
    return events;
}
