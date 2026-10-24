/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Flat-RAM bus for psx_ai.c: lets scripts/verify_psx_ai.py run the C AI on
 * the same 2 MB guest image as the original routine in the CPU harness. */
#include "psx_ai.h"
#include <string.h>

typedef int (*condition_fn)(uint32_t row,uint32_t self,uint32_t opp);
typedef void (*probe_fn)(uint32_t address,uint32_t record);
typedef struct {uint8_t* ram;condition_fn condition;probe_fn probe;} flat_t;

/* Optional read coverage: one byte per guest RAM byte, set on every read. */
static uint8_t* g_coverage;
void flat_ai_coverage(uint8_t* map) {g_coverage=map;}

static uint32_t flat_read(void* user,uint32_t address,unsigned size) {
    const uint8_t* p=((flat_t*)user)->ram+(address&0x1FFFFFu);
    if(g_coverage)for(unsigned i=0;i<size;++i)g_coverage[(address+i)&0x1FFFFFu]|=1u;
    uint32_t v=0;
    for(unsigned i=0;i<size;++i)v|=(uint32_t)p[i]<<(8*i);
    return v;
}
static void flat_write(void* user,uint32_t address,unsigned size,uint32_t value) {
    uint8_t* p=((flat_t*)user)->ram+(address&0x1FFFFFu);
    if(g_coverage)for(unsigned i=0;i<size;++i)g_coverage[(address+i)&0x1FFFFFu]|=2u;
    for(unsigned i=0;i<size;++i)p[i]=(uint8_t)(value>>(8*i));
}
static int flat_condition(void* user,uint32_t row,uint32_t self,uint32_t opp) {
    return ((flat_t*)user)->condition(row,self,opp);
}
static void flat_probe(void* user,uint32_t address,uint32_t record) {
    if(((flat_t*)user)->probe)((flat_t*)user)->probe(address,record);
}
static t3_psx_ai_bus_t bus_for(flat_t* flat) {
    return (t3_psx_ai_bus_t){flat,flat_read,flat_write,flat_condition,flat_probe};
}

uint32_t flat_ai_update(uint8_t* ram,uint32_t fighter,int has_stale,uint32_t stale,condition_fn condition,
                        probe_fn probe) {
    flat_t flat={ram,condition,probe};
    const t3_psx_ai_bus_t bus=bus_for(&flat);
    uint16_t held,pressed;
    t3_psx_ai_update(&bus,fighter,has_stale,stale,&held,&pressed);
    return (uint32_t)held|(uint32_t)pressed<<16;
}
void flat_ai_reset(uint8_t* ram,uint32_t fighter,int group,int level,int disabled,condition_fn condition) {
    flat_t flat={ram,condition,0};
    const t3_psx_ai_bus_t bus=bus_for(&flat);
    t3_psx_ai_reset(&bus,fighter,group,level,disabled);
}
void flat_ai_init_round(uint8_t* ram,condition_fn condition) {
    flat_t flat={ram,condition,0};
    const t3_psx_ai_bus_t bus=bus_for(&flat);
    t3_psx_ai_init_round(&bus);
}
