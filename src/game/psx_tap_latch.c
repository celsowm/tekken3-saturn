/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Adapted from Tekken3Remake remake/core/fighter/fighter_input.gd's
 * documented LatchButtonTaps rule. Changes: caller-owned compact C state,
 * explicit frame counters and shared register; retains original bug #52.
 * See NOTICE.md and third_party/Tekken3Remake/LICENSE.md. */
#include "psx_tap_latch.h"

void t3_psx_tap_latch_step(t3_psx_tap_latch_t* s,uint8_t entry,
                          uint32_t frame,uint32_t seen,uint32_t* scratch) {
    if(frame==seen)return;
    if(s->condition_used!=1u || !(s->lp|s->rp|s->both)) {
        const unsigned fresh=entry & (entry ^ s->previous);
        *scratch=entry;
        if((entry&0x30u)==0x30u && (fresh&0x30u))s->both=1;
        else if(fresh&0x10u)s->lp=1;
        else if(fresh&0x20u)s->rp=1;
        else s->lp=s->rp=s->both=0;
    }
    s->previous=(uint16_t)*scratch;
}
