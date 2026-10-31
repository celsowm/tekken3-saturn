/* The CPU opponent (psx_ai.c) on the owned match: a bus that shows the typed
 * fighters, the linked motion banks and the private EXE tables at their
 * original Japan Rev.1 addresses. */
#ifndef T3_PSX_AI_GAME_H
#define T3_PSX_AI_GAME_H
#include <stddef.h>
#include <stdint.h>
#include "fight.h"
#include "divmot.h"
#include "branch_rules.h"
#include "psx_ai.h"

struct t3_psx_match;
/* The AI's own memory: index, flags, records, candidates and throw rows. */
#define T3_PSX_AI_BLOCK 0x8009F690u
#define T3_PSX_AI_BLOCK_SIZE 0xFC0u
#define T3_PSX_AI_MEMO 256u
/* g_divmotBuffer's banks in the original (fight traces): fighter 0, the
 * second fighter's at +0x62958 (+0x5233C when it uses bank type 3), divmot99. */
#define T3_PSX_AI_BANK_BUFFER 0x800F9EA0u
/* Fighter fields below this offset are copied into an image per call. */
#define T3_PSX_AI_IMAGE 0x130u

typedef struct t3_psx_ai_game {
    uint8_t block[T3_PSX_AI_BLOCK_SIZE] __attribute__((aligned(4)));
    uint32_t self,opp;                  /* 0x800AFF24, 0x800A8B44 */
    uint32_t game_mode,attract,force_layout;
    uint16_t difficulty;uint8_t cpu_level; /* 0x800AE6D0, 0x800AE6D9 */
    uint8_t cpu_mask;                   /* fighters the AI drives (isCpu) */
    int8_t slot[3],target[3];           /* fighter +0x1886, +0x1887 */
    uint32_t bank_base[3];              /* fighter 0, fighter 1, divmot99 */
    /* psx_ai.bin (scripts/extract_psx_ai_tables.py), caller-owned. */
    const uint8_t* tables;size_t tables_size;
    /* Bus state during a call: the match, its banks (fighter 0, fighter 1,
     * divmot99), the fighters' low fields and branch facts. */
    struct t3_psx_match* match;
    const t3_divmot_t* bank[3];
    uint8_t image[2][T3_PSX_AI_IMAGE];
    t3_branch_context_t facts[2];unsigned facts_ready; /* bit i: facts[i] is this call's */
    t3_psx_ai_window_t window[9];unsigned window_count; /* constant rows, set per call */
    /* The globals and each fighter's fields above the image (fighter_high)
     * as host words for the call; +0x1884 (slot, target) is copied back. */
    uint32_t high[2][9];
    t3_psx_ai_word_t word[64];const struct t3_psx_match* word_match;
    t3_psx_ai_memo_t pick[128];uint32_t pick_index; /* the bus's picks */
    t3_psx_ai_collect_t collect;        /* and its last collection */
    /* Linked bank reads are constant while the banks are: a direct-mapped memo
     * of (address,size) -> value, cleared when a bank or its placement changes. */
    t3_psx_ai_memo_t memo[T3_PSX_AI_MEMO];
    const t3_divmot_t* memo_bank[3];uint32_t memo_base[3];
    uint32_t reads,conditions;         /* bus reads and BranchCondition calls (cost study) */
    uint32_t misses,last_miss;          /* accesses outside every mapped field */
    uint16_t held,pressed;              /* the last AiUpdate's pad words */
    t3_input_t pad[2];                  /* each fighter's last CPU input */
    /* Optional, for verification: called before and after each AiUpdate. */
    void (*observe)(struct t3_psx_ai_game* ai,unsigned fighter,int after);
    /* Optional: every bus read (size 0: a branch condition at `address`
     * and its result). */
    void (*trace)(struct t3_psx_ai_game* ai,uint32_t address,unsigned size,uint32_t value);
    void* observer;
} t3_psx_ai_game_t;

/* Validates the table blob; zeroes the AI's memory. 1 on success. */
int t3_psx_ai_game_init(t3_psx_ai_game_t* ai,const uint8_t* tables,size_t size,uint8_t cpu_mask,
                        uint16_t difficulty,uint8_t level);
/* The original buffer layout for the two fighters' bank types. */
void t3_psx_ai_game_place_banks(t3_psx_ai_game_t* ai,unsigned type0,unsigned type1,int shared);
/* AiInitRound, after the round's placement, distances and camera. */
int t3_psx_ai_game_init_round(t3_psx_ai_game_t* ai,struct t3_psx_match* match);
/* CpuOrPadInput for a CPU fighter: AiUpdate while it has health (else no
 * input), returned as fighter-relative input (side 0). */
/* AiUpdate's pad words as the owned core's relative input. */
t3_input_t t3_psx_ai_game_pad(uint16_t held,uint16_t pressed);
int t3_psx_ai_game_input(t3_psx_ai_game_t* ai,struct t3_psx_match* match,unsigned i,t3_input_t* out);
/* The guest address of the move row a fighter's merged index gives `slot`,
 * 0 when absent. */
uint32_t t3_psx_ai_game_row(t3_psx_ai_game_t* ai,struct t3_psx_match* match,unsigned i,unsigned slot);
#endif
