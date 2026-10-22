#include "branch_input_filter.h"
#include "branch_rules.h"
#include "divmot.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

enum { ROWS=96,COMMON_ROWS=48,BANK_BYTES=64+ROWS*12+56,
       WORDS=T3_BRANCH_INPUT_CLASSES*((ROWS+31)/32) };
static uint8_t bytes[BANK_BYTES],common[COMMON_ROWS*12];
static uint32_t own_words[WORDS+1],common_words[WORDS+1];
static t3_command_sequence_t sequences[]={
    {0xC00E,20,{6,5,6,0}}, {0xC00F,20,{6,0x0106,0}},
    {0xC010,0,{5,0}}, {0xC7FF,60,{5,4,0}}, {0xC00E,20,{0x0105,0}}
};
static uint32_t random_state=42;
static uint32_t random_word(void) {return random_state=random_state*1664525u+1013904223u;}
static void put16(uint8_t* p,uint16_t value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void put32(uint8_t* p,uint32_t value) {put16(p,(uint16_t)value);put16(p+2,(uint16_t)(value>>16));}
static void row(uint8_t* p,uint16_t command,uint16_t target) {
    memset(p,0,12);put16(p,command);put16(p+6,target);p[10]=255;
}
static void bank_init(t3_divmot_t* bank) {
    memset(bytes,0,sizeof(bytes));bytes[1]=99;put16(bytes+2,1);
    put32(bytes+4,64);put32(bytes+8,64+ROWS*12);
    for(unsigned i=2;i<15;++i)put32(bytes+4+4*i,BANK_BYTES);
    assert(t3_divmot_open(bank,bytes,sizeof(bytes)));
    for(unsigned i=0;i<ROWS;++i)row(bytes+64+i*12,1,(uint16_t)i);
    for(unsigned i=0;i<COMMON_ROWS;++i)row(common+i*12,1,(uint16_t)(1000+i));
    row(bytes+64+(ROWS-1)*12,0xC000,4000);
    row(common+(COMMON_ROWS-1)*12,0xC00D,0);
    assert(t3_divmot_set_common_branches(bank,common,sizeof(common)));
}
static void build(t3_divmot_t* filtered,const t3_divmot_t* plain,
                  t3_branch_input_filter_t filters[2]) {
    own_words[WORDS]=common_words[WORDS]=0x12345678;
    assert(t3_branch_input_filter_init(&filters[0],bytes+64,ROWS*12,
        sequences,sizeof(sequences)/sizeof(*sequences),own_words,WORDS));
    assert(t3_branch_input_filter_init(&filters[1],common,sizeof(common),
        sequences,sizeof(sequences)/sizeof(*sequences),common_words,WORDS));
    assert(own_words[WORDS]==0x12345678 && common_words[WORDS]==0x12345678);
    *filtered=*plain;
    assert(t3_divmot_set_input_filters(filtered,&filters[0],&filters[1]));
}
static int compare(const t3_divmot_t* plain,const t3_divmot_t* filtered,
                   const t3_input_history_t* input,unsigned first,unsigned frame,
                   const t3_branch_context_t* facts,int end,int alternate_table) {
    const t3_move_record_t move={.branch_index=first,.length=60};
    t3_branch_record_t expected,actual;
    memset(&expected,0xA5,sizeof(expected));actual=expected;
    const t3_command_sequence_t* table=alternate_table?sequences+1:sequences;
    const size_t count=sizeof(sequences)/sizeof(*sequences)-(alternate_table?1u:0u);
    int a,b;
    if(end) {
        a=t3_divmot_branch_find_ex(plain,move,input,frame,0,facts,table,count,&expected);
        b=t3_divmot_branch_find_ex(filtered,move,input,frame,0,facts,table,count,&actual);
    } else {
        a=t3_divmot_branch_pick_ex(plain,move,input,frame,0,facts,table,count,&expected);
        b=t3_divmot_branch_pick_ex(filtered,move,input,frame,0,facts,table,count,&actual);
    }
    assert(a==b && !memcmp(&actual,&expected,sizeof(actual)));
    return a?(int)actual.target_slot:-1;
}
static void predicate_implication(void) {
    t3_input_history_t h={0};
    for(unsigned key=0;key<10;++key) {
        t3_history_clear(&h);
        if(key) {
            for(unsigned i=0;i<30;++i) {
                h.frames[i].direction=(uint8_t)(i%9+1);
                h.frames[i].held_buttons=h.frames[i].pressed_buttons=(uint8_t)(i%16);
            }
            h.next=31;h.frames[30]=(t3_command_frame_t){(uint8_t)key,0,0};
            h.direction_cleared=0;
        }
        t3_input_history_t prototype={0};
        prototype.next=prototype.count=1;prototype.direction_cleared=key==0;
        prototype.frames[0].direction=(uint8_t)(key?key:5);
        /* The immutable class deliberately omits held buttons and history.
         * Neither omission may remove a command accepted by a live history. */
        for(unsigned held=0;held<16;++held) {
            h.frames[(h.next+T3_INPUT_HISTORY_FRAMES-1u)%T3_INPUT_HISTORY_FRAMES]
                .held_buttons=(uint8_t)held;
            for(unsigned command=0;command<65536;++command)
                if(t3_command_matches_table((uint16_t)command,&h,sequences,
                        sizeof(sequences)/sizeof(*sequences)))
                    assert(t3_command_current_possible((uint16_t)command,&prototype,sequences,
                        sizeof(sequences)/sizeof(*sequences)));
        }
    }
    /* A valid historical sequence may be possible with one current sample
     * although that one sample cannot itself establish the sequence. */
    t3_history_reset(&h);h.count=h.next=1;h.frames[0].direction=6;
    assert(t3_command_current_possible(0xC00E,&h,sequences,5));
    assert(!t3_command_matches_table(0xC00E,&h,sequences,5));
    h.direction_cleared=1;
    assert(t3_branch_input_filter_class(&h)==T3_BRANCH_INPUT_UNFILTERED);
    h.direction_cleared=0;h.frames[0].direction=0;
    assert(t3_branch_input_filter_class(&h)==T3_BRANCH_INPUT_UNFILTERED);
    h.frames[0].direction=10;
    assert(t3_branch_input_filter_class(&h)==T3_BRANCH_INPUT_UNFILTERED);
    h.frames[0].direction=5;h.next=T3_INPUT_HISTORY_FRAMES;
    assert(t3_branch_input_filter_class(&h)==T3_BRANCH_INPUT_UNFILTERED);
    h.next=1;h.count=T3_INPUT_HISTORY_FRAMES+1;
    assert(t3_branch_input_filter_class(&h)==T3_BRANCH_INPUT_UNFILTERED);
}
static void controlled_walks(void) {
    t3_divmot_t plain,filtered;t3_branch_input_filter_t filters[2];
    t3_input_history_t h={0};t3_branch_context_t facts={.known=T3_FACT_CONTACT};
    bank_init(&plain);
    row(bytes+64+31*12,0,31);bytes[64+31*12+3]=1; /* contact condition */
    row(bytes+64+32*12,0,32);
    build(&filtered,&plain,filters);t3_history_clear(&h);
    unsigned next=0;
    assert(t3_branch_input_filter_next(&filters[0],0,0,&next) && next==31);
    assert(compare(&plain,&filtered,&h,0,20,&facts,0,0)==32);
    facts.contact=1;
    assert(compare(&plain,&filtered,&h,0,20,&facts,0,0)==31);
    assert(compare(&plain,&filtered,&h,32,20,&facts,0,0)==32);
    assert(compare(&plain,&filtered,&h,33,20,&facts,0,0)==-1);
    assert(compare(&plain,&filtered,&h,33,20,&facts,1,0)==4000);
    /* The sequence table's identity is part of the binding. */
    assert(compare(&plain,&filtered,&h,0,20,&facts,0,1)==31);
    bytes[64+31*12+8]=0x2C; /* Eligible skip must still find raw END. */
    build(&filtered,&plain,filters);
    assert(compare(&plain,&filtered,&h,0,70,&facts,0,0)==4000);
    facts.contact=0;
    assert(compare(&plain,&filtered,&h,0,20,&facts,0,0)==32);
    /* CALL/RETURN survive even when preceding/following button rows vanish. */
    bank_init(&plain);row(bytes+64+31*12,0xC00C,0);
    row(common+32*12,0,2000);build(&filtered,&plain,filters);
    assert(compare(&plain,&filtered,&h,0,20,NULL,0,0)==2000);
    row(common+32*12,0xC00D,0);build(&filtered,&plain,filters);
    assert(compare(&plain,&filtered,&h,0,70,NULL,0,0)==4000);
    row(common,0xC00C,0);build(&filtered,&plain,filters);
    assert(compare(&plain,&filtered,&h,0,70,NULL,0,0)==-1); /* nested call */
    row(bytes+64+31*12,0xC00D,0);build(&filtered,&plain,filters);
    assert(compare(&plain,&filtered,&h,0,70,NULL,0,0)==-1); /* bare return */
    /* Rebinding common rows and reopening a bank invalidate attachments. */
    assert(t3_divmot_set_common_branches(&filtered,common,sizeof(common)));
    assert(!filtered.common_input_filter && filtered.input_filter==&filters[0]);
    assert(t3_divmot_open(&filtered,bytes,sizeof(bytes)));
    assert(!filtered.input_filter && !filtered.common_input_filter);
}
static void malformed_and_fuzz(void) {
    t3_divmot_t plain,filtered;t3_branch_input_filter_t filters[2];
    t3_branch_input_filter_t invalid;
    own_words[0]=0xBAD;
    assert(!t3_branch_input_filter_init(&invalid,NULL,12,sequences,5,own_words,WORDS));
    assert(!invalid.words && own_words[0]==0xBAD);
    assert(!t3_branch_input_filter_init(&invalid,bytes,13,sequences,5,own_words,WORDS));
    assert(!t3_branch_input_filter_init(&invalid,bytes+64,ROWS*12,sequences,5,own_words,WORDS-1));
    assert(own_words[0]==0xBAD);
    const uint16_t commands[]={0,1,2,15,16,0x10,0x410,0x2000,0xC000,
        0xC001,0xC002,0xC00C,0xC00D,0xC00E,0xC00F,0xC010,0xC7FF,0xC827,0xFFFF};
    for(unsigned trial=0;trial<3000;++trial) {
        bank_init(&plain);
        for(unsigned stream=0;stream<2;++stream) {
            const unsigned count=stream?COMMON_ROWS:ROWS;
            uint8_t* rows=stream?common:bytes+64;
            for(unsigned i=0;i<count;++i) {
                uint8_t* p=rows+i*12;
                row(p,commands[random_word()%(sizeof(commands)/sizeof(*commands))],
                    (uint16_t)(random_word()%(COMMON_ROWS+2)));
                p[2]=(uint8_t)(random_word()%0x60);p[3]=(uint8_t)(random_word()%0x48);
                p[8]=(random_word()%7==0)?0x2C:6;
                p[9]=(uint8_t)(random_word()%30);p[10]=(uint8_t)(random_word()%80);
            }
        }
        build(&filtered,&plain,filters);
        t3_input_history_t h={0};t3_branch_context_t facts={0};
        facts.known=0x1FFF;facts.contact=(uint8_t)(trial&1);
        facts.distance=(int32_t)(trial%4000);facts.own_health=(uint16_t)trial;
        for(unsigned i=0;i<T3_INPUT_HISTORY_FRAMES;++i) {
            h.frames[i].direction=(uint8_t)(random_word()%9+1);
            h.frames[i].held_buttons=(uint8_t)(random_word()%16);
            h.frames[i].pressed_buttons=(uint8_t)(random_word()%16);
        }
        h.count=60;h.next=1;
        for(unsigned key=0;key<12;++key) {
            h.direction_cleared=key==0;
            h.frames[0]=(t3_command_frame_t){(uint8_t)(key==0?5:key<=9?key:5),
                (uint8_t)(key>9?15:0),(uint8_t)(key>9?1:0)};
            assert(t3_branch_input_filter_class(&h)==(key<10?key:10));
            for(unsigned end=0;end<2;++end)
                (void)compare(&plain,&filtered,&h,trial%ROWS,trial%100,
                    trial%5?&facts:NULL,(int)end,(int)(trial%7==0));
        }
        h.count=0;
        assert(t3_branch_input_filter_class(&h)==T3_BRANCH_INPUT_UNFILTERED);
        (void)compare(&plain,&filtered,&h,0,0,NULL,1,0);
    }
}
int main(void) {
    predicate_implication();controlled_walks();malformed_and_fuzz();
    puts("Input prefilter preserves ordered branch results and closed failures");
    return 0;
}
