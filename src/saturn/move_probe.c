#include "move_probe.h"
#if defined(T3_LOCAL_DIVMOT_ENABLED)
#include "t3_local_divmot.h"
#include "divmot.h"
#include "move_vm.h"

static t3_divmot_t g_bank, g_common;
static t3_move_vm_t g_vms[2];
static uint8_t g_active, g_round;
static unsigned g_start_slot;

sat_result_t t3_move_probe_init(void) {
    if (!t3_divmot_open(&g_bank,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES))
        return SAT_ERR_INVALID_ARG;
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
    if (!t3_divmot_open(&g_common,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES))
        return SAT_ERR_INVALID_ARG;
#endif
    /* Slot 3 is the ordinary Tekken standing stance. Fallback is useful for
     * synthetic tests, but must not be mistaken for authentic startup. */
    g_start_slot=3u;
    if (t3_divmot_slot(&g_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
          &g_common,
#else
          NULL,
#endif
          g_start_slot,&g_vms[0].move)!=1) g_start_slot=0u;
    g_active=1;
    t3_move_probe_reset(1u);
    return SAT_OK;
}

void t3_move_probe_reset(unsigned round) {
    unsigned i;
    if (!g_active) return;
    g_round=(uint8_t)round;
    for (i=0;i<2u;++i) {
        if (!t3_move_vm_start(&g_vms[i],&g_bank,
#if T3_LOCAL_DIVMOT_COMMON_BYTES > 0
                   &g_common,
#else
                   NULL,
#endif
                   g_start_slot)) g_active=0u;
    }
}

void t3_move_probe_tick(const t3_match_t* match) {
    unsigned i;
    if (!g_active || !match) return;
    if (g_round!=match->round) t3_move_probe_reset(match->round);
    if (match->round_done || match->match_done || !g_active) return;
    for (i=0;i<2u;++i)
        (void)t3_move_vm_tick(&g_vms[i],&match->input_histories[i]);
}

uint8_t t3_move_probe_active(void) {return g_active;}
uint16_t t3_move_probe_slot(unsigned i) {return i<2u && g_active?g_vms[i].slot:0;}
uint16_t t3_move_probe_frame(unsigned i) {return i<2u && g_active?g_vms[i].frame:0;}
uint16_t t3_move_probe_attack_word(unsigned i) {return i<2u && g_active?g_vms[i].move.attack_word:0;}
uint32_t t3_move_probe_transitions(unsigned i) {return i<2u && g_active?g_vms[i].transitions:0;}

#else
sat_result_t t3_move_probe_init(void) {return SAT_OK;}
void t3_move_probe_reset(unsigned round) {(void)round;}
void t3_move_probe_tick(const t3_match_t* match) {(void)match;}
uint8_t t3_move_probe_active(void) {return 0u;}
uint16_t t3_move_probe_slot(unsigned i) {(void)i;return 0u;}
uint16_t t3_move_probe_frame(unsigned i) {(void)i;return 0u;}
uint16_t t3_move_probe_attack_word(unsigned i) {(void)i;return 0u;}
uint32_t t3_move_probe_transitions(unsigned i) {(void)i;return 0u;}
#endif
