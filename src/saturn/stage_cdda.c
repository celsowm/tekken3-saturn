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
 * CD-DA range and playback adapted from LibSaturn's dino_demo/music_cdda.c.
 * SCSP external-input routing: Sega SCSP manual 4.2.7; see LibSaturn's
 * docs/SCSP_AUDIO_STREAMING_GUIDE.md. All CD data reads must finish first. */
#include "stage_cdda.h"
#if defined(T3_STAGE_CDDA)
#include "disc.h"
#include <saturn/video.h>

#define EXTS_LEFT (*(volatile uint16_t*)0x25B00216u)
#define EXTS_RIGHT (*(volatile uint16_t*)0x25B00236u)

void t3_stage_cdda_mix(void) {
    /* Sound voices can update slot 16/17 direct-send words. Preserve those
     * high bytes while restoring the external-input return after SFX updates. */
    EXTS_LEFT=(uint16_t)((EXTS_LEFT&0xff00u)|0x00bfu);
    EXTS_RIGHT=(uint16_t)((EXTS_RIGHT&0xff00u)|0x00afu);
}

sat_result_t t3_stage_cdda_start(void) {
    sat_cd_block_t* block;
    SAT_TRY(t3_disc_block(&block));
    const uint16_t toc[4]={0x0200u,0u,0u,0u}, end[4]={0x0600u,0u,0u,0u};
    uint16_t response[4];
    *(volatile uint16_t*)SAT_CD_BLOCK_HIRQ=(uint16_t)~SAT_CD_BLOCK_HIRQ_DRDY;
    SAT_TRY(sat_cd_block_command(block,toc,response));
    if(response[1]!=204u) { (void)sat_cd_block_command(block,end,response);return SAT_ERR_IO; }
    unsigned field;
    for(field=0;field<120u;++field) {
        if((*(volatile uint16_t*)SAT_CD_BLOCK_HIRQ&SAT_CD_BLOCK_HIRQ_DRDY)!=0u)break;
        SAT_TRY(sat_wait_vblank());
    }
    if(field==120u)return SAT_ERR_TIMEOUT;
    uint32_t track=0,last=0,lead=0;
    for(unsigned i=0;i<102u;++i) {
        const uint32_t hi=*(volatile uint16_t*)SAT_CD_BLOCK_DTR;
        const uint32_t lo=*(volatile uint16_t*)SAT_CD_BLOCK_DTR;
        const uint32_t value=(hi<<16u)|lo;
        if(i==1u)track=value;
        if(i==100u)last=value;
        if(i==101u)lead=value;
    }
    SAT_TRY(sat_cd_block_command(block,end,response));
    if(response[1]!=204u)return SAT_ERR_IO;
    const uint32_t start=track&0xffffffu,finish=lead&0xffffffu;
    if((track>>24u)!=1u || ((last>>16u)&255u)!=2u || finish<=start)return SAT_ERR_INVALID_ARG;
    const uint32_t length=finish-start;
    const uint16_t play[4]={(uint16_t)(0x1080u|(start>>16u)),(uint16_t)start,
                            (uint16_t)(0x0f80u|(length>>16u)),(uint16_t)length};
    SAT_TRY(sat_cd_block_command(block,play,response));
    if((response[0]>>8u)==0xffu)return SAT_ERR_IO;
    /* -6 dB per CD channel leaves headroom for simultaneous fight sounds. */
    t3_stage_cdda_mix();
    return SAT_OK;
}
#endif
