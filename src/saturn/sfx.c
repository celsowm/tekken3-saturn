#include "sfx.h"

#if defined(T3_SFX)
#include <saturn/audio.h>
#include <saturn/cdfs.h>
#include <saturn/time.h>
#include "disc.h"
#include "combat_events.h"
#include "psx_fighter_sounds.h"
#include "move_probe.h"
#include "t3_local_sfx.h"
#if defined(T3_FIGHTER_PACKS)
#include "fighter_pack.h"
#endif

/* Sound codes and rates: Tekken3Remake docs/research/code/sound.md
 * (PolyForm Noncommercial; the rules are re-implemented, not copied). */
static const struct {uint32_t offset,samples,rate;} g_table[T3_SFX_COUNT]={T3_SFX_TABLE};
static const struct {uint8_t bank,slot;uint16_t code;} g_key[T3_SFX_COUNT]={T3_SFX_KEY_TABLE};
static sat_sound_t g_sound[T3_SFX_COUNT];
#if defined(T3_FIGHTER_PACKS)
/* Each fighter's voices come from its pack; a mirror match shares one set. */
static sat_sound_t g_voice[2][T3_ROSTER_VOICES];
static uint16_t g_voice_code[2][T3_ROSTER_VOICES];
static uint8_t g_voice_count[2];
#endif
static uint8_t g_ready;
/* Dumped by scripts/saturn_probe.py. */
/* Sound requests with no extracted sample, and the first distinct ones as
 * (kind<<24 | player<<16 | code). Dumped by scripts/saturn_probe.py. */
volatile uint32_t g_t3_sfx_unmapped,g_t3_sfx_unmapped_count,g_t3_sfx_unmapped_seen[16];
volatile uint32_t g_t3_sfx_played,g_t3_sfx_play_ms,g_t3_sfx_play_max_ms,g_t3_sfx_update_ms,g_t3_sfx_ko_voices;

#if defined(T3_FIGHTER_PACKS)
_Static_assert(T3_ROSTER_MAX_VOICE_SAMPLES>0u,"voices");
static sat_result_t load_voices(unsigned actor,void* scratch,uint32_t scratch_bytes) {
    /* Rows are big-endian words: code, PCM offset, samples, rate. */
    uint32_t rows[T3_ROSTER_VOICES][4];
    const t3_fighter_pack_t* pack=t3_fighter_pack(actor);
    const uint32_t pcm_bytes=t3_fighter_pack_section_bytes(actor,T3_PACK_PCM);
    uint32_t window=0,window_bytes=0;
    SAT_TRY(t3_fighter_pack_read(actor,T3_PACK_VOICES,0,rows,pack->voice_count*sizeof rows[0]));
    for(unsigned i=0;i<pack->voice_count;++i) {
        if(!window_bytes || rows[i][1]<window || rows[i][1]+rows[i][2]>window+window_bytes) {
            uint32_t want=pcm_bytes-rows[i][1];
            if(want>scratch_bytes)want=scratch_bytes;
            if(rows[i][2]>want)return SAT_ERR_CAPACITY;
            SAT_TRY(t3_fighter_pack_read(actor,T3_PACK_PCM,rows[i][1],scratch,want));
            window=rows[i][1];window_bytes=want;
        }
        const sat_sound_desc_t desc={(const uint8_t*)scratch+(rows[i][1]-window),rows[i][2],
                                     rows[i][3],0,0,SAT_AUDIO_PCM_S8,0,0,0};
        SAT_TRY(sat_sound_create(&g_voice[actor][i],&desc));
        g_voice_code[actor][i]=(uint16_t)rows[i][0];
    }
    g_voice_count[actor]=(uint8_t)pack->voice_count;
    return SAT_OK;
}
#endif

