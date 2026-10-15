#ifndef T3_FIGHT_H
#define T3_FIGHT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Deterministic 60-Hz simulation. Horizontal coordinates are signed Q8;
 * 256 units = 1 world unit. No floats, allocator, I/O, or Saturn headers.
 * Inputs are relative to the opponent, not fixed world left/right. */
enum {
    T3_HZ = 60,
    T3_MAX_HEALTH = 1000,
    T3_ROUND_TICKS = 60 * T3_HZ,
    T3_ROUND_PAUSE = 120,
    T3_ARENA_X = 1200,
    T3_ARENA_Z = 550,
    T3_BODY_DISTANCE = 150,
    T3_WALK_SPEED = 11,
    T3_STEP_SPEED = 9
};

enum {
    T3_IN_FORWARD = 1u << 0,
    T3_IN_BACK = 1u << 1,
    T3_IN_LEFT = 1u << 2,
    T3_IN_RIGHT = 1u << 3,
    T3_IN_GUARD = 1u << 4,
    T3_IN_LP = 1u << 5,
    T3_IN_RP = 1u << 6,
    T3_IN_LK = 1u << 7,
    T3_IN_RK = 1u << 8,
    T3_IN_CROUCH = 1u << 9,
    T3_IN_JUMP = 1u << 10
};

typedef struct {
    uint16_t held;
    uint16_t pressed;
} t3_input_t;

/* Portable 60-frame input history. Keeping it in fight.h avoids any
 * dependency cycle with the standalone command interpreter. */
enum { T3_INPUT_HISTORY_FRAMES = 60, T3_INPUT_SEQUENCE_MAX = 32 };
typedef struct {
    uint8_t direction, held_buttons, pressed_buttons;
} t3_command_frame_t;
typedef struct {
    t3_command_frame_t frames[T3_INPUT_HISTORY_FRAMES];
    uint8_t next, count;
} t3_input_history_t;

/* Attack indices 1..4, 0 = not attacking. */
typedef struct {
    int32_t x, z;
    int16_t health;
    int16_t stun;
    int8_t facing;            /* -1 or +1 */
    uint8_t guarding;
    uint8_t attack;
    uint8_t attack_age;
    uint8_t attack_connected;
    uint8_t crouching;
    uint8_t jump_frames;
    uint8_t dash_frames;
    int8_t dash_direction;
} t3_fighter_t;

typedef struct {
    t3_fighter_t fighters[2];
    t3_input_history_t input_histories[2];
    uint32_t total_ticks;
    uint16_t timer;
    uint16_t round_pause;
    uint16_t hits;
    uint16_t blocks;
    uint8_t round;
    uint8_t wins[2];
    uint8_t round_done;
    uint8_t match_done;
    int8_t winner;            /* -1 tie, 0 or 1 */
} t3_match_t;

void t3_match_reset(t3_match_t* game);
void t3_match_step(t3_match_t* game, t3_input_t player1, t3_input_t player2);
t3_input_t t3_cpu_input(const t3_match_t* game, unsigned fighter_index);

#ifdef __cplusplus
}
#endif

#endif /* T3_FIGHT_H */
