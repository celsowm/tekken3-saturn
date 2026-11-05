#ifndef T3_PSX_HANDS_H
#define T3_PSX_HANDS_H
#include <stdint.h>
/* The two hand channels, fighter +0x127C current, +0x1280 target, +0x1284
 * rate: HandFaceCommand sets a shape, FighterHandPoses moves towards it each
 * animated frame and picks the vertex variant each hand part shows. Channel 0
 * is part 16's hand (Kuma's and Gon's jaw), channel 1 part 12's. True Ogre's
 * level channel stays in psx_match.c. */
typedef struct {
    int16_t current[2],target[2],rate[2];
    uint8_t variant[2];
} t3_psx_hands_t;

/* HandFaceCommand for `hands` (bit 0 channel 0, bit 1 channel 1): shapes 0
 * and 1 blend between the open hand and the fist over speed + 1 frames, 2..7
 * are set at once; 8 and above (the face shapes) leave the hands alone. */
void t3_psx_hands_command(t3_psx_hands_t* hands,unsigned which,unsigned shape,int speed);
/* A move event's hand command (t3_psx_move_events_t.hand, 0 for none), with
 * the costume's own shape for command 21. Returns 1 when it was one. */
int t3_psx_hands_event(t3_psx_hands_t* hands,uint16_t event,unsigned costume_shape);
/* One animated frame (not paused, not frozen). */
void t3_psx_hands_step(t3_psx_hands_t* hands);
#endif
