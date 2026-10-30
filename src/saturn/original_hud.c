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
       GLYPH_FIRST, REPLAY_FIRST=53, PIECES=59, TEXT_PALETTES=9 };
/* Immutable descriptors fit the release build's narrow Low WRAM margin.
 * Startup-only upload scratch sits in High WRAM, with its reclaimed pose
 * cache capacity in the opt-in build. The font-2 glyph descriptors and the
 * message colour tables normally live in High WRAM; the experimental
 * ordered-mask build moves them to Low WRAM to preserve its stack reserve. */
static sat_vdp1_texture_t g_piece[GLYPH_FIRST] __attribute__((section(".wram_l")));
static sat_vdp1_texture_t g_glyph[PIECES-GLYPH_FIRST]
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    __attribute__((section(".wram_l")))
#endif
    ;
static uint16_t g_lut[16] __attribute__((section(".wram_l")));
/* FUN_8004D15C (print_text) keeps one glyph set and picks the colour with
 * %c; the Saturn mirrors that with one table per message colour. */
static uint16_t g_text_lut[TEXT_PALETTES]
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    __attribute__((section(".wram_l")))
#endif
    ;
/* The whole HUD.BIN is read in one CDFS call: per-record reads cost a CD
 * seek each, and the sixty-one records of the text glyphs made the boot
 * several seconds longer. The buffer is boot-only scratch. */
/* HUD.BIN is read into the caller's boot scratch (the idle face store); a
 * file larger than that scratch is refused. */
static uint8_t* g_hud_file;
static unsigned g_hud_capacity;

static unsigned u16(const uint8_t* p) {return (unsigned)p[0]|((unsigned)p[1]<<8);}

sat_result_t t3_original_hud_init(void* scratch,unsigned bytes) {
    g_hud_file=scratch;
    g_hud_capacity=bytes;
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"HUD.BIN",&file));
    if(file.size<12 || file.size>g_hud_capacity)return SAT_ERR_VERSION;
    uint32_t read=0;
    const sat_result_t st=sat_cdfs_read_at(volume,&file,0,g_hud_file,file.size,&read);
    if(st!=SAT_OK || read!=file.size)return st!=SAT_OK?st:SAT_ERR_IO;
    const uint8_t* data=g_hud_file;
    static const uint8_t magic[8]={'T','3','H','U','D','0','4',0};
    for(unsigned j=0;j<8;++j)if(data[j]!=magic[j])return SAT_ERR_VERSION;
    if(u16(data+8)!=PIECES || u16(data+10)!=TEXT_PALETTES)return SAT_ERR_VERSION;
    uint32_t at=12;
    for(unsigned i=0;i<PIECES;++i) {
        if(at+40u>file.size)return SAT_ERR_VERSION;
        const unsigned width=u16(data+at),padded=u16(data+at+2),height=u16(data+at+4),
                       bytes=u16(data+at+6);
        if(!width || width>padded || padded&7u || !height || bytes!=padded*height/2u ||
           at+40u+bytes>file.size)return SAT_ERR_VERSION;
        at+=8;
        for(unsigned j=0;j<16;++j)g_lut[j]=(uint16_t)u16(data+at+2u*j);
        at+=32u;
        uint16_t lut;
        SAT_TRY(sat_vdp1_upload_lut(g_lut,&lut));
        SAT_TRY(sat_tex_upload_lut4_pixels(i<GLYPH_FIRST?&g_piece[i]:&g_glyph[i-GLYPH_FIRST],
                                           data+at,(uint16_t)padded,
                                           (uint16_t)height,lut));
        at+=bytes;
    }
    for(unsigned i=0;i<TEXT_PALETTES;++i) {
        if(at+32u>file.size)return SAT_ERR_VERSION;
        for(unsigned j=0;j<16;++j)g_lut[j]=(uint16_t)u16(data+at+2u*j);
        at+=32u;
        SAT_TRY(sat_vdp1_upload_lut(g_lut,&g_text_lut[i]));
    }
    return at==file.size?SAT_OK:SAT_ERR_VERSION;
}

static sat_result_t draw(unsigned piece,int x,int y,int width,int height,uint16_t palette) {
    if(piece>=PIECES || width<=0 || height<=0)return SAT_ERR_INVALID_ARG;
    const sat_vdp1_texture_t* texture=
        piece<GLYPH_FIRST?&g_piece[piece]:&g_glyph[piece-GLYPH_FIRST];
    return sat_draw_sprite_scaled_screen(texture,(int16_t)(x+width/2),
        (int16_t)(y+height/2),(uint16_t)width,(uint16_t)height,palette);
}

