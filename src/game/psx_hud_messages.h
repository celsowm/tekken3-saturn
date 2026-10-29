#ifndef T3_PSX_HUD_MESSAGES_H
#define T3_PSX_HUD_MESSAGES_H
#include <stdint.h>

/* Normal Arcade/VS round messages. Names are private character metadata,
 * supplied by the caller; no font, renderer or retail data dependency. */
typedef struct {
    int32_t state,frame,played,max_rounds,result,replay_request,human_mask,winner;
    int32_t replay_mode;
    uint32_t frame_counter;
    const char* winner_name;
} t3_psx_hud_messages_t;
typedef struct {
    char text[32];
    int16_t x,y;
    uint8_t colour,font;
} t3_psx_hud_message_t;
/* At most two lines, in original submission order. */
unsigned t3_psx_hud_messages(const t3_psx_hud_messages_t* facts,
                           t3_psx_hud_message_t out[2]);
#endif
