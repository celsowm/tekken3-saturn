/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * ReversalCondition rules adapted from pinned Tekken3Remake
 * remake/core/fighter/move_system.gd. Changes: compact caller-supplied C
 * facts, explicit animation-list query, signed-angle/slot arithmetic checked
 * against the original routine. See NOTICE.md and upstream LICENSE.md. */
#include "psx_reversal.h"

static unsigned le16(const uint8_t* p){return p[0]|((unsigned)p[1]<<8);}
int t3_psx_reversal_list_has(const uint8_t* data,size_t size,int16_t list,int16_t tag) {
    if(!data || size<2 || list<0)return -1;
    unsigned count=le16(data),head=2u+2u*(count+1u);
    if(count!=52u || size<head || (unsigned)list>=count)return -1;
    unsigned at=le16(data+2u+2u*(unsigned)list),end=le16(data+4u+2u*(unsigned)list);
    if(at<head || at>=end || end>size || (at&1u) || (end&1u) || le16(data+end-2u))return -1;
    int found=0;
    for(;at+2u<end;at+=2u) {
        const unsigned value=le16(data+at);
        if(!value || value>=32768u)return -1;
        if((int32_t)value==tag)found=1;
    }
    return found;
}

int t3_psx_reversal_find(const t3_divmot_t* bank,t3_move_record_t move,
    const t3_input_history_t* input,unsigned frame,uint8_t own_bank,
    const t3_branch_context_t* restrictions,const t3_command_sequence_t* sequences,size_t sequence_count,
    int (*condition)(void*,const t3_branch_record_t*),void* user,t3_branch_record_t* out) {
    if(!bank || !input || !condition || !out)return -1;
    t3_divmot_t shared={0};shared.data=bank->common_branches;
    shared.branch_count=bank->common_branch_count;
    const t3_divmot_t* table=bank;
    unsigned index=move.branch_index,back=0;const t3_divmot_t* back_table=NULL;
    for(unsigned steps=0;steps<8192u;++steps) {
        t3_branch_record_t row;
        if(!t3_divmot_branch(table,index,&row))return -1;
        if(row.command==0xC000u)return 0;
        if(row.command==0xC00Du) {
            if(!back_table)return -1;
            table=back_table;index=back+1u;back_table=NULL;continue;
        }
        if(row.command==0xC00Cu) {
            /* Like the original, a call replaces the one saved return. */
            back_table=table;back=index;table=&shared;index=row.target_slot;
            if(!t3_divmot_branch(table,index,&row))return -1;
        }
        if(frame>=row.first_frame && frame<=row.last_frame &&
           t3_command_matches_table(row.command,input,sequences,sequence_count) &&
           t3_branch_restriction_matches(row.restriction,own_bank,restrictions) && row.condition>=0x45u) {
            const int matched=condition(user,&row);
            if(matched<0)return -1;
            if(matched){*out=row;return 1;}
        }
        ++index;
    }
    return -1;
}

int t3_psx_reversal_condition(uint8_t type,uint16_t p,
                             const t3_psx_reversal_facts_t* f) {
    if(!f || type<0x45u || type>0x4Bu)return 0;
    if(f->self_angle>=0x2AAA || f->opponent_angle>=0x2AAA)return 0;
    if(type==0x45u)return f->got_hit!=0;
    if(type==0x4Au)return (int32_t)p==(int32_t)f->opponent_slot;
    if(type==0x4Bu) {
        const int16_t list=(int16_t)(p<32768u?(int32_t)p:(int32_t)p-65536);
        return f->animation_in_list && f->animation_in_list(f->user,list)!=0;
    }
    if(f->opponent_flags&0x800u)return 0;
    const int high=f->opponent_attack==0x412,mid=f->opponent_attack==0x217;
    if((type==0x46u && !high) || (type==0x47u && !mid) ||
       (type==0x48u && f->opponent_attack!=0x10F) ||
       (type==0x49u && !high && !mid))return 0;
    if(!p)return 1;
    for(unsigned i=0;i<4;++i)if(f->joints[i]==p)return 1;
    return 0;
}
