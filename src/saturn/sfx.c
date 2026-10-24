#include "sfx.h"

#if defined(T3_SFX)
#include <saturn/audio.h>
#include <saturn/cdfs.h>
#include <saturn/time.h>
#include "disc.h"
#include "combat_events.h"
#include "move_probe.h"
#include "t3_local_sfx.h"

/* Sound codes and rates: Tekken3Remake docs/research/code/sound.md
 * (PolyForm Noncommercial; the rules are re-implemented, not copied). */
static const struct {uint32_t offset,samples,rate;} g_table[T3_SFX_COUNT]={T3_SFX_TABLE};
static sat_sound_t g_sound[T3_SFX_COUNT];
static uint8_t g_ready;
/* Dumped by scripts/saturn_probe.py. */
volatile uint32_t g_t3_sfx_played,g_t3_sfx_play_ms,g_t3_sfx_play_max_ms,g_t3_sfx_update_ms,g_t3_sfx_ko_voices;

sat_result_t t3_sfx_init(void* scratch, uint32_t scratch_bytes) {
    sat_cdfs_volume_t* volume;sat_cdfs_file_t file;sat_result_t st;
    uint32_t window=0,window_bytes=0;
    if(scratch_bytes<T3_SFX_MAX_SAMPLES)return SAT_ERR_INVALID_ARG;
    if((st=t3_disc_volume(&volume))!=SAT_OK)return st;
    if((st=sat_cdfs_lookup(volume,"SFX.BIN",&file))!=SAT_OK)return st;
    if(file.size!=T3_SFX_FILE_BYTES)return SAT_ERR_VERSION;
    if((st=sat_audio_init())!=SAT_OK)return st;
    for(unsigned i=0;i<T3_SFX_COUNT;++i) {
        /* The samples are stored in order: one large read serves several. */
        if(!window_bytes || g_table[i].offset<window ||
           g_table[i].offset+g_table[i].samples>window+window_bytes) {
            uint32_t want=T3_SFX_FILE_BYTES-g_table[i].offset,read=0;
            if(want>scratch_bytes)want=scratch_bytes;
            if((st=sat_cdfs_read_at(volume,&file,g_table[i].offset,scratch,want,&read))!=SAT_OK)return st;
            if(read!=want)return SAT_ERR_IO;
            window=g_table[i].offset;window_bytes=want;
        }
        const sat_sound_desc_t desc={(const uint8_t*)scratch+(g_table[i].offset-window),g_table[i].samples,
                                     g_table[i].rate,0,0,SAT_AUDIO_PCM_S8,0,0,0};
        if((st=sat_sound_create(&g_sound[i],&desc))!=SAT_OK)return st;
    }
    g_ready=1u;
    return SAT_OK;
}

static void play(unsigned id) {
    const sat_sound_play_params_t params={SAT_AUDIO_VOLUME_MAX,SAT_AUDIO_PAN_CENTER,1u,0u,SAT_FX16_ONE,0u};
    const uint32_t started=sat_time_ms();
    if(sat_sound_play(g_sound[id],&params,NULL)==SAT_OK)++g_t3_sfx_played;
    const uint32_t spent=sat_time_ms()-started;
    g_t3_sfx_play_ms+=spent;
    if(spent>g_t3_sfx_play_max_ms)g_t3_sfx_play_max_ms=spent;
}

/* Voices: a hurt voice for a hit (not a block) with a 20-image cooldown (the
 * original waits 40 frames), and a shout on one swing in four. The match does
 * not carry the original's random draws, so the picks cycle. */
static uint8_t g_hurt_wait[2],g_hurt_next[2],g_shout_next[2],g_swings;
void t3_sfx_play_events(uint8_t events) {
    if(!g_ready)return;
    for(unsigned i=0;i<2u;++i) {
        const uint8_t mine=(uint8_t)(events>>(T3_EVENT_BITS*i));
        if(g_hurt_wait[i])--g_hurt_wait[i];
        if(mine&T3_EVENT_SWING) {
            play(T3_SFX_SWING);
            if((++g_swings&3u)==0u)play(i?T3_SFX_F1_SHOUT0+(g_shout_next[i]++&1u):T3_SFX_F0_SHOUT0+(g_shout_next[i]++&1u));
        }
        /* The attacker's descriptor joint picks the block sound: below 12 is a hand. */
        if(mine&T3_EVENT_BLOCK)
            play(t3_move_probe_owned_value(1u-i,26u)<12?T3_SFX_GUARD_HAND:T3_SFX_GUARD_FOOT);
        else if(mine&T3_EVENT_HIT) {
            play(T3_SFX_HIT);
            if(!g_hurt_wait[i]) {
                const unsigned pick=g_hurt_next[i]++%3u;
                play(i?T3_SFX_F1_HURT0+pick:T3_SFX_F0_HURT0+pick);
                g_hurt_wait[i]=20u;
            }
        }
    }
    const uint32_t updated=sat_time_ms();
    (void)sat_audio_update();
    g_t3_sfx_update_ms+=sat_time_ms()-updated;
}

/* The round announcer (FUN_8003DCE0): "round" or "final round" at frame 61 of the
 * first round (frame 1 of later ones), the number at 100 / 40 unless final, and
 * "fight" when the intro ends. */
static int32_t g_prev_state=-1,g_prev_frame;
static uint8_t g_prev_down[2];
void t3_sfx_round_update(void) {
    if(!g_ready)return;
    const int32_t state=t3_move_probe_round_value(1),frame=t3_move_probe_round_value(11);
    const int32_t played=t3_move_probe_round_value(10);
    const int final=t3_move_probe_round_value(12)!=0;
    if(state==0) {
        const int first=played==1;
        const int32_t before=g_prev_state==0?g_prev_frame:0;
        if(before<(first?61:1) && frame>=(first?61:1))play(final?T3_SFX_ANN_FINAL:T3_SFX_ANN_ROUND);
        if(!final && played>=1 && played<=3 && before<(first?100:40) && frame>=(first?100:40))
            play(T3_SFX_ANN_ONE+(unsigned)(played-1));
    } else if(state==1 && (g_prev_state==0 ||
              /* Round 1 has no intro: "fight" at the match's start or after a reset. */
              (played==1 && frame<=40 && (g_prev_state!=1 || frame<g_prev_frame))))play(T3_SFX_ANN_FIGHT);
    /* The loser's KO voice (category 0 of g_charVoices) as the collapse begins. */
    for(unsigned i=0;i<2u;++i) {
        const uint8_t down=t3_move_probe_owned_value(i,25u)!=0;
        if(down && !g_prev_down[i]) {play(i?T3_SFX_F1_KO0:T3_SFX_F0_KO0);++g_t3_sfx_ko_voices;}
        g_prev_down[i]=down;
    }
    g_prev_state=state;g_prev_frame=frame;
}

uint32_t t3_sfx_played(void) { return g_t3_sfx_played; }
#endif
