/* The boot intro (INTRO.BIN): the original opening movie, decoded by the SH-2s.
 *
 * INTRO.MOV (scripts/pack_intro_video.py) is read once from start to end through one CD Block play
 * (sat_cd_stream_begin_ring), so the drive never restarts. Each packet holds a frame of Cinepak with RGB555
 * codebooks (saturn/movie.h) and the IMA ADPCM sound that plays about a half second later. A frame is decoded
 * into a picture in Work RAM High, copied by the SCU to the hidden one of two 256 KiB VDP2 bitmaps (VRAM-A
 * and VRAM-B) and shown by moving NBG0's base in VBlank. The picture is smaller than the screen and the rest
 * stays black, as commercial Saturn ports show their video. The sound goes to two mono SCSP streams.
 *
 * Which frame is due comes from the field count, started when the sound is: a slow frame is skipped, never
 * waited for. START leaves at once; at the end, or then, the picture fades out and the title program
 * (MENU.BIN) takes over. main.c starts this program only when the disc has it. */
#include <stdint.h>

#include "saturn/app.h"
#include "saturn/audio.h"
#include "saturn/cd_block.h"
#include "saturn/cdfs.h"
#include "saturn/color.h"
#include "saturn/dma.h"
#include "saturn/exec.h"
#include "saturn/input.h"
#include "saturn/movie.h"
#include "saturn/saturn.h"
#include "saturn/vdp2.h"
#include "saturn/vdp2_color_offset.h"
#include "saturn/vdp2_layers.h"
#include "saturn/video.h"
#include "disc.h"
#if defined(T3_INTRO_STATS)
#include "saturn/hud.h"
#include "saturn/time.h"
#endif

enum {
    SCREEN_W = 320, SCREEN_H = 224,
    STRIDE = 512, BITMAP_ROWS = 256,
    HEADER_BYTES = 2048, SECTOR = 2048,
    RING_SECTORS = 64, PREROLL_SECTORS = 24,
    MAX_PACKET = 24 * 1024,
    FADE_FIELDS = 20,
    BANK_BYTES = 0x40000, VRAM_BASE = 0x25E00000,
    AUDIO_RING_FRAMES = 16384, AUDIO_LATENCY_FIELDS = 8,
    PHASE_TITLE = 'T', BOOT_ARG_BYTES = 8
};

static sat_movie_decoder_t g_decoder;
static uint16_t g_picture[STRIDE * BITMAP_ROWS] __attribute__((aligned(16)));
static uint8_t g_ring[RING_SECTORS * SECTOR] __attribute__((aligned(4)));
static uint8_t g_packet[MAX_PACKET] __attribute__((aligned(4)));
static uint8_t g_audio_ring[2][AUDIO_RING_FRAMES * 2] __attribute__((aligned(4)));
static uint8_t g_pcm[2 * 2 * SAT_AUDIO_IMA_BLOCK_FRAMES] __attribute__((aligned(4)));
static sat_audio_stream_t g_voice[2];

/* Where the movie is: the sectors of the packet area that have landed in the ring, and how much is used. */
static sat_cd_block_t* g_block;
static uint32_t g_sectors_taken, g_remaining, g_used;
static uint16_t* g_origin;

#if defined(T3_INTRO_STATS)
static uint32_t g_starved, g_held_min = RING_SECTORS;
#endif

static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t be16(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }

static int has_magic(const uint8_t* p) {
    static const uint8_t magic[8] = {'S', 'A', 'T', 'M', 'O', 'V', '0', '1'};
    for (unsigned i = 0; i < 8; ++i) if (p[i] != magic[i]) return 0;
    return 1;
}

static void stream_poll(void) {
    const uint32_t held = g_sectors_taken - g_used / SECTOR;
#if defined(T3_INTRO_STATS)
    if (g_remaining && held < g_held_min) g_held_min = held;
#endif
    if (g_remaining == 0 || held >= RING_SECTORS) return;
    uint32_t left = 0;
    SAT_PANIC_IF_ERROR(sat_cd_stream_poll(g_block, RING_SECTORS - held, &left));
    g_sectors_taken += g_remaining - left;
    g_remaining = left;
}

static uint32_t stream_available(void) { return g_sectors_taken * SECTOR - g_used; }

