#include "practice_data.h"
#include <assert.h>
static t3_practice_facts_t hit(int16_t damage){
    t3_practice_facts_t f={0};f.hit_clean=1;f.got_hit=1;f.in_reaction=1;f.was_hit_this_move=1;
    f.damage=damage;f.base_damage=damage;return f;
}
int main(void){
    t3_practice_data_t d;t3_practice_data_reset(&d);
    t3_practice_facts_t f[2]={{0},{0}};
    /* The dummy (player 1) takes a first clean hit, then a second one while it reacts: a 2-hit combo. */
    f[1]=hit(10);f[1].react_chain=1;f[0].move_changed=1;
    t3_practice_data_step(&d,f);
    assert(d.p[1].combo==1 && d.p[1].total==10 && d.p[1].hit_damage==10 && d.p[1].combo_damage==10);
    f[0].move_changed=0;f[1]=hit(20);f[1].react_chain=2;
    t3_practice_data_step(&d,f);
    assert(d.p[1].combo==2 && d.p[1].total==30 && d.p[1].combo_damage==30 && d.p[1].shown_damage==30);
    assert(d.p[1].new_best==2 && d.p[1].marker==0x59);
    /* The reaction ends: the counter falls back to 0 and the end is reported. */
    f[1]=(t3_practice_facts_t){0};
    t3_practice_data_step(&d,f);
    assert(d.p[1].combo==0 && d.p[1].ended==2);
    /* Combo damage is clamped at 999. */
    t3_practice_data_reset(&d);
    for(int i=0;i<60;++i){f[1]=hit(30);f[1].react_chain=(int16_t)(i+1);t3_practice_data_step(&d,f);}
    assert(d.p[1].combo==60 && d.p[1].combo_damage<=999);
    /* The chain counts the clean hit, holds in a reaction and resets outside one. */
    t3_practice_facts_t c=hit(10);
    assert(t3_practice_react_chain(0,&c)==1 && t3_practice_react_chain(1,&c)==2);
    c=(t3_practice_facts_t){0};c.in_reaction=1;assert(t3_practice_react_chain(3,&c)==3);
    c.in_reaction=0;assert(t3_practice_react_chain(3,&c)==0);
    /* A throw: the opponent's move with the buffer flag counts as a throw. */
    t3_practice_data_reset(&d);
    f[0]=(t3_practice_facts_t){0};f[0].move_changed=1;f[0].move_flags=T3_PRACTICE_FLAG_BUFFER_INPUT;
    f[1]=(t3_practice_facts_t){0};
    t3_practice_data_step(&d,f);
    assert(d.p[1].throw_now==1);
    return 0;
}
