/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Original OT head insertion/traversal follows Tekken3Remake research.
 * See NOTICE.md. */
#include "psx_scene_ot.h"
int t3_psx_scene_ot_order(const t3_psx_scene_ot_entry_t* entries,unsigned count,
                           uint16_t* order,uint16_t* next) {
    if(count>65535u || (count && (!entries || !order || !next || order==next)))return -1;
    /* Only the buckets in use are cleared and walked. */
    unsigned low=1024,high=0;
    for(unsigned i=0;i<count;++i)if(entries[i].visible) {
        const unsigned bucket=entries[i].bucket;
        if(bucket>1023u)return -1;
        if(bucket<low)low=bucket;
        if(bucket>=high)high=bucket+1u;
    }
    unsigned n=0;
    if(low>=high)return 0;
    uint16_t heads[1024];for(unsigned i=low;i<high;++i)heads[i]=0xffffu;
    for(unsigned i=0;i<count;++i)if(entries[i].visible) {
        unsigned bucket=entries[i].bucket;next[i]=heads[bucket];heads[bucket]=(uint16_t)i;
    }
    for(unsigned b=high;b-->low;)for(uint16_t i=heads[b];i!=0xffffu;i=next[i])order[n++]=i;
    return (int)n;
}
