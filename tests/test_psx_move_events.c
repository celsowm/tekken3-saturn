#include "psx_move_events.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { B0=64,B1=76,B2=132,B3=132+4023*4, B4=B3+20 };
static uint8_t raw[B4];
static void w16(unsigned at,unsigned v) {
    raw[at]=(uint8_t)v;raw[at+1u]=(uint8_t)(v>>8u);
}
static void w32(unsigned at,uint32_t v) {
    w16(at,v&0xffffu);w16(at+2u,v>>16u);
}
static void fixture(t3_divmot_t* bank,t3_move_record_t* move) {
    unsigned i;
    memset(raw,0,sizeof(raw));
    raw[1]=0u;
    w16(2,1u);
    /* Section 4 is present for a single 20-byte event list. */
    w32(4,B0);w32(8,B1);w32(12,B2);
    for(i=3u;i<15u;++i)
        w32(4u+4u*i,i<=4u?B3:B4);
    for(i=1;i<4023u;++i)w32(B2+4*i,0x3fffffffu);
    w16(B0,0xC000u);
    w32(B1+32u,0u); /* first pair in section4 */
    w16(B3,2u);w16(B3+2u,0x0B09u); /* throw kind1, damage9 */
    w16(B3+4u,5u);w16(B3+6u,0x0C1Au); /* kind2 damage26 */
    w16(B3+8u,7u);w16(B3+10u,0x0D03u); /* kind3 damage3 */
    w16(B3+12u,9u);w16(B3+14u,0x0904u);/* non-extra spark */
    w16(B3+16u,0u);w16(B3+18u,0u);    /* terminator */
    assert(t3_divmot_open(bank,raw,sizeof(raw)));
    assert(t3_divmot_move(bank,0u,move));
    assert(move->event_index==0u);
}
static void exact_crossed_frames(void) {
    t3_divmot_t bank;
    t3_move_record_t move;
    t3_psx_move_events_t e;
    fixture(&bank,&move);
    assert(t3_psx_move_events_sample(&bank,&move,0u,1u,&e));
    assert(e.extra_kind==0u && e.fired==0u);
    assert(t3_psx_move_events_sample(&bank,&move,1u,2u,&e));
    assert(e.extra_kind==1u && e.extra_damage==9u &&
           e.extra_frame==2u && e.fired==1u);
    assert(t3_psx_move_events_sample(&bank,&move,2u,7u,&e));
    assert(e.extra_kind==3u && e.extra_damage==3u &&
           e.extra_frame==7u && e.fired==2u);
    assert(t3_psx_move_events_sample(&bank,&move,7u,9u,&e));
    assert(e.extra_kind==0u && e.fired==1u); /* extra clears per tick */
    assert(t3_psx_move_events_sample(&bank,&move,9u,3u,&e));
    assert(e.extra_kind==0u && e.fired==0u); /* reversed frame range */
    assert(t3_psx_move_events_sample(&bank,&move,0u,10u,&e));
    assert(e.extra_kind==3u && e.extra_damage==3u &&
           e.extra_frame==7u && e.fired==4u);
}
static void stripped_and_bad_are_atomic(void) {
    t3_divmot_t bank;
    t3_move_record_t move;
    t3_psx_move_events_t got,saved;
    fixture(&bank,&move);
    memset(&got,0x55,sizeof(got));saved=got;
    bank.sections[5]=bank.sections[4];
    assert(t3_psx_move_events_sample(&bank,&move,0u,3u,&got)==0);
    assert(!memcmp(&got,&saved,sizeof(got)));
    bank.sections[5]=B4;
    move.event_index=99999u;
    assert(t3_psx_move_events_sample(&bank,&move,0u,3u,&got)==-1);
    assert(!memcmp(&got,&saved,sizeof(got)));
    move.event_index=0xffffffffu;
    assert(t3_psx_move_events_sample(&bank,&move,0u,3u,&got)==1);
    assert(got.fired==0u && got.extra_kind==0u);
    move.event_index=0u;
    w16(B3+16u,12u); /* unterminated list */
    memset(&got,0x55,sizeof(got));saved=got;
    assert(t3_psx_move_events_sample(&bank,&move,0u,12u,&got)==-1);
    assert(!memcmp(&got,&saved,sizeof(got)));
    assert(t3_psx_move_events_sample(NULL,&move,0u,1u,&got)==-1);
    assert(t3_psx_move_events_sample(&bank,&move,0u,65536u,&got)==-1);
    assert(!memcmp(&got,&saved,sizeof(got)));
}
int main(void) {
    exact_crossed_frames();
    stripped_and_bad_are_atomic();
    puts("Original DIVMOT section4 frame events 11..13: passed");
    return 0;
}
