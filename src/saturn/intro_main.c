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

/* The ending's file: Doctor B. and True Ogre play Yoshimitsu's and Ogre's movies (ending.ovl's lookup table). */
static void ending_name(unsigned character, char name[10]) {
    if (character == DOCTOR_B) character = YOSHIMITSU;
    if (character == T3_LADDER_TRUE_OGRE) character = T3_LADDER_OGRE;
    const char text[10] = {'E', 'N', 'D', (char)('0' + character / 10u % 10u), (char)('0' + character % 10u),
                           '.', 'M', 'O', 'V', 0};
    __builtin_memcpy(name, text, sizeof text);
}

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
        (void)sat_movie_play(&g_player, volume, block, name, &options, &skipped, 0);
        SAT_PANIC_IF_ERROR(sat_exec(volume, "MENU.BIN", arg, BOOT_ARG_BYTES));
        for (;;) {}
    }
    for (unsigned i = 0; i < sizeof kMovies / sizeof kMovies[0]; ++i) {
        sat_cdfs_file_t file;
        uint8_t skipped = 0;
        if (sat_cdfs_lookup(volume, kMovies[i], &file) != SAT_OK) continue;
        (void)sat_movie_play(&g_player, volume, block, kMovies[i], &options, &skipped, 0);
        if (skipped) break;
    }
    const uint8_t title[BOOT_ARG_BYTES] = {PHASE_TITLE, 0, 1};
    SAT_PANIC_IF_ERROR(sat_exec(volume, "MENU.BIN", title, sizeof title));
    for (;;) {}
}
