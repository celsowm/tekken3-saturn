/* Experimental: the whole Paul theme (intro and two loop passes, 206 s) as blocked IMA ADPCM on the disc, played by
 * sat_music through the disc scheduler (saturn/disc_stream.h). No sample of the track is resident in Sound RAM: the
 * two stream rings are the only Sound RAM it takes, and the drive feeds them. The file comes from
 * scripts/encode_ima_stream.py (private, git-ignored).
 * g_ima_proto holds what scripts/run_ima_stream_prototype.py reads back. */
#include <stdint.h>

#include "saturn/app.h"
#include "saturn/asset.h"
#include "saturn/audio.h"
#include "saturn/cd_block.h"
#include "saturn/cd_mount.h"
#include "saturn/cdfs.h"
#include "saturn/disc_stream.h"
#include "saturn/music.h"
#include "saturn/time.h"
#include "ima_meta.h"

#define PROTO_MAGIC SAT_FOURCC('I', 'M', 'A', '1')
#define TRACK_LOGICAL "music/paul"
#define TRACK_DISC "IMA.BIN"
#define TRACK_SOURCE "disc/ima/paul.bin"

typedef struct ima_proto_results {
    uint32_t magic;
    uint32_t audio_status, cd_status, stream_status, source_status, asset_status, open_status, play_status;
    uint32_t track_bytes, track_frames, track_rate, track_channels;
    uint32_t frames;
    uint32_t playing_frame;                 /* display frame the streams were released (0: never) */
    uint32_t first_loop_frame;              /* display frame the track wrapped to its start (0: not yet) */
    uint32_t sound_ram_used, sound_ram_high_water, active_voices;
    uint32_t stream_underruns, stream_refills, stream_consumed, stream_min_fill, stream_rejected;
    uint32_t disc_segments, disc_bytes, disc_music_segments, disc_music_loops, disc_data_deferred;
    uint32_t disc_waits, disc_min_buffered;
    uint32_t disc_max_ticks_poll, disc_max_ticks_music, disc_max_ticks_data;
    uint32_t disc_standby_visits, disc_standby_worst_dip;
    uint32_t ticks_per_second;
    uint32_t sizeof_disc_stream;
    uint32_t first_error;
} ima_proto_results_t;

volatile ima_proto_results_t g_ima_proto;

static sat_cd_block_t g_cd;
static sat_cd_device_t g_cd_device;
static sat_cdfs_volume_t g_volume;
static sat_cdfs_file_source_t g_source;
static sat_cdfs_source_desc_t g_source_desc;
static sat_asset_desc_t g_asset_desc;
static sat_asset_t g_asset;
static sat_disc_stream_t g_disc;
static sat_music_t g_music;
/* sat_music's workspace for two channels at the default ring: 4-byte aligned by its word type */
static uint32_t g_music_workspace[SAT_MUSIC_WORKSPACE_BYTES(SAT_MUSIC_DEFAULT_RING_FRAMES, 2u) / 4u];

static void note_error(sat_result_t status) {
    if (status != SAT_OK && g_ima_proto.first_error == 0u) g_ima_proto.first_error = (uint32_t)(-status);
}

