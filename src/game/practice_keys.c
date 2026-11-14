/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from Tekken3Remake remake/core/game/practice_mode.gd; see practice_keys.h and NOTICE.md. */
#include "practice_keys.h"

void t3_practice_keys_reset(t3_practice_keys_t* k) {
    /* FUN_800B7A88 empties the entries (their flag bit kept), the head and sets the idle wait to 30. */
    for(unsigned i=0;i<T3_KEY_RING_WORDS;++i)k->word[i]&=0x80000000u;
    k->head=0;k->wait=T3_KEY_IDLE_FRAMES;
}

int t3_practice_keys_idle(t3_practice_keys_t* k,int buttons_held,int in_stance,int idle_move) {
    if(in_stance)--k->wait;
    if(!buttons_held && idle_move)--k->wait;
    if(k->wait<0)k->wait=0;
    if(buttons_held) {
        const int was=k->wait;
        k->wait=T3_KEY_IDLE_FRAMES;
        return was==0;
    }
    return 0;
}

void t3_practice_keys_log(t3_practice_keys_t* k,uint16_t pad,int side_flag) {
    const uint32_t buttons=pad&0xF0F0u;
    uint32_t entry=k->word[k->head];
    if((((entry&0xFFu)<<4)|(((entry>>8)&0xFu)<<12))==buttons) {
        const uint32_t kept=entry&0x8000FFFFu;
        uint32_t count=(((entry&0x7FFF0000u)>>16)+1u)&0x7FFFu;
        if(count>0x3FFEu)count=0x3FFFu;
        k->word[k->head]=kept|(count<<16);
        return;
    }
    int32_t head=k->head+1;
    head=head<0?T3_KEY_RING_WORDS-1:head>=T3_KEY_RING_WORDS?0:head;
    k->word[head]=((buttons>>4)&0xFFu)|((((uint32_t)pad>>12)&0xFFu)<<8)|0x10000u|(side_flag?0x80000000u:0u);
    k->head=head;
}

unsigned t3_practice_keys_items(const t3_practice_keys_t* k,int guide_end,unsigned prompt,unsigned prompt_timer,int recording,
                                t3_practice_key_item_t out[T3_KEY_ITEMS]) {
    unsigned dir_col=0,btn_col=1,dir_guide=0,btn_guide=1;   /* colours before and after the guide's end */
    if(prompt==2 || (prompt==3 && (prompt_timer&0x10u))){dir_col=3;btn_col=0;dir_guide=3;btn_guide=0;}
    else if(prompt==4 || prompt==5){dir_col=0;btn_col=1;dir_guide=3;btn_guide=0;}
    t3_practice_key_item_t items[2*T3_KEY_RING_WORDS];
    unsigned n=0,since=0;
    unsigned prev_dir=0,prev_btn=0;
    int i=k->head+1;
    i=i<0?T3_KEY_RING_WORDS-1:i<T3_KEY_RING_WORDS?i:0;
    for(unsigned step=0;step<T3_KEY_RING_WORDS;++step) {
        const uint32_t e=k->word[i];
        if(i==0)since=0;
        const unsigned d=(e>>8)&0xFFu,b=e&0xFFu;
        if(d && d!=prev_dir) {
            items[n++]=(t3_practice_key_item_t){(uint16_t)(d<<12),(uint8_t)(guide_end<i?dir_guide:dir_col)};
            ++since;
        }
        if(b & (b^prev_btn)) {
            items[n++]=(t3_practice_key_item_t){(uint16_t)(b<<4),(uint8_t)(guide_end<i?btn_guide:btn_col)};
            ++since;
        }
        prev_dir=d;prev_btn=b;
        ++i;
        if(i>=T3_KEY_RING_WORDS)i=0;
    }
    unsigned shown;
    if(recording){shown=since<15u?since:15u;if(shown>=n)shown=n;}
    else shown=n<15u?n:15u;
    for(unsigned j=0;j<shown;++j)out[j]=items[n-shown+j];
    return shown;
}

void t3_practice_keys_text(const t3_practice_keys_t* k,unsigned age,char out[8]) {
    unsigned n=0;
    out[0]=0;
    if(age>=T3_KEY_RING_WORDS)return;
    const int32_t at=((k->head-(int32_t)age)%T3_KEY_RING_WORDS+T3_KEY_RING_WORDS)%T3_KEY_RING_WORDS;
    const uint32_t e=k->word[at];
    if(!(e&0x7FFF0000u))return;
    const unsigned buttons=e&0xFu,dirs=(e>>8)&0xFu;
    if(dirs&1u)out[n++]='U';
    if(dirs&4u)out[n++]='D';
    if(dirs&2u)out[n++]='R';
    if(dirs&8u)out[n++]='L';
    if(buttons&&n)out[n++]='+';
    if(buttons&8u)out[n++]='1';   /* LP 0x80 */
    if(buttons&1u)out[n++]='2';   /* RP 0x10 */
    if(buttons&4u)out[n++]='3';   /* LK 0x40 */
    if(buttons&2u)out[n++]='4';   /* RK 0x20 */
    if(!n)out[n++]='N';
    out[n]=0;
}
