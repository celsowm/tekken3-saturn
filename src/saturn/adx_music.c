#include "adx_music.h"

#include "saturn/asset.h"
#include "saturn/audio.h"
#include "saturn/disc_stream.h"
#include "saturn/music.h"

#define ADX_LOGICAL "music/adx"
#define ADX_DISC "ADXMUSIC.BIN"
#define ADX_SOURCE "disc/adxmusic.bin"
#define ADX_MAGIC SAT_FOURCC('A', 'D', 'X', '1')

volatile t3_adx_music_diag_t g_t3_adx_music;

/* Everything but the pointer lives in the lent arena: the first bytes hold this, then the scheduler, then the
 * workspace. */
typedef struct adx_state {
    sat_cdfs_file_source_t source;
    sat_cdfs_source_desc_t source_desc;
    sat_asset_desc_t asset_desc;
    sat_asset_t asset;
    sat_music_t music;
} adx_state_t;

#define STATE_BYTES ((sizeof(adx_state_t) + 15u) & ~15u)
#define DISC_BYTES ((sizeof(sat_disc_stream_t) + 15u) & ~15u)
#define WORKSPACE_BYTES SAT_MUSIC_DISC_WORKSPACE_BYTES(T3_ADX_MUSIC_RING_FRAMES, 2u)

static adx_state_t* g_state;
static sat_disc_stream_t* g_disc;
static int g_open;

static sat_result_t fail(uint32_t step, sat_result_t status) {
    g_t3_adx_music.step = step;
    g_t3_adx_music.status = (uint32_t)(-status);
    return status;
}

uint32_t t3_adx_music_arena_bytes(void) { return STATE_BYTES + DISC_BYTES + WORKSPACE_BYTES; }

sat_result_t t3_adx_music_start(sat_cdfs_volume_t* volume, sat_cd_block_t* block, void* arena,
                                uint32_t arena_bytes, uint32_t frames, uint32_t rate, uint32_t bytes) {
    sat_result_t st;
    if (((uintptr_t)arena & 3u) != 0u || arena_bytes < t3_adx_music_arena_bytes()) return fail(0u, SAT_ERR_CAPACITY);
    g_t3_adx_music = (t3_adx_music_diag_t){0};
    g_t3_adx_music.magic = ADX_MAGIC;
    g_state = (adx_state_t*)arena;
    g_disc = (sat_disc_stream_t*)((uint8_t*)arena + STATE_BYTES);
    void* const workspace = (uint8_t*)arena + STATE_BYTES + DISC_BYTES;
    adx_state_t* const a = g_state;
    if ((st = sat_disc_stream_init(g_disc, block)) != SAT_OK) return fail(1u, st);
    /* The registries keep these descriptors: they stay valid while the program runs. */
    a->source_desc = (sat_cdfs_source_desc_t){ADX_DISC, ADX_SOURCE, bytes};
    if ((st = sat_cdfs_register_source_manifest(volume, &a->source_desc, 1u, &a->source)) != SAT_OK) return fail(2u, st);
    a->asset_desc = (sat_asset_desc_t){0};
    a->asset_desc.logical_path = ADX_LOGICAL;
    a->asset_desc.source_path = ADX_SOURCE;
    a->asset_desc.size = a->source.file.size;
    a->asset_desc.sample_rate = rate;
    a->asset_desc.sample_count = frames;
    a->asset_desc.channels = 2u;
    a->asset_desc.format = SAT_AUDIO_ADX_ADPCM;
    a->asset_desc.kind = SAT_ASSET_STREAM;
    if ((st = sat_asset_register_manifest(&a->asset_desc, 1u, &a->asset)) != SAT_OK) return fail(3u, st);
    {
        sat_music_desc_t desc = {ADX_LOGICAL, g_disc, workspace, WORKSPACE_BYTES, T3_ADX_MUSIC_RING_FRAMES};
        if ((st = sat_music_open(&a->music, &desc)) != SAT_OK) return fail(4u, st);
    }
    g_open = 1;
    if ((st = sat_music_play(a->music)) != SAT_OK) return fail(5u, st);
    g_t3_adx_music.playing = 1u;
    return SAT_OK;
}

void t3_adx_music_service(void) {
    sat_audio_stream_stats_t stream = {0};
    if (!g_open) return;
    if (sat_music_update(g_state->music) != SAT_OK) g_t3_adx_music.step = 6u;
    if (sat_music_stats(g_state->music, &stream) == SAT_OK) {
        g_t3_adx_music.underruns = stream.underrun_count;
        g_t3_adx_music.rejected_writes = stream.rejected_write_count;
        g_t3_adx_music.min_fill = stream.minimum_fill;
        g_t3_adx_music.consumed = stream.consumed_frames;
    }
    g_t3_adx_music.segments = g_disc->stats.segments;
    g_t3_adx_music.waits = g_disc->stats.waits;
    g_t3_adx_music.music_loops = g_disc->stats.music_loops;
}

void t3_adx_music_stop(void) {
    if (!g_open) return;
    (void)sat_music_stop(g_state->music);
    (void)sat_music_close(g_state->music);   /* detaches the scheduler, waiting for the segment in flight */
    g_open = 0;
    g_t3_adx_music.playing = 0u;
}
