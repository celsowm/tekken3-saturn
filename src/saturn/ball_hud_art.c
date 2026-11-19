/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * The rules and positions follow the pinned Tekken3Remake (hud.md, modes.md, draw_sim.py hud_mode_line,
 * hud_coin and ball_gauges), PolyForm Noncommercial 1.0.0; see NOTICE.md. Changes: each text is one
 * composed VDP1 sprite in the Saturn's 320 x 224 image (psx_viewport.h); the charging gauge's pulse
 * (the PS1 scales the texels by 0..2) is a VDP1 Gouraud offset of the same brightness. */
#include "ball_hud_art.h"
#include "disc.h"
#include "psx_viewport.h"
#include "saturn/cdfs.h"
#include "saturn/render3d.h"
#include "saturn/vdp1.h"

/* extract_ball_hud.py's sheet order: LABELS, then the gauge's caps and its strip per CLUT. */
enum { BHUD_TEKKEN_BALL=0, BHUD_INSERT_COIN, BHUD_PUSH_START, BHUD_FREE_PLAY, BHUD_CREDIT,
       BHUD_CAP_LEFT, BHUD_CAP_RIGHT, BHUD_STRIP_TRAIL, BHUD_STRIP_FILL_P2, BHUD_STRIP_FILL_P1, BHUD_STRIP_EMPTY,
       BHUD_SHEETS };
static sat_vdp1_texture_t g_tex[BHUD_SHEETS];
static uint16_t g_lut[BHUD_SHEETS];
static uint16_t g_padded[BHUD_SHEETS],g_height[BHUD_SHEETS];
static uint8_t g_ready;

static unsigned le16(const uint8_t* p) {return (unsigned)p[0]|(unsigned)p[1]<<8;}

sat_result_t t3_ball_hud_art_init(void* scratch,unsigned bytes) {
    uint8_t* data=scratch;
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    uint32_t size=0;
    g_ready=0;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"BHUD.BIN",&file));
    if(file.size>bytes)return SAT_ERR_CAPACITY;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,data,file.size,&size));
    if(size!=file.size)return SAT_ERR_IO;
    static const uint8_t magic[8]={'T','3','B','H','U','D','0','1'};
    for(unsigned j=0;j<8;++j)if(size<10u || data[j]!=magic[j])return SAT_ERR_VERSION;
    if(le16(data+8)!=BHUD_SHEETS)return SAT_ERR_VERSION;
    uint32_t at=10;
    for(unsigned i=0;i<BHUD_SHEETS;++i) {
        if(at+40u>size)return SAT_ERR_VERSION;
        const unsigned width=le16(data+at),padded=le16(data+at+2),height=le16(data+at+4),count=le16(data+at+6);
        if(!width || width>padded || padded&7u || count!=padded*height/2u || at+40u+count>size)return SAT_ERR_VERSION;
        uint16_t lut[16];
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(data+at+8u+2u*j);
        SAT_TRY(sat_vdp1_upload_lut(lut,&g_lut[i]));
        SAT_TRY(sat_tex_upload_lut4_pixels(&g_tex[i],data+at+40u,(uint16_t)padded,(uint16_t)height,g_lut[i]));
        g_padded[i]=(uint16_t)padded;g_height[i]=(uint16_t)height;
        at+=40u+count;
    }
    if(at!=size)return SAT_ERR_VERSION;
    g_ready=1;
    return SAT_OK;
}

/* One sheet over original x0..x1, y0..y1 (heights halve, as the 480-line frame does); with `shade`, tinted by
 * that Gouraud colour. */
static sat_result_t draw(unsigned sheet,int x0,int y0,int x1,int y1,const uint16_t* shade) {
    const int x=t3_psx_viewport_x(x0),y=t3_psx_viewport_y(y0),w=t3_psx_viewport_x(x1)-x,h=t3_psx_viewport_y(y1)-y;
    if(w<=0 || h<=0)return SAT_OK;
    if(!shade)return sat_draw_sprite_scaled_screen(&g_tex[sheet],(int16_t)(x+w/2),(int16_t)(y+h/2),(uint16_t)w,(uint16_t)h,
                                                   g_lut[sheet]);
    const sat_distorted_sprite_cmd_t cmd={{(int16_t)x,(int16_t)(x+w-1),(int16_t)(x+w-1),(int16_t)x},
                                          {(int16_t)y,(int16_t)y,(int16_t)(y+h-1),(int16_t)(y+h-1)},
                                          &g_tex[sheet],g_lut[sheet],0};
    const uint16_t gouraud[4]={*shade,*shade,*shade,*shade};
    return sat_draw_sprite_distorted_gouraud(&cmd,gouraud);
}

