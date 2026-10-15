#include "input_history.h"
#include <stddef.h>

void t3_history_reset(t3_input_history_t* h) {
    if (h) __builtin_memset(h, 0, sizeof(*h));
}

static uint8_t buttons(uint16_t bits) {
    uint8_t b = 0;
    if (bits & T3_IN_LP) b |= 1u;
    if (bits & T3_IN_RP) b |= 2u;
    if (bits & T3_IN_LK) b |= 4u;
    if (bits & T3_IN_RK) b |= 8u;
    return b;
}

static uint8_t direction(uint16_t held) {
    int x = !!(held & T3_IN_FORWARD) - !!(held & T3_IN_BACK);
    int y = !!(held & T3_IN_LEFT) - !!(held & T3_IN_RIGHT);
    /* Opposing directions neutralise one another. */
    return (uint8_t)(5 + x + y * 3);
}

void t3_history_push(t3_input_history_t* h, t3_input_t input) {
    t3_command_frame_t* frame;
    if (!h) return;
    frame = &h->frames[h->next];
    frame->direction = direction(input.held);
    frame->held_buttons = buttons(input.held | input.pressed);
    frame->pressed_buttons = buttons(input.pressed);
    h->next = (uint8_t)((h->next + 1u) % T3_INPUT_HISTORY_FRAMES);
    if (h->count < T3_INPUT_HISTORY_FRAMES) ++h->count;
}

t3_command_frame_t t3_history_at(const t3_input_history_t* h, unsigned age) {
    const t3_command_frame_t empty = {5u, 0u, 0u};
    if (!h || age >= h->count) return empty;
    return h->frames[(h->next + T3_INPUT_HISTORY_FRAMES - 1u - age)
                     % T3_INPUT_HISTORY_FRAMES];
}

static int double_tap(const t3_input_history_t* h, uint8_t dir) {
    unsigned i;
    int phase = 0;
    if (!h || h->count < 3u ||
        t3_history_at(h, 0).direction != dir ||
        t3_history_at(h, 1).direction == dir)
        return 0;
    /* Walk newest -> oldest, require release to neutral before prior tap. */
    for (i = 1u; i < h->count && i < 20u; ++i) {
        uint8_t d = t3_history_at(h, i).direction;
        if (!phase) {
            if (d == 5u) phase = 1;
        } else if (d == dir) {
            return 1;
        }
    }
    return 0;
}

int t3_command_matches(uint16_t cmd, const t3_input_history_t* h) {
    t3_command_frame_t f;
    uint16_t dir_mask;
    uint8_t mask;
    if (!h || !h->count) return 0;
    f = t3_history_at(h, 0);
    if (cmd == 0xC00Du) return 1;
    if (cmd == 0xC001u) return double_tap(h, 6u);
    if (cmd == 0xC002u) return double_tap(h, 4u);
    if (cmd >= 0xC000u) return 0;
    dir_mask = cmd & 0x3FE0u;
    if (dir_mask && !(dir_mask & (uint16_t)(1u << (f.direction + 4u))))
        return 0;
    mask = (uint8_t)(cmd & 15u);
    if (cmd & 16u) return mask ? !!(f.pressed_buttons & mask) : 1;
    if (!mask) return f.pressed_buttons == 0u;
    return !!(f.pressed_buttons & mask) && (f.held_buttons & mask) == mask;
}

static int sequence_step(uint16_t step, t3_command_frame_t f) {
    uint8_t dir = (uint8_t)(step & 15u);
    uint8_t b = (uint8_t)((step >> 8u) & 15u);
    if (dir && dir != f.direction) return 0;
    if (b && (!(f.pressed_buttons & b) || (f.held_buttons & b) != b))
        return 0;
    return 1;
}

int t3_sequence_matches(const t3_input_history_t* h,
                        const uint16_t* steps, unsigned max_window) {
    unsigned n = 0, i, age;
    if (!h || !steps || !h->count || !max_window) return 0;
    while (n < T3_INPUT_SEQUENCE_MAX && steps[n]) ++n;
    if (!n || n == T3_INPUT_SEQUENCE_MAX ||
        max_window > T3_INPUT_HISTORY_FRAMES) return 0;
    if (!sequence_step(steps[n-1u], t3_history_at(h, 0))) return 0;
    age = 1u;
    for (i = n - 1u; i > 0u; --i) {
        int found = 0;
        for (; age < h->count && age < max_window; ++age) {
            t3_command_frame_t frame = t3_history_at(h, age);
            if (sequence_step(steps[i-1u], frame)) {
                found = 1;
                ++age;
                break;
            }
            /* An unrelated button press cancels the historical sequence. */
            if (frame.pressed_buttons) return 0;
        }
        if (!found) return 0;
    }
    return 1;
}

int t3_command_matches_table(uint16_t command, const t3_input_history_t* h,
                             const t3_command_sequence_t* table, size_t count) {
    size_t i;
    if (command < 0xC00Eu) return t3_command_matches(command,h);
    if (!((command >= 0xC00Eu && command <= 0xC04Cu) ||
          (command >= 0xC7FFu && command <= 0xC827u)) ||
        !table || !h) return 0;
    for (i=0;i<count;++i) {
        if (table[i].command == command) {
            unsigned j;
            for (j=0;j<T3_INPUT_SEQUENCE_MAX+1u &&
                     table[i].steps[j];++j) {}
            if (j==0 || j>T3_INPUT_SEQUENCE_MAX ||
                table[i].steps[j]!=0u) return 0;
            return t3_sequence_matches(h,table[i].steps,table[i].window);
        }
    }
    return 0;
}
