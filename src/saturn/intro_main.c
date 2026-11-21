/* The boot intro (INTRO.BIN): the LibSaturn logo movie, then the original's opening movie.
 *
 * Both are movie files played by LibSaturn's sat_movie_play (saturn/movie.h): Cinepak pictures and IMA ADPCM sound
 * read from the disc once, decoded by the SH-2s, shown on a 15-bit VDP2 bitmap flipped in VBlank. LSINTRO.MOV is
 * LibSaturn's own (share/libsaturn/assets/movies/libsaturn_intro.mov); INTRO.MOV is made from the owner's disc by
 * scripts/pack_intro_video.py. Either may be missing. START ends the whole intro; the title program (MENU.BIN)
 * follows. main.c starts this program only when the disc has it.
 *
 * After the tenth arcade fight main.c starts it again with PHASE_ENDING: it then plays the winner's ending,
 * ENDnn.MOV (scripts/pack_ending_movies.py), and hands the same argument to MENU.BIN for the closing screen. */
#include <stdint.h>

#include "saturn/app.h"
#include "saturn/audio.h"
#include "saturn/exec.h"
#ifdef T3_MOVIE_DIAGNOSTICS
#include "saturn/hud.h"
#endif
#include "saturn/input.h"
#include "saturn/movie.h"
#include "saturn/saturn.h"
#include "saturn/vdp2_color_offset.h"
#include "saturn/video.h"
#include "arcade_ladder.h"
#include "boot_arg.h"
#include "disc.h"

enum { SCREEN_W = 320, SCREEN_H = 224, FADE_IN_FIELDS = 10, FADE_OUT_FIELDS = 20, YOSHIMITSU = 4, DOCTOR_B = 19 };

static sat_movie_player_t g_player;

#ifdef T3_MOVIE_DIAGNOSTICS
static sat_ascii_font_t g_movie_font;
static sat_hud_t g_movie_hud;

/* This page deliberately appears after EACH movie, including the identical LibSaturn example
 * LSINTRO.MOV. Photograph it on SAROO and compare HASH/PACKETS with the Mac's expected-hashes.txt.
 * An unmodified player cannot prove that the bytes from the CD interface matched the file. */
static void movie_report(const char* name, sat_result_t status, const sat_movie_stats_t* stats) {
    SAT_PANIC_IF_ERROR(sat_vdp2_color_offset_disable(SAT_VDP2_LAYER_ALL));
    SAT_PANIC_IF_ERROR(sat_hud_init_default(&g_movie_hud, &g_movie_font));
    for (;;) {
        sat_pad_state_t pad = {0};
        SAT_PANIC_IF_ERROR(sat_app_frame_begin(SAT_COLOR_BLACK, SAT_COLOR_BLACK, &pad));
        sat_hud_reset_cursor(&g_movie_hud, 8, 22);
        (void)sat_hud_text(&g_movie_hud, name, 8, 4);
        (void)sat_hud_line(&g_movie_hud, "STATUS", (uint32_t)(status < 0 ? -status : status));
        (void)sat_hud_line(&g_movie_hud, "PACKETS", stats->packets_checked);
        (void)sat_hud_line(&g_movie_hud, "HASH", stats->packet_hash);
        (void)sat_hud_line(&g_movie_hud, "CD ERR", stats->cd_errors);
        (void)sat_hud_line(&g_movie_hud, "PKT ERR", stats->packet_errors);
        (void)sat_hud_line(&g_movie_hud, "VIDEO ERR", stats->video_errors);
        (void)sat_hud_line(&g_movie_hud, "AUDIO ERR", stats->audio_errors);
        (void)sat_hud_line(&g_movie_hud, "ERR FRAME", stats->last_error_frame);
        (void)sat_hud_line(&g_movie_hud, "ERR CODE", (uint32_t)(stats->last_error_code < 0
                                                     ? -stats->last_error_code : stats->last_error_code));
        (void)sat_hud_line(&g_movie_hud, "STARVED", stats->starved_fields);
        (void)sat_hud_line(&g_movie_hud, "SKIPPED", stats->frames_skipped);
        (void)sat_hud_line(&g_movie_hud, "RING LOW", stats->ring_low_sectors);
        (void)sat_hud_line(&g_movie_hud, "WORK MAX US", stats->work_max_us);
        (void)sat_hud_text(&g_movie_hud, "START NEXT", 8, 202);
        SAT_PANIC_IF_ERROR(sat_app_frame_end());
        if ((pad.pressed & SAT_PAD_START) != 0u) break;
    }
}
#endif

