/* Copyright (c) 2026 libsaturn contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * CD-DA range and playback go through LibSaturn's saturn/cdda.h.
 * SCSP external-input routing: Sega SCSP manual 4.2.7; see LibSaturn's
 * docs/SCSP_AUDIO_STREAMING_GUIDE.md. All CD data reads must finish first. */
#include "stage_cdda.h"
#if defined(T3_STAGE_CDDA)
#include "disc.h"
#include <saturn/cdda.h>

/* -6 dB per CD channel leaves headroom for simultaneous fight sounds. */
void t3_stage_cdda_mix(void) {sat_cdda_set_level(SAT_CDDA_LEVEL_MAX-2);}

sat_result_t t3_stage_cdda_status(sat_cd_status_t* status) {
    sat_cd_block_t* block;
    SAT_TRY(t3_disc_block(&block));
    return sat_cd_get_status(block,status);
}

/* Plays the range [track start + skip, + length) of audio track 2 on repeat; length 0 is to its end. */
static sat_result_t cdda_play(uint32_t skip,uint32_t limit) {
    sat_cd_block_t* block;
    SAT_TRY(t3_disc_block(&block));
    sat_cd_toc_t toc;
    uint32_t start,whole;
    SAT_TRY(sat_cd_read_toc(block,&toc));
    if(toc.last_track!=2u)return SAT_ERR_INVALID_ARG;
    SAT_TRY(sat_cd_toc_audio_track(&toc,2u,&start,&whole));
    if(whole<skip+limit || whole==skip)return SAT_ERR_INVALID_ARG;
    SAT_TRY(sat_cdda_play(block,start+skip,limit?limit:whole-skip));
    t3_stage_cdda_mix();
    return SAT_OK;
}
#if defined(T3_SELECT_CDDA_SECTORS)
/* The fighter themes follow the select music on the same track. */
#if defined(T3_MUSIC_TABLE)
#include "t3_music.h"
sat_result_t t3_stage_cdda_start(unsigned track) {
    static const struct {uint32_t start,sectors;} themes[]=T3_MUSIC_THEMES;
    const unsigned theme=track-T3_MUSIC_FIRST_TRACK;
    if(theme>=sizeof themes/sizeof themes[0])return SAT_ERR_INVALID_ARG;
    return cdda_play(T3_SELECT_CDDA_SECTORS+themes[theme].start,themes[theme].sectors);
}
#else
sat_result_t t3_stage_cdda_start(unsigned track) {(void)track;return cdda_play(T3_SELECT_CDDA_SECTORS,0);}
#endif
sat_result_t t3_select_cdda_start(void) {return cdda_play(0,T3_SELECT_CDDA_SECTORS);}
#else
sat_result_t t3_stage_cdda_start(unsigned track) {(void)track;return cdda_play(0,0);}
#endif
#endif
