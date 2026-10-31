/* The title and character select program (MENU.BIN).
 *
 * It runs 704x448 double-density interlace, apart from the 320x224 fight, and
 * hands the fight its picks through sat_exec. Both screens are one 352x448
 * VDP2 bitmap shown at 2x width (scripts/pack_menu_art.py bakes the frame, logo
 * and face grid into it); only what moves is VDP1: text, cursors, the glow and
 * the two big portraits. Hi-res VDP1 draws palette codes of ONE CRAM bank, so
 * the codes are split: text and cursors 1-7, portrait slot A 8-127, slot B
 * 128-247, glow ramps 248-255.
 *
 * Flow, timings and layout follow Tekken3Remake docs/research/code/modes.md
 * (title sequence, main menu, character select) and tools/research/select_sim.py;
 * Copyright (c) 2026 n0madic, PolyForm Noncommercial 1.0.0; see NOTICE.md. */
#include <stdint.h>


#include "disc.h"
#include "menu_layout.h"
#include "select_grid.h"
#include "saturn/app.h"
#include "saturn/color.h"
#include "saturn/exec.h"
#include "saturn/font.h"
#include "saturn/input.h"
#include "saturn/saturn.h"
#include "saturn/vdp1.h"
#include "saturn/vdp2.h"
#include "saturn/vdp2_color_offset.h"
#include "saturn/vdp2_layers.h"
#include "saturn/video.h"

const char* t3_fighter_name(unsigned id);

enum {
    SCREEN_W=704, SCREEN_H=448, BG_STRIDE=512, BG_BASE=0, BG_BANK=0, SPR_BANK=7,
    BG_HEADER=12+512, PORT_STRIDE=128, PORT_BYTES=PORT_STRIDE*MENU_PORT_H, PORT_PAL_WORDS=116,
    PORT_FILE=PORT_PAL_WORDS*2+PORT_BYTES, FIGHTERS=21,
    CODE_WHITE=1, CODE_GREY=2, CODE_GOLD=3, CODE_P1=4, CODE_P2=5, CODE_P1_HI=6, CODE_P2_HI=7,
    SLOT_A=8, SLOT_B=124, GLOW_P1=240, GLOW_P2=244, CODE_BLACK=248,
    BOOT_ARG_BYTES=5, PHASE_SELECT='S', PHASE_FIGHT='F',
    FADE_FRAMES=32, SELECT_TIMER_UNIT=93, SELECT_FRAMES=20*SELECT_TIMER_UNIT,
    PORT_REST_FRAMES=4, PORT_FADE_FRAMES=16,
    LAYERS=SAT_VDP2_LAYER_NBG0|SAT_VDP2_LAYER_SPRITE|SAT_VDP2_LAYER_BACK
};

static sat_cdfs_volume_t* g_volume;
/* The largest file read in one go: a backdrop. */
static uint8_t g_io[BG_HEADER+MENU_BG_W*MENU_BG_H+16] __attribute__((aligned(4)));
static uint16_t g_pal[256];
static sat_ascii_font_t g_font[3];     /* white, grey, gold */
static sat_vdp1_texture_t g_port_tex[2];
static uint16_t g_port_pal[2][PORT_PAL_WORDS];
static uint8_t g_arg[BOOT_ARG_BYTES]={PHASE_SELECT,0,1,0,0};

static unsigned w555(unsigned r,unsigned g,unsigned b) {return 0x8000u|r|(g<<5)|(b<<10);}

static sat_result_t read_file(const char* name,uint32_t offset,void* dst,uint32_t bytes) {
    sat_cdfs_file_t file;
    uint32_t read=0;
    SAT_TRY(sat_cdfs_lookup(g_volume,name,&file));
    if(offset+bytes>file.size)return SAT_ERR_IO;
    SAT_TRY(sat_cdfs_read_at(g_volume,&file,offset,dst,bytes,&read));
    return read==bytes?SAT_OK:SAT_ERR_IO;
}

/* ---- video ---- */

