#include "practice_keys.h"
#include <assert.h>
#include <string.h>
int main(void){
    t3_practice_keys_t k;t3_practice_keys_reset(&k);
    char t[8];
    t3_practice_keys_log(&k,0x1000,0);                 /* up held */
    t3_practice_keys_log(&k,0x1000,0);
    t3_practice_keys_log(&k,0x1000,0);                 /* the same input counts on */
    assert(((k.word[k.head]>>16)&0x7FFF)==3);
    t3_practice_keys_text(&k,0,t);assert(!strcmp(t,"U"));
    t3_practice_keys_log(&k,0x4000|0x80,0);            /* down + LP: a new entry */
    t3_practice_keys_text(&k,0,t);assert(!strcmp(t,"D+1"));
    t3_practice_keys_text(&k,1,t);assert(!strcmp(t,"U"));
    t3_practice_keys_log(&k,0x2000|0x10|0x20,1);       /* right + RP + RK, side flag */
    t3_practice_keys_text(&k,0,t);assert(!strcmp(t,"R+24") && (k.word[k.head]&0x80000000u));
    t3_practice_keys_text(&k,5,t);assert(!strcmp(t,""));
    for(int i=0;i<60;++i)t3_practice_keys_log(&k,(uint16_t)((i&1)?0x40:0x80),0);   /* the ring wraps */
    assert(k.head>=0 && k.head<T3_KEY_RING_WORDS);
    t3_practice_keys_reset(&k);t3_practice_keys_text(&k,0,t);assert(!strcmp(t,""));
    /* Idle: 30 frames in the stance, then a held button asks for an empty ring; the wait restarts. */
    for(int i=0;i<T3_KEY_IDLE_FRAMES;++i)assert(!t3_practice_keys_idle(&k,0,1,1));
    assert(k.wait==0);
    assert(t3_practice_keys_idle(&k,1,1,0)==1 && k.wait==T3_KEY_IDLE_FRAMES);
    assert(t3_practice_keys_idle(&k,1,0,0)==0);
    return 0;
}
