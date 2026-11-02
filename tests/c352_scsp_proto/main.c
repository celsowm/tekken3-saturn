/* Experimental: plays a converted window of a Namco C352 song on the SCSP.
 * The sample bank is loaded into Sound RAM first, then the 68000 sound driver runs a register score
 * (sat_sound_driver_score_load). After that the SH-2 only draws the status lines: it decodes nothing, keys
 * nothing and does not call sat_audio_update. Data comes from scripts/c352_scsp_convert.py (private).
 * g_c352_proto holds what scripts/run_c352_scsp_prototype.py reads back. */
#include <stdint.h>
#include "saturn/app.h"
#include "saturn/audio.h"
#include "saturn/color.h"
#include "saturn/font.h"
#include "saturn/sound_driver.h"
#include "saturn/video.h"
#include "score_meta.h"

__asm__(".section .rodata\n.balign 4\n"
        ".global c352_bank_asm\nc352_bank_asm:\n.incbin \"" C352_BANK_FILE "\"\n"
        ".balign 4\n.global c352_score_asm\nc352_score_asm:\n.incbin \"" C352_SCORE_FILE "\"\n"
        ".previous\n");
extern const uint8_t c352_bank[] __asm__("c352_bank_asm");
extern const sat_sound_score_event_t c352_score[] __asm__("c352_score_asm");

#define PROTO_MAGIC SAT_FOURCC('C', '3', 'S', '1')
#define CHUNK_BYTES 65534u              /* sat_sound_create takes up to 65535 samples */
#define SOUND_RAM ((volatile uint16_t*)0x25A00000u)
#define SLOT_REGS ((volatile uint16_t*)0x25B00000u)
#define SLOTS 32u

typedef struct c352_proto_results {
    uint32_t magic;
    uint32_t audio_status, driver_status, create_status, score_status;
    uint32_t chunks;
    uint32_t bank_bytes, bank_base, bank_mismatch_words;
    uint32_t sound_ram_used, sound_ram_capacity, sound_ram_high_water, resident_sounds, failed_plays, voice_steals;
    uint32_t score_events, loop_ticks, window_ms;
    uint32_t tick_before_load, tick_after_load;
    uint32_t frames;
    uint32_t tick_at_frame_a, frame_a, tick_at_frame_b, frame_b;
    uint32_t heartbeat, executed, score_executed, score_loops, max_lateness, last_lateness, queued;
    uint32_t active_voices_during_play, sound_ram_used_during_play;
} c352_proto_results_t;

volatile c352_proto_results_t g_c352_proto;

static sat_ascii_font_t font;

static void line(const char* label, uint32_t value, int y) {
    (void)sat_ascii_font_draw_label_u32(&font, label, value, 8, y, 8, 0u, 0u);
}

/* Slots start silent; the score changes only what differs per note (address, loop, level, pan, pitch, key). */
static void slots_init(void) {
    for (uint32_t s = 0u; s < SLOTS; ++s) {
        volatile uint16_t* r = SLOT_REGS + s * 0x10u;
        r[0x00 / 2] = 0u;
        r[0x08 / 2] = 0x001Fu;          /* AR 31: attacks at once */
        r[0x0A / 2] = 0x3C1Fu;          /* RR 31, no key-rate scaling */
        r[0x0C / 2] = 0x00FFu;
        r[0x0E / 2] = 0u;
        r[0x10 / 2] = 0u;
        r[0x12 / 2] = 0u;
        r[0x14 / 2] = 0u;               /* no DSP send */
        r[0x16 / 2] = 0u;               /* DISDL 0: muted until the score sets it */
    }
    SLOT_REGS[0] = 0x1000u;             /* KYONEX: every slot to key-off */
}

