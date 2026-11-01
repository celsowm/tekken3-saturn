/* The boot intro (INTRO.BIN): the LibSaturn logo movie, then the original's opening movie.
 *
 * Both are movie files played by LibSaturn's sat_movie_play (saturn/movie.h): Cinepak pictures and IMA ADPCM sound
 * read from the disc once, decoded by the SH-2s, shown on a 15-bit VDP2 bitmap flipped in VBlank. LSINTRO.MOV is
 * LibSaturn's own (share/libsaturn/assets/movies/libsaturn_intro.mov); INTRO.MOV is made from the owner's disc by
 * scripts/pack_intro_video.py. Either may be missing. START ends the whole intro; the title program (MENU.BIN)
 * follows. main.c starts this program only when the disc has it. */
#include <stdint.h>

#include "saturn/app.h"
#include "saturn/audio.h"
#include "saturn/exec.h"
#include "saturn/input.h"
#include "saturn/movie.h"
#include "saturn/saturn.h"
#include "saturn/vdp2_color_offset.h"
#include "saturn/video.h"
#include "disc.h"

enum { SCREEN_W = 320, SCREEN_H = 224, FADE_IN_FIELDS = 10, FADE_OUT_FIELDS = 20, PHASE_TITLE = 'T', BOOT_ARG_BYTES = 8 };

static sat_movie_player_t g_player;

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
    for (unsigned i = 0; i < sizeof kMovies / sizeof kMovies[0]; ++i) {
        sat_cdfs_file_t file;
        uint8_t skipped = 0;
        if (sat_cdfs_lookup(volume, kMovies[i], &file) != SAT_OK) continue;
        (void)sat_movie_play(&g_player, volume, block, kMovies[i], &options, &skipped, 0);
        if (skipped) break;
    }
    const uint8_t arg[BOOT_ARG_BYTES] = {PHASE_TITLE, 0, 1, 0, 0, 0, 0, 0};
    SAT_PANIC_IF_ERROR(sat_exec(volume, "MENU.BIN", arg, sizeof arg));
    for (;;) {}
}
