#ifndef T3_SATURN_CHAR_SELECT_H
#define T3_SATURN_CHAR_SELECT_H
#include <stdint.h>
#include "saturn/hud.h"

/* Runs the character select (SELECT.BIN faces) until both sides have chosen or
 * the timer ends. P1 chooses with pad 1; pad 2 chooses P2, or pad 1 chooses the
 * CPU's fighter when no pad 2 is connected, and B takes the first pick back.
 * `picks` holds the ids the cursors start on and returns the choices. `scratch`
 * holds the file while it is uploaded and must be at least 20 KB. */
sat_result_t t3_character_select(const sat_hud_t* hud,void* scratch,uint32_t bytes,uint8_t picks[2]);

#endif