static void sprite_palette(void) {
    __builtin_memset(g_pal,0,sizeof g_pal);
    g_pal[CODE_WHITE]=(uint16_t)w555(31,31,31);
    g_pal[CODE_GREY]=(uint16_t)w555(18,18,20);
    g_pal[CODE_GOLD]=(uint16_t)w555(31,26,6);
    g_pal[CODE_P1]=(uint16_t)w555(6,14,31);
    g_pal[CODE_P2]=(uint16_t)w555(31,14,3);
    g_pal[CODE_BLACK]=(uint16_t)w555(0,0,0);
    g_pal[CODE_P1_HI]=(uint16_t)w555(20,26,31);
    g_pal[CODE_P2_HI]=(uint16_t)w555(31,24,14);
    for(unsigned i=0;i<4;++i) {
        g_pal[GLOW_P1+i]=(uint16_t)w555(1+i,2+2*i,5+4*i);
        g_pal[GLOW_P2+i]=(uint16_t)w555(5+4*i,2+2*i,1+i);
    }
}

static void init_fonts(void) {
    static const uint8_t codes[3]={CODE_WHITE,CODE_GREY,CODE_GOLD};
    for(unsigned set=0;set<3;++set)
        for(unsigned glyph=0;glyph<SAT_ASCII_FONT_GLYPH_COUNT;++glyph) {
            uint8_t tall[16*16]={0};
            SAT_PANIC_IF_ERROR(sat_font_pack_8x8_glyph_indexed8(tall,16,16,0,0,
                sat_font_ascii_8x8_rows((char)(glyph+32u)),2));
            for(unsigned i=0;i<sizeof tall;++i)if(tall[i])tall[i]=codes[set];
            SAT_PANIC_IF_ERROR(sat_tex_upload_indexed8(&g_font[set].glyphs[glyph],tall,16,16,g_pal,SPR_BANK));
        }
}

static void init_video(void) {
    sat_video_config_t video={SCREEN_W,SCREEN_H,SAT_VIDEO_AUTO,0};
    sat_vdp2_layer_config_t layer;
    SAT_PANIC_IF_ERROR(sat_init(&video));
    sprite_palette();
    init_fonts();
    SAT_PANIC_IF_ERROR(sat_vdp2_palette_upload(g_pal,256,SPR_BANK*256));
    SAT_PANIC_IF_ERROR(sat_vdp2_sprite_palette_bank_set(SPR_BANK));
    for(unsigned side=0;side<2;++side) {
        uint32_t offset;
        SAT_PANIC_IF_ERROR(sat_vdp1_vram_reserve(PORT_BYTES,&offset));
        g_port_tex[side]=(sat_vdp1_texture_t){(uint16_t)(offset/8u),PORT_STRIDE,MENU_PORT_H,SPR_BANK,1,
                                              SAT_VDP1_TEXTURE_INDEXED8};
    }
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&layer);
    layer.bitmap=1;
    layer.bitmap_size=SAT_VDP2_BITMAP_512X512;
    layer.color_mode=SAT_VDP2_COLOR_MODE_256;
    layer.palette=BG_BANK;
    layer.priority=1;
    layer.transparent=0;
    for(unsigned i=0;i<4;++i)layer.plane_address[i]=BG_BASE;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&layer));
    /* 352 baked columns over 704 dots. */
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0,0x20000u,0x10000u));
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_scroll(SAT_VDP2_NBG0,0,0));
    SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(SAT_COLOR_BLACK));
    SAT_PANIC_IF_ERROR(sat_vdp2_color_offset_enable(LAYERS,SAT_VDP2_COLOR_OFFSET_A));
}

static void fade(int level) {
    const sat_vdp2_color_offset_t offset={(int16_t)level,(int16_t)level,(int16_t)level};
    (void)sat_vdp2_color_offset_set(SAT_VDP2_COLOR_OFFSET_A,&offset);
}

static void backdrop_show(int on) {(void)sat_vdp2_layer_set_enabled(SAT_VDP2_NBG0,(uint8_t)on);}