/* The ending's file: Doctor B. and True Ogre play Yoshimitsu's and Ogre's movies (ending.ovl's lookup table). */
static void ending_name(unsigned character, char name[10]) {
    if (character == DOCTOR_B) character = YOSHIMITSU;
    if (character == T3_LADDER_TRUE_OGRE) character = T3_LADDER_OGRE;
    const char text[10] = {'E', 'N', 'D', (char)('0' + character / 10u % 10u), (char)('0' + character % 10u),
                           '.', 'M', 'O', 'V', 0};
    __builtin_memcpy(name, text, sizeof text);
}

#ifndef T3_TEST_SKIP_INTRO
#define T3_TEST_SKIP_INTRO 0
#endif

int main(void) {
    static const char* const kMovies[] = {"LSINTRO.MOV", "INTRO.MOV"};
    sat_video_config_t video = {SCREEN_W, SCREEN_H, SAT_VIDEO_AUTO, 0};
    sat_cdfs_volume_t* volume;
    sat_cd_block_t* block;
    const sat_movie_play_options_t options = {SAT_PAD_START, FADE_IN_FIELDS, FADE_OUT_FIELDS, 0, {0, 0, 0}};

    SAT_PANIC_IF_ERROR(sat_init(&video));
    SAT_PANIC_IF_ERROR(sat_audio_init());
    SAT_PANIC_IF_ERROR(t3_disc_volume(&volume));
    SAT_PANIC_IF_ERROR(t3_disc_block(&block));
    uint8_t arg[SAT_EXEC_ARG_MAX];
    uint32_t arg_bytes = 0;
    if (sat_exec_arg(arg, sizeof arg, &arg_bytes) == SAT_OK && arg_bytes == BOOT_ARG_BYTES &&
        arg[ARG_PHASE] == PHASE_ENDING) {
        char name[10];
        uint8_t skipped = 0;
        ending_name(arg[ARG_PICK0], name);
#ifdef T3_MOVIE_DIAGNOSTICS
        sat_movie_stats_t movie_stats = {0};
        const sat_result_t movie_status = sat_movie_play(&g_player, volume, block, name, &options,
                                                         &skipped, &movie_stats);
        movie_report(name, movie_status, &movie_stats);
#else
        (void)sat_movie_play(&g_player, volume, block, name, &options, &skipped, 0);
#endif
        SAT_PANIC_IF_ERROR(sat_exec(volume, "MENU.BIN", arg, BOOT_ARG_BYTES));
        for (;;) {}
    }
    /* T3_TEST_SKIP_INTRO (measurement builds): the disc keeps both movies, the cold boot goes to the title. */
    for (unsigned i = 0; !T3_TEST_SKIP_INTRO && i < sizeof kMovies / sizeof kMovies[0]; ++i) {
        sat_cdfs_file_t file;
        uint8_t skipped = 0;
        if (sat_cdfs_lookup(volume, kMovies[i], &file) != SAT_OK) continue;
#ifdef T3_MOVIE_DIAGNOSTICS
        sat_movie_stats_t movie_stats = {0};
        const sat_result_t movie_status = sat_movie_play(&g_player, volume, block, kMovies[i],
                                                         &options, &skipped, &movie_stats);
        movie_report(kMovies[i], movie_status, &movie_stats);
        if (movie_status != SAT_OK) break;
#else
        const sat_result_t movie_status = sat_movie_play(&g_player, volume, block, kMovies[i],
                                                         &options, &skipped, 0);
        if (movie_status != SAT_OK) break;
#endif
        if (skipped) break;
    }
    const uint8_t title[BOOT_ARG_BYTES] = {PHASE_TITLE, 0, 1};
    SAT_PANIC_IF_ERROR(sat_exec(volume, "MENU.BIN", title, sizeof title));
    for (;;) {}
}