int main(void) {
    sat_audio_stats_t audio = {0};
    sat_music_info_t info = {0};
    sat_audio_stream_stats_t stream = {0};

    g_ima_proto.magic = PROTO_MAGIC;
    g_ima_proto.track_bytes = IMA_BYTES;
    g_ima_proto.track_frames = IMA_SAMPLE_COUNT;
    g_ima_proto.track_rate = IMA_SAMPLE_RATE;
    g_ima_proto.track_channels = IMA_CHANNELS;
    g_ima_proto.ticks_per_second = sat_time_frequency_hz();
    g_ima_proto.sizeof_disc_stream = sizeof(sat_disc_stream_t);

    if (sat_app_init_default() != SAT_OK) return 1;
    g_ima_proto.audio_status = (uint32_t)(-sat_audio_init());
    g_ima_proto.cd_status = (uint32_t)(-sat_cd_mount_default(&g_cd, 0, 0, &g_cd_device, 0u, &g_volume));
    if (g_ima_proto.audio_status == 0u && g_ima_proto.cd_status == 0u) {
        /* The drive is the scheduler's: the track is read in segments while the frames run. Set before the open. */
        g_ima_proto.stream_status = (uint32_t)(-sat_disc_stream_init(&g_disc, &g_cd));
    }
    if (g_ima_proto.audio_status == 0u && g_ima_proto.cd_status == 0u && g_ima_proto.stream_status == 0u) {
        g_source_desc = (sat_cdfs_source_desc_t){TRACK_DISC, TRACK_SOURCE, IMA_BYTES};
        g_ima_proto.source_status = (uint32_t)(-sat_cdfs_register_source_manifest(&g_volume, &g_source_desc, 1u, &g_source));
        g_asset_desc = (sat_asset_desc_t){0};
        g_asset_desc.logical_path = TRACK_LOGICAL;
        g_asset_desc.source_path = TRACK_SOURCE;
        g_asset_desc.size = g_source.file.size;
        g_asset_desc.sample_rate = IMA_SAMPLE_RATE;
        g_asset_desc.sample_count = IMA_SAMPLE_COUNT;
        g_asset_desc.channels = IMA_CHANNELS;
        g_asset_desc.format = SAT_AUDIO_IMA_ADPCM;
        g_asset_desc.kind = SAT_ASSET_STREAM;
        if (g_ima_proto.source_status == 0u) {
            g_ima_proto.asset_status = (uint32_t)(-sat_asset_register_manifest(&g_asset_desc, 1u, &g_asset));
        }
        if (g_ima_proto.asset_status == 0u) {
            /* The descriptor's disc is the scheduler: sat_music_open installs it, so the track reads from the drive. */
            sat_music_desc_t music = {TRACK_LOGICAL, &g_disc, g_music_workspace, sizeof(g_music_workspace),
                                      SAT_MUSIC_DEFAULT_RING_FRAMES};
            g_ima_proto.open_status = (uint32_t)(-sat_music_open(&g_music, &music));
        }
        if (g_ima_proto.open_status == 0u) g_ima_proto.play_status = (uint32_t)(-sat_music_play(g_music));
    }

    for (;;) {
        sat_pad_state_t pad = {0};
        if (sat_app_frame_begin(SAT_COLOR_BLACK, SAT_COLOR_BLACK, &pad) != SAT_OK) break;
        ++g_ima_proto.frames;
        if (g_ima_proto.play_status == 0u) {
            /* sat_music_update advances the scheduler (it lands the segment that arrived); sat_audio_update
             * refills the SCSP halves. Both once a frame, as the jukebox example does. */
            if (sat_music_info(g_music, &info) == SAT_OK && info.starting == 0u && info.playing != 0u &&
                g_ima_proto.playing_frame == 0u) {
                g_ima_proto.playing_frame = g_ima_proto.frames;
            }
            note_error(sat_music_update(g_music));
            note_error(sat_audio_update());
            if (g_disc.stats.music_loops != 0u && g_ima_proto.first_loop_frame == 0u) {
                g_ima_proto.first_loop_frame = g_ima_proto.frames;
            }
            if (sat_music_stats(g_music, &stream) == SAT_OK) {
                g_ima_proto.stream_underruns = stream.underrun_count;
                g_ima_proto.stream_refills = stream.refill_count;
                g_ima_proto.stream_consumed = stream.consumed_frames;
                g_ima_proto.stream_min_fill = stream.minimum_fill;
                g_ima_proto.stream_rejected = stream.rejected_write_count;
            }
            if (sat_audio_get_stats(&audio) == SAT_OK) {
                g_ima_proto.sound_ram_used = audio.sound_ram_used;
                g_ima_proto.sound_ram_high_water = audio.sound_ram_high_water;
                g_ima_proto.active_voices = audio.active_voices;
            }
            g_ima_proto.disc_segments = g_disc.stats.segments;
            g_ima_proto.disc_bytes = g_disc.stats.bytes;
            g_ima_proto.disc_music_segments = g_disc.stats.music_segments;
            g_ima_proto.disc_music_loops = g_disc.stats.music_loops;
            g_ima_proto.disc_data_deferred = g_disc.stats.data_deferred;
            g_ima_proto.disc_waits = g_disc.stats.waits;
            g_ima_proto.disc_min_buffered = g_disc.stats.music_min_buffered;
            g_ima_proto.disc_max_ticks_poll = g_disc.stats.max_ticks_poll;
            g_ima_proto.disc_max_ticks_music = g_disc.stats.max_ticks_music;
            g_ima_proto.disc_max_ticks_data = g_disc.stats.max_ticks_data;
            g_ima_proto.disc_standby_visits = g_disc.stats.standby_visits;
            g_ima_proto.disc_standby_worst_dip = g_disc.stats.standby_worst_dip;
            if (g_disc.stats.last_error != 0) note_error((sat_result_t)g_disc.stats.last_error);
        }
        (void)sat_app_frame_end();
        if ((pad.pressed & SAT_PAD_START) != 0u) break;
    }
    (void)sat_music_stop(g_music);
    (void)sat_shutdown();
    return 0;
}
