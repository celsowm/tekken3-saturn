/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Adapted from the pinned Tekken3Remake (practice_sim.py, practice_view.gd); see practice_icons.h and NOTICE.md. */
#include "practice_icons.h"
#include "disc.h"
#include "psx_viewport.h"
#include "saturn/cdfs.h"
#include "saturn/vdp1.h"

enum { ARROWS=36,BUTTONS=32,PICTURES=ARROWS+BUTTONS };
static sat_vdp1_texture_t g_tex[PICTURES];
static uint8_t g_ready;

static unsigned le16(const uint8_t* p) {return (unsigned)(p[0]|(p[1]<<8));}

sat_result_t t3_practice_icons_init(void* scratch,unsigned bytes) {
    g_ready=0;
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"PKEYS.BIN",&file));
    if(file.size<10u || file.size>bytes)return SAT_ERR_CAPACITY;
    uint32_t read=0;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,scratch,file.size,&read));
    if(read!=file.size)return SAT_ERR_IO;
    const uint8_t* data=scratch;
    static const uint8_t magic[8]={'T','3','P','K','E','Y','1',0};
    for(unsigned j=0;j<8;++j)if(data[j]!=magic[j])return SAT_ERR_VERSION;
    if(le16(data+8)!=PICTURES)return SAT_ERR_VERSION;
    uint32_t at=10;
    for(unsigned i=0;i<PICTURES;++i) {
        if(at+40u>file.size)return SAT_ERR_VERSION;
        const unsigned padded=le16(data+at+2),height=le16(data+at+4),count=le16(data+at+6);
        if(!padded || padded&7u || count!=padded*height/2u || at+40u+count>file.size)return SAT_ERR_VERSION;
        uint16_t lut[16],id;
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(data+at+8u+2u*j);
        SAT_TRY(sat_vdp1_upload_lut(lut,&id));
        SAT_TRY(sat_tex_upload_lut4_pixels(&g_tex[i],data+at+40u,(uint16_t)padded,(uint16_t)height,id));
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
    return sat_draw_sprite_scaled_screen(&g_tex[piece],(int16_t)(x+w/2),(int16_t)(y+h/2),(uint16_t)w,(uint16_t)h,0);
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

sat_result_t t3_practice_icons_button(unsigned pad,unsigned colour,int x,int y) {
    /* FUN_800B721C: LP 0x80, RP 0x10, LK 0x40, RK 0x20 as mask bits 1, 2, 4, 8; the guide colour picks the plate. */
    const unsigned mask=((pad&0x80u)?1u:0u)|((pad&0x10u)?2u:0u)|((pad&0x40u)?4u:0u)|((pad&0x20u)?8u:0u);
    return draw(ARROWS+mask*2u+(colour?1u:0u),x,y,20,32);
}