static sat_result_t load_backdrop(const char* name) {
    uint16_t palette[256];
    SAT_TRY(read_file(name,0,g_io,BG_HEADER+MENU_BG_W*MENU_BG_H));
    for(unsigned i=0;i<8;++i)if(g_io[i]!=(uint8_t)"T3BG0001"[i])return SAT_ERR_VERSION;
    __builtin_memcpy(palette,g_io+12,sizeof palette);
    SAT_TRY(sat_vdp2_palette_upload(palette,256,BG_BANK*256));
    /* Whole 512x512 bitmap: the picture, then black, so a shifted or shrunk view never shows old VRAM. */
    static uint16_t row[BG_STRIDE/2];
    for(unsigned y=0;y<BG_STRIDE;++y) {
        __builtin_memset(row,0,sizeof row);
        if(y<MENU_BG_H)__builtin_memcpy(row,g_io+BG_HEADER+y*MENU_BG_W,MENU_BG_W);
        SAT_TRY(sat_vdp2_vram_write_words((BG_BASE+y*BG_STRIDE)/2u,row,BG_STRIDE/2u));
    }
    return SAT_OK;
}

/* The title picture whole, or shrunk to the top so the mode list fits below it. */
static void backdrop_view(int menu) {
    if(menu) {
        (void)sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0,0x11A00u,0x8CCCu);        /* 1.1 x 0.55 */
        (void)sat_vdp2_layer_set_scroll(SAT_VDP2_NBG0,(int32_t)(368u<<16),0);  /* 144 dots right */
    } else {
        (void)sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0,0x20000u,0x10000u);
        (void)sat_vdp2_layer_set_scroll(SAT_VDP2_NBG0,0,0);
    }
}

/* ---- frame helpers ---- */

typedef struct {sat_pad_state_t pad[2];} pads_t;

/* Hi-res draws a picture per two fields, so each frame is 2 ticks of the original's 60 Hz clock. */
static unsigned g_now;

static void frame_begin(pads_t* pads) {
    g_now+=2u;
    SAT_PANIC_IF_ERROR(sat_wait_vblank());
    (void)sat_vdp2_layers_commit();
    SAT_PANIC_IF_ERROR(sat_input_poll());
    SAT_PANIC_IF_ERROR(sat_pad_tap_state(0,0,&pads->pad[0]));
    SAT_PANIC_IF_ERROR(sat_pad_tap_state(1,0,&pads->pad[1]));
    SAT_PANIC_IF_ERROR(sat_begin_frame());
}

static void frame_end(void) {SAT_PANIC_IF_ERROR(sat_end_frame());}

/* Text in baked-bitmap coordinates (352 wide): the screen is twice as wide. */
static void text(unsigned set,const char* s,int bg_cx,int y) {
    (void)sat_ascii_font_draw_text_screen_centered_indexed8(&g_font[set],s,bg_cx*2,y,14,0,0);
}

static void number(unsigned set,unsigned value,int bg_cx,int y) {
    char s[4]={(char)('0'+(value/10u)%10u),(char)('0'+value%10u),0,0};
    text(set,s,bg_cx,y);
}

static void rect(int x,int y,int w,int h,unsigned code) {
    (void)sat_draw_rect_screen((int16_t)x,(int16_t)y,(uint16_t)w,(uint16_t)h,(uint16_t)code);
}

/* ---- title ---- */

typedef enum {MODE_ARCADE,MODE_VS,MODE_TEAM,MODE_TIME,MODE_SURVIVAL,MODE_FORCE,MODE_PRACTICE,MODE_OPTION}
    mode_t3;
static const char* const g_modes[]={"ARCADE","VS","TEAM BATTLE","TIME ATTACK","SURVIVAL",
                                    "TEKKEN FORCE","PRACTICE","OPTION"};
enum { MODES=8 };
#define START_BUTTONS (SAT_PAD_START|SAT_PAD_A|SAT_PAD_B|SAT_PAD_C|SAT_PAD_X|SAT_PAD_Y|SAT_PAD_Z)

static int mode_playable(unsigned mode,const pads_t* pads) {
    return mode==MODE_ARCADE || mode==MODE_OPTION || (mode==MODE_VS && pads->pad[1].connected);
}

