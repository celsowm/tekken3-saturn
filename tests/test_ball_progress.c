#include "ball_progress.h"
#include <assert.h>
#include <string.h>
/* Tekken Ball's saved opponent state: the first game is Gon once ever, and the met counts carry from game to game. */
static t3_ball_choice_t fresh(const uint8_t* saved){
    t3_ball_choice_t s={0};
    if(saved)assert(t3_ball_progress_unpack(saved,T3_BALL_PROGRESS_BYTES,&s));
    s.unlocked=BALL_OPPONENT_POOL;s.lcg=12345u;s.frame=1u;
    return s;
}
int main(void){
    uint8_t rec[T3_BALL_PROGRESS_BYTES];
    t3_ball_choice_t s=fresh(0);
    assert(t3_ball_opponent(&s)==BALL_GON_KEY && s.first_done);
    assert(t3_ball_progress_pack(&s,rec));
    /* A restart reads the flag back: the next game is not forced to be Gon, and its meeting is counted. */
    s=fresh(rec);
    const uint32_t key=t3_ball_opponent(&s);
    assert(key!=BALL_GON_KEY || s.met[BALL_GON_KEY>>2]==1);
    assert(s.met[key>>2]==1 && (BALL_OPPONENT_POOL>>(key>>2)&1u) && (key&3u)==1u);
    s.met[5]=65535u;
    assert(t3_ball_progress_pack(&s,rec));
    t3_ball_choice_t back=fresh(rec);
    assert(back.first_done && !memcmp(back.met,s.met,sizeof s.met));
    /* A damaged or foreign record is refused and leaves the choice untouched. */
    t3_ball_choice_t untouched=fresh(0);
    rec[20]^=1u;assert(!t3_ball_progress_unpack(rec,sizeof rec,&untouched) && !untouched.first_done);
    rec[20]^=1u;assert(!t3_ball_progress_unpack(rec,sizeof rec-1u,&untouched));
    rec[4]=2u;assert(!t3_ball_progress_unpack(rec,sizeof rec,&untouched));
    s.first_done=2u;assert(!t3_ball_progress_pack(&s,rec));
    return 0;
}