static sat_result_t label(unsigned sheet,int x,int y) {
    return draw(sheet,x,y,x+g_padded[sheet],y+g_height[sheet],0);
}

sat_result_t t3_ball_hud_mode_line(unsigned human,uint32_t fields,int pad2) {
    /* the mode line's x per side (0x80022228) and the coin prompt's centre (0x8002222C), y 24 */
    static const int16_t LINE_X[2]={19,208},COIN_X[2]={92,284};
    /* the coin texts' half widths (0x800223A0, 0x800223D8): the prompt is centred on COIN_X */
    static const uint8_t HALF[BHUD_CAP_LEFT]={0,49,58,45,40};
    enum { LINE_Y=24 };
    if(!g_ready || human>1u)return SAT_OK;
    SAT_TRY(label(BHUD_TEKKEN_BALL,LINE_X[human],LINE_Y));
    /* FUN_8004F1D0: hidden while the counter's bits 4-5 are 0; INSERT COIN (PUSH P2 START with a controller in
     * the port), every other 64 fields FREE PLAY (CREDIT 0 without one) */
    if(!(fields&0x30u))return SAT_OK;
    const unsigned sheet=!(fields&0x40u)?(pad2?BHUD_PUSH_START:BHUD_INSERT_COIN):(pad2?BHUD_FREE_PLAY:BHUD_CREDIT);
    return label(sheet,COIN_X[human^1u]-HALF[sheet],LINE_Y);
}

sat_result_t t3_ball_hud_gauges(const t3_ball_gauge_t gauge[2],unsigned highlight,uint32_t fields) {
    /* the records at 0x800B68E8: player 1 at x 10 filling rightwards, player 2 at x 278 leftwards, y 434 */
    static const int16_t GAUGE_X[2]={10,278};
    enum { GAUGE_Y=434, HEIGHT=0x18, CAP=6, RIGHT_CAP=0x4A, SEGMENTS=0x4E };
    if(!g_ready)return SAT_OK;
    for(unsigned g=0;g<2u;++g) {
        /* the charging side pulses: colour = white scaled by the triangle wave of the frame counter << 4 (0..255,
         * 128 neutral); here the same brightness as a Gouraud offset */
        uint16_t shade_value=0;
        const uint16_t* shade=0;
        if(highlight==1u<<g) {
            const unsigned v=(fields<<4)&0x1FFu,k=v>=0x100u?0x1FFu-v:v;
            shade_value=sat_gouraud_grey(((int)k-128)/8);
            shade=&shade_value;
        }
        const int x=GAUGE_X[g],top=GAUGE_Y,bottom=GAUGE_Y+HEIGHT;
        SAT_TRY(draw(BHUD_CAP_LEFT,x,top,x+CAP,bottom,shade));
        SAT_TRY(draw(BHUD_CAP_RIGHT,x+RIGHT_CAP,top,x+RIGHT_CAP+CAP,bottom,shade));
        int fill=gauge[g].fill,trail=gauge[g].trail;
        if(fill<0)fill=0;
        if(fill>BALL_GAUGE_WIDTH)fill=BALL_GAUGE_WIDTH;
        if(trail<fill)trail=fill;
        if(trail>BALL_GAUGE_WIDTH)trail=BALL_GAUGE_WIDTH;
        const int length[3]={fill,trail-fill,BALL_GAUGE_WIDTH-trail};
        const unsigned sheet[3]={g?BHUD_STRIP_FILL_P2:BHUD_STRIP_FILL_P1,BHUD_STRIP_TRAIL,BHUD_STRIP_EMPTY};
        int pen=g?x+SEGMENTS:x+2;
        for(unsigned k=0;k<3u;++k) {
            if(length[k]<=0)continue;
            const int a=g?pen-length[k]:pen;
            pen=g?a:pen+length[k];
            SAT_TRY(draw(sheet[k],a,top,a+length[k],bottom,shade));
        }
    }
    return SAT_OK;
}