static void prompt(const pads_t* pads,unsigned frame) {
    if(!(frame&0x30u))return;
    text(0,pads->pad[1].connected?"PUSH P1 AND/OR P2 BUTTON":"PUSH P1 START BUTTON",
         MENU_BG_W/2,TITLE_PROMPT_Y);
}

static void copyright(void) {
    text(1,"(C)1994 1995 1996 1998 NAMCO LTD.",MENU_BG_W/2,TITLE_COPY_Y0);
    text(1,"ALL RIGHTS RESERVED",MENU_BG_W/2,TITLE_COPY_Y1);
}

/* Returns the chosen mode; the profiler flag may change in OPTION. */
static unsigned title_flow(void) {
    pads_t pads;
    SAT_PANIC_IF_ERROR(load_backdrop("TITLEBG.BIN"));
    for(;;) {
        /* NAMCO PRESENTS: 32 frames in, at least 90 up, 32 out. */
        backdrop_show(0);
        for(unsigned f=0,t0=g_now;f<32+90+32;f=g_now-t0) {
            int level=f<32?-255+(int)(f*255/32):f>=122?-(int)((f-122)*255/32):0;
            fade(level);
            frame_begin(&pads);
            text(0,"NAMCO",MENU_BG_W/2-34,MENU_BG_H/2-16);
            text(1,"PRESENTS.",MENU_BG_W/2+30,MENU_BG_H/2-16);
            frame_end();
            if(pads.pad[0].pressed&SAT_PAD_START)goto menu;
        }
        /* White flash: 25 frames up, 11 held; then the picture fades in from white over 86. */
        for(unsigned f=0,t0=g_now;f<36;f=g_now-t0) {
            fade(f<25?(int)(10*f+10):255);
            frame_begin(&pads);
            frame_end();
        }
        backdrop_show(1);
        for(unsigned f=0,t0=g_now;f<86;f=g_now-t0) {
            fade(255-(int)(f*255/86));
            frame_begin(&pads);
            frame_end();
            if(pads.pad[0].pressed&SAT_PAD_START)goto menu;
        }
        fade(0);
        for(unsigned f=0,t0=g_now;f<300;f=g_now-t0) {
            frame_begin(&pads);
            prompt(&pads,f);
            copyright();
            frame_end();
            if(pads.pad[0].pressed&START_BUTTONS || pads.pad[1].pressed&START_BUTTONS)goto menu;
        }
        for(unsigned f=0,t0=g_now;f<FADE_FRAMES;f=g_now-t0) {
            fade(-(int)(f*255/FADE_FRAMES));
            frame_begin(&pads);
            frame_end();
        }
    }
menu:;
    {
        unsigned selected=0,idle=0,option=0;
        backdrop_show(1);
        backdrop_view(1);
        fade(0);
        for(;;) {
            frame_begin(&pads);
            rect(0,250,SCREEN_W,SCREEN_H-250,CODE_BLACK);
            idle+=2u;
            if(idle>480u && !option) {
                frame_end();
                backdrop_view(0);
                return title_flow();
            }
            if(option) {
                text(2,"OPTION MODE",MENU_BG_W/2,200);
                text(0,g_arg[4]?"DEBUG: ON":"DEBUG: OFF",MENU_BG_W/2,240);
                if(pads.pad[0].pressed&(SAT_PAD_LEFT|SAT_PAD_RIGHT|SAT_PAD_A|SAT_PAD_X))g_arg[4]^=1u;
                if(pads.pad[0].pressed&(SAT_PAD_B|SAT_PAD_START))option=0;
                frame_end();
                continue;
            }
            const unsigned first=selected>3u?(selected-3u<MODES-7u?selected-3u:MODES-7u):0u;
            for(unsigned row=0;row<7u;++row) {
                const unsigned mode=first+row;
                const unsigned set=mode==selected?2u:mode_playable(mode,&pads)?0u:1u;
                text(set,g_modes[mode],MENU_BG_W/2,254+(int)row*26);
            }
            if(pads.pad[0].pressed&SAT_PAD_UP) {selected=selected?selected-1u:MODES-1u;idle=0;}
            if(pads.pad[0].pressed&SAT_PAD_DOWN) {selected=(selected+1u)%MODES;idle=0;}
            if((pads.pad[0].pressed&START_BUTTONS) && mode_playable(selected,&pads)) {
                if(selected==MODE_OPTION)option=1;
                else {frame_end();return selected;}
            }
            frame_end();
        }
    }
}