/* Copies `bytes` from the ring at stream offset `from`, wrapping once at most. */
static void ring_copy(uint8_t* to, uint32_t from, uint32_t bytes) {
    uint32_t at = from % (RING_SECTORS * SECTOR);
    while (bytes) {
        uint32_t n = RING_SECTORS * SECTOR - at;
        if (n > bytes) n = bytes;
        const uint32_t* in = (const uint32_t*)(g_ring + at);
        uint32_t* out = (uint32_t*)to;
        for (uint32_t i = 0; i < (n + 3) / 4; ++i) out[i] = in[i];
        to += n;
        bytes -= n;
        at = 0;
    }
}

/* Decodes the next packet's frame into the picture and hands its sound to the SCSP streams. Returns 0 when the
 * packet has not landed yet or the sound has no room (nothing is used then), 1 when a frame was decoded. */
static int decode_next(void) {
    uint8_t head[8] __attribute__((aligned(4)));
    if (stream_available() < 8) return 0;
    ring_copy(head, g_used, 8);
    const uint32_t video_bytes = be32(head), audio_bytes = be32(head + 4);
    const uint32_t packet = 8 + audio_bytes + video_bytes + ((4u - (video_bytes & 3u)) & 3u);
    if (packet > MAX_PACKET) SAT_PANIC_IF_ERROR(SAT_ERR_CAPACITY);
    const uint32_t blocks = audio_bytes / SAT_AUDIO_IMA_BLOCK_BYTES(2);
    if (stream_available() < packet || sat_audio_stream_available(g_voice[0]) < blocks * SAT_AUDIO_IMA_BLOCK_FRAMES) return 0;
    ring_copy(g_packet, g_used, packet);
    for (uint32_t b = 0; b < blocks; ++b) {
        SAT_PANIC_IF_ERROR(sat_audio_ima_decode_block(g_packet + 8 + b * SAT_AUDIO_IMA_BLOCK_BYTES(2), 2, g_pcm));
        for (unsigned c = 0; c < 2; ++c)
            SAT_PANIC_IF_ERROR(sat_audio_stream_write(g_voice[c], g_pcm + c * 2 * SAT_AUDIO_IMA_BLOCK_FRAMES,
                                                      SAT_AUDIO_IMA_BLOCK_FRAMES));
    }
    SAT_PANIC_IF_ERROR(sat_movie_decode_frame(&g_decoder, g_packet + 8 + audio_bytes, video_bytes, g_origin, STRIDE));
    g_used += packet;
    return 1;
}

static void fade(int level) {
    const sat_vdp2_color_offset_t offset = {(int16_t)level, (int16_t)level, (int16_t)level};
    (void)sat_vdp2_color_offset_set(SAT_VDP2_COLOR_OFFSET_A, &offset);
}

static void leave(sat_cdfs_volume_t* volume) {
    for (unsigned f = 0; f <= FADE_FIELDS; ++f) {
        fade(-(int)(f * 255u / FADE_FIELDS));
        (void)sat_audio_set_master_volume((uint16_t)(SAT_AUDIO_VOLUME_MAX - f * SAT_AUDIO_VOLUME_MAX / FADE_FIELDS));
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        (void)sat_vdp2_layers_commit();
        (void)sat_audio_update();
    }
    SAT_PANIC_IF_ERROR(sat_cd_stream_abort(g_block));
    for (unsigned c = 0; c < 2; ++c) (void)sat_audio_stream_close(g_voice[c]);
    (void)sat_audio_set_master_volume(SAT_AUDIO_VOLUME_MAX);
    const uint8_t arg[BOOT_ARG_BYTES] = {PHASE_TITLE, 0, 1, 0, 0, 0, 0, 0};
    SAT_PANIC_IF_ERROR(sat_exec(volume, "MENU.BIN", arg, sizeof arg));
    for (;;) {}
}

