#ifndef T3_ADX_MUSIC_H
#define T3_ADX_MUSIC_H
/* Experimental: a whole track streamed from the disc as CRI ADX by LibSaturn's sat_music (docs/IMA_STREAM_EXPERIMENT.md).
 * The file ADXMUSIC.BIN is the payload of scripts/encode_adx_stream.py (private): no sample of the track is in Sound
 * RAM, the drive refills two small rings through the disc scheduler. t3_adx_music_service() runs once per frame, next
 * to sat_audio_update(). The CD Block belongs to the scheduler while the track plays, so CD-DA must not be playing. */
#include <stdint.h>
#include "saturn/cd_block.h"
#include "saturn/cdfs.h"
#include "saturn/core.h"

typedef struct t3_adx_music_diag {
    uint32_t magic, status, step, playing;
    uint32_t underruns, rejected_writes, min_fill, consumed;
    uint32_t segments, waits, music_loops;
} t3_adx_music_diag_t;

extern volatile t3_adx_music_diag_t g_t3_adx_music;

/* Registers ADXMUSIC.BIN (`frames` frames at `rate` Hz, stereo, `bytes` long) and starts it. Needs sat_audio_init.
 * `arena` (4-byte aligned, ADX_MUSIC_ARENA_BYTES) holds the disc scheduler and the music workspace: about 116 KB that
 * the caller lends for as long as the track plays, so the program carries no resident buffer for a test. */
#define T3_ADX_MUSIC_ARENA_BYTES (36000u + 2u * 16384u * 2u + 2u * 8192u + 1032u)
sat_result_t t3_adx_music_start(sat_cdfs_volume_t* volume, sat_cd_block_t* block, void* arena,
                                uint32_t arena_bytes, uint32_t frames, uint32_t rate, uint32_t bytes);
void t3_adx_music_service(void);
void t3_adx_music_stop(void);
#endif
