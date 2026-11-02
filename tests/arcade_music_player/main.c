/* Experimental: plays a converted Arcade song from the disc through src/saturn/arcade_music.c, alone (no game).
 * Data comes from scripts/c352_scsp_convert.py (META.BIN, BANK.BIN, LOADS.BIN, PAGES.BIN; private). The harness
 * (scripts/run_arcade_music.py) reads g_t3_music back. START stops the song. */
#include <stdint.h>
#include "arcade_music.h"
#include "saturn/app.h"
#include "saturn/audio.h"
#include "saturn/cd_mount.h"
#include "saturn/cdfs.h"
#include "saturn/color.h"
#include "saturn/font.h"
#include "saturn/video.h"

static sat_cd_block_t g_cd;
static sat_cd_device_t g_cd_device;
static sat_cdfs_volume_t g_volume;
static sat_ascii_font_t g_font;

volatile uint32_t g_player_start_status = 0xFFFFFFFFu, g_player_frames;

static void line(const char* label, uint32_t value, int y) {
    (void)sat_ascii_font_draw_label_u32(&g_font, label, value, 8, y, 8, 0u, 0u);
}

int main(void) {
    if (sat_app_init_default() != SAT_OK) return 1;
    if (sat_ascii_font_init_8x8_indexed8(&g_font, SAT_COLOR_WHITE, SAT_COLOR_BLACK, 7u) != SAT_OK) return 1;
    if (sat_audio_init() != SAT_OK) return 1;
    sat_result_t st = sat_cd_mount_default(&g_cd, 0, 0, &g_cd_device, 0u, &g_volume);
    if (st == SAT_OK) st = t3_music_start(&g_volume, MUSIC_SLOTS);
    g_player_start_status = (uint32_t)(-st);
    for (;;) {
        sat_pad_state_t pad = {0};
        if (sat_app_frame_begin(SAT_COLOR_BLACK, SAT_COLOR_BLACK, &pad) != SAT_OK) break;
        ++g_player_frames;
        t3_music_service();
        (void)sat_ascii_font_draw_text_screen_indexed8(&g_font, "ARCADE MUSIC", 8, 8, 8, 0u, 0u);
        line("STATE ", g_t3_music.state, 24);
        line("PAGE ", g_t3_music.page_played, 40);
        line("EVENTS ", g_t3_music.events_executed, 56);
        line("LATE ", g_t3_music.late_events, 72);
        line("LOADS ", g_t3_music.loads_done, 88);
        (void)sat_app_frame_end();
        if ((pad.pressed & SAT_PAD_START) != 0u) break;
    }
    t3_music_stop();
    (void)sat_shutdown();
    return 0;
}
