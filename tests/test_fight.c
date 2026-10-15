#include "fight.h"
#ifdef NDEBUG
#undef NDEBUG /* Test assertions must remain active in Release configurations. */
#endif
#include <assert.h>
#include <stdio.h>

static t3_input_t in(uint16_t held, uint16_t pressed) {
    t3_input_t x = {held, pressed};
    return x;
}
static void walk_into_range(t3_match_t* g) {
    int i;
    for (i = 0; i < 32; ++i)
        t3_match_step(g, in(T3_IN_FORWARD, 0), in(T3_IN_FORWARD, 0));
}
static void advance(t3_match_t* g, int frames, t3_input_t a, t3_input_t b) {
    int i;
    for (i = 0; i < frames; ++i) t3_match_step(g, a, b);
}
static void test_reset_and_movement(void) {
    t3_match_t g;
    t3_match_reset(&g);
    assert(g.round == 1 && g.timer == T3_ROUND_TICKS);
    assert(g.fighters[0].x == -360 && g.fighters[1].x == 360);
    assert(g.fighters[0].facing == 1 && g.fighters[1].facing == -1);
    walk_into_range(&g);
    assert(g.fighters[1].x - g.fighters[0].x >= T3_BODY_DISTANCE);
    assert(g.fighters[0].x < -70);
    assert(g.fighters[1].x > 70);
}
static void test_hit_and_one_hit_per_attack(void) {
    t3_match_t g;
    t3_match_reset(&g);
    walk_into_range(&g);
    t3_match_step(&g, in(0,T3_IN_LP), in(0,0));
    advance(&g, 24, in(0,0), in(0,0));
    assert(g.hits == 1 && g.blocks == 0);
    assert(g.fighters[1].health == T3_MAX_HEALTH - 70);
    assert(g.fighters[0].attack == 0);
}
static void test_guard_and_sidesteps(void) {
    t3_match_t g;
    int i;
    t3_match_reset(&g);
    walk_into_range(&g);
    t3_match_step(&g, in(0,T3_IN_RP), in(T3_IN_GUARD,0));
    advance(&g, 8, in(0,0), in(T3_IN_GUARD,0));
    assert(g.blocks == 1 && g.hits == 0);
    assert(g.fighters[1].health == T3_MAX_HEALTH);

    t3_match_reset(&g);
    walk_into_range(&g);
    for (i = 0; i < 25; ++i)
        t3_match_step(&g, in(0,0), in(T3_IN_RIGHT,0));
    t3_match_step(&g, in(0,T3_IN_RK), in(0,0));
    advance(&g, 45, in(0,0), in(0,0));
    assert(g.hits == 0);
}
static void test_timer_round_match(void) {
    t3_match_t g;
    t3_match_reset(&g);
    advance(&g, T3_ROUND_TICKS, in(0,0), in(0,0));
    assert(g.round_done && g.winner == -1);
    assert(!g.wins[0] && !g.wins[1]);
    advance(&g, T3_ROUND_PAUSE, in(0,0), in(0,0));
    assert(g.round == 2 && !g.round_done && g.timer == T3_ROUND_TICKS);

    g.fighters[1].health = 1;
    walk_into_range(&g);
    t3_match_step(&g, in(0,T3_IN_LP), in(0,0));
    advance(&g, 8, in(0,0), in(0,0));
    assert(g.round_done && g.wins[0] == 1);
    advance(&g, T3_ROUND_PAUSE, in(0,0), in(0,0));
    g.fighters[1].health = 1;
    walk_into_range(&g);
    t3_match_step(&g, in(0,T3_IN_LP), in(0,0));
    advance(&g, 8, in(0,0), in(0,0));
    assert(g.match_done && g.wins[0] == 2);
    {
        uint32_t old = g.total_ticks;
        t3_match_step(&g, in(0,0), in(0,0));
        assert(g.total_ticks == old);
    }
}
static void test_deterministic_cpu(void) {
    t3_match_t a, b;
    int i;
    t3_match_reset(&a);
    t3_match_reset(&b);
    for (i = 0; i < 2000; ++i) {
        t3_input_t p1 = t3_cpu_input(&a, 0);
        t3_input_t p2 = t3_cpu_input(&a, 1);
        t3_match_step(&a, p1, p2);
        t3_match_step(&b, t3_cpu_input(&b,0), t3_cpu_input(&b,1));
        assert(a.fighters[0].x == b.fighters[0].x);
        assert(a.fighters[1].x == b.fighters[1].x);
        assert(a.fighters[0].health == b.fighters[0].health);
        assert(a.fighters[1].health == b.fighters[1].health);
        assert(a.round == b.round && a.round_done == b.round_done);
    }
}
int main(void) {
    test_reset_and_movement();
    test_hit_and_one_hit_per_attack();
    test_guard_and_sidesteps();
    test_timer_round_match();
    test_deterministic_cpu();
    puts("fight_core: all deterministic checks passed");
    return 0;
}
