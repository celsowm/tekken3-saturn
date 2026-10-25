/* Original HUD texture pages and CLUT selection follow the pinned
 * Tekken3Remake research (hud.md and tools/remake_import/hud.py), adapted
 * to Saturn VDP1 LUT4 sprites. PolyForm Noncommercial 1.0.0, NOTICE.md.
 * Required Notice: Copyright (c) 2026 n0madic. */
#include "original_hud.h"
#include "disc.h"
#include "saturn/cdfs.h"
#include "saturn/vdp1.h"
#include <stdint.h>

enum { CAP_LEFT=0, CAP_RIGHT, HEALTH, DAMAGE, EMPTY,
       MARK_WON, MARK_EMPTY, DIGIT_FIRST, DIGIT_LAST=18, PLATE, NAME_P1, NAME_P2,
       PIECES=22 };
/* Immutable descriptors fit the release build's narrow Low WRAM margin.
 * Startup-only upload scratch sits in High WRAM, with its reclaimed pose
 * cache capacity in the opt-in build. */
static sat_vdp1_texture_t g_piece[PIECES] __attribute__((section(".wram_l")));
static uint16_t g_lut[16] __attribute__((section(".wram_l")));
static uint8_t g_pixels[512] __attribute__((aligned(4)));

static unsigned u16(const uint8_t* p) {return (unsigned)p[0]|((unsigned)p[1]<<8);}
static sat_result_t read_at(sat_cdfs_volume_t* volume,const sat_cdfs_file_t* file,
                            uint32_t at,void* data,uint32_t bytes) {
    uint32_t read=0;
    const sat_result_t st=sat_cdfs_read_at(volume,file,at,data,bytes,&read);
    return st!=SAT_OK?st:read==bytes?SAT_OK:SAT_ERR_IO;
}

sat_result_t t3_original_hud_init(void) {
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    uint8_t header[12],record[8];
    uint32_t at=12;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"HUD.BIN",&file));
    if(file.size<12)return SAT_ERR_VERSION;
    SAT_TRY(read_at(volume,&file,0,header,sizeof header));
    static const uint8_t magic[8]={'T','3','H','U','D','0','2',0};
    for(unsigned j=0;j<8;++j)if(header[j]!=magic[j])return SAT_ERR_VERSION;
    if(u16(header+8)!=PIECES || u16(header+10))return SAT_ERR_VERSION;
    for(unsigned i=0;i<PIECES;++i) {
        if(at+40>file.size)return SAT_ERR_VERSION;
        SAT_TRY(read_at(volume,&file,at,record,sizeof record));at+=8;
        const unsigned width=u16(record),padded=u16(record+2),height=u16(record+4),bytes=u16(record+6);
        if(!width || width>padded || padded&7u || !height || bytes!=padded*height/2u ||
           bytes>sizeof g_pixels || at+32u+bytes>file.size)return SAT_ERR_VERSION;
        uint8_t raw_lut[32];
        SAT_TRY(read_at(volume,&file,at,raw_lut,sizeof raw_lut));at+=32;
        for(unsigned j=0;j<16;++j)g_lut[j]=(uint16_t)u16(raw_lut+2u*j);
        SAT_TRY(read_at(volume,&file,at,g_pixels,bytes));at+=bytes;
        uint16_t lut;
        SAT_TRY(sat_vdp1_upload_lut(g_lut,&lut));
        SAT_TRY(sat_tex_upload_lut4_pixels(&g_piece[i],g_pixels,(uint16_t)padded,
                                           (uint16_t)height,lut));
    }
    return at==file.size?SAT_OK:SAT_ERR_VERSION;
}

static sat_result_t draw(unsigned piece,int x,int y,int width,int height) {
    if(piece>=PIECES || width<=0 || height<=0)return SAT_ERR_INVALID_ARG;
    return sat_draw_sprite_scaled_screen(&g_piece[piece],(int16_t)(x+width/2),
        (int16_t)(y+height/2),(uint16_t)width,(uint16_t)height,0);
}

sat_result_t t3_original_hud_bar(int x,int y,int width,int length,int recent,int inner_right) {
    /* FUN_8004E55C: the caps overlap the fill by two original pixels, so the
     * fill spans x+2..width-2; `length` and `recent` are already in those
     * fill pixels (152 original units mapped by the caller). */
    const int cap=5,fill=width-4,height=12;
    if(width<2*cap || length<0 || recent<0 || length>fill || recent>fill)return SAT_ERR_INVALID_ARG;
    SAT_TRY(draw(CAP_LEFT,x,y,cap,height));
    SAT_TRY(draw(CAP_RIGHT,x+width-cap,y,cap,height));
    SAT_TRY(draw(EMPTY,x+2,y,fill,height));
    if(recent>0)
        SAT_TRY(draw(DAMAGE,inner_right?x+2+fill-recent:x+2,y,recent,height));
    if(length>0)
        SAT_TRY(draw(HEALTH,inner_right?x+2+fill-length:x+2,y,length,height));
    return SAT_OK;
}

sat_result_t t3_original_hud_timer(unsigned seconds,int x,int y) {
    if(seconds>99)seconds=99;
    /* FUN_8004E410: 16x46 digits fifteen original pixels apart. */
    SAT_TRY(draw(DIGIT_FIRST+seconds/10,x,y,14,23));
    return draw(DIGIT_FIRST+seconds%10,x+13,y,14,23);
}

sat_result_t t3_original_hud_mark(unsigned player,unsigned won,int x,int y) {
    (void)player;
    /* FUN_8004E87C: 15x18 marks. */
    return draw(won?MARK_WON:MARK_EMPTY,x,y,13,9);
}

sat_result_t t3_original_hud_plate(int x,int y,int width) {
    /* FUN_8004EB9C: the 2x10 plate quad stretched under the name; the
     * original rows y+9..y+19 halve to five screen rows. */
    if(width<=0)return SAT_ERR_INVALID_ARG;
    return draw(PLATE,x,y,width,5);
}

sat_result_t t3_original_hud_name(unsigned player,int x,int y,int width) {
    /* FUN_8004EB9C: the padded name sheet, 16 rows halved to eight. P1's
     * art is flush left in its sheet and P2's flush right (hud.py), so the
     * drawn width is the padded sheet scaled by the screen mapping. */
    if(width<=0)return SAT_ERR_INVALID_ARG;
    return draw(player?NAME_P2:NAME_P1,x,y,width,8);
}
