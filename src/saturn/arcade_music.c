#include "arcade_music.h"

#include "saturn/audio.h"
#include "saturn/sound_driver.h"
#include "saturn/time.h"

#define MUSIC_MAGIC 0x4D555331u        /* MUS1 */
#define META_MAGIC 0x43334D31u         /* C3M1 */
#define STAGE_BYTES 65536u
#define MAX_LOADS 320u
#ifndef LOAD_SLICE_BYTES
#define LOAD_SLICE_BYTES 65536u        /* the CD read latency dominates (about 100 ms a call on Ymir), so fewer calls stall less */
#endif
#define PAGE_HEADER 16u
#define SLOT_REGS ((volatile uint16_t*)0x25B00000u)
#define SOUND_RAM ((volatile uint16_t*)0x25A00000u)

typedef struct load_record { int32_t load_after, needed_by; uint32_t address, file_offset, bytes, checksum; } load_record_t;

volatile t3_music_diag_t g_t3_music;

static sat_cdfs_volume_t* g_volume;
static sat_cdfs_file_t g_bank_file, g_pages_file;
static load_record_t g_loads[MAX_LOADS];
static uint8_t g_stage[STAGE_BYTES] __attribute__((aligned(4)));
static uint8_t g_record[PAGE_HEADER + 8u * SAT_SOUND_DRIVER_PAGE_EVENTS] __attribute__((aligned(4)));
static uint8_t g_record_valid;
static uint32_t g_pages_offset, g_page_fetched, g_page_next, g_load_next, g_region, g_slot_mask, g_running;

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }

static void fail(uint32_t step, sat_result_t status) {
    g_t3_music.state = T3_MUSIC_FAILED;
    g_t3_music.step = step;
    g_t3_music.status = (uint32_t)(-status);
}

static void sram_stats(void) {
    sat_audio_stats_t st;
    if (sat_audio_get_stats(&st) != SAT_OK) return;
    g_t3_music.sram_used = st.sound_ram_used;
    g_t3_music.sram_capacity = st.sound_ram_capacity;
    g_t3_music.sram_high_water = st.sound_ram_high_water;
    g_t3_music.resident_sounds = st.resident_sounds;
    g_t3_music.voice_steals = st.voice_steals;
    g_t3_music.failed_plays = st.failed_play_requests;
    if (st.active_voices > g_t3_music.sh2_voices_max) g_t3_music.sh2_voices_max = st.active_voices;
}

/* Reads the next page record (16-byte header, then `count` events) into g_record. */
static void fetch_next(void) {
    uint32_t got = 0u;
    if (g_record_valid || g_page_fetched >= g_t3_music.meta.pages) return;
    const uint32_t t0 = sat_sound_driver_tick();
    sat_result_t st = sat_cdfs_read_at(g_volume, &g_pages_file, g_pages_offset, g_record, PAGE_HEADER, &got);
    if (st == SAT_OK && got == PAGE_HEADER) {
        const uint32_t count = ((uint32_t)g_record[0] << 8) | g_record[1];
        if (count == 0u || count > SAT_SOUND_DRIVER_PAGE_EVENTS) st = SAT_ERR_INVALID_ARG;
        else st = sat_cdfs_read_at(g_volume, &g_pages_file, g_pages_offset + PAGE_HEADER, g_record + PAGE_HEADER, count * 8u, &got);
        if (st == SAT_OK && got == count * 8u) {
            g_pages_offset += PAGE_HEADER + count * 8u;
            g_record_valid = 1u;
            ++g_page_fetched;
        }
    } else if (st == SAT_OK) st = SAT_ERR_IO;
    if (st != SAT_OK || !g_record_valid) ++g_t3_music.page_read_failures;
    const uint32_t dt = sat_sound_driver_tick() - t0;
    ++g_t3_music.page_reads;
    g_t3_music.page_read_ticks_total += dt;
    if (dt > g_t3_music.page_read_ticks_max) g_t3_music.page_read_ticks_max = dt;
}

