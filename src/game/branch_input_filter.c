#include "branch_input_filter.h"

int t3_branch_input_filter_init(t3_branch_input_filter_t* out,
    const uint8_t* rows,size_t bytes,const t3_command_sequence_t* sequences,
    size_t sequence_count,uint32_t* words,size_t capacity) {
    if(!out)return 0;
    *out=(t3_branch_input_filter_t){0};
    if(!rows || !bytes || bytes%12u || bytes/12u>65535u || !words)return 0;
    const unsigned count=(unsigned)(bytes/12u),stride=(count+31u)/32u;
    if(capacity<(size_t)stride*T3_BRANCH_INPUT_CLASSES)return 0;
    for(unsigned i=0;i<stride*T3_BRANCH_INPUT_CLASSES;++i)words[i]=0;
    for(unsigned key=0;key<T3_BRANCH_INPUT_CLASSES;++key) {
        t3_input_history_t input={0};
        input.count=input.next=1;
        input.direction_cleared=key==0;
        input.frames[0].direction=(uint8_t)(key?key:5u);
        for(unsigned row=0;row<count;++row) {
            const uint8_t* p=rows+row*12u;
            const uint16_t command=(uint16_t)(p[0]|((uint16_t)p[1]<<8u));
            /* Structural control flow cannot depend on input matching. */
            if(command==0xC000u || command==0xC00Cu || command==0xC00Du ||
               t3_command_current_possible(command,&input,sequences,sequence_count))
                words[key*stride+row/32u]|=UINT32_C(1)<<(row&31u);
        }
    }
    *out=(t3_branch_input_filter_t){rows,sequences,sequence_count,words,
        (uint16_t)count,(uint16_t)stride};
    return 1;
}

unsigned t3_branch_input_filter_class(const t3_input_history_t* input) {
    if(!input || !input->count || input->count>T3_INPUT_HISTORY_FRAMES ||
       input->next>=T3_INPUT_HISTORY_FRAMES)return T3_BRANCH_INPUT_UNFILTERED;
    const t3_command_frame_t current=t3_history_at(input,0);
    if(current.pressed_buttons || current.direction<1 || current.direction>9)
        return T3_BRANCH_INPUT_UNFILTERED;
    if(input->direction_cleared)
        return current.direction==5?0:T3_BRANCH_INPUT_UNFILTERED;
    return current.direction;
}

static unsigned first_bit(uint32_t bits) {
    unsigned n=0;
    if(!(bits&0xffffu)) {n=16;bits>>=16;}
    if(!(bits&0xffu)) {n+=8;bits>>=8;}
    if(!(bits&15u)) {n+=4;bits>>=4;}
    if(!(bits&3u)) {n+=2;bits>>=2;}
    return n+(!(bits&1u));
}

int t3_branch_input_filter_next(const t3_branch_input_filter_t* f,
    unsigned key,unsigned first,unsigned* next) {
    if(!f || !f->words || !next || key>=T3_BRANCH_INPUT_CLASSES ||
       first>=f->row_count)return 0;
    unsigned word=first/32u;
    const uint32_t* bits=f->words+(size_t)key*f->word_count;
    uint32_t selected=bits[word] & (UINT32_MAX<<(first&31u));
    while(!selected) {
        if(++word>=f->word_count)return 0;
        selected=bits[word];
    }
    const unsigned index=word*32u+first_bit(selected);
    if(index>=f->row_count)return 0;
    *next=index;
    return 1;
}
