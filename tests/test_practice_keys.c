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
    /* The key display: a new direction and newly held buttons are items; a held input is one item, not one per frame. */
    {
        t3_practice_keys_t q;t3_practice_keys_reset(&q);
        t3_practice_key_item_t items[T3_KEY_ITEMS];
        t3_practice_keys_log(&q,0x2000,0);t3_practice_keys_log(&q,0x2000,0);                /* right held */
        t3_practice_keys_log(&q,0x2000|0x80,0);                                              /* LP joins: a button item */
        t3_practice_keys_log(&q,0x2000|0x80,0);t3_practice_keys_log(&q,0x80,0);              /* right released, LP still held: nothing new */
        t3_practice_keys_log(&q,0x4000|0x80|0x10,0);                                         /* down, and RP newly held */
        unsigned n=t3_practice_keys_items(&q,-1,1,0,0,items);
        assert(n==4 && items[0].pad==0x2000 && items[1].pad==0x0080 && items[2].pad==0x4000 && items[3].pad==0x0090);
        assert(items[0].colour==0 && items[1].colour==1);                                    /* directions 0, buttons 1 (no guide) */
    }
    /* Idle: 30 frames in the stance, then a held button asks for an empty ring; the wait restarts. */
    for(int i=0;i<T3_KEY_IDLE_FRAMES;++i)assert(!t3_practice_keys_idle(&k,0,1,1));
    assert(k.wait==0);
    assert(t3_practice_keys_idle(&k,1,1,0)==1 && k.wait==T3_KEY_IDLE_FRAMES);
    assert(t3_practice_keys_idle(&k,1,0,0)==0);
    return 0;
}
