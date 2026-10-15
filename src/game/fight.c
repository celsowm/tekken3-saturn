#include "fight.h"

#include <stddef.h>
#include <string.h>

typedef struct {
    uint8_t startup, active, recovery;
    uint16_t damage, reach, side_reach;
    uint8_t hitstun;
} attack_spec_t;

static const attack_spec_t attacks[4] = {
    { 5, 3, 13, 70,  410, 115, 13 }, /* LP */
    { 7, 3, 16, 95,  445, 120, 15 }, /* RP */
    { 9, 4, 19, 115, 480, 135, 18 }, /* LK */
    { 12, 4, 22, 145, 505, 145, 21 } /* RK */
};

static int32_t iabs(int32_t n) { return n < 0 ? -n : n; }
static int32_t clamp(int32_t n, int32_t lo, int32_t hi) {
    return n < lo ? lo : (n > hi ? hi : n);
}
static int attack_from_input(uint16_t pressed) {
    if (pressed & T3_IN_LP) return 1;
    if (pressed & T3_IN_RP) return 2;
    if (pressed & T3_IN_LK) return 3;
    if (pressed & T3_IN_RK) return 4;
    return 0;
}

static void start_round(t3_match_t* game) {
    memset(game->fighters, 0, sizeof(game->fighters));
    game->fighters[0].x = -360;
    game->fighters[1].x = 360;
    game->fighters[0].health = T3_MAX_HEALTH;
    game->fighters[1].health = T3_MAX_HEALTH;
    game->fighters[0].facing = 1;
    game->fighters[1].facing = -1;
    game->timer = T3_ROUND_TICKS;
    game->round_done = 0;
    game->round_pause = 0;
    game->winner = -1;
}

void t3_match_reset(t3_match_t* game) {
    if (!game) return;
    memset(game, 0, sizeof(*game));
    game->round = 1;
    start_round(game);
}

static void update_facing(t3_match_t* game) {
    t3_fighter_t* a = &game->fighters[0];
    t3_fighter_t* b = &game->fighters[1];
    if (a->x < b->x) {
        a->facing = 1;
        b->facing = -1;
    } else if (a->x > b->x) {
        a->facing = -1;
        b->facing = 1;
    }
}

/* Guard, movement and attack startup operate on the same frame's controls.
 * Pressed attacks are edge-triggered, not repeated while a key is held. */
static void update_fighter(t3_fighter_t* f, t3_input_t in) {
    if (f->stun) {
        --f->stun;
        f->guarding = 0;
        return;
    }
    if (f->attack) {
        f->guarding = 0;
        return;
    }
    f->guarding = (in.held & T3_IN_GUARD) != 0;
    if (f->guarding) return;

    {
        int attack = attack_from_input(in.pressed);
        if (attack) {
            f->attack = (uint8_t)attack;
            f->attack_age = 0;
            f->attack_connected = 0;
            return;
        }
    }
    {
        int forward = !!(in.held & T3_IN_FORWARD) - !!(in.held & T3_IN_BACK);
        int sideways = !!(in.held & T3_IN_RIGHT) - !!(in.held & T3_IN_LEFT);
        f->x = clamp(f->x + forward * f->facing * T3_WALK_SPEED,
                     -T3_ARENA_X, T3_ARENA_X);
        f->z = clamp(f->z + sideways * T3_STEP_SPEED, -T3_ARENA_Z, T3_ARENA_Z);
    }
}

/* A 2.5D pushbox allows stepping around a fighter (z separation), while
 * forbidding characters from passing through each other in close quarters. */
static void separate_bodies(t3_match_t* game) {
    t3_fighter_t* a = &game->fighters[0];
    t3_fighter_t* b = &game->fighters[1];
    int32_t dx = b->x - a->x;
    int32_t dz = b->z - a->z;
    if (iabs(dz) >= T3_BODY_DISTANCE || iabs(dx) >= T3_BODY_DISTANCE) return;
    {
        int32_t sign = dx < 0 ? -1 : 1;
        int32_t push = T3_BODY_DISTANCE - iabs(dx);
        int32_t left = push / 2;
        int32_t right = push - left;
        a->x = clamp(a->x - sign * left, -T3_ARENA_X, T3_ARENA_X);
        b->x = clamp(b->x + sign * right, -T3_ARENA_X, T3_ARENA_X);
        /* If an arena wall absorbs a push, put the remainder on the
         * other fighter. This keeps the hard separation invariant. */
        dx = b->x - a->x;
        if (iabs(dx) < T3_BODY_DISTANCE) {
            if (sign > 0)
                b->x = clamp(a->x + T3_BODY_DISTANCE, -T3_ARENA_X, T3_ARENA_X);
            else
                a->x = clamp(b->x + T3_BODY_DISTANCE, -T3_ARENA_X, T3_ARENA_X);
        }
    }
}