static uint32_t publish_next(const sat_sound_pages_info_t* info) {
    if (g_page_next >= g_t3_music.meta.pages || info->pages_free == 0u || !g_record_valid) return 0u;
    const uint8_t* h = g_record;
    const uint16_t count = (uint16_t)((h[0] << 8) | h[1]);
    const uint16_t flags = (uint16_t)((h[2] << 8) | h[3]);
    const sat_sound_score_event_t* events = (const sat_sound_score_event_t*)(h + PAGE_HEADER);
    if (sat_sound_driver_page_publish(events, count, be32(h + 4), be32(h + 8), (flags & 0x8000u) != 0u) != SAT_OK) return 0u;
    ++g_page_next;
    g_record_valid = 0u;
    return 1u;
}

/* A residency is read in slices, so that a frame is never held for a whole sample: BANK.BIN bytes into Sound RAM at
 * the planned address, checked against the planned word sum when the last slice lands. */
typedef struct load_job { uint32_t active, done, t0, ms0; } load_job_t;
static load_job_t g_job;

/* Returns 1 when the residency is complete (or failed). The song's clock is the driver tick minus `origin`. */
static int load_slice(const load_record_t* r, int32_t origin, uint32_t slice_bytes) {
    if (!g_job.active) { g_job.active = 1u; g_job.done = 0u; g_job.t0 = sat_sound_driver_tick(); }
    uint32_t budget = slice_bytes;
    while (g_job.done < r->bytes && budget > 0u) {
        uint32_t got = 0u;
        uint32_t n = r->bytes - g_job.done < STAGE_BYTES ? r->bytes - g_job.done : STAGE_BYTES;
        if (n > budget) n = budget;
        n &= ~1u;
        if (n == 0u) n = r->bytes - g_job.done;
        if (sat_cdfs_read_at(g_volume, &g_bank_file, r->file_offset + g_job.done, g_stage, n, &got) != SAT_OK || got != n ||
            sat_sound_ram_write(r->address + g_job.done, g_stage, n) != SAT_OK) {
            ++g_t3_music.load_failures;
            g_job.active = 0u;
            return 1;
        }
        g_job.done += n;
        budget = budget > n ? budget - n : 0u;
    }
    if (g_job.done < r->bytes) return 0;
    uint32_t sum = 0u;
    for (uint32_t w = 0u; w < r->bytes / 2u; ++w) sum += SOUND_RAM[(r->address / 2u) + w];
    if (sum != r->checksum) ++g_t3_music.checksum_errors;
    const uint32_t dt = sat_sound_driver_tick() - g_job.t0;
    if (dt > g_t3_music.load_ticks_max) g_t3_music.load_ticks_max = dt;
    const int32_t margin = r->needed_by - ((int32_t)sat_sound_driver_tick() - origin);
    if (g_t3_music.loads_done == 0u || margin < g_t3_music.load_margin_min_ticks) g_t3_music.load_margin_min_ticks = margin;
    if (margin < 0) ++g_t3_music.loads_late;
    g_t3_music.load_bytes += r->bytes;
    ++g_t3_music.loads_done;
    g_job.active = 0u;
    return 1;
}

