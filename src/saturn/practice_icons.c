/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from the pinned Tekken3Remake (practice_sim.py, practice_view.gd); see practice_icons.h and NOTICE.md. */
#include "practice_icons.h"
#include "disc.h"
#include "psx_viewport.h"
#include "saturn/cdfs.h"
#include "saturn/vdp1.h"

enum { ARROWS=36,BUTTONS=32,BURST=ARROWS+BUTTONS,LABELS,BANG=LABELS+3,RED_BURST,TECH,GLYPH=TECH+6,GLYPHS=94,PICTURES=GLYPH+GLYPHS };
static sat_vdp1_texture_t g_tex[PICTURES];
static uint8_t g_ready;
enum { MAX_LUTS=96 };
static uint16_t g_lut[MAX_LUTS][16],g_lut_id[MAX_LUTS],g_piece_lut[PICTURES];

static unsigned le16(const uint8_t* p) {return (unsigned)(p[0]|(p[1]<<8));}

sat_result_t t3_practice_icons_init(void* scratch,unsigned bytes) {
    g_ready=0;
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"PKEYS.BIN",&file));
    if(file.size<10u)return SAT_ERR_VERSION;
    /* Read piece by piece: the whole file (33 KB) is more than the scratch holds. */
    uint8_t head[40];
    uint32_t read=0,at=10;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,head,10u,&read));
    if(read!=10u)return SAT_ERR_IO;
    static const uint8_t magic[8]={'T','3','P','K','E','Y','1',0};
    for(unsigned j=0;j<8;++j)if(head[j]!=magic[j])return SAT_ERR_VERSION;
    if(le16(head+8)!=PICTURES)return SAT_ERR_VERSION;
    unsigned luts=0;
    for(unsigned i=0;i<PICTURES;++i) {
        if(at+40u>file.size)return SAT_ERR_VERSION;
        SAT_TRY(sat_cdfs_read_at(volume,&file,at,head,40u,&read));
        if(read!=40u)return SAT_ERR_IO;
        const unsigned padded=le16(head+2),height=le16(head+4),count=le16(head+6);
        if(!padded || padded&7u || count!=padded*height/2u || at+40u+count>file.size || count>bytes)return SAT_ERR_VERSION;
        uint16_t lut[16],id=0;
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(head+8u+2u*j);
        /* Pictures that share their 16 colours share one VDP1 table (the LUT area is small; the arrows repeat their colours). */
        unsigned shared=0;
        for(unsigned k=0;k<luts && !shared;++k) {
            unsigned same=1;
            for(unsigned j=0;j<16u;++j)if(g_lut[k][j]!=lut[j])same=0;
            if(same){id=g_lut_id[k];shared=1;}
        }
        if(!shared) {
            SAT_TRY(sat_vdp1_upload_lut(lut,&id));
            if(luts>=MAX_LUTS)return SAT_ERR_CAPACITY;
            for(unsigned j=0;j<16u;++j)g_lut[luts][j]=lut[j];
            g_lut_id[luts++]=id;
        }
        g_piece_lut[i]=id;
        /* The quick-roll arrows keep the original palette indices: the three colours of one direction share one texture. */
        if(i>=TECH+2u && i<GLYPH)g_tex[i]=g_tex[TECH+(i-TECH)%2u];
        else {
            SAT_TRY(sat_cdfs_read_at(volume,&file,at+40u,scratch,count,&read));
            if(read!=count)return SAT_ERR_IO;
            SAT_TRY(sat_tex_upload_lut4_pixels(&g_tex[i],scratch,(uint16_t)padded,(uint16_t)height,id));
        }
        at+=40u+count;
    }
    if(at!=file.size)return SAT_ERR_VERSION;
    g_ready=1;
    return SAT_OK;
}

int t3_practice_icons_ready(void) {return g_ready;}

/* One picture over original x..x+w, y..y+h (heights halve, as the 480-line frame does). */
static sat_result_t draw(unsigned piece,int x0,int y0,int w0,int h0) {
    const int x=t3_psx_viewport_x(x0),y=t3_psx_viewport_y(y0),w=t3_psx_viewport_x(x0+w0)-x,h=t3_psx_viewport_y(y0+h0)-y;
    if(!g_ready || w<=0 || h<=0)return SAT_OK;
    return sat_draw_sprite_scaled_screen(&g_tex[piece],(int16_t)(x+w/2),(int16_t)(y+h/2),(uint16_t)w,(uint16_t)h,piece>=TECH?g_piece_lut[piece]:0u);
}

sat_result_t t3_practice_icons_arrow(unsigned pad,unsigned colour,int x,int y) {
    /* FUN_800B7354: up, up-right, right, down-right, down, down-left, left, up-left (pad bits 0x1000, 0x2000, 0x4000, 0x8000). */
    unsigned index=0;
    switch(pad&0xF000u) {
    case 0x1000u:index=1;break;case 0x3000u:index=2;break;case 0x2000u:index=3;break;case 0x6000u:index=4;break;
    case 0x4000u:index=5;break;case 0xC000u:index=6;break;case 0x8000u:index=7;break;case 0x9000u:index=8;break;
    default:break;
    }
    return draw(index*4u+(colour&3u),x,y,19,31);
}

sat_result_t t3_practice_icons_marker(unsigned kind,int x,int y) {
    /* FUN_800B75AC at the hit's screen point: the burst 48 x 48 under a 48 x 16 label (high, mid, low), or the exclamation
     * mark 12 x 48 over a red burst for an unblockable attack. */
    if(kind==3u) {
        SAT_TRY(draw(RED_BURST,x-24,y-24,48,48));
        return draw(BANG,x-6,y-24,12,48);
    }
    if(kind>2u)return SAT_OK;
    SAT_TRY(draw(BURST,x-24,y-24,48,48));
    return draw(LABELS+kind,x-24,y-8,48,16);
}

sat_result_t t3_practice_icons_button(unsigned pad,unsigned colour,int x,int y) {
    /* FUN_800B721C: LP 0x80, RP 0x10, LK 0x40, RK 0x20 as mask bits 1, 2, 4, 8; the guide colour picks the plate. */
    const unsigned mask=((pad&0x80u)?1u:0u)|((pad&0x10u)?2u:0u)|((pad&0x40u)?4u:0u)|((pad&0x20u)?8u:0u);
    return draw(ARROWS+mask*2u+(colour?1u:0u),x,y,20,32);
}

sat_result_t t3_practice_icons_tech_arrow(unsigned down,unsigned colour,int x,int y) {
    /* FUN_800B7704: the 'u' and 'd' marks of the tech-roll line, 10 x 16 pictures in the row's colour (10 or 11). */
    return draw(TECH+(colour==10u?0u:colour==11u?2u:4u)+(down?1u:0u),x,y,10,16);
}

sat_result_t t3_practice_icons_glyphs(const char* text,int x,int y) {
    /* The move-text atlas's ASCII glyphs, 6 x 12 at a 6-pixel advance (FUN_800789E4's names and commands, mixed case). */
    for(;*text;++text,x+=6) {
        const unsigned code=(unsigned char)*text;
        if(code<0x21u || code>=0x21u+GLYPHS)continue;
        SAT_TRY(draw(GLYPH+code-0x21u,x,y,10,12));   /* the 5-column picture sits in an 8-texel pad: drawn 1:1 it spans 10 original pixels */
    }
    return SAT_OK;
}