static void end_round(t3_match_t* game) {
    int a = game->fighters[0].health;
    int b = game->fighters[1].health;
    game->winner = a == b ? -1 : (a > b ? 0 : 1);
    if (game->winner >= 0) {
        uint8_t* wins = &game->wins[(unsigned)game->winner];
        if (*wins < 255) ++*wins;
        if (*wins >= 2) game->match_done = 1;
    }
    game->round_done = 1;
    game->round_pause = T3_ROUND_PAUSE;
}

void t3_match_step(t3_match_t* game, t3_input_t player1, t3_input_t player2) {
    int touching[2] = {0, 0};
    int blocked[2] = {0, 0};
    unsigned i;
    if (!game || game->match_done) return;
    ++game->total_ticks;
    if (game->round_done) {
        if (game->round_pause) --game->round_pause;
        if (!game->round_pause) {
            if (game->round < 255) ++game->round;
            start_round(game);
        }
        return;
    }

    update_facing(game);
    update_fighter(&game->fighters[0], player1);
    update_fighter(&game->fighters[1], player2);
    separate_bodies(game);
    update_facing(game);

    /* Snapshot both possible strikes before applying either hit. A trade
     * is resolved symmetrically rather than depending on iteration order. */
    for (i = 0; i < 2; ++i) {
        t3_fighter_t* f = &game->fighters[i];
        const t3_fighter_t* d = &game->fighters[1u - i];
        if (!f->attack) continue;
        const attack_spec_t* a = &attacks[f->attack - 1u];
        ++f->attack_age;
        if (!f->attack_connected &&
            f->attack_age >= a->startup &&
            f->attack_age < a->startup + a->active &&
            (d->x - f->x) * f->facing > 0 &&
            iabs(d->x - f->x) <= a->reach &&
            iabs(d->z - f->z) <= a->side_reach) {
            touching[i] = 1;
            blocked[i] = d->guarding;
            f->attack_connected = 1;
        }
    }
    for (i = 0; i < 2; ++i) {
        if (!touching[i]) continue;
        t3_fighter_t* defender = &game->fighters[1u - i];
        const t3_fighter_t* attacker = &game->fighters[i];
        const attack_spec_t* a = &attacks[attacker->attack - 1u];
        if (blocked[i]) {
            defender->stun = 7;
            defender->guarding = 0;
            ++game->blocks;
        } else {
            defender->health = defender->health > (int)a->damage
                ? (int16_t)(defender->health - (int)a->damage) : 0;
            defender->stun = a->hitstun;
            ++game->hits;
        }
    }
    for (i = 0; i < 2; ++i) {
        t3_fighter_t* f = &game->fighters[i];
        if (!f->attack) continue;
        const attack_spec_t* a = &attacks[f->attack - 1u];
        if (f->stun || f->attack_age >= a->startup + a->active + a->recovery) {
            f->attack = 0;
            f->attack_age = 0;
            f->attack_connected = 0;
        }
    }
    if (game->timer) --game->timer;
    if (!game->timer || !game->fighters[0].health || !game->fighters[1].health)
        end_round(game);
}

t3_input_t t3_cpu_input(const t3_match_t* game, unsigned fighter_index) {
    t3_input_t out = {0, 0};
    const t3_fighter_t* me;
    const t3_fighter_t* enemy;
    uint32_t phase;
    int32_t gap;
    if (!game || fighter_index > 1u || game->round_done || game->match_done)
        return out;
    me = &game->fighters[fighter_index];
    enemy = &game->fighters[1u - fighter_index];
    gap = iabs(enemy->x - me->x);
    phase = game->total_ticks + fighter_index * 23u;
    if (gap > 375) {
        out.held |= T3_IN_FORWARD;
    } else if (gap < 170) {
        out.held |= T3_IN_BACK;
    }
    if (iabs(enemy->z - me->z) > 75)
        out.held |= enemy->z > me->z ? T3_IN_RIGHT : T3_IN_LEFT;
    if (gap <= 505 && iabs(enemy->z - me->z) < 125) {
        if (phase % 100u >= 65u && phase % 100u < 83u)
            out.held |= T3_IN_GUARD;
        else if (phase % 43u == 0u)
            out.pressed |= (phase / 43u) & 1u ? T3_IN_LP : T3_IN_LK;
    }
    return out;
}
