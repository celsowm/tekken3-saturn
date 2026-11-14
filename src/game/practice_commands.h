/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Practice's COMMAND LIST data (modes.md#practice, FUN_8007906C / FUN_800789E4): each character's move names and commands
 * as text (scripts/extract_command_lists.py); see NOTICE.md. */
#ifndef T3_PRACTICE_COMMANDS_H
#define T3_PRACTICE_COMMANDS_H
#include <stdint.h>

/* Blob: "T3CL", u8 characters, per character u16 lines, per line name NUL command NUL. */
unsigned t3_practice_command_count(const uint8_t* blob,uint32_t size,unsigned character);
/* One line of a character's list; the strings point into the blob. 1 when found. */
int t3_practice_command_line(const uint8_t* blob,uint32_t size,unsigned character,unsigned index,const char** name,const char** command);
#endif
