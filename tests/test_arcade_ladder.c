#include "arcade_ladder.h"
#include <stdio.h>

static int g_failed;
#define CHECK(c) do{if(!(c)){printf("failed line %d: %s\n",__LINE__,#c);g_failed=1;}}while(0)

static void check_ladder(uint32_t seed,unsigned player) {
    uint8_t out[T3_LADDER_FIGHTS];
    uint32_t seen=0;
    t3_arcade_ladder(seed,player,T3_LADDER_ROSTER,out);
    for(unsigned i=0;i<T3_LADDER_FIGHTS;++i) {
        CHECK(out[i]<=20u);
        CHECK(out[i]!=player || (i==9u && player==T3_LADDER_OGRE));   /* fight 10 is always Ogre */
        CHECK(!(seen>>out[i]&1u));          /* ten distinct opponents */
        seen|=1u<<out[i];
        if(i<4u)CHECK(out[i]<10u);          /* the ten original characters first */
        if(i<8u)CHECK(out[i]!=T3_LADDER_HEIHACHI && out[i]!=T3_LADDER_OGRE && out[i]!=T3_LADDER_TRUE_OGRE);
    }
    CHECK(out[8]==(player==T3_LADDER_HEIHACHI?T3_LADDER_JIN:T3_LADDER_HEIHACHI));
    CHECK(out[9]==T3_LADDER_OGRE);
}

int main(void) {
    for(unsigned player=0;player<=20u;++player)
        for(uint32_t seed=0;seed<200u;++seed)check_ladder(seed*2654435761u,player);
    /* The same seed gives the same ladder; another seed a different one. */
    uint8_t a[T3_LADDER_FIGHTS],b[T3_LADDER_FIGHTS],c[T3_LADDER_FIGHTS];
    t3_arcade_ladder(1234u,6u,T3_LADDER_ROSTER,a);
    t3_arcade_ladder(1234u,6u,T3_LADDER_ROSTER,b);
    t3_arcade_ladder(98765u,6u,T3_LADDER_ROSTER,c);
    int same=1,differ=0;
    for(unsigned i=0;i<T3_LADDER_FIGHTS;++i){same&=a[i]==b[i];differ|=a[i]!=c[i];}
    CHECK(same && differ);
    /* The first draw takes x*5+1 mod the pool: seed 0 picks pool[1], the player 0 pool being 1..9. */
    t3_arcade_ladder(0u,0u,T3_LADDER_ROSTER,a);
    CHECK(a[0]==2u);
    /* CPU levels are the fight index; the countdown shows 9 for 89 fields, then 8 .. 1. */
    CHECK(t3_arcade_level(0)==0 && t3_arcade_level(9)==9 && t3_arcade_level(12)==9);
    CHECK(t3_continue_digit(T3_CONTINUE_FIELDS)==9 && t3_continue_digit(720)==9 && t3_continue_digit(719)==8);
    CHECK(t3_continue_digit(1)==1 && t3_continue_digit(0)==1);
    if(!g_failed)printf("arcade ladder ok\n");
    return g_failed;
}
