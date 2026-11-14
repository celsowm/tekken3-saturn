/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's key ring (practice.ovl FUN_800B7B08, FUN_800B7A88), adapted from Tekken3Remake
 * remake/core/game/practice_mode.gd (_key_log, key_ring_reset) and modes.md#practice; see NOTICE.md. */
#ifndef T3_PRACTICE_KEYS_H
#define T3_PRACTICE_KEYS_H
#include <stdint.h>

enum { T3_KEY_RING_WORDS=50 };
/* One word per entry: face buttons (pad & 0xF0 >> 4) in byte 0, directions (pad >> 12) in byte 1, a hold count in
 * bits 16-30 (at most 0x3FFF), bit 31 the fighter's side flag. */
enum { T3_KEY_IDLE_FRAMES=30 };
typedef struct { uint32_t word[T3_KEY_RING_WORDS]; int32_t head; int32_t wait; } t3_practice_keys_t;
void t3_practice_keys_reset(t3_practice_keys_t* k);
/* FUN_800B7968: counts the fighter's idle frames down (in its stance, or with no move pending and no button held)
 * and returns 1 when an input comes after the ring has been idle for 30 frames: the caller empties the ring first. */
int t3_practice_keys_idle(t3_practice_keys_t* k,int buttons_held,int in_stance,int idle_move);
/* The pad word of the frame: a repeated input counts on, a new one starts the next entry. */
void t3_practice_keys_log(t3_practice_keys_t* k,uint16_t pad,int side_flag);
/* FUN_800B7C84: the key display's items, oldest first: a new direction (its pad word in 0xF000) or the buttons newly held
 * (in 0xF0), each with a colour 0-3 (0 and 1 before the guide's end, 3 or 0 after, by the prompt). `guide_end` is the replay
 * position, `prompt` and `prompt_timer` S+0xA3/0xA4, `recording` S+0xA2. Returns the number of items written (at most 15). */
typedef struct { uint16_t pad;uint8_t colour; } t3_practice_key_item_t;
enum { T3_KEY_ITEMS=15 };
unsigned t3_practice_keys_items(const t3_practice_keys_t* k,int guide_end,unsigned prompt,unsigned prompt_timer,int recording,
                                t3_practice_key_item_t out[T3_KEY_ITEMS]);
/* The entry `age` back from the newest (0), as text "DR+1" (directions U R D L, buttons 1 LP, 2 RP, 3 LK, 4 RK);
 * "" when the entry is empty. `out` takes at least 8 bytes. */
void t3_practice_keys_text(const t3_practice_keys_t* k,unsigned age,char out[8]);
#endif