int main(void) {
    sat_video_config_t video = {SCREEN_W, SCREEN_H, SAT_VIDEO_AUTO, 0};
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    uint32_t read = 0;

    SAT_PANIC_IF_ERROR(sat_init(&video));
    SAT_PANIC_IF_ERROR(sat_vdp2_color_offset_enable(SAT_VDP2_LAYER_ALL, SAT_VDP2_COLOR_OFFSET_A));
    fade(-255);
    (void)sat_vdp2_layers_commit();
    /* The VDP1 buffers hold whatever the last program left; the sprite layer must stay empty. */
    SAT_PANIC_IF_ERROR(sat_begin_frame());
    SAT_PANIC_IF_ERROR(sat_end_frame());

    SAT_PANIC_IF_ERROR(sat_audio_init());
    {   /* The sound is two mono streams panned hard left and right, as sat_music plays a stereo track. */
        const sat_audio_spec_t spec = {22050, AUDIO_RING_FRAMES, 1, SAT_AUDIO_PCM_S16, 0};
        for (unsigned c = 0; c < 2; ++c) {
            SAT_PANIC_IF_ERROR(sat_audio_stream_open(&g_voice[c], &spec, g_audio_ring[c], sizeof g_audio_ring[c]));
            SAT_PANIC_IF_ERROR(sat_audio_stream_pause(g_voice[c]));
            SAT_PANIC_IF_ERROR(sat_audio_stream_set_pan(g_voice[c], c ? SAT_AUDIO_PAN_RIGHT : SAT_AUDIO_PAN_LEFT));
        }
    }

    SAT_PANIC_IF_ERROR(t3_disc_volume(&volume));
    SAT_PANIC_IF_ERROR(t3_disc_block(&g_block));
    SAT_PANIC_IF_ERROR(sat_cdfs_lookup(volume, "INTRO.MOV", &file));
    SAT_PANIC_IF_ERROR(sat_cdfs_read_at(volume, &file, 0, g_packet, HEADER_BYTES, &read));
    if (!has_magic(g_packet)) SAT_PANIC_IF_ERROR(SAT_ERR_VERSION);
    const uint32_t width = be16(g_packet + 8), height = be16(g_packet + 10), frames = be32(g_packet + 12);
    const uint32_t frame_us = be32(g_packet + 16), total_bytes = be32(g_packet + 32);
    if (width > SCREEN_W || height > SCREEN_H || be32(g_packet + 20) != 22050 || g_packet[24] != 2 || g_packet[25] != 1)
        SAT_PANIC_IF_ERROR(SAT_ERR_UNSUPPORTED);
    SAT_PANIC_IF_ERROR(sat_movie_decoder_init(&g_decoder, (uint16_t)width, (uint16_t)height));
    const uint32_t x0 = (SCREEN_W - width) / 2, y0 = (SCREEN_H - height) / 2;
    g_origin = g_picture + y0 * STRIDE + x0;

    uint32_t first_lba, sectors;
    SAT_PANIC_IF_ERROR(sat_cdfs_file_sectors(&file, HEADER_BYTES, 0, &first_lba, &sectors));
    g_remaining = (total_bytes + SECTOR - 1) / SECTOR;
    if (g_remaining > sectors) SAT_PANIC_IF_ERROR(SAT_ERR_IO);
    SAT_PANIC_IF_ERROR(sat_cd_stream_begin_ring(g_block, first_lba, g_remaining, g_ring, RING_SECTORS));

    /* Both bitmaps start black: the picture is smaller than the bitmap, and the border is never rewritten. */
    for (uint32_t i = 0; i < sizeof g_picture / 4; ++i) ((uint32_t*)g_picture)[i] = 0;
    SAT_PANIC_IF_ERROR(sat_dma_copy((void*)(VRAM_BASE), g_picture, sizeof g_picture));
    SAT_PANIC_IF_ERROR(sat_dma_copy((void*)(VRAM_BASE + BANK_BYTES), g_picture, sizeof g_picture));

    sat_vdp2_layer_config_t layer;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0, &layer);
    layer.bitmap = 1;
    layer.bitmap_size = SAT_VDP2_BITMAP_512X256;
    layer.color_mode = SAT_VDP2_COLOR_MODE_32768;
    layer.priority = 7;
    layer.transparent = 0;
    for (unsigned i = 0; i < 4; ++i) layer.plane_address[i] = 0;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&layer));
    SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(SAT_COLOR_BLACK));
    SAT_PANIC_IF_ERROR(sat_vdp2_layers_commit());

    /* Let the ring fill before the clock starts, so the first frames are not late. */
    while (g_sectors_taken < PREROLL_SECTORS && g_remaining) {
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        stream_poll();
    }

    /* The first packet carries the sound that fills the SCSP rings; with it in, the streams start, and the clock
     * starts a little later, when the sound is heard. */
    while (!decode_next()) {
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        stream_poll();
    }
    SAT_PANIC_IF_ERROR(sat_dma_copy((void*)(VRAM_BASE + y0 * STRIDE * 2), g_picture + y0 * STRIDE, height * STRIDE * 2));
    for (unsigned c = 0; c < 2; ++c) SAT_PANIC_IF_ERROR(sat_audio_stream_resume(g_voice[c]));
    const uint32_t hz = sat_video_refresh_hz();
    const uint32_t t0 = sat_field_count() + AUDIO_LATENCY_FIELDS;
    uint32_t shown_base = 0, next = 1, faded_in = 0;
