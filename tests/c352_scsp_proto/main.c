/* Experimental: plays a converted window of a Namco C352 song on the SCSP.
 * The sample bank is loaded into Sound RAM first, then the 68000 sound driver runs a register score: either the
 * 1024-event looping score (sat_sound_driver_score_load) or, with C352_PAGED, a long score in pages of 512
 * events that the SH-2 publishes whenever the driver has freed one (sat_sound_driver_page_publish). The SH-2
 * moves pages; it decodes nothing, keys nothing and does not call sat_audio_update. Data comes from
 * scripts/c352_scsp_convert.py (private).
 * g_c352_proto holds what scripts/run_c352_scsp_prototype.py reads back. */
#include <stdint.h>
#include "saturn/app.h"
#include "saturn/audio.h"
#include "saturn/color.h"
#include "saturn/font.h"
#include "saturn/sound_driver.h"
#include "saturn/video.h"
#include "score_meta.h"

#if C352_CD
#include "saturn/cd_mount.h"
#include "saturn/cdfs.h"
/* Bank and pages are read from the disc (BANK.BIN, PAGES.BIN); nothing but the program is in the image. */
static sat_cd_block_t g_cd;
static sat_cd_device_t g_cd_device;
static sat_cdfs_volume_t g_cd_volume;
static sat_cdfs_file_t g_pages_file;
static uint32_t g_pages_offset;
static uint8_t g_record[16 + 4096] __attribute__((aligned(4)));
static uint8_t g_record_valid;
static uint8_t g_stage[65536] __attribute__((aligned(4)));
static const uint8_t c352_bank[1];
static const uint8_t c352_pages[1];
static const sat_sound_score_event_t c352_score[1];
#else
__asm__(".section .rodata\n.balign 4\n"
        ".global c352_bank_asm\nc352_bank_asm:\n.incbin \"" C352_BANK_FILE "\"\n"
        ".balign 4\n.global c352_score_asm\nc352_score_asm:\n.incbin \"" C352_SCORE_FILE "\"\n"
        ".balign 4\n.global c352_pages_asm\nc352_pages_asm:\n.incbin \"" C352_PAGES_FILE "\"\n"
        ".previous\n");
extern const uint8_t c352_bank[] __asm__("c352_bank_asm");
extern const sat_sound_score_event_t c352_score[] __asm__("c352_score_asm");
extern const uint8_t c352_pages[] __asm__("c352_pages_asm");
#endif

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
    /* paged score */
    uint32_t pg_published, pg_played, pg_underrun_ticks, pg_late_events, pg_max_lateness, pg_bad_sequence;
    uint32_t pg_max_burst, pg_refused, pg_executed, pg_done, pg_base_tick, pg_tick_samples;
    uint32_t pg_abuse_result, pg_total_pages, pg_publish_attempts, pg_frame_done;
    uint32_t cd_mode, cd_status, cd_bank_read_ticks, cd_page_reads, cd_page_read_ticks_max, cd_page_read_ticks_total;
    uint32_t cd_bank_checksum_file, cd_bank_checksum_ram;
    int32_t pg_margin_ticks[16];        /* per page: ticks between its publication and its first event's time */
} c352_proto_results_t;

volatile c352_proto_results_t g_c352_proto;

static sat_ascii_font_t font;
static const uint8_t* g_page_cursor;
static uint32_t g_page_next;
static uint32_t g_page_fetched;

/* Publishes the next page if the driver has freed a slot. Returns 1 when one went out. */
#if C352_CD
/* Reads the next page record (16-byte header, then count events) into g_record. */
static void fetch_next(void) {
    uint32_t got = 0u;
    if (g_record_valid || g_page_fetched >= C352_PAGES) return;
    const uint32_t t0 = sat_sound_driver_tick();
    sat_result_t st = sat_cdfs_read_at(&g_cd_volume, &g_pages_file, g_pages_offset, g_record, 16u, &got);
    if (st == SAT_OK && got == 16u) {
        const uint32_t count = ((uint32_t)g_record[0] << 8) | g_record[1];
        st = sat_cdfs_read_at(&g_cd_volume, &g_pages_file, g_pages_offset + 16u, g_record + 16, count * 8u, &got);
        if (st == SAT_OK && got == count * 8u) {
            g_pages_offset += 16u + count * 8u;
            g_record_valid = 1u;
            ++g_page_fetched;
        }
    }
    g_c352_proto.cd_status |= (uint32_t)(-st);
    const uint32_t dt = sat_sound_driver_tick() - t0;
    ++g_c352_proto.cd_page_reads;
    g_c352_proto.cd_page_read_ticks_total += dt;
    if (dt > g_c352_proto.cd_page_read_ticks_max) g_c352_proto.cd_page_read_ticks_max = dt;
}
#endif

