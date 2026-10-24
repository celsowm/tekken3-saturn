/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * C adaptation of the merged move index (FighterSetupParts, _link_banks) in
 * pinned Tekken3Remake remake/core/fight/fight_simulation.gd and of
 * MoveTotalDamage in remake/content/move_row.gd. Changes: looked up per slot
 * from the linked banks instead of building row tables; bounded event walk. */
#include "move_index.h"
#include "divmot.h"

static uint32_t entry_at(const t3_divmot_t* bank,unsigned slot) {
    const uint8_t* p=bank->data+bank->sections[2]+4u*slot;
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
int t3_move_index_entry(const t3_divmot_t* bank,unsigned slot,
                        const t3_divmot_t** source,uint32_t* entry) {
    uint32_t id;
    if(!bank || !bank->data || !source || !entry || slot>=T3_DIVMOT_MOVE_SLOTS)return 0;
    id=entry_at(bank,slot);
    if(id==T3_DIVMOT_EMPTY_SLOT && bank->linked) {
        const t3_divmot_t* other=bank->linked;
        const uint32_t theirs=entry_at(other,slot);
        if(theirs!=T3_DIVMOT_EMPTY_SLOT){*source=other;*entry=theirs;return 1;}
        id=entry_at(bank,T3_MOVE_INDEX_FALLBACK_SLOT);
    }
    if(id==T3_DIVMOT_EMPTY_SLOT)return 0;
    *source=bank;*entry=id;
    return 1;
}
uint16_t t3_move_total_damage(const t3_divmot_t* bank,uint16_t word,uint32_t event_index) {
    uint32_t len,at;
    if(event_index==0xFFFFFFFFu || bank->sections[5]<bank->sections[4] ||
       bank->sections[5]>bank->size)return word;
    len=bank->sections[5]-bank->sections[4];
    if(len%4u || event_index>=len/4u)return word;
    for(at=event_index*4u;at+4u<=len;at+=4u) {
        const uint8_t* p=bank->data+bank->sections[4]+at;
        if(!(p[0]|p[1]))break;
        if(p[3]==0x0Bu)word=(uint16_t)(word+p[2]);
    }
    return word;
}
