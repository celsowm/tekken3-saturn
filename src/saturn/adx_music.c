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

static sat_disc_stream_t* g_disc;
static sat_cdfs_file_source_t g_source;
static sat_cdfs_source_desc_t g_source_desc;
static sat_asset_desc_t g_asset_desc;
static sat_asset_t g_asset;
static sat_music_t g_music;
static int g_open;

static sat_result_t fail(uint32_t step, sat_result_t status) {
    g_t3_adx_music.step = step;
    g_t3_adx_music.status = (uint32_t)(-status);
    return status;
}

sat_result_t t3_adx_music_start(sat_cdfs_volume_t* volume, sat_cd_block_t* block, void* arena,
                                uint32_t arena_bytes, uint32_t frames, uint32_t rate, uint32_t bytes) {
    sat_result_t st;
    const uint32_t disc_bytes = (sizeof(sat_disc_stream_t) + 3u) & ~3u;
    const uint32_t workspace_bytes = SAT_MUSIC_WORKSPACE_BYTES(SAT_MUSIC_DEFAULT_RING_FRAMES, 2u);
    if (((uintptr_t)arena & 3u) != 0u || arena_bytes < disc_bytes + workspace_bytes) return fail(0u, SAT_ERR_CAPACITY);
    g_disc = (sat_disc_stream_t*)arena;
    void* workspace = (uint8_t*)arena + disc_bytes;
    g_t3_adx_music = (t3_adx_music_diag_t){0};
    g_t3_adx_music.magic = ADX_MAGIC;
    if ((st = sat_disc_stream_init(g_disc, block)) != SAT_OK) return fail(1u, st);
    g_source_desc = (sat_cdfs_source_desc_t){ADX_DISC, ADX_SOURCE, bytes};
    if ((st = sat_cdfs_register_source_manifest(volume, &g_source_desc, 1u, &g_source)) != SAT_OK) return fail(2u, st);
    g_asset_desc = (sat_asset_desc_t){0};
    g_asset_desc.logical_path = ADX_LOGICAL;
    g_asset_desc.source_path = ADX_SOURCE;
    g_asset_desc.size = g_source.file.size;
    g_asset_desc.sample_rate = rate;
    g_asset_desc.sample_count = frames;
    g_asset_desc.channels = 2u;
    g_asset_desc.format = SAT_AUDIO_ADX_ADPCM;
    g_asset_desc.kind = SAT_ASSET_STREAM;
    if ((st = sat_asset_register_manifest(&g_asset_desc, 1u, &g_asset)) != SAT_OK) return fail(3u, st);
    {
        sat_music_desc_t desc = {ADX_LOGICAL, g_disc, workspace, workspace_bytes, SAT_MUSIC_DEFAULT_RING_FRAMES};
        if ((st = sat_music_open(&g_music, &desc)) != SAT_OK) return fail(4u, st);
    }
    g_open = 1;
    if ((st = sat_music_play(g_music)) != SAT_OK) return fail(5u, st);
    g_t3_adx_music.playing = 1u;
    return SAT_OK;
}

void t3_adx_music_service(void) {
    sat_audio_stream_stats_t stream = {0};
    if (!g_open) return;
    if (sat_music_update(g_music) != SAT_OK) g_t3_adx_music.step = 6u;
    if (sat_music_stats(g_music, &stream) == SAT_OK) {
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
    (void)sat_music_stop(g_music);
    g_open = 0;
    g_t3_adx_music.playing = 0u;
}
