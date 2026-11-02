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

/* The ring the player keeps per channel, in frames (LibSaturn needs 12288 at least; Ymir: 12288 gave 106 underruns, 16384 none). */
#ifndef T3_ADX_MUSIC_RING_FRAMES
#define T3_ADX_MUSIC_RING_FRAMES 16384u
#endif

/* Bytes of memory the player needs for as long as the track plays: its state, the disc scheduler and the music
 * workspace. The caller lends them (4-byte aligned) to t3_adx_music_start(); the program carries no resident buffer. */
uint32_t t3_adx_music_arena_bytes(void);

/* Registers ADXMUSIC.BIN (`frames` frames at `rate` Hz, stereo, `bytes` long) and starts it. Needs sat_audio_init. */
sat_result_t t3_adx_music_start(sat_cdfs_volume_t* volume, sat_cd_block_t* block, void* arena,
                                uint32_t arena_bytes, uint32_t frames, uint32_t rate, uint32_t bytes);
/* The caller did not start the track: step 0x100|why (1 arena too small, 2 disc volume, 3 CD block), status = the bytes
 * the arena had (why 1) or the error code. Lets a measured fight say why it fell back to CD-DA. */
void t3_adx_music_skipped(uint32_t why, uint32_t value);
void t3_adx_music_service(void);
/* Stops the track, detaches the scheduler (waits for a segment in flight) and releases the arena. */
void t3_adx_music_stop(void);
#endif