/* Slots start silent; the score changes only what differs per note (address, loop, level, pan, pitch, key). */
static void slots_init(uint32_t count) {
    for (uint32_t s = 0u; s < count; ++s) {
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
    SLOT_REGS[0] = 0x1000u;             /* KYONEX: the written slots to key-off */
}

sat_result_t t3_music_start(sat_cdfs_volume_t* volume, uint32_t slot_count) {
    sat_cdfs_file_t meta_file, loads_file;
    uint8_t raw[sizeof(t3_music_meta_t)] __attribute__((aligned(4)));
    uint32_t got = 0u, offset = 0u;
    sat_result_t st;
    if (slot_count == 0u || slot_count > 32u || g_running) return SAT_ERR_INVALID_ARG;
    g_volume = volume;
    for (volatile uint8_t* p = (volatile uint8_t*)&g_t3_music; p < (volatile uint8_t*)&g_t3_music + sizeof g_t3_music; ++p) *p = 0u;
    g_t3_music.magic = MUSIC_MAGIC;
    g_record_valid = 0u; g_pages_offset = g_page_fetched = g_page_next = g_load_next = 0u;
    g_slot_mask = slot_count == 32u ? 0xFFFFFFFFu : ((1u << slot_count) - 1u);
    g_t3_music.slot_mask = g_slot_mask;

    if ((st = sat_cdfs_lookup(volume, "ARCMETA.BIN", &meta_file)) != SAT_OK || (st = sat_cdfs_lookup(volume, "ARCBANK.BIN", &g_bank_file)) != SAT_OK ||
        (st = sat_cdfs_lookup(volume, "ARCLOADS.BIN", &loads_file)) != SAT_OK || (st = sat_cdfs_lookup(volume, "ARCPAGES.BIN", &g_pages_file)) != SAT_OK) {
        fail(1u, st); return st;
    }
    if ((st = sat_cdfs_read_at(volume, &meta_file, 0u, raw, sizeof raw, &got)) != SAT_OK || got != sizeof raw) { fail(2u, st == SAT_OK ? SAT_ERR_IO : st); return SAT_ERR_IO; }
    for (unsigned i = 0; i < sizeof raw / 4u; ++i) ((uint32_t*)&g_t3_music.meta)[i] = be32(raw + 4u * i);
    const t3_music_meta_t* m = (const t3_music_meta_t*)&g_t3_music.meta;
    if (m->magic != META_MAGIC || m->version != 1u || m->loads > MAX_LOADS || m->pages == 0u) { fail(3u, SAT_ERR_VERSION); return SAT_ERR_VERSION; }
    if ((st = sat_cdfs_read_at(volume, &loads_file, 0u, g_loads, m->loads * 24u, &got)) != SAT_OK || got != m->loads * 24u) { fail(4u, st == SAT_OK ? SAT_ERR_IO : st); return SAT_ERR_IO; }
    for (uint32_t i = 0u; i < m->loads; ++i) {            /* big-endian on disc */
        uint32_t* w = (uint32_t*)&g_loads[i];
        uint8_t* b = (uint8_t*)&g_loads[i];
        for (unsigned k = 0; k < 6; ++k) w[k] = be32(b + 4u * k);
    }
    if ((st = sat_sound_ram_reserve(m->region_bytes, &offset)) != SAT_OK) { fail(5u, st); return st; }
    g_region = offset;
    g_t3_music.region_offset = offset;
    if (offset != m->base) {                              /* the score's sample addresses are absolute */
        g_t3_music.base_mismatch = 1u;
        (void)sat_sound_ram_release(offset);
        fail(6u, SAT_ERR_CAPACITY);
        return SAT_ERR_CAPACITY;
    }
    if ((st = sat_sound_driver_start()) != SAT_OK) { (void)sat_sound_ram_release(offset); fail(7u, st); return st; }
    if ((st = sat_sound_driver_claim_slots(g_slot_mask)) != SAT_OK) { (void)sat_sound_driver_stop(); (void)sat_sound_ram_release(offset); fail(8u, st); return st; }
    slots_init(slot_count);

    const uint32_t t_begin = sat_sound_driver_tick();
    g_t3_music.loads_total = m->loads;
    while (g_load_next < m->loads && g_loads[g_load_next].load_after < 0) {
        while (!load_slice(&g_loads[g_load_next], (int32_t)sat_sound_driver_tick() + 1000, STAGE_BYTES)) {}   /* before the stream starts: every margin is ample */
        ++g_load_next;
        ++g_t3_music.loads_initial;
    }
    g_t3_music.initial_ticks = sat_sound_driver_tick() - t_begin;
    g_t3_music.loads_late = 0u;
    g_t3_music.load_margin_min_ticks = 0x7FFFFFFF;
    if (g_t3_music.load_failures || g_t3_music.checksum_errors) { t3_music_stop(); fail(9u, SAT_ERR_IO); return SAT_ERR_IO; }
    fetch_next();
    if ((st = sat_sound_driver_pages_start(m->tick_samples, m->lead_ticks)) != SAT_OK) { t3_music_stop(); fail(10u, st); return st; }
    for (unsigned k = 0u; k < 2u; ++k) {
        sat_sound_pages_info_t info = {0};
        (void)sat_sound_driver_pages_info(&info);
        (void)publish_next(&info);
        fetch_next();
    }
    g_running = 1u;
    g_t3_music.state = T3_MUSIC_PLAYING;
    sram_stats();
    return SAT_OK;
}

static void take_seam(const sat_sound_pages_info_t* info, int32_t stream_tick) {
    const uint32_t k = g_t3_music.seams_taken;
    const t3_music_meta_t* m = (const t3_music_meta_t*)&g_t3_music.meta;
    /* Boundaries: the end of the intro, then the end of each loop pass but the last. */
    if (k >= T3_MUSIC_SEAMS || m->loop_ms == 0u) return;
    const uint32_t at_ms = m->intro_ms + k * m->loop_ms;
    if (at_ms >= m->window_ms) return;
    const int32_t at_tick = (int32_t)((uint64_t)at_ms * 44100u / 1000u / m->tick_samples);
    if (stream_tick < at_tick) return;
    t3_music_seam_t* s = (t3_music_seam_t*)&g_t3_music.seam[k];
    s->stream_tick = (uint32_t)stream_tick; s->executed = info->executed; s->late_events = info->late_events;
    s->underrun_ticks = info->underrun_ticks; s->late_loads = g_t3_music.loads_late; s->bad_sequence = info->bad_sequence;
    s->page = info->pages_played;
    ++g_t3_music.seams_taken;
}

void t3_music_service(void) {
    sat_sound_pages_info_t info = {0};
    if (!g_running) return;
    const uint32_t t0 = sat_sound_driver_tick(), ms0 = sat_time_ms();
    if (sat_sound_driver_pages_info(&info) != SAT_OK) return;
    if (publish_next(&info) != 0u && sat_sound_driver_pages_info(&info) == SAT_OK) (void)publish_next(&info);
    fetch_next();                                         /* prefetch while the 68000 plays */
    const int32_t stream_tick = (int32_t)(sat_sound_driver_tick() - info.base_tick);
    if (g_load_next < g_t3_music.meta.loads && (g_job.active || stream_tick >= g_loads[g_load_next].load_after)) {
        if (load_slice(&g_loads[g_load_next], (int32_t)info.base_tick, LOAD_SLICE_BYTES)) {
            ++g_load_next;
            ++g_t3_music.bank_transitions;
        }
    }
    (void)sat_sound_driver_pages_info(&info);
    g_t3_music.stream_tick = (uint32_t)stream_tick;
    g_t3_music.page_published = info.pages_published;
    g_t3_music.page_played = info.pages_played;
    g_t3_music.page_current = info.pages_played;
    g_t3_music.events_executed = info.executed;
    g_t3_music.late_events = info.late_events;
    g_t3_music.max_lateness_ticks = info.max_lateness_ticks;
    g_t3_music.underrun_ticks = info.underrun_ticks;
    g_t3_music.bad_sequence = info.bad_sequence;
    g_t3_music.refused = info.refused;
    g_t3_music.max_burst = info.max_burst;
    take_seam(&info, stream_tick);
    if (info.done && !g_t3_music.done) { g_t3_music.done = 1u; g_t3_music.state = T3_MUSIC_DONE; }
    sram_stats();
    const uint32_t dt = sat_sound_driver_tick() - t0;
    ++g_t3_music.service_calls;
    g_t3_music.service_ticks_total += dt;
    if (dt > g_t3_music.service_ticks_max) g_t3_music.service_ticks_max = dt;
    const uint32_t ms = sat_time_ms() - ms0;
    g_t3_music.service_ms_total += ms;
    if (ms > g_t3_music.service_ms_max) g_t3_music.service_ms_max = ms;
    unsigned bucket = 0u;
    for (uint32_t limit = 8u; bucket < 5u && ms >= limit; limit <<= 1) ++bucket;
    ++g_t3_music.service_ms_hist[bucket];
}

void t3_music_stop(void) {
    if (g_region == 0u && !g_running) return;
    (void)sat_sound_driver_pages_stop();
    for (uint32_t s = 0u; s < 32u; ++s)
        if (g_slot_mask & (1u << s)) SLOT_REGS[s * 0x10u] = 0u;
    SLOT_REGS[0] = 0x1000u;
    (void)sat_sound_driver_stop();
    if (g_region) (void)sat_sound_ram_release(g_region);
    g_region = 0u;
    g_running = 0u;
    if (g_t3_music.state == T3_MUSIC_PLAYING) g_t3_music.state = T3_MUSIC_IDLE;
}
