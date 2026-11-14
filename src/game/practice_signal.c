/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from Tekken3Remake remake/core/fighter/move_system.gd; see practice_signal.h and NOTICE.md. */
#include "practice_signal.h"

enum { ROW_END=0xC000,ROW_CALL=0xC00C,ROW_RETURN=0xC00D };

/* _can_act_find: the first row whose window, restriction and condition hold (the input is ignored), walking CALLs
 * into the common rows. 1 with the row in `out`; 0 with the END row it stopped at. In the continuation (`ahead`)
 * rows open from the first frame. */
static int find(const t3_divmot_t* bank,unsigned start,unsigned frame,uint8_t own_bank,
                const t3_branch_context_t* facts,int ahead,t3_branch_record_t* out) {
    unsigned i=start,return_index=0,in_common=0,walked=0;
    const unsigned limit=(unsigned)bank->branch_count*(1u+bank->common_branch_count)+1u;
    t3_branch_record_t row;
    for(;walked++<limit;++i) {
        if(in_common) {
            if(!bank->common_branches || i>=bank->common_branch_count)return 0;
            if(!t3_divmot_common_branch(bank,i,&row))return 0;
        } else if(!t3_divmot_branch(bank,i,&row))return 0;
        if(row.command==ROW_RETURN) {
            if(!in_common)return 0;
            i=return_index;in_common=0;
            continue;
        }
        if(row.command==ROW_CALL) {
            if(in_common || !bank->common_branches || row.target_slot>=bank->common_branch_count)return 0;
            return_index=i;in_common=1;
            i=(unsigned)row.target_slot-1u;   /* the loop's increment enters the target's first row */
            continue;
        }
        if(row.command==ROW_END){*out=row;return 0;}
        const int in_window=ahead?(row.first_frame<2u && row.last_frame!=0u):(frame>=row.first_frame && frame<=row.last_frame);
        const unsigned c=row.condition;
        if(in_window && t3_branch_restriction_matches(row.restriction,own_bank,facts) && c<0x45u &&
           !(row.command==0x10u && c==0u && row.flags==2u) &&
           ((!ahead && c>=0x3Au && c-0x3Au<3u) || c==0u ||
            t3_branch_condition_matches(row.condition,row.parameter,facts))) {
            *out=row;return 1;
        }
    }
    return 0;
}

int t3_practice_can_act(const t3_divmot_t* bank,const t3_divmot_t* common,const t3_move_record_t* current,
                        unsigned frame,int frame_step_positive,uint8_t own_bank,const t3_branch_context_t* facts) {
    t3_branch_record_t row;
    if(!bank || !bank->data || !current || current->branch_index>=bank->branch_count)return 0;
    if(find(bank,current->branch_index,frame,own_bank,facts,0,&row))return 1;
    const unsigned first=row.first_frame;
    if(!(((first>=5u?first-5u:0u)<=frame && frame<first) || (frame>=first && frame<=row.last_frame)) || !frame_step_positive)
        return 0;
    t3_move_record_t target;const t3_divmot_t* owner=NULL;
    if(t3_divmot_slot_resolve(bank,common,row.target_slot,&target,&owner)<=0 || !owner || target.branch_index>=owner->branch_count)
        return 0;
    t3_branch_record_t ahead;
    return find(owner,target.branch_index,frame,own_bank,facts,1,&ahead);
}

void t3_practice_signal_colour(int can_act,uint8_t rgb[3]) {
    rgb[0]=can_act?0:250;rgb[1]=can_act?250:0;rgb[2]=0;
}
