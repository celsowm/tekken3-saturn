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