#if defined(T3_INTRO_STATS)
    uint32_t skipped = 0, behind_max = 0, work_max_us = 0, late_fields = 0;
#endif
    for (;;) {
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        (void)sat_vdp2_layers_commit();                                   /* applies the flip decided last time */
        (void)sat_audio_update();
        SAT_PANIC_IF_ERROR(sat_input_poll());
        if ((sat_pad_held_port(0) | sat_pad_held_port(1)) & SAT_PAD_START) break;
        stream_poll();
        if (faded_in <= FADE_FIELDS) {
            fade(-(int)(255 - faded_in * 255u / FADE_FIELDS));
            ++faded_in;
        }

        const int32_t elapsed_fields = (int32_t)(sat_field_count() - t0);
        const uint32_t due = elapsed_fields <= 0 ? 0
            : (uint32_t)(((uint64_t)elapsed_fields * 1000000u / hz) / frame_us);
        if (due >= frames) break;
#if defined(T3_INTRO_STATS)
        const uint64_t work_start = sat_time_ticks();
#endif
        uint32_t decoded = 0;
        while (next <= due && decode_next()) {
            ++next;
            ++decoded;
        }
#if defined(T3_INTRO_STATS)
        if (next <= due) ++g_starved;                                     /* a frame was due and its data was not here */
#endif
        if (decoded == 0) continue;
#if defined(T3_INTRO_STATS)
        if (decoded > 1) skipped += decoded - 1;
        if (due + 1 > next && due + 1 - next > behind_max) behind_max = due + 1 - next;
#endif
        const uint32_t hidden = shown_base ? 0 : BANK_BYTES;
        SAT_PANIC_IF_ERROR(sat_dma_copy((void*)(VRAM_BASE + hidden + y0 * STRIDE * 2), g_picture + y0 * STRIDE,
                                        height * STRIDE * 2));
        SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_pattern_name_base(SAT_VDP2_NBG0, hidden));
        shown_base = hidden;
#if defined(T3_INTRO_STATS)
        const uint32_t us = (uint32_t)((sat_time_ticks() - work_start) * 1000000u / sat_time_frequency_hz());
        if (us > work_max_us) work_max_us = us;
        if (us > 1000000u / hz) ++late_fields;
#endif
    }
#if defined(T3_INTRO_STATS)
    {   /* Stop on the numbers: a screenshot reads them. */
        static sat_ascii_font_t font;
        static sat_hud_t hud;
        SAT_PANIC_IF_ERROR(sat_hud_init_default(&hud, &font));
        SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_enabled(SAT_VDP2_NBG0, 0));   /* the bitmap would cover the sprites */
        SAT_PANIC_IF_ERROR(sat_vdp2_layers_commit());
        fade(0);
        for (;;) {
            SAT_PANIC_IF_ERROR(sat_wait_vblank());
            (void)sat_audio_update();
            SAT_PANIC_IF_ERROR(sat_begin_frame());
            (void)sat_hud_value(&hud, "FRAMES", next, 4, 4);
            (void)sat_hud_value(&hud, "SKIPPED", skipped, 4, 14);
            (void)sat_hud_value(&hud, "STARVED", g_starved, 4, 24);
            (void)sat_hud_value(&hud, "BEHIND MAX", behind_max, 4, 34);
            (void)sat_hud_value(&hud, "RING MIN SECT", g_held_min, 4, 44);
            (void)sat_hud_value(&hud, "WORK MAX US", work_max_us, 4, 54);
            (void)sat_hud_value(&hud, "OVER A FIELD", late_fields, 4, 64);
            SAT_PANIC_IF_ERROR(sat_end_frame());
        }
    }
#endif
    leave(volume);
    return 0;
}
