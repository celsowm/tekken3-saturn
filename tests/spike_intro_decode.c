/* Gate 1 of the intro movie: how long does the SH-2 take to decode a frame, and to move it to VDP2?
 *
 * Reads INTRO.MOV (scripts/pack_intro_video.py build) from the disc a chunk at a time and decodes every
 * frame with LibSaturn's sat_movie_decode_frame into a 512-dot-stride picture in Work RAM High, timing
 * each decode (disc reads are outside the timer) and one SCU DMA of the picture rows to a hidden VDP2
 * bank. The last frame is left on screen with the results, so a Ymir or Mednafen screenshot shows both.
 *
 *   cmake -DT3_INTRO_SPIKE=ON -DT3_LOCAL_INTRO_MOVIE=.../INTRO.MOV ...; inject tekken3_spike_intro.app.bin
 *   over a disc that holds INTRO.MOV. */
#include <stdint.h>

#include "saturn/app.h"
#include "saturn/dma.h"
#include "saturn/hud.h"
#include "saturn/movie.h"
#include "saturn/saturn.h"
#include "saturn/time.h"
#include "saturn/vdp2.h"
#include "saturn/vdp2_layers.h"
#include "saturn/video.h"
#include "disc.h"

enum {
    STRIDE = 512, MAX_W = 256, MAX_H = 224,
    CHUNK = 256 * 1024,
    HEADER = 2048,
    SHOWN_BASE = 0x00000, HIDDEN_BASE = 0x40000,
    BUCKETS = 8
};

static sat_movie_decoder_t g_decoder;
static uint16_t g_picture[STRIDE * MAX_H] __attribute__((aligned(16)));
static uint8_t g_stream[CHUNK + 32 * 1024] __attribute__((aligned(4)));
static sat_ascii_font_t g_font;
static sat_hud_t g_hud;

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t be16(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }

static int has_magic(const uint8_t* p) {
    static const char magic[8] = {'S', 'A', 'T', 'M', 'O', 'V', '0', '1'};
    for (unsigned i = 0; i < 8; ++i) if (p[i] != (uint8_t)magic[i]) return 0;
    return 1;
}

static uint32_t micros(uint64_t ticks) { return (uint32_t)(ticks * 1000000u / sat_time_frequency_hz()); }

int main(void) {
    sat_video_config_t video = {320, 224, SAT_VIDEO_AUTO, 0};
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    uint32_t read = 0;
    SAT_PANIC_IF_ERROR(sat_init(&video));
    SAT_PANIC_IF_ERROR(sat_hud_init_default(&g_hud, &g_font));
    SAT_PANIC_IF_ERROR(t3_disc_volume(&volume));
    SAT_PANIC_IF_ERROR(sat_cdfs_lookup(volume, "INTRO.MOV", &file));
    SAT_PANIC_IF_ERROR(sat_cdfs_read_at(volume, &file, 0, g_stream, 2048, &read));
    if (!has_magic(g_stream)) SAT_PANIC_IF_ERROR(SAT_ERR_VERSION);
    const uint32_t width = be16(g_stream + 8), height = be16(g_stream + 10), frames = be32(g_stream + 12);
    SAT_PANIC_IF_ERROR(sat_movie_decoder_init(&g_decoder, (uint16_t)width, (uint16_t)height));
    const uint32_t x0 = (320 - width) / 2, y0 = (224 - height) / 2;
    uint16_t* const origin = g_picture + y0 * STRIDE + x0;
    for (uint32_t i = 0; i < sizeof g_picture / 4; ++i) ((uint32_t*)g_picture)[i] = 0;

    uint32_t file_pos = HEADER, have = 0, used = 0, n = 0;
    uint32_t sum = 0, worst = 0, dma_sum = 0, dma_worst = 0, over[BUCKETS] = {0};
    static const uint32_t limit_us[BUCKETS] = {10000, 20000, 30000, 35000, 40000, 50000, 70000, 100000};
    while (n < frames) {
        if (have - used < 24 * 1024 && file_pos < file.size) {      /* top the chunk up */
            for (uint32_t i = 0; i < have - used; ++i) g_stream[i] = g_stream[used + i];   /* no libc memmove here */
            have -= used;
            used = 0;
            uint32_t got = 0, want = CHUNK;
            if (want > file.size - file_pos) want = file.size - file_pos;
            SAT_PANIC_IF_ERROR(sat_cdfs_read_at(volume, &file, file_pos, g_stream + have, want, &got));
            file_pos += got;
            have += got;
        }
        const uint32_t video_bytes = be32(g_stream + used), audio_bytes = be32(g_stream + used + 4);
        const uint8_t* frame = g_stream + used + 8 + audio_bytes;
        const uint64_t t0 = sat_time_ticks();
        SAT_PANIC_IF_ERROR(sat_movie_decode_frame(&g_decoder, frame, video_bytes, origin, STRIDE));
        const uint32_t decode_us = micros(sat_time_ticks() - t0);
        const uint64_t t1 = sat_time_ticks();
        SAT_PANIC_IF_ERROR(sat_dma_copy((uint8_t*)0x25E00000 + HIDDEN_BASE, g_picture, (y0 + height) * STRIDE * 2u));
        const uint32_t dma_us = micros(sat_time_ticks() - t1);
        sum += decode_us;
        dma_sum += dma_us;
        if (decode_us > worst) worst = decode_us;
        if (dma_us > dma_worst) dma_worst = dma_us;
        for (unsigned b = 0; b < BUCKETS; ++b) if (decode_us > limit_us[b]) ++over[b];
        used += 8 + audio_bytes + video_bytes + ((4u - (video_bytes & 3u)) & 3u);
        ++n;
    }

    SAT_PANIC_IF_ERROR(sat_dma_copy((uint8_t*)0x25E00000 + SHOWN_BASE, g_picture, (y0 + height) * STRIDE * 2u));
    sat_vdp2_layer_config_t layer;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0, &layer);
    layer.bitmap = 1;
    layer.bitmap_size = SAT_VDP2_BITMAP_512X256;
    layer.color_mode = SAT_VDP2_COLOR_MODE_32768;
    layer.priority = 1;
    layer.transparent = 0;
    for (unsigned i = 0; i < 4; ++i) layer.plane_address[i] = SHOWN_BASE;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&layer));
    SAT_PANIC_IF_ERROR(sat_vdp2_layers_commit());

    for (;;) {
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        (void)sat_hud_value(&g_hud, "FRAMES", n, 4, 4);
        (void)sat_hud_value(&g_hud, "DECODE US AVG", sum / n, 4, 14);
        (void)sat_hud_value(&g_hud, "DECODE US MAX", worst, 4, 24);
        (void)sat_hud_value(&g_hud, "DMA US AVG", dma_sum / n, 4, 34);
        (void)sat_hud_value(&g_hud, "DMA US MAX", dma_worst, 4, 44);
        (void)sat_hud_value(&g_hud, "OVER 35 MS", over[3], 4, 54);
        (void)sat_hud_value(&g_hud, "OVER 50 MS", over[5], 4, 64);
        (void)sat_hud_value(&g_hud, "OVER 100 MS", over[7], 4, 74);
        SAT_PANIC_IF_ERROR(sat_end_frame());
    }
}
