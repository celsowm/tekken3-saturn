/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Original OT head insertion/traversal follows Tekken3Remake research.
 * See NOTICE.md. */
#include "psx_scene_ot.h"
int t3_psx_scene_ot_order(const t3_psx_scene_ot_entry_t* entries,unsigned count,
                           uint16_t* order,uint16_t* next) {
    if(count>65535u || (count && (!entries || !order || !next || order==next)))return -1;
    for(unsigned i=0;i<count;++i)if(entries[i].visible && entries[i].bucket>1023u)return -1;
    uint16_t heads[1024];for(unsigned i=0;i<1024;++i)heads[i]=0xffffu;
    for(unsigned i=0;i<count;++i)if(entries[i].visible) {
        unsigned bucket=entries[i].bucket;next[i]=heads[bucket];heads[bucket]=(uint16_t)i;
    }
    unsigned n=0;
    for(unsigned b=1024;b--;)for(uint16_t i=heads[b];i!=0xffffu;i=next[i])order[n++]=i;
    return (int)n;
}
