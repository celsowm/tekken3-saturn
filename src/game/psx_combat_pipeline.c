#include "psx_combat_pipeline.h"

int t3_psx_combat_preview_pair(
    const t3_psx_combat_attack_t attacks[2],
    const t3_psx_hit_apply_frame_t defender_frames[2],
    t3_psx_combat_preview_t* out) {
    t3_psx_combat_preview_t temp={0};
    t3_psx_hit_apply_frame_t frames[2];
    t3_psx_hit_test_facts_t gate_facts[2];
    unsigned attacker,defender,slot,j;
    if(!attacks || !defender_frames || !out)return 0;
    frames[0]=defender_frames[0];
    frames[1]=defender_frames[1];
    for(attacker=0u;attacker<2u;++attacker) {
        defender=attacker^1u;
        gate_facts[attacker]=attacks[attacker].test;
        /* HitTest's free-slot bitmask MUST match the two real PS1
         * HitSlot.used fields; never manufacture an empty slot. */
        for(j=0u;j<2u;++j) {
            unsigned empty=(unsigned)(
                gate_facts[attacker].free_slots_mask & (1u<<j));
            if(frames[defender].slots[j].used>1u ||
               empty != (frames[defender].slots[j].used?0u:(1u<<j)))
                return 0;
        }
    }
    if(!t3_psx_hit_test_pair(gate_facts,temp.gate))return 0;
    for(attacker=0u;attacker<2u;++attacker) {
        const t3_psx_hit_test_gate_t* g=&temp.gate[attacker];
        if(g->status!=T3_HIT_GATE_ACCEPT)continue;
        defender=attacker^1u;
        if(!attacks[attacker].move || !attacks[attacker].hit_facts ||
           attacks[attacker].test.attack_word !=
               attacks[attacker].move->attack_word ||
           g->hit_slot<0 || g->hit_slot>=2)return 0;
        slot=(unsigned)g->hit_slot;
        if(frames[defender].slots[slot].used)return 0;
        frames[defender].slots[slot].used=1u;
        frames[defender].slots[slot].attacker=(uint8_t)attacker;
        frames[defender].slots[slot].zone=(uint8_t)g->zone;
        frames[defender].slots[slot].move=attacks[attacker].move;
        frames[defender].slots[slot].facts=attacks[attacker].hit_facts;
    }
    if(!t3_psx_hit_apply_preview(&frames[0],&temp.defend[0]) ||
       !t3_psx_hit_apply_preview(&frames[1],&temp.defend[1]))
        return 0;
    *out=temp;
    return 1;
}
