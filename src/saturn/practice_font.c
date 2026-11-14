/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from the pinned Tekken3Remake (draw_sim.py print_text); see practice_font.h and NOTICE.md. */
#include "practice_font.h"
#include "disc.h"
#include "psx_viewport.h"
#include "saturn/cdfs.h"
#include "saturn/vdp1.h"

enum { FONTS=2,CHARS=58,COLOURS=16,MAX_GLYPHS=128 };
static sat_vdp1_texture_t g_tex[MAX_GLYPHS];
static uint8_t g_glyph[FONTS][CHARS];       /* 1 + index into g_tex, 0 for none */
static uint16_t g_height[MAX_GLYPHS],g_padded[MAX_GLYPHS];
static uint16_t g_palette[FONTS][COLOURS];
static uint8_t g_palette_known[FONTS][COLOURS];
static uint8_t g_ready;

static unsigned le16(const uint8_t* p) {return (unsigned)(p[0]|(p[1]<<8));}

sat_result_t t3_practice_font_init(void* scratch,unsigned bytes) {
    g_ready=0;
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"PFONT.BIN",&file));
    if(file.size<12u || file.size>bytes)return SAT_ERR_CAPACITY;
    uint32_t read=0;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,scratch,file.size,&read));
    if(read!=file.size)return SAT_ERR_IO;
    const uint8_t* data=scratch;
    static const uint8_t magic[8]={'T','3','P','F','N','T','1',0};
    for(unsigned j=0;j<8;++j)if(data[j]!=magic[j])return SAT_ERR_VERSION;
    const unsigned glyphs=le16(data+8),palettes=le16(data+10);
    if(glyphs>MAX_GLYPHS)return SAT_ERR_CAPACITY;
    __builtin_memset(g_glyph,0,sizeof g_glyph);
    __builtin_memset(g_palette_known,0,sizeof g_palette_known);
    uint32_t at=12;
    uint16_t upload_lut[FONTS]={0,0};
    uint8_t have_lut[FONTS]={0,0};
    for(unsigned i=0;i<palettes;++i,at+=34u) {
        if(at+34u>file.size)return SAT_ERR_VERSION;
        const unsigned font=data[at],colour=data[at+1];
        if(font>=FONTS || colour>=COLOURS)return SAT_ERR_VERSION;
        uint16_t lut[16];
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(data+at+2u+2u*j);
        SAT_TRY(sat_vdp1_upload_lut(lut,&g_palette[font][colour]));
        g_palette_known[font][colour]=1;
        if(!have_lut[font]){upload_lut[font]=g_palette[font][colour];have_lut[font]=1;}
    }
    for(unsigned i=0;i<glyphs;++i) {
        if(at+10u>file.size)return SAT_ERR_VERSION;
        const unsigned font=data[at],c=data[at+1],padded=le16(data+at+4),height=le16(data+at+6),count=le16(data+at+8);
        if(font>=FONTS || c<0x21u || c>=0x21u+CHARS || !padded || padded&7u || count!=padded*height/2u || at+10u+count>file.size ||
           !have_lut[font])return SAT_ERR_VERSION;
        SAT_TRY(sat_tex_upload_lut4_pixels(&g_tex[i],data+at+10u,(uint16_t)padded,(uint16_t)height,upload_lut[font]));
        g_glyph[font][c-0x21u]=(uint8_t)(i+1u);
        g_padded[i]=(uint16_t)padded;g_height[i]=(uint16_t)height;
        at+=10u+count;
    }
    if(at!=file.size)return SAT_ERR_VERSION;
    g_ready=1;
    return SAT_OK;
}

int t3_practice_font_ready(void) {return g_ready;}
unsigned t3_practice_font_advance(unsigned font) {return font?13u:9u;}
unsigned t3_practice_font_line(unsigned font) {return font?26u:18u;}

/* `advance` is the pen step in original pixels and `height` the line's glyph height: the glyphs are drawn scaled by
 * advance / the font's advance across and height / 16 (font 0) or / 24 (font 1) down. */
sat_result_t t3_practice_font_text_sized(unsigned font,unsigned colour,const char* text,int x,int y,unsigned advance,unsigned height) {
    if(!g_ready || font>=FONTS || colour>=COLOURS || !g_palette_known[font][colour])return SAT_OK;
    const unsigned base=t3_practice_font_advance(font),base_height=font?24u:16u;
    for(int pen=x;*text;++text,pen+=(int)advance) {
        const unsigned c=(unsigned char)*text;
        if(c<0x21u || c>=0x21u+CHARS || !g_glyph[font][c-0x21u])continue;
        const unsigned piece=g_glyph[font][c-0x21u]-1u;
        const int pw=(int)(g_padded[piece]*advance/base),ph=(int)(g_height[piece]*height/base_height);
        const int sx=t3_psx_viewport_x(pen),sy=t3_psx_viewport_y(y);
        const int w=t3_psx_viewport_x(pen+pw)-sx,h=t3_psx_viewport_y(y+ph)-sy;
        if(w<=0 || h<=0)continue;
        SAT_TRY(sat_draw_sprite_scaled_screen(&g_tex[piece],(int16_t)(sx+w/2),(int16_t)(sy+h/2),(uint16_t)w,(uint16_t)h,
                                              g_palette[font][colour]));
    }
    return SAT_OK;
}

sat_result_t t3_practice_font_text(unsigned font,unsigned colour,const char* text,int x,int y) {
    return t3_practice_font_text_sized(font,colour,text,x,y,t3_practice_font_advance(font),font?24u:16u);
}
