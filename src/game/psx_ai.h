/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * The CPU opponent (AiUpdate 0x80059B78 and its helpers, Japan Rev.1),
 * adapted from pinned Tekken3Remake tools/research/ai_sim.py and
 * ai_update.py. See NOTICE.md. */
#ifndef T3_PSX_AI_H
#define T3_PSX_AI_H
#include <stdint.h>

/* The AI works on original guest addresses. The bus reads and writes them:
 * a flat RAM image on the host, a mapped view of the owned core in the game.
 * Reads return the value zero-extended to 32 bits. */
/* Constant guest memory the AI may read straight from the host (little-endian
 * `data`), so the many row reads skip the bus. Optional: a bus that traces
 * its reads leaves them out. */
typedef struct t3_psx_ai_window {
    uint32_t base,size;
    const uint8_t* data;
} t3_psx_ai_window_t;
/* A two-way memo of constant reads at or above `memo_from`: set
 * s=(address^address>>8^size)&mask is entries 2s and 2s+1, each holding key
 * (address&0x1FFFFFFF)|size<<29 and the value; 2s is the newer. */
typedef struct t3_psx_ai_memo {uint32_t key,value;} t3_psx_ai_memo_t;
/* A guest word held as a host word, direct-mapped: entry (address>>2^address>>9)&mask
 * holds `address` (aligned, 0 when empty). Reads and writes of any part of it
 * use `host`, which the bus owns for the call. */
typedef struct t3_psx_ai_word {uint32_t address;uint32_t* host;} t3_psx_ai_word_t;
typedef struct t3_psx_ai_bus {
    void* user;
    uint32_t (*read)(void* user,uint32_t address,unsigned size);
    void (*write)(void* user,uint32_t address,unsigned size,uint32_t value);
    /* BranchCondition (0x8002E310) of the 12-byte row at `row` for `self`. */
    int (*condition)(void* user,uint32_t row,uint32_t self,uint32_t opp);
    /* Optional: verification writes at a game address the port also passes. */
    void (*probe)(void* user,uint32_t address,uint32_t record);
    const t3_psx_ai_window_t* windows;unsigned window_count;
    /* Optional: the AI's own block (T3_PSX_AI_INDEX..) as little-endian host
     * bytes that guest writes may also go straight into; `ram` is 4-aligned. */
    uint8_t* ram;uint32_t ram_base,ram_size;
    const t3_psx_ai_memo_t* memo;uint32_t memo_mask,memo_from;
    const t3_psx_ai_word_t* words;uint32_t word_mask;
    /* Optional: candidate collection's picks, direct-mapped by branch row
     * ((row>>2^row>>9)&pick_mask): key the row, value its target (the index
     * entry) with bit 0 set when the target's +4 has bit 23. Valid for the
     * index *pick_index; the AI empties it for another index, the bus when its
     * banks change. */
    t3_psx_ai_memo_t* picks;uint32_t pick_mask;uint32_t* pick_index;
    /* Optional, with picks: the last full candidate collection, reused while
     * its inputs match. The bus zeroes it whenever it empties the picks. */
    struct t3_psx_ai_collect* collect;
} t3_psx_ai_bus_t;
/* What a candidate collection depends on: the walk's inputs, the frames over
 * which every window test it made has the same result, and the BranchCondition
 * rows it asked with their results (bit k of `held`). Its entries stay in the
 * AI block: only collection writes an entry's target and row. */
#define T3_PSX_AI_COLLECT_CONDITIONS 4u
typedef struct t3_psx_ai_collect {
    uint32_t valid,f,start,move,index,sit;
    int32_t self16,self18,opp16,first,last,count;
    uint32_t condition[T3_PSX_AI_COLLECT_CONDITIONS],conditions,held;
} t3_psx_ai_collect_t;

#define T3_PSX_AI_RECORDS 0x8009F6C0u
#define T3_PSX_AI_RECORD_SIZE 0x330u
#define T3_PSX_AI_CANDIDATES 0x8009FD20u
#define T3_PSX_AI_MAX_CANDIDATES 180u
#define T3_PSX_AI_THROW_ROWS 0x800A0590u
#define T3_PSX_AI_INDEX 0x8009F690u
#define T3_PSX_AI_SELF 0x800AFF24u
#define T3_PSX_AI_OPP 0x800A8B44u
#define T3_PSX_AI_LCG 0x800AE168u
#define T3_PSX_AI_RAND 0x800A3E80u
#define T3_PSX_AI_GAME_MODE 0x800AFF50u
#define T3_PSX_AI_MULTI 0x8009F6A4u
#define T3_PSX_AI_TEAM_OR_SURVIVAL 0x800AE39Cu
#define T3_PSX_AI_FRAME_COUNTER 0x800AFF14u
#define T3_PSX_AI_FIGHTER_DISTANCE 0x800AFF04u
#define T3_PSX_AI_BANKS 0x800AE0E8u
#define T3_PSX_AI_DIFFICULTY 0x800AE6D0u
#define T3_PSX_AI_CPU_LEVEL 0x800AE6D9u
#define T3_PSX_AI_FIGHTER_BASE 0x800A96F0u
#define T3_PSX_AI_FIGHTER_STRIDE 0x188Cu
#define T3_PSX_AI_COMMON_ROWS 0x80017C78u

/* AiUpdate: the held and newly pressed pad of `fighter` (side 0, so 0x2000
 * is towards the opponent). `stale` is the uninitialised stack word that
 * AiCollectCandidates compares targets with; has_stale 0 never matches. */
void t3_psx_ai_update(const t3_psx_ai_bus_t* bus,uint32_t fighter,int has_stale,uint32_t stale,
                      uint16_t* held,uint16_t* pressed);
/* AiInitRound (0x8005993C): AI slots, targets and records for the round. */
void t3_psx_ai_init_round(const t3_psx_ai_bus_t* bus);
/* AiReset (0x800596B0) for the record of `fighter`. */
void t3_psx_ai_reset(const t3_psx_ai_bus_t* bus,uint32_t fighter,int group,int level,int disabled);
#endif