/* ---- character select ---- */

static t3_select_cursor_t steer(t3_select_cursor_t cursor,const sat_pad_state_t* pad) {
    const int across=(pad->pressed&SAT_PAD_RIGHT)?1:(pad->pressed&SAT_PAD_LEFT)?-1:0;
    return t3_select_move(cursor,across,(pad->pressed&(SAT_PAD_UP|SAT_PAD_DOWN))!=0);
}

static void portrait_palette(unsigned side,unsigned level) {
    uint16_t out[PORT_PAL_WORDS];
    for(unsigned i=0;i<PORT_PAL_WORDS;++i) {
        const unsigned w=g_port_pal[side][i];
        out[i]=(uint16_t)w555(((w&31u)*level)>>5,(((w>>5)&31u)*level)>>5,(((w>>10)&31u)*level)>>5);
    }
    (void)sat_vdp2_palette_upload(out,PORT_PAL_WORDS,(uint16_t)(SPR_BANK*256u+(side?SLOT_B:SLOT_A)));
}

static sat_result_t load_portrait(unsigned side,unsigned id) {
    char name[12]={'P','O','R','T',(char)('0'+id/10u),(char)('0'+id%10u),'.','B','I','N',0,0};
    const unsigned offset=side?SLOT_B-1u:SLOT_A-1u;
    SAT_TRY(read_file(name,0,g_io,PORT_FILE));
    __builtin_memcpy(g_port_pal[side],g_io,sizeof g_port_pal[side]);
    uint8_t* pixels=g_io+PORT_PAL_WORDS*2;
    for(unsigned i=0;i<PORT_BYTES;++i)if(pixels[i])pixels[i]=(uint8_t)(pixels[i]+offset);
    return sat_vdp1_vram_write((uint32_t)g_port_tex[side].srca*8u,pixels,PORT_BYTES);
}

static void cell_frame(unsigned side,t3_select_cursor_t cursor,unsigned frame,int chosen) {
    const unsigned code=(chosen||(frame&8u))?(side?CODE_P2_HI:CODE_P1_HI):(side?CODE_P2:CODE_P1);
    const int x=(SEL_GRID_X+(int)cursor.column*SEL_PITCH-2)*2,y=SEL_GRID_Y+(int)cursor.row*SEL_ROW_PITCH-2;
    const int w=(SEL_CELL_W+4)*2,h=SEL_CELL_H+4;
    rect(x,y,w,3,code);rect(x,y+h-3,w,3,code);rect(x,y,4,h,code);rect(x+w-4,y,4,h,code);
}