static uint32_t publish_next(const sat_sound_pages_info_t* info) {
#if C352_CD
    if (g_page_next >= C352_PAGES || info->pages_free == 0u || !g_record_valid) return 0u;
    const uint8_t* h = g_record;
#else
    if (g_page_next >= C352_PAGES || info->pages_free == 0u) return 0u;
    const uint8_t* h = g_page_cursor;
#endif
    const uint16_t count = (uint16_t)((h[0] << 8) | h[1]);
    const uint16_t flags = (uint16_t)((h[2] << 8) | h[3]);
    const uint32_t time_base = ((uint32_t)h[4] << 24) | ((uint32_t)h[5] << 16) | ((uint32_t)h[6] << 8) | h[7];
    const uint32_t end = ((uint32_t)h[8] << 24) | ((uint32_t)h[9] << 16) | ((uint32_t)h[10] << 8) | h[11];
    const sat_sound_score_event_t* events = (const sat_sound_score_event_t*)(h + 16);
    ++g_c352_proto.pg_publish_attempts;
    if (sat_sound_driver_page_publish(events, count, time_base, end, (flags & 0x8000u) != 0u) != SAT_OK) return 0u;
    if (g_page_next < 16u) {
        g_c352_proto.pg_margin_ticks[g_page_next] =
            (int32_t)(info->base_tick + time_base + events[0].tick) - (int32_t)sat_sound_driver_tick();
    }
    g_page_cursor = h + 16 + 8u * count;
    ++g_page_next;
#if C352_CD
    g_record_valid = 0u;
#endif
    return 1u;
}

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
#if C352_CD
        {
            /* Mount the disc and load the bank from BANK.BIN in consecutive sounds, 64 KiB at a time. */
            sat_cdfs_file_t bank_file;
            uint32_t sum_file = 0u, sum_ram = 0u;
            const uint32_t t_begin = sat_sound_driver_tick();
            g_c352_proto.cd_mode = 1u;
            g_c352_proto.cd_status = (uint32_t)(-sat_cd_mount_default(&g_cd, 0, 0, &g_cd_device, 0u, &g_cd_volume));
            if (g_c352_proto.cd_status == 0u) g_c352_proto.cd_status = (uint32_t)(-sat_cdfs_lookup(&g_cd_volume, "BANK.BIN", &bank_file));
            if (g_c352_proto.cd_status == 0u) g_c352_proto.cd_status = (uint32_t)(-sat_cdfs_lookup(&g_cd_volume, "PAGES.BIN", &g_pages_file));
            for (uint32_t offset = 0u; g_c352_proto.cd_status == 0u && offset < C352_BANK_BYTES; offset += CHUNK_BYTES) {
                sat_sound_t sound = {0};
                sat_sound_desc_t desc = {0};
                uint32_t got = 0u;
                const uint32_t bytes = C352_BANK_BYTES - offset < CHUNK_BYTES ? C352_BANK_BYTES - offset : CHUNK_BYTES;
                g_c352_proto.cd_status |= (uint32_t)(-sat_cdfs_read_at(&g_cd_volume, &bank_file, offset, g_stage, bytes, &got));
                if (got != bytes) { g_c352_proto.cd_status |= 1u << 8; break; }
                for (uint32_t w = 0u; w < bytes / 2u; ++w) sum_file += (uint32_t)((g_stage[2u * w] << 8) | g_stage[2u * w + 1u]);
                desc.samples = g_stage;
                desc.sample_count = bytes;
                desc.sample_rate = 44100u;
                desc.format = SAT_AUDIO_PCM_S8;
                g_c352_proto.create_status |= (uint32_t)(-sat_sound_create(&sound, &desc));
                ++created;
            }
            for (uint32_t w = 0u; w < C352_BANK_BYTES / 2u; ++w) sum_ram += SOUND_RAM[C352_BANK_BASE / 2u + w];
            g_c352_proto.cd_bank_checksum_file = sum_file;
            g_c352_proto.cd_bank_checksum_ram = sum_ram;
            g_c352_proto.bank_mismatch_words = sum_file == sum_ram ? 0u : 1u;
            g_c352_proto.cd_bank_read_ticks = sat_sound_driver_tick() - t_begin;
            g_c352_proto.chunks = created;
            if (g_c352_proto.cd_status != 0u) g_c352_proto.create_status |= 1u << 8;
        }
