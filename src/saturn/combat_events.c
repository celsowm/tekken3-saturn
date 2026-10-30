#include "combat_events.h"
#include "move_probe.h"

#include "placement.h"

enum { RING = 64u };
static volatile t3_combat_event_t g_ring[RING] T3_HOT_WRAM;
static volatile uint32_t g_head T3_HOT_WRAM, g_tail T3_HOT_WRAM;
/* Dumped by scripts/saturn_probe.py: dropped, pushed per kind, popped. */
volatile uint32_t g_t3_event_overflow T3_HOT_WRAM,g_t3_event_hits T3_HOT_WRAM,g_t3_event_blocks T3_HOT_WRAM,
    g_t3_event_swings T3_HOT_WRAM,g_t3_event_sounds T3_HOT_WRAM,g_t3_event_popped T3_HOT_WRAM;
static volatile uint32_t g_stamped T3_HOT_WRAM;
static volatile uint8_t g_sound_connected T3_HOT_WRAM;
static uint32_t g_seen_hits[2] T3_HOT_WRAM, g_seen_blocks[2] T3_HOT_WRAM;
static uint8_t g_swinging[2] T3_HOT_WRAM;

void t3_combat_events_reset(void) {
    for(unsigned i=0;i<2u;++i)g_seen_hits[i]=g_seen_blocks[i]=g_swinging[i]=0u;
    g_head=g_tail=g_stamped=0u;
}

static void push(uint32_t tick,unsigned fighter,uint8_t kind,unsigned attacker) {
    if(g_head-g_tail>=RING){++g_t3_event_overflow;return;}
    if(kind==T3_EVENT_HIT)++g_t3_event_hits;
    else if(kind==T3_EVENT_BLOCK)++g_t3_event_blocks;
    else ++g_t3_event_swings;
    volatile t3_combat_event_t* e=&g_ring[g_head%RING];
    e->tick=tick;e->fighter=(uint8_t)fighter;e->kind=kind;e->sound=0u;e->code=0u;
    e->joint=(uint8_t)t3_move_probe_owned_value(attacker,26u);
    for(unsigned axis=0;axis<3u;++axis)e->at[axis]=t3_move_probe_owned_value(attacker,27u+axis);
    g_head=g_head+1u; /* publish after the payload */
}

void t3_combat_events_sound(void* user,unsigned kind,unsigned player,uint16_t code) {
    (void)user;
    if(g_head-g_tail>=RING){++g_t3_event_overflow;return;}
    volatile t3_combat_event_t* e=&g_ring[g_head%RING];
    e->tick=0u;e->fighter=(uint8_t)player;e->kind=T3_EVENT_SOUND;e->joint=0u;
    e->sound=(uint8_t)kind;e->code=code;e->at[0]=e->at[1]=e->at[2]=0;
    ++g_t3_event_sounds;
    g_head=g_head+1u;
}
void t3_combat_events_connect_sound(void) { g_sound_connected=1u; }
int t3_combat_events_sound_connected(void) { return g_sound_connected; }

void t3_combat_events_tick(uint32_t tick) {
    /* Sound requests queued by the step that just ran belong to this tick. */
    for(;g_stamped!=g_head;++g_stamped)g_ring[g_stamped%RING].tick=tick;
    for(unsigned i=0;i<2u;++i) {
        const uint32_t hits=(uint32_t)t3_move_probe_owned_value(i,16u);
        const uint32_t blocks=(uint32_t)t3_move_probe_owned_value(i,17u);
        /* A reset starts the counters again from zero. */
        if(hits<g_seen_hits[i] || blocks<g_seen_blocks[i]){g_seen_hits[i]=hits;g_seen_blocks[i]=blocks;}
        if(blocks>g_seen_blocks[i])push(tick,i,T3_EVENT_BLOCK,1u-i);
        else if(hits>g_seen_hits[i])push(tick,i,T3_EVENT_HIT,1u-i);
        g_seen_hits[i]=hits;g_seen_blocks[i]=blocks;
        const uint8_t swinging=t3_move_probe_owned_value(i,30u)!=0;
        if(swinging && !g_swinging[i])push(tick,i,T3_EVENT_SWING,i);
        g_swinging[i]=swinging;
    }
}

int t3_combat_events_pop(t3_combat_event_t* out) {
    if(g_tail==g_head)return 0;
    const volatile t3_combat_event_t* e=&g_ring[g_tail%RING];
    out->tick=e->tick;out->fighter=e->fighter;out->kind=e->kind;out->joint=e->joint;out->sound=e->sound;out->code=e->code;
    for(unsigned axis=0;axis<3u;++axis)out->at[axis]=e->at[axis];
    g_tail=g_tail+1u;
    ++g_t3_event_popped;
    return 1;
}

uint32_t t3_combat_events_overflow(void) { return g_t3_event_overflow; }
