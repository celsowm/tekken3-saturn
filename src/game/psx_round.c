/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Result normalization/classification adapted from pinned Tekken3Remake
 * round_flow.gd. Changes: explicit bounded first-to-two slice policy; no
 * intro/replay/win-pose state machine or original round-flow parity claim.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_round.h"
void t3_psx_round_init(t3_psx_round_t* r) {
    *r=(t3_psx_round_t){.timer=3600,.number=1,.winner=-1};
}
int t3_psx_round_step(t3_psx_round_t* r,const int32_t hp[2],int32_t maximum) {
    if(r->done) {
        if(r->hold)--r->hold;
        if(r->hold || r->match_done)return 0;
        ++r->number;r->timer=3600;r->done=0;r->winner=-1;r->result_flags=0;
        return 1;
    }
    if(hp[0]>0 && hp[1]>0 && r->timer) {--r->timer;return 0;}
    /* FUN_8003E6D0: ceil(health/(maximum >> 12)), not a host float ratio. */
    int32_t unit=maximum>>12;
    int32_t a=unit?(int32_t)(((int64_t)hp[0]+unit-1)/unit):0;
    int32_t b=unit?(int32_t)(((int64_t)hp[1]+unit-1)/unit):0;
    uint16_t time=r->timer?0u:1u;
    r->result_flags=time;
    if(a==b)r->result_flags=(uint16_t)(time|(a?0x20u:0x26u));
    else if(!a || !b) {
        int32_t winner=a>b?a:b;
        r->result_flags=(uint16_t)(time|(winner==0x1000?10u:winner<0xCD?0x12u:2u));
    }
    r->winner=a==b?-1:(a>b?0:1);
    if(r->winner>=0)++r->wins[(unsigned)r->winner];
    /* Slice policy: draws award neither side; first to two, no max-round cap.
     * This differs explicitly from original VS draw/tie-break handling. */
    r->match_done=r->wins[0]>=2u || r->wins[1]>=2u;
    r->done=1;r->hold=time?90u:240u;
    return 0;
}
