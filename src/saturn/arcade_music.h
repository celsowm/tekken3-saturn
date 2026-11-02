#ifndef T3_ARCADE_MUSIC_H
#define T3_ARCADE_MUSIC_H
/* Experimental: an Arcade (Namco C352) song played by the SCSP with the 68000 sound driver sequencing it alone.
 *
 * The song is converted offline by scripts/c352_scsp_convert.py into four private files on the disc (ARCMETA.BIN,
 * ARCBANK.BIN, ARCLOADS.BIN, ARCPAGES.BIN; they are meta.bin, bank.bin, loads.bin and pages.bin of its output):
 *   META.BIN   (this header's t3_music_meta_t), BANK.BIN (samples), LOADS.BIN (when each sample is loaded, where),
 *   PAGES.BIN  (the score in 512-event pages).
 * The SH-2 only moves data: it reads pages and sample sections from the CD file system into Sound RAM and hands
 * the pages to the 68000, which keys, retunes and mutes the slots by itself. t3_music_service() is called once per
 * frame. The SH-2 never keys a slot of the song. The CD Block is read by this module, so CD-DA must not play
 * while it runs. Everything is counted in g_t3_music, which scripts/measure_arcade_music.py reads back. */
#include <stdint.h>
#include "saturn/cdfs.h"
#include "saturn/core.h"

typedef struct t3_music_meta {
    uint32_t magic, version, pages, loads, tick_samples, lead_ticks, base, region_bytes, bank_bytes, events;
    uint32_t total_ticks, window_ms, intro_ms, loop_ms, loop_passes, reserved;
} t3_music_meta_t;

enum { T3_MUSIC_IDLE, T3_MUSIC_PLAYING, T3_MUSIC_DONE, T3_MUSIC_FAILED };
enum { T3_MUSIC_SEAMS = 4 };

typedef struct t3_music_seam {
    uint32_t stream_tick, executed, late_events, underrun_ticks, late_loads, bad_sequence, page;
} t3_music_seam_t;

typedef struct t3_music_diag {
    uint32_t magic, state, status, step;
    t3_music_meta_t meta;
    /* Sound RAM */
    uint32_t sram_used, sram_capacity, sram_high_water, resident_sounds, region_offset, base_mismatch;
    uint32_t sh2_voices_max, voice_steals, failed_plays, slot_mask;
    /* sequencer */
    uint32_t page_published, page_played, page_current, events_executed, late_events, max_lateness_ticks;
    uint32_t underrun_ticks, bad_sequence, refused, max_burst, stream_tick, done;
    /* sample banks */
    uint32_t loads_total, loads_done, loads_initial, loads_late, load_failures, checksum_errors;
    uint32_t load_ticks_max, load_bytes, bank_transitions;
    int32_t load_margin_min_ticks;
    uint32_t initial_ticks;
    /* disc */
    uint32_t page_reads, page_read_ticks_max, page_read_ticks_total, page_read_failures;
    /* SH-2 time spent in t3_music_service, in driver ticks (meta.tick_samples samples each) and ms */
    uint32_t service_calls, service_ticks_total, service_ticks_max, service_ms_total, service_ms_max;
    uint32_t service_ms_hist[6];     /* calls taking < 8, < 17, < 34, < 67, < 134 and >= 134 ms */
    t3_music_seam_t seam[T3_MUSIC_SEAMS];
    uint32_t seams_taken;
} t3_music_diag_t;

extern volatile t3_music_diag_t g_t3_music;

/* Opens META/BANK/LOADS/PAGES on `volume`, claims SCSP slots 0..slot_count-1 for the score, reserves the sample
 * region in the pool, loads the first samples and the first pages, and starts the 68000 stream. Needs
 * sat_audio_init. SAT_ERR_* on a missing file, a region the pool cannot hold at the planned address, a failed
 * read. Nothing plays on failure and the audio engine is left usable. */
sat_result_t t3_music_start(sat_cdfs_volume_t* volume, uint32_t slot_count);
/* Once per frame: publishes freed pages, reads the next page, performs due sample loads, updates g_t3_music. */
void t3_music_service(void);
/* Stops the 68000 score, silences the song's slots and releases the claim and the region. */
void t3_music_stop(void);
#endif