sat_result_t t3_sfx_init(void* scratch, uint32_t scratch_bytes) {
    sat_cdfs_volume_t* volume;sat_cdfs_file_t file;sat_result_t st;
    uint32_t window=0,window_bytes=0;
    if(scratch_bytes<T3_SFX_MAX_SAMPLES)return SAT_ERR_INVALID_ARG;
#if defined(T3_FIGHTER_PACKS)
    if(scratch_bytes<T3_ROSTER_MAX_VOICE_SAMPLES)return SAT_ERR_INVALID_ARG;
#endif
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
#if defined(T3_FIGHTER_PACKS)
    SAT_TRY(load_voices(0,scratch,scratch_bytes));
    if(t3_fighter_pack(0)->id==t3_fighter_pack(1)->id) {
        g_voice_count[1]=g_voice_count[0];
        for(unsigned i=0;i<g_voice_count[0];++i) {
            g_voice[1][i]=g_voice[0][i];g_voice_code[1][i]=g_voice_code[0][i];
        }
    } else SAT_TRY(load_voices(1,scratch,scratch_bytes));
#endif
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

/* Voices: a hurt voice for a hit (not a block) with the original's 40-tick
 * cooldown, and a shout on one swing in four. The match does not carry the
 * original's random draws, so the picks cycle. Events arrive in tick order. */
static uint32_t g_hurt_tick[2];
static uint8_t g_hurt_set[2],g_hurt_next[2],g_shout_next[2],g_swings;
/* A match sound request: the sample extracted for that exact original code, or
 * counted as unmapped (no approximation). Kinds: T3_SOUND_SYSTEM/VOICE/EFFECT. */
static void play_request(const t3_combat_event_t* e) {
    unsigned bank;
    if(e->sound==T3_SOUND_EFFECT)bank=0u;
    else if(e->sound==T3_SOUND_SYSTEM)bank=1u;
    else if(e->sound==T3_SOUND_VOICE)bank=2u;
    else return; /* voice-off: nothing is playing a script voice here */
#if defined(T3_FIGHTER_PACKS)
    if(bank==2u && e->fighter<2u) {
        for(unsigned i=0;i<g_voice_count[e->fighter];++i)
            if(g_voice_code[e->fighter][i]==e->code) {
                const sat_sound_play_params_t params={SAT_AUDIO_VOLUME_MAX,SAT_AUDIO_PAN_CENTER,1u,0u,SAT_FX16_ONE,0u};
                if(sat_sound_play(g_voice[e->fighter][i],&params,NULL)==SAT_OK)++g_t3_sfx_played;
                return;
            }
    }
#endif
    for(unsigned i=0;i<T3_SFX_COUNT;++i)
        if(g_key[i].bank==bank &&
           (bank==0u ? (e->code>>12 && (g_key[i].code&0xfffu)==(e->code&0xfffu)) : g_key[i].code==e->code) &&
           (bank!=2u || g_key[i].slot==e->fighter)) {
            play(i);return;
        }
    const uint32_t key=((uint32_t)e->sound<<24)|((uint32_t)e->fighter<<16)|e->code;
    unsigned n=g_t3_sfx_unmapped_count;
    ++g_t3_sfx_unmapped;
    for(unsigned k=0;k<n;++k)if(g_t3_sfx_unmapped_seen[k]==key)return;
    if(n<16u){g_t3_sfx_unmapped_seen[n]=key;g_t3_sfx_unmapped_count=n+1u;}
}

void t3_sfx_play_events(const t3_combat_event_t* events,unsigned count) {
    if(!g_ready)return;
    const int connected=t3_combat_events_sound_connected();
    for(unsigned n=0;n<count;++n) {
        const t3_combat_event_t* e=&events[n];
        const unsigned i=e->fighter;
        if(e->kind==T3_EVENT_SOUND) {play_request(e);continue;}
#if defined(T3_FIGHTER_PACKS)
        /* The original sound decisions are always connected in a pack build. */
        (void)connected;(void)i;
        continue;
#else
        if(connected)continue; /* the original decisions arrive as T3_EVENT_SOUND */
        if(e->kind==T3_EVENT_SWING) {
            play(T3_SFX_SWING);
            if((++g_swings&3u)==0u)play(i?T3_SFX_F1_SHOUT0+(g_shout_next[i]++&1u):T3_SFX_F0_SHOUT0+(g_shout_next[i]++&1u));
        } else if(e->kind==T3_EVENT_BLOCK) {
            /* The attacker's descriptor joint picks the block sound: below 12 is a hand. */
            play(e->joint<12?T3_SFX_GUARD_HAND:T3_SFX_GUARD_FOOT);
        } else if(e->kind==T3_EVENT_HIT) {
            play(T3_SFX_HIT);
            /* A tick count below the last voice means the match restarted. */
            const int ready=!g_hurt_set[i] || e->tick<g_hurt_tick[i] || e->tick-g_hurt_tick[i]>=40u;
            if(ready) {
                const unsigned pick=g_hurt_next[i]++%3u;
                play(i?T3_SFX_F1_HURT0+pick:T3_SFX_F0_HURT0+pick);
                g_hurt_tick[i]=e->tick;g_hurt_set[i]=1u;
            }
        }
#endif
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
#if !defined(T3_FIGHTER_PACKS)
    /* The loser's KO voice (category 0 of g_charVoices) as the collapse begins. */
    for(unsigned i=0;i<2u;++i) {
        const uint8_t down=t3_move_probe_owned_value(i,25u)!=0;
        if(down && !g_prev_down[i] && !t3_combat_events_sound_connected()) {play(i?T3_SFX_F1_KO0:T3_SFX_F0_KO0);++g_t3_sfx_ko_voices;}
        g_prev_down[i]=down;
    }
#endif
    g_prev_state=state;g_prev_frame=frame;
}

uint32_t t3_sfx_played(void) { return g_t3_sfx_played; }
#endif