#else
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
#endif
#if C352_PAGED
        if (g_c352_proto.create_status == 0u) {
            sat_sound_pages_info_t pinfo = {0};
            g_c352_proto.pg_total_pages = C352_PAGES;
#if C352_CD
            fetch_next();                               /* page 0 is in hand before the stream's base is set */
#endif
            g_c352_proto.tick_before_load = sat_sound_driver_tick();
            g_c352_proto.score_status = (uint32_t)(-sat_sound_driver_pages_start(C352_TICK_SAMPLES, C352_LEAD_TICKS));
            g_page_cursor = c352_pages;
            for (uint32_t k = 0u; k < 2u && g_c352_proto.score_status == 0u; ++k) {
                (void)sat_sound_driver_pages_info(&pinfo);
                (void)publish_next(&pinfo);
#if C352_CD
                fetch_next();
#endif
            }
            g_c352_proto.tick_after_load = sat_sound_driver_tick();
        }
#else
        if (g_c352_proto.create_status == 0u) {
            g_c352_proto.tick_before_load = sat_sound_driver_tick();
            g_c352_proto.score_status = (uint32_t)(-sat_sound_driver_score_load(c352_score, C352_SCORE_EVENTS, C352_LOOP_TICKS));
            g_c352_proto.tick_after_load = sat_sound_driver_tick();
        }
#endif
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
#if C352_PAGED
        {
            sat_sound_pages_info_t pinfo = {0};
            if (sat_sound_driver_pages_info(&pinfo) == SAT_OK) {
                if (publish_next(&pinfo) != 0u && sat_sound_driver_pages_info(&pinfo) == SAT_OK) (void)publish_next(&pinfo);
#if C352_CD
                fetch_next();                           /* prefetch the next page while the 68000 plays */
#endif
                if (g_c352_proto.frames == 200u && pinfo.pages_free == 0u && g_page_next < C352_PAGES) {
                    /* a deliberate publish into a page still in use: it must be refused */
                    const uint8_t* h = g_page_cursor;
                    g_c352_proto.pg_abuse_result = (uint32_t)(-sat_sound_driver_page_publish(
                        (const sat_sound_score_event_t*)(h + 16), 1u, 0u, 1u, 0u));
                }
                g_c352_proto.pg_published = pinfo.pages_published;
                g_c352_proto.pg_played = pinfo.pages_played;
                g_c352_proto.pg_underrun_ticks = pinfo.underrun_ticks;
                g_c352_proto.pg_late_events = pinfo.late_events;
                g_c352_proto.pg_max_lateness = pinfo.max_lateness_ticks;
                g_c352_proto.pg_bad_sequence = pinfo.bad_sequence;
                g_c352_proto.pg_max_burst = pinfo.max_burst;
                g_c352_proto.pg_refused = pinfo.refused;
                g_c352_proto.pg_executed = pinfo.executed;
                g_c352_proto.pg_base_tick = pinfo.base_tick;
                g_c352_proto.pg_tick_samples = pinfo.tick_samples;
                if (pinfo.done && g_c352_proto.pg_done == 0u) {
                    g_c352_proto.pg_done = 1u;
                    g_c352_proto.pg_frame_done = g_c352_proto.frames;
                }
            }
        }
#endif
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
