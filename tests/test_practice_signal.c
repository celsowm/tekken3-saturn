#include "practice_signal.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

/* The synthetic bank of test_psx_match.c: move 0's branches are an always-true row for frames 0-4 and an END row. */
enum { B0=64,B1=B0+36,B2=B1+112,B6=B2+4023*4,BYTES=B6+52 };
static uint8_t bytes[BYTES];
static void p16(uint8_t* p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void p32(uint8_t* p,uint32_t v){p16(p,(uint16_t)v);p16(p+2,(uint16_t)(v>>16));}
int main(void) {
    t3_divmot_t bank;
    memset(bytes,0,sizeof bytes);p16(bytes+2,2);
    p32(bytes+4,B0);p32(bytes+8,B1);p32(bytes+12,B2);
    for(unsigned i=3;i<7;++i)p32(bytes+4+4*i,B6);
    for(unsigned i=7;i<15;++i)p32(bytes+4+4*i,BYTES);
    for(unsigned i=0;i<4023;++i)p32(bytes+B2+4*i,T3_DIVMOT_EMPTY_SLOT);
    p32(bytes+B2+3*4,0);
    for(unsigned i=0;i<2;++i){uint8_t* row=bytes+B1+56*i;p32(row,2);p32(row+4,0x842);p32(row+12,0);row[24]=4;}
    bytes[B6+2]=4;
    p16(bytes+B0,0x410);p16(bytes+B0+6,21);bytes[B0+8]=6;bytes[B0+10]=4;bytes[B0+11]=1;   /* row 0: frames 0-4 */
    p16(bytes+B0+12,0xC000);p16(bytes+B0+18,3);bytes[B0+20]=6;bytes[B0+21]=5;bytes[B0+22]=20; /* END: frames 5-20, to slot 3 */
    assert(t3_divmot_open(&bank,bytes,sizeof(bytes)));
    t3_move_record_t move;
    assert(t3_divmot_move(&bank,0,&move));
    t3_branch_context_t facts={0};
    assert(t3_practice_can_act(&bank,NULL,&move,2,1,bank.bank_type,&facts)==1);       /* a row open now */
    assert(t3_practice_can_act(&bank,NULL,&move,6,1,bank.bank_type,&facts)==1);       /* the continuation's row opens */
    assert(t3_practice_can_act(&bank,NULL,&move,6,0,bank.bank_type,&facts)==0);       /* not while playing backwards */
    assert(t3_practice_can_act(&bank,NULL,&move,30,1,bank.bank_type,&facts)==0);      /* past the END row's window */
    bytes[B0+2]=0x60;                                                                /* a restriction that excludes this bank */
    assert(t3_divmot_open(&bank,bytes,sizeof(bytes)));
    assert(t3_practice_can_act(&bank,NULL,&move,2,1,bank.bank_type,&facts)==0);
    uint8_t rgb[3];
    t3_practice_signal_colour(1,rgb);assert(rgb[0]==0 && rgb[1]==250 && rgb[2]==0);
    t3_practice_signal_colour(0,rgb);assert(rgb[0]==250 && rgb[1]==0 && rgb[2]==0);
    return 0;
}