sat_result_t t3_original_hud_bar(int x,int y,int width,int length,int recent,int inner_right) {
    /* FUN_8004E55C: the caps overlap the fill by two original pixels, so the
     * fill spans x+2..width-2; `length` and `recent` are already in those
     * fill pixels (152 original units mapped by the caller). */
    const int cap=5,fill=width-4,height=12;
    if(width<2*cap || length<0 || recent<0 || length>fill || recent>fill)return SAT_ERR_INVALID_ARG;
    SAT_TRY(draw(CAP_LEFT,x,y,cap,height,0));
    SAT_TRY(draw(CAP_RIGHT,x+width-cap,y,cap,height,0));
    SAT_TRY(draw(EMPTY,x+2,y,fill,height,0));
    if(recent>0)
        SAT_TRY(draw(DAMAGE,inner_right?x+2+fill-recent:x+2,y,recent,height,0));
    if(length>0)
        SAT_TRY(draw(HEALTH,inner_right?x+2+fill-length:x+2,y,length,height,0));
    return SAT_OK;
}

sat_result_t t3_original_hud_timer(unsigned seconds,int x,int y) {
    if(seconds>99)seconds=99;
    /* FUN_8004E410: 16x46 digits fifteen original pixels apart. */
    SAT_TRY(draw(DIGIT_FIRST+seconds/10,x,y,14,23,0));
    return draw(DIGIT_FIRST+seconds%10,x+13,y,14,23,0);
}

sat_result_t t3_original_hud_mark(unsigned player,unsigned won,int x,int y) {
    (void)player;
    /* FUN_8004E87C: 15x18 marks. */
    return draw(won?MARK_WON:MARK_EMPTY,x,y,13,9,0);
}

sat_result_t t3_original_hud_plate(int x,int y,int width) {
    /* FUN_8004EB9C: the 2x10 plate quad stretched under the name; the
     * original rows y+9..y+19 halve to five screen rows. */
    if(width<=0)return SAT_ERR_INVALID_ARG;
    return draw(PLATE,x,y,width,5,0);
}

sat_result_t t3_original_hud_name(unsigned player,int x,int y,int width) {
    /* FUN_8004EB9C: the padded name sheet, 16 rows halved to eight. P1's
     * art is flush left in its sheet and P2's flush right (hud.py), so the
     * drawn width is the padded sheet scaled by the screen mapping. */
    if(width<=0)return SAT_ERR_INVALID_ARG;
    return draw(player?NAME_P2:NAME_P1,x,y,width,8,0);
}

sat_result_t t3_original_hud_text(const char* text,unsigned colour,unsigned font,int x,int y) {
    /* FUN_8004D15C (print_text) with font 2: one 24 x 40 glyph per character
     * at a 22-pixel advance, in the original 384 x 480 coordinates; the
     * caller passes the original %c colour and the original x, y (%H, %V).
     * Each glyph maps by the 2D rectangle x' = x*20/23, y' = (y-20)/2
     * from its own original x, so the positions stay exact; spaces advance
     * the pen like the original. The colour picks one of the message
     * tables, replacing the glyph's embedded palette (%c). */
    static const char GLYPH_CHARS[]="ABCDEFIKLMNOPRSTUWY!.?123456789";
    static const char REPLAY_CHARS[]="REPLAY";
    _Static_assert(sizeof GLYPH_CHARS-1==REPLAY_FIRST-GLYPH_FIRST,"font 2 glyph order");
    _Static_assert(sizeof REPLAY_CHARS-1==PIECES-REPLAY_FIRST,"font 1 glyph order");
    static const uint8_t COLOUR_SLOT[10]={9u,0u,1u,2u,3u,4u,5u,9u,6u,7u};
    if(!text || (font!=1u && font!=2u) || colour>9u)return SAT_ERR_INVALID_ARG;
    const unsigned slot=font==1u?(colour==7u?8u:9u):COLOUR_SLOT[colour];
    if(slot==9u)return SAT_ERR_INVALID_ARG;
    const uint16_t palette=g_text_lut[slot];
    const int sy=(y-20)/2;
    const char* chars=font==1u?REPLAY_CHARS:GLYPH_CHARS;
    const unsigned first=font==1u?REPLAY_FIRST:GLYPH_FIRST;
    const int advance=font==1u?13:22;
    int pen=x;
    for(const char* at=text;*at;++at) {
        const char c=*at;
        if(c==' ') { pen+=advance; continue; }
        unsigned piece=PIECES;
        for(unsigned i=0;chars[i];++i)
            if(chars[i]==c) { piece=first+i; break; }
        if(piece==PIECES)return SAT_ERR_INVALID_ARG;
        const int sx=t3_original_hud_x(pen);
        SAT_TRY(draw(piece,sx,sy,font==1u?14:21,font==1u?12:20,palette));
        pen+=advance;
    }
    return SAT_OK;
}
