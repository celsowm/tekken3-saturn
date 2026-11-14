/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * The original print_text glyphs (fonts 0 and 1) practice's text uses, from PFONT.BIN (scripts/build_practice_font.py); see
 * NOTICE.md. */
#ifndef T3_PRACTICE_FONT_H
#define T3_PRACTICE_FONT_H
#include "saturn/core.h"
#include <stdint.h>

/* Reads PFONT.BIN into `scratch` and uploads its glyphs and colours. Practice only; without the file or the room the text stays
 * in the debug font. */
sat_result_t t3_practice_font_init(void* scratch,unsigned bytes);
int t3_practice_font_ready(void);
/* The font's advance and line height in original pixels (font 0: 9 and 18; font 1: 13 and 26). */
unsigned t3_practice_font_advance(unsigned font);
unsigned t3_practice_font_line(unsigned font);
/* Prints `text` (capitals, digits and punctuation; a space advances) from the original coordinates (x, y), the pen of `%H%V`. */
sat_result_t t3_practice_font_text(unsigned font,unsigned colour,const char* text,int x,int y);
/* The same with the pen step and glyph height set (the command list's 6 x 12 text is font 0 at advance 6, height 12). */
sat_result_t t3_practice_font_text_sized(unsigned font,unsigned colour,const char* text,int x,int y,unsigned advance,unsigned height);
#endif