static void select_flow(uint8_t picks[2]) {
    pads_t pads;
    t3_select_cursor_t cursor[2]={t3_select_find(picks[0]),t3_select_find(picks[1])};
    unsigned chosen[2]={0,0},shown[2]={FIGHTERS,FIGHTERS},rest[2]={0,0},fade_in[2]={0,0};
    backdrop_show(0);
    SAT_PANIC_IF_ERROR(load_backdrop("SELBG.BIN"));
    backdrop_view(0);
    backdrop_show(1);
    for(unsigned elapsed=0,t0=g_now;elapsed<SELECT_FRAMES&&!(chosen[0]&&chosen[1]);elapsed=g_now-t0) {
        frame_begin(&pads);
        fade(elapsed<FADE_FRAMES?-255+(int)(elapsed*255/FADE_FRAMES):0);
        if(pads.pad[1].connected) {
            for(unsigned side=0;side<2;++side) {
                if(chosen[side])continue;
                cursor[side]=steer(cursor[side],&pads.pad[side]);
                if(pads.pad[side].pressed&START_BUTTONS)chosen[side]=1;
            }
        } else if(!chosen[0]) {
            cursor[0]=steer(cursor[0],&pads.pad[0]);
            if(pads.pad[0].pressed&START_BUTTONS)chosen[0]=1;
        } else if(pads.pad[0].pressed&SAT_PAD_B) {
            chosen[0]=0;
        } else {
            cursor[1]=steer(cursor[1],&pads.pad[0]);
            if(pads.pad[0].pressed&START_BUTTONS)chosen[1]=1;
        }
        for(unsigned side=0;side<2;++side) {
            const unsigned id=t3_select_id(cursor[side]);
            const int bg_x=side?SEL_PORT_RIGHT_X:SEL_PORT_LEFT_X;
            if(id!=shown[side]) {
                if(++rest[side]>=PORT_REST_FRAMES) {
                    portrait_palette(side,0);
                    SAT_PANIC_IF_ERROR(load_portrait(side,id));
                    shown[side]=id;rest[side]=0;fade_in[side]=0;
                }
            } else rest[side]=0;
            if(shown[side]<FIGHTERS && fade_in[side]<=PORT_FADE_FRAMES)
                portrait_palette(side,2u*fade_in[side]++);
            if(shown[side]<FIGHTERS) {
                const unsigned pulse=(elapsed>>3)&3u,band=SEL_PORT_Y;
                for(unsigned i=0;i<4;++i)
                    rect((bg_x-2)*2,band+(int)i*(MENU_PORT_H/4),(MENU_PORT_W+4)*2,MENU_PORT_H/4,
                         (side?GLOW_P2:GLOW_P1)+((i+pulse)&3u));
                (void)sat_draw_sprite_scaled_screen(&g_port_tex[side],(int16_t)((bg_x+MENU_PORT_W/2)*2),
                    (int16_t)(SEL_PORT_Y+MENU_PORT_H/2),PORT_STRIDE*2,MENU_PORT_H,0);
            }
            text(0,t3_fighter_name(id),bg_x+MENU_PORT_W/2,SEL_PLATE_Y+1);
            text(2,side?(pads.pad[1].connected?"2P":"CPU"):"1P",bg_x+MENU_PORT_W/2,SEL_PORT_Y+4);
        }
        text(2,"TIME",MENU_BG_W/2,SEL_PORT_Y+40);
        number(0,(SELECT_FRAMES-elapsed+SELECT_TIMER_UNIT-1u)/SELECT_TIMER_UNIT,MENU_BG_W/2,SEL_PORT_Y+60);
        text(0,"VS",MENU_BG_W/2,SEL_PORT_Y+104);
        for(unsigned side=0;side<2;++side) {
            if(cursor[side].column>=T3_SELECT_DRAWN_COLUMNS)continue;
            if(!chosen[side]&&(elapsed&16u)&&!(side==0u&&!pads.pad[1].connected&&chosen[0]))continue;
            cell_frame(side,cursor[side],elapsed,(int)chosen[side]);
        }
        if(!pads.pad[1].connected&&chosen[0])text(1,"CHOOSE THE OPPONENT  B: BACK",MENU_BG_W/2,SEL_PORT_Y+150);
        frame_end();
    }
    picks[0]=(uint8_t)t3_select_id(cursor[0]);
    picks[1]=(uint8_t)t3_select_id(cursor[1]);
    for(unsigned f=0,t0=g_now;f<FADE_FRAMES;f=g_now-t0) {
        fade(-(int)(f*255/FADE_FRAMES));
        frame_begin(&pads);
        frame_end();
    }
}

int main(void) {
    uint32_t arg_bytes=0;
    uint8_t arg[SAT_EXEC_ARG_MAX];
    init_video();
    SAT_PANIC_IF_ERROR(t3_disc_volume(&g_volume));
    if(sat_exec_arg(arg,sizeof arg,&arg_bytes)==SAT_OK && arg_bytes==BOOT_ARG_BYTES)__builtin_memcpy(g_arg,arg,sizeof g_arg);
    else g_arg[0]=0;                                  /* cold boot: the title first */
    if(g_arg[0]!=PHASE_SELECT)(void)title_flow();
    select_flow(&g_arg[1]);
    g_arg[0]=PHASE_FIGHT;
    SAT_PANIC_IF_ERROR(sat_exec(g_volume,"0.BIN",g_arg,sizeof g_arg));
    for(;;) {}
}