int main(void) {
    sat_sound_driver_info_t info = {0};
    sat_audio_stats_t stats = {0};
    uint32_t created = 0u;

    if (sat_app_init_default() != SAT_OK) return 1;
    if (sat_ascii_font_init_8x8_indexed8(&font, SAT_COLOR_WHITE, SAT_COLOR_BLACK, 7u) != SAT_OK) return 1;
    g_c352_proto.magic = PROTO_MAGIC;
    g_c352_proto.bank_bytes = C352_BANK_BYTES;
    g_c352_proto.bank_base = C352_BANK_BASE;
    g_c352_proto.score_events = C352_SCORE_EVENTS;
    g_c352_proto.loop_ticks = C352_LOOP_TICKS;
    g_c352_proto.window_ms = C352_WINDOW_MS;

    g_c352_proto.audio_status = (uint32_t)(-sat_audio_init());
    if (g_c352_proto.audio_status == 0u) g_c352_proto.driver_status = (uint32_t)(-sat_sound_driver_start());
    if (g_c352_proto.audio_status == 0u && g_c352_proto.driver_status == 0u) {
        slots_init();
        /* The bank in consecutive sounds: the first fit starts at C352_BANK_BASE and nothing is freed. */
        for (uint32_t offset = 0u; offset < C352_BANK_BYTES; offset += CHUNK_BYTES) {
            sat_sound_t sound = {0};
            sat_sound_desc_t desc = {0};
            const uint32_t bytes = C352_BANK_BYTES - offset < CHUNK_BYTES ? C352_BANK_BYTES - offset : CHUNK_BYTES;
            desc.samples = c352_bank + offset;
            desc.sample_count = bytes;
            desc.sample_rate = 44100u;
            desc.format = SAT_AUDIO_PCM_S8;
            g_c352_proto.create_status |= (uint32_t)(-sat_sound_create(&sound, &desc));
            ++created;
        }
        g_c352_proto.chunks = created;
        for (uint32_t w = 0u; w < C352_BANK_BYTES / 2u; ++w) {
            const uint16_t want = (uint16_t)((c352_bank[2u * w] << 8) | c352_bank[2u * w + 1u]);
            if (SOUND_RAM[C352_BANK_BASE / 2u + w] != want) ++g_c352_proto.bank_mismatch_words;
        }
        if (g_c352_proto.create_status == 0u) {
            g_c352_proto.tick_before_load = sat_sound_driver_tick();
            g_c352_proto.score_status = (uint32_t)(-sat_sound_driver_score_load(c352_score, C352_SCORE_EVENTS, C352_LOOP_TICKS));
            g_c352_proto.tick_after_load = sat_sound_driver_tick();
        }
        if (sat_audio_get_stats(&stats) == SAT_OK) {
            g_c352_proto.sound_ram_used = stats.sound_ram_used;
            g_c352_proto.sound_ram_capacity = stats.sound_ram_capacity;
            g_c352_proto.sound_ram_high_water = stats.sound_ram_high_water;
            g_c352_proto.resident_sounds = stats.resident_sounds;
            g_c352_proto.failed_plays = stats.failed_play_requests;
            g_c352_proto.voice_steals = stats.voice_steals;
        }
    }

    for (;;) {
        sat_pad_state_t pad = {0};
        if (sat_app_frame_begin(SAT_COLOR_BLACK, SAT_COLOR_BLACK, &pad) != SAT_OK) break;
        ++g_c352_proto.frames;
        if (g_c352_proto.frames == 60u) {
            g_c352_proto.tick_at_frame_a = sat_sound_driver_tick();
            g_c352_proto.frame_a = g_c352_proto.frames;
        }
        if (g_c352_proto.frames == 300u && sat_audio_get_stats(&stats) == SAT_OK) {
            /* mid-playback: the SH-2 owns no voice (the 68000 keys the slots itself) and calls no audio update */
            g_c352_proto.active_voices_during_play = stats.active_voices;
            g_c352_proto.sound_ram_used_during_play = stats.sound_ram_used;
        }
        if (g_c352_proto.frames == 600u) {
            g_c352_proto.tick_at_frame_b = sat_sound_driver_tick();
            g_c352_proto.frame_b = g_c352_proto.frames;
        }
        if ((g_c352_proto.frames % 30u) == 0u && sat_sound_driver_info(&info) == SAT_OK) {
            g_c352_proto.heartbeat = info.heartbeat;
            g_c352_proto.executed = info.executed;
            g_c352_proto.score_executed = info.score_executed;
            g_c352_proto.score_loops = info.score_loops;
            g_c352_proto.max_lateness = info.max_lateness_ticks;
            g_c352_proto.last_lateness = info.last_lateness_ticks;
            g_c352_proto.queued = info.queued;
        }
        (void)sat_ascii_font_draw_text_screen_indexed8(&font, "C352 TO SCSP", 8, 8, 8, 0u, 0u);
        line("EVENTS ", g_c352_proto.score_executed, 24);
        line("PASSES ", g_c352_proto.score_loops, 40);
        line("LATE ", g_c352_proto.max_lateness, 56);
        line("BYTES ", g_c352_proto.bank_bytes, 72);
        (void)sat_app_frame_end();
        if ((pad.pressed & SAT_PAD_START) != 0u) break;
    }
    (void)sat_sound_driver_stop();
    (void)sat_shutdown();
    return 0;
}
