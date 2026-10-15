#include "psx_state_transition.h"

static int valid_state(const t3_psx_fighter_transition_state_t* s) {
    if(s->phase>T3_PSX_PHASE_KO ||
       s->move_slot>=T3_DIVMOT_MOVE_SLOTS)return 0;
    if(s->phase==T3_PSX_PHASE_NEUTRAL || s->phase==T3_PSX_PHASE_KO)
        return s->frames_left==0u;
    return s->frames_left>0u;
}
static int valid_facts(const t3_psx_transition_facts_t* f) {
    return f->reaction_timing_known<=1u &&
           f->recovery_known<=1u && f->neutral_known<=1u &&
           f->combo_known<=1u && f->increment_combo<=1u &&
           f->reset_combo_on_neutral<=1u &&
           (!f->recovery_known ||
              f->recovery_move_slot<T3_DIVMOT_MOVE_SLOTS) &&
           (!f->neutral_known ||
              f->neutral_move_slot<T3_DIVMOT_MOVE_SLOTS);
}
static int advance_without_hit(t3_psx_fighter_transition_state_t* s,
                               const t3_psx_transition_facts_t* f,
                               uint8_t* entered_recovery,
                               uint8_t* returned_neutral) {
    if(s->phase==T3_PSX_PHASE_NEUTRAL || s->phase==T3_PSX_PHASE_KO)
        return 1;
    if(s->frames_left>1u) {
        if(s->phase_frame==UINT16_MAX)return 0;
        ++s->phase_frame;
        --s->frames_left;
        return 1;
    }
    if(s->phase==T3_PSX_PHASE_RECOVERY) {
        if(!f->neutral_known)return 0;
        s->phase=T3_PSX_PHASE_NEUTRAL;
        s->move_slot=f->neutral_move_slot;
        s->phase_frame=0u;
        s->frames_left=0u;
        if(f->reset_combo_on_neutral) {
            if(!f->combo_known)return 0;
            s->combo_hits_received=0u;
        }
        *returned_neutral=1u;
        return 1;
    }
    if(!f->recovery_known || !f->recovery_frames)return 0;
    s->phase=T3_PSX_PHASE_RECOVERY;
    s->move_slot=f->recovery_move_slot;
    s->phase_frame=0u;
    s->frames_left=f->recovery_frames;
    *entered_recovery=1u;
    return 1;
}
int t3_psx_combat_transition_preview_pair(
    const t3_psx_combat_attack_t attacks[2],
    const t3_psx_hit_apply_frame_t defender_frames[2],
    const t3_psx_fighter_transition_state_t before[2],
    const t3_psx_transition_facts_t facts[2],
    t3_psx_transition_preview_t* out) {
    t3_psx_transition_preview_t tmp={0};
    unsigned i;
    if(!attacks || !defender_frames || !before || !facts || !out)
        return 0;
    for(i=0u;i<2u;++i) {
        if(!valid_state(&before[i]) || !valid_facts(&facts[i]) ||
           before[i].health_q16!=defender_frames[i].health_q16 ||
           before[i].health_q16<0 ||
           before[i].health_q16>defender_frames[i].max_health_q16)
            return 0;
    }
    if(!t3_psx_combat_preview_pair(attacks,defender_frames,&tmp.combat))
        return 0;
    for(i=0u;i<2u;++i) {
        const t3_psx_hit_apply_result_t* hit=&tmp.combat.defend[i];
        const t3_psx_transition_facts_t* f=&facts[i];
        t3_psx_fighter_transition_state_t* s=&tmp.fighter[i];
        unsigned accepted=hit->best_slot>=0;
        *s=before[i];
        s->health_q16=hit->health_q16;
        if(!s->health_q16) {
            s->phase=T3_PSX_PHASE_KO;
            s->phase_frame=0u;
            s->frames_left=0u;
            continue;
        }
        if(s->phase==T3_PSX_PHASE_KO)return 0;
        /* A classified contact cannot silently consume an unknown reaction. */
        if(accepted && !hit->reaction_known)return 0;
        if(accepted && hit->reaction.selected) {
            if(!f->reaction_timing_known || !f->reaction_frames ||
               f->reaction_phase<T3_PSX_PHASE_HITSTUN ||
               f->reaction_phase>T3_PSX_PHASE_KNOCKDOWN ||
               hit->reaction.move_slot>=T3_DIVMOT_MOVE_SLOTS)
                return 0;
            /* Explicit policy avoids inferring juggle/combo semantics. */
            if(!f->combo_known)return 0;
            if(f->increment_combo) {
                if(s->combo_hits_received==UINT16_MAX)return 0;
                ++s->combo_hits_received;
            }
            s->phase=f->reaction_phase;
            s->move_slot=hit->reaction.move_slot;
            s->phase_frame=0u;
            s->frames_left=f->reaction_frames;
            tmp.reaction_started[i]=1u;
        } else {
            if(!advance_without_hit(s,f,&tmp.entered_recovery[i],
                                    &tmp.returned_neutral[i]))return 0;
        }
    }
    *out=tmp;
    return 1;
}
