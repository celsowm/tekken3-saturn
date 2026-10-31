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
#include "stage_cdda.h"
#include "menu_text.h"
#include "saturn/app.h"
#include "saturn/audio.h"
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
    BG_HEADER=12+512, PORT_STRIDE=128, PORT_BYTES=PORT_STRIDE*MENU_PORT_H, PORT_PAL_WORDS=92,
    PORT_HEADER=4, PORT_FILE=PORT_HEADER+PORT_PAL_WORDS*2+PORT_BYTES, FIGHTERS=21, SILHOUETTE=21,
    FONT_SETS=3, TILE_BASE=0x40000, TILE_BANK=1,
    CODE_WHITE=1, CODE_GREY=2, CODE_GOLD=3, CODE_P1=4, CODE_P2=5, CODE_P1_HI=6, CODE_P2_HI=7,
    SLOT_A=8, SLOT_B=100, GLOW_P1=192, GLOW_P2=198, GLOW_BANDS=6, CODE_BLACK=204, CODE_BAR=205, CODE_BAR_EDGE=206,
    BOOT_ARG_BYTES=8, PHASE_SELECT='S', PHASE_FIGHT='F', PHASE_DEMO='D', PHASE_RESULT='R', MODE_DEMO=100,
    FADE_FRAMES=32, SELECT_TIMER_UNIT=93, SELECT_FRAMES=20*SELECT_TIMER_UNIT,
    PORT_REST_FRAMES=4, PORT_FADE_FRAMES=16,
    LAYERS=SAT_VDP2_LAYER_NBG0|SAT_VDP2_LAYER_NBG1|SAT_VDP2_LAYER_SPRITE|SAT_VDP2_LAYER_BACK
};

static sat_cdfs_volume_t* g_volume;
/* The largest file read in one go: a backdrop. */
static uint8_t g_io[BG_HEADER+MENU_BG_W*512+16] __attribute__((aligned(4)));
static uint16_t g_pal[256];
static sat_ascii_font_t g_font[FONT_SETS];   /* white, grey, gold: the SDK font, for the plain pages */
static sat_vdp1_texture_t g_port_tex[2];
static uint16_t g_port_pal[2][PORT_PAL_WORDS];
/* Every portrait is read up front: the select music plays from the same disc, and a data read stops it. */
static uint8_t g_port_cache[SILHOUETTE+1][PORT_FILE] __attribute__((section(".wram_l"),aligned(4)));
static uint8_t g_arg[BOOT_ARG_BYTES]={PHASE_SELECT,0,1,0,0,0,0,0};

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
    g_pal[CODE_P1]=(uint16_t)w555(31,5,5);
    g_pal[CODE_P2]=(uint16_t)w555(6,14,31);
    g_pal[CODE_BLACK]=(uint16_t)w555(0,0,0);
    g_pal[CODE_BAR]=(uint16_t)w555(15,15,16);
    g_pal[CODE_BAR_EDGE]=(uint16_t)w555(6,6,7);
    g_pal[CODE_P1_HI]=(uint16_t)w555(31,22,18);
    g_pal[CODE_P2_HI]=(uint16_t)w555(20,26,31);
    for(unsigned i=0;i<4;++i) {
        g_pal[GLOW_P1+i]=(uint16_t)w555(1+i,2+2*i,5+4*i);
        g_pal[GLOW_P2+i]=(uint16_t)w555(5+4*i,2+2*i,1+i);
    }
}

static void fade(int level);

static void init_fonts(void) {
    static const uint8_t codes[FONT_SETS]={CODE_WHITE,CODE_GREY,CODE_GOLD};
    for(unsigned set=0;set<FONT_SETS;++set)
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
    /* The VDP1 framebuffer holds whatever the last program left until the first frame is drawn, and
     * loading the fonts takes seconds: show black, and send one empty frame to clear it. */
    SAT_PANIC_IF_ERROR(sat_vdp2_color_offset_enable(LAYERS,SAT_VDP2_COLOR_OFFSET_A));
    fade(-255);
    (void)sat_vdp2_layers_commit();
    SAT_PANIC_IF_ERROR(sat_begin_frame());
    SAT_PANIC_IF_ERROR(sat_end_frame());
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
    layer.priority=2;
    layer.transparent=1;
    for(unsigned i=0;i<4;++i)layer.plane_address[i]=BG_BASE;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&layer));
    /* 352 baked columns over 704 dots. */
    layer.priority=2;
    layer.transparent=1;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&layer));
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0,0x20000u,0x10000u));
    /* The scrolling steel tiles behind the select's frame. */
    sat_vdp2_layer_config_default(SAT_VDP2_NBG1,&layer);
    layer.bitmap=1;
    layer.bitmap_size=SAT_VDP2_BITMAP_512X512;
    layer.color_mode=SAT_VDP2_COLOR_MODE_256;
    layer.palette=TILE_BANK;
    layer.priority=1;
    layer.transparent=0;
    for(unsigned i=0;i<4;++i)layer.plane_address[i]=TILE_BASE;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&layer));
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_zoom(SAT_VDP2_NBG1,0x20000u,0x10000u));
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_enabled(SAT_VDP2_NBG1,0));
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_scroll(SAT_VDP2_NBG0,0,0));
    SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(SAT_COLOR_BLACK));
    SAT_PANIC_IF_ERROR(sat_vdp2_color_offset_enable(LAYERS,SAT_VDP2_COLOR_OFFSET_A));
}

static void fade(int level) {
    const sat_vdp2_color_offset_t offset={(int16_t)level,(int16_t)level,(int16_t)level};
    (void)sat_vdp2_color_offset_set(SAT_VDP2_COLOR_OFFSET_A,&offset);
}

static void backdrop_show(int on) {(void)sat_vdp2_layer_set_enabled(SAT_VDP2_NBG0,(uint8_t)on);}

/* A baked picture ('T3BG0001' + 256 palette words + w*h codes) into a w-wide VDP2 bitmap of
 * `stride` x `rows` dots, padded with code 0 so a wrapped view never shows old VRAM. */
static sat_result_t load_bitmap(const char* name,uint32_t base,unsigned bank,unsigned w,unsigned h,
                                unsigned rows) {
    uint16_t palette[256];
    static uint16_t row[BG_STRIDE/2];
    SAT_TRY(read_file(name,0,g_io,BG_HEADER+w*h));
    for(unsigned i=0;i<8;++i)if(g_io[i]!=(uint8_t)"T3BG0001"[i])return SAT_ERR_VERSION;
    __builtin_memcpy(palette,g_io+12,sizeof palette);
    SAT_TRY(sat_vdp2_palette_upload(palette,256,(uint16_t)(bank*256u)));
    for(unsigned y=0;y<rows;++y) {
        __builtin_memset(row,0,sizeof row);
        if(y<h)__builtin_memcpy(row,g_io+BG_HEADER+y*w,w);
        SAT_TRY(sat_vdp2_vram_write_words((base+y*BG_STRIDE)/2u,row,BG_STRIDE/2u));
    }
    return SAT_OK;
}

static sat_result_t load_backdrop(const char* name) {
    return load_bitmap(name,BG_BASE,BG_BANK,MENU_BG_W,MENU_BG_H,BG_STRIDE);
}

/* ---- sounds ---- */

enum { SFX_MOVE, SFX_PICK, SFX_UPDOWN, SFX_CONFIRM, SFX_START, SFX_COUNT, SFX_HEAD=64 };
static sat_sound_t g_sfx[SFX_COUNT];
static int g_sfx_ready;

/* MENUSFX.BIN: the original menu sounds (scripts/extract_menu_sfx.py). Silent if they are absent. */
static void init_sfx(void) {
    uint8_t* const data=g_io;
    sat_cdfs_file_t file;
    if(sat_cdfs_lookup(g_volume,"MENUSFX.BIN",&file)!=SAT_OK || file.size>sizeof g_io)return;
    if(read_file("MENUSFX.BIN",0,data,file.size)!=SAT_OK || sat_audio_init()!=SAT_OK)return;
    for(unsigned i=0;i<SFX_COUNT;++i) {
        const uint32_t* row=(const uint32_t*)(data+12*i);       /* offset, samples, rate */
        const sat_sound_desc_t desc={data+SFX_HEAD+row[0],row[1],row[2],0,0,SAT_AUDIO_PCM_S8,0,0,0};
        if(sat_sound_create(&g_sfx[i],&desc)!=SAT_OK)return;
    }
    g_sfx_ready=1;
}

static void sfx(unsigned id) {
    const sat_sound_play_params_t params={SAT_AUDIO_VOLUME_MAX,SAT_AUDIO_PAN_CENTER,1u,0u,SAT_FX16_ONE,0u};
    if(g_sfx_ready)(void)sat_sound_play(g_sfx[id],&params,NULL);
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

static void frame_end(void) {
    SAT_PANIC_IF_ERROR(sat_end_frame());
    if(g_sfx_ready)(void)sat_audio_update();
#if defined(T3_SELECT_CDDA_SECTORS)
    t3_stage_cdda_mix();
#endif
}

/* Text in baked-bitmap coordinates (352 wide): the screen is twice as wide. */
static void text(unsigned set,const char* s,int bg_cx,int y) {
    (void)sat_ascii_font_draw_text_screen_centered_indexed8(&g_font[set],s,bg_cx*2,y,14,0,0);
}

/* The strings in the original fonts (TEXT.BIN, scripts/pack_menu_art.py): glyph codes ready for the
 * sprite bank, one group per screen. A picture is drawn 2 dots per texel across and 1:1 down: a
 * vertical scale would shimmer between the interlaced fields. */
static uint8_t g_text_blob[160*1024] __attribute__((section(".wram_l"),aligned(4)));
static const uint8_t* g_text_data[TX_COUNT];
static sat_vdp1_texture_t g_tx[TX_COUNT];
static unsigned g_text_palettes;

static unsigned be16(const uint8_t* p) {return ((unsigned)p[0]<<8)|p[1];}

static sat_result_t read_text(void) {
    sat_cdfs_file_t file;
    SAT_TRY(sat_cdfs_lookup(g_volume,"TEXT.BIN",&file));
    if(file.size>sizeof g_text_blob)return SAT_ERR_CAPACITY;
    SAT_TRY(read_file("TEXT.BIN",0,g_text_blob,file.size));
    for(unsigned i=0;i<8;++i)if(g_text_blob[i]!=(uint8_t)"T3TX0001"[i])return SAT_ERR_VERSION;
    g_text_palettes=be16(g_text_blob+8);
    const uint8_t* at=g_text_blob+12+g_text_palettes*34u;
    for(unsigned i=0;i<TX_COUNT;++i) {
        g_text_data[i]=at;                       /* group, pad, w, h, codes */
        at+=6u+be16(at+2)*be16(at+4);
    }
    return SAT_OK;
}

/* Uploads one screen's palettes into the master palette and its pictures into VDP1 VRAM. */
static void load_text_group(unsigned group) {
    const uint8_t* at=g_text_blob+12;
    for(unsigned i=0;i<g_text_palettes;++i,at+=34)
        if(at[0]==group)for(unsigned c=0;c<16;++c)g_pal[at[1]+c]=(uint16_t)be16(at+2+2*c);
    for(unsigned i=0;i<TX_COUNT;++i)
        if(g_text_data[i][0]==group)
            SAT_PANIC_IF_ERROR(sat_tex_upload_indexed8(&g_tx[i],g_text_data[i]+6,
                (uint16_t)be16(g_text_data[i]+2),(uint16_t)be16(g_text_data[i]+4),g_pal,SPR_BANK));
}

/* Draws picture `id` with its left edge at bg_x (352-wide units) and its middle at screen y. */
static void pic_left(unsigned id,int bg_x,int y) {
    const unsigned w=be16(g_text_data[id]+2),h=be16(g_text_data[id]+4);
    (void)sat_draw_sprite_scaled_screen(&g_tx[id],(int16_t)(bg_x*2+(int)w),(int16_t)y,(uint16_t)(w*2),
                                        (uint16_t)h,0);
}

/* The same, centred on bg_cx by the glyphs' own width. */
static void pic(unsigned id,int bg_cx,int y) {
    pic_left(id,bg_cx-(int)g_text_size[id][0]/2*1,y);
}

static void rect(int x,int y,int w,int h,unsigned code) {
    (void)sat_draw_rect_screen((int16_t)x,(int16_t)y,(uint16_t)w,(uint16_t)h,(uint16_t)code);
}

/* ---- title ---- */

typedef enum {MODE_ARCADE,MODE_VS,MODE_TEAM,MODE_TIME,MODE_SURVIVAL,MODE_FORCE,MODE_PRACTICE,MODE_OPTION}
    mode_t3;
enum { MODES=8 };
#define START_BUTTONS (SAT_PAD_START|SAT_PAD_A|SAT_PAD_B|SAT_PAD_C|SAT_PAD_X|SAT_PAD_Y|SAT_PAD_Z)

static int mode_playable(unsigned mode,const pads_t* pads) {
    return mode==MODE_ARCADE || mode==MODE_PRACTICE || mode==MODE_OPTION || mode==MODE_TIME ||
           mode==MODE_SURVIVAL ||
           (mode==MODE_VS && pads->pad[1].connected);
}

static void prompt(const pads_t* pads,unsigned frame) {
    if(!(frame&0x30u))return;
    pic(pads->pad[1].connected?TX_PROMPT_BOTH:TX_PROMPT_ONE,MENU_BG_W/2,TITLE_PROMPT_Y+12);
}

static void copyright(void) {
    pic_left(TX_COPYRIGHT,43,TITLE_COPY_Y0+8);
    pic_left(TX_RESERVED,43,TITLE_COPY_Y1+8);
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
            rect(0,0,SCREEN_W,SCREEN_H,CODE_BLACK);   /* nothing behind: do not trust the framebuffer */
            pic_left(TX_NAMCO,112,MENU_BG_H/2);
            pic_left(TX_PRESENTS,112+(int)g_text_size[TX_NAMCO][0]+9,MENU_BG_H/2);
            frame_end();
            if(pads.pad[0].pressed&SAT_PAD_START) {sfx(SFX_START);goto menu;}
        }
        /* White flash: 25 frames up, 11 held; then the picture fades in from white over 86. */
        for(unsigned f=0,t0=g_now;f<36;f=g_now-t0) {
            fade(f<25?(int)(10*f+10):255);
            frame_begin(&pads);
            rect(0,0,SCREEN_W,SCREEN_H,CODE_BLACK);
            frame_end();
        }
        backdrop_show(1);
        for(unsigned f=0,t0=g_now;f<86;f=g_now-t0) {
            fade(255-(int)(f*255/86));
            frame_begin(&pads);
            frame_end();
            if(pads.pad[0].pressed&SAT_PAD_START) {sfx(SFX_START);goto menu;}
        }
        fade(0);
        for(unsigned f=0,t0=g_now;f<300;f=g_now-t0) {
            frame_begin(&pads);
            prompt(&pads,f);
            copyright();
            frame_end();
            if(pads.pad[0].pressed&START_BUTTONS || pads.pad[1].pressed&START_BUTTONS) {sfx(SFX_START);goto menu;}
        }
        for(unsigned f=0,t0=g_now;f<FADE_FRAMES;f=g_now-t0) {
            fade(-(int)(f*255/FADE_FRAMES));
            frame_begin(&pads);
            frame_end();
        }
        return MODE_DEMO;       /* nobody pressed anything: the demonstration fight */
    }
menu:;
    {
        /* The ring of modes around the selected row, 26 dots apart (the original's 28 of 480). */
        unsigned selected=0,idle=0,option=0;
        backdrop_show(1);
        fade(0);
        for(;;) {
            frame_begin(&pads);
            idle+=2u;
            if(idle>480u && !option) {
                frame_end();
                return title_flow();
            }
            rect(0,296,SCREEN_W,SCREEN_H-296,CODE_BLACK);
            if(option) {
                text(2,"OPTION MODE",MENU_BG_W/2,330);
                text(0,g_arg[4]?"DEBUG: ON":"DEBUG: OFF",MENU_BG_W/2,370);
                if(pads.pad[0].pressed&(SAT_PAD_LEFT|SAT_PAD_RIGHT|SAT_PAD_A|SAT_PAD_X))g_arg[4]^=1u;
                if(pads.pad[0].pressed&(SAT_PAD_B|SAT_PAD_START))option=0;
                frame_end();
                continue;
            }
            rect(0,350-12,SCREEN_W,24,CODE_BAR_EDGE);
            rect(48,350-12,SCREEN_W-96,24,CODE_BAR);
            for(int k=-2;k<=2;++k) {
                const unsigned mode=(selected+MODES+(unsigned)(k+MODES))%MODES;
                const int distance=k<0?-k:k;
                const unsigned variant=distance==0?0u:distance==2||!mode_playable(mode,&pads)?2u:1u;
                pic(TX_MODE0_SELECTED+mode*3u+variant,MENU_BG_W/2,350+k*26);
            }
            if(pads.pad[0].pressed&SAT_PAD_UP) {selected=selected?selected-1u:MODES-1u;idle=0;sfx(SFX_UPDOWN);}
            if(pads.pad[0].pressed&SAT_PAD_DOWN) {selected=(selected+1u)%MODES;idle=0;sfx(SFX_UPDOWN);}
            if((pads.pad[0].pressed&START_BUTTONS) && mode_playable(selected,&pads)) {
                sfx(SFX_CONFIRM);
                if(selected==MODE_OPTION)option=1;
                else {frame_end();return selected;}
            }
            frame_end();
        }
    }
}

/* ---- run results ---- */

static void digits(char* out,unsigned value,unsigned width) {
    for(unsigned i=width;i-->0;value/=10u)out[i]=(char)('0'+value%10u);
}

/* After a survival loss or the tenth time attack win: the wins or the time, until START. */
static void result_flow(void) {
    pads_t pads;
    const int survival=((g_arg[4]>>2)&3u)==1u;
    const unsigned seconds=((unsigned)g_arg[6]<<8)|g_arg[7];
    char line[16]={0};
    SAT_PANIC_IF_ERROR(load_backdrop("TITLEBG.BIN"));
    backdrop_show(1);
    fade(0);
    if(survival) {__builtin_memcpy(line,"WINS 00",8);digits(line+5,g_arg[5],2);}
    else {__builtin_memcpy(line,"TIME 00:00",11);digits(line+5,(seconds/60u)%100u,2);digits(line+8,seconds%60u,2);}
    for(unsigned f=0,t0=g_now;f<1800u;f=g_now-t0) {
        frame_begin(&pads);
        rect(0,296,SCREEN_W,SCREEN_H-296,CODE_BLACK);
        text(2,survival?"SURVIVAL RESULT":"TIME ATTACK RESULT",MENU_BG_W/2,330);
        text(0,line,MENU_BG_W/2,370);
        frame_end();
        if(pads.pad[0].pressed&START_BUTTONS)break;
    }
}

/* ---- character select ---- */

static t3_select_cursor_t steer(t3_select_cursor_t cursor,const sat_pad_state_t* pad) {
    const int across=(pad->pressed&SAT_PAD_RIGHT)?1:(pad->pressed&SAT_PAD_LEFT)?-1:0;
    if(pad->pressed&(SAT_PAD_RIGHT|SAT_PAD_LEFT|SAT_PAD_UP|SAT_PAD_DOWN))sfx(SFX_MOVE);
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

/* The panel behind a portrait is its dominant colour, brightest at the top. */
static void glow_palette(unsigned side,unsigned colour) {
    static const unsigned level[GLOW_BANDS]={31,26,21,16,11,7};
    uint16_t out[GLOW_BANDS];
    for(unsigned i=0;i<GLOW_BANDS;++i)
        out[i]=(uint16_t)w555(((colour&31u)*level[i])/31u,(((colour>>5)&31u)*level[i])/31u,
                              (((colour>>10)&31u)*level[i])/31u);
    (void)sat_vdp2_palette_upload(out,GLOW_BANDS,(uint16_t)(SPR_BANK*256u+(side?GLOW_P2:GLOW_P1)));
}

/* The original name pictures (NAMES.BIN): a black glyph mask per fighter, 96 x 16, and its extent. */
enum { NAME_W=96, NAME_H=16, NAME_HEAD=48 };
static sat_vdp1_texture_t g_name[SILHOUETTE];
static uint8_t g_name_extent[SILHOUETTE][2];

static sat_result_t load_names(void) {
    static uint8_t pixels[NAME_W*NAME_H];
    SAT_TRY(read_file("NAMES.BIN",0,g_io,NAME_HEAD+SILHOUETTE*NAME_W*NAME_H));
    for(unsigned id=0;id<SILHOUETTE;++id) {
        g_name_extent[id][0]=g_io[2*id];g_name_extent[id][1]=g_io[2*id+1];
        const uint8_t* mask=g_io+NAME_HEAD+id*NAME_W*NAME_H;
        for(unsigned i=0;i<NAME_W*NAME_H;++i)pixels[i]=mask[i]?CODE_BLACK:0;
        SAT_TRY(sat_tex_upload_indexed8(&g_name[id],pixels,NAME_W,NAME_H,g_pal,SPR_BANK));
    }
    return SAT_OK;
}

static void draw_name(unsigned id,int bg_cx,int plate_y) {
    const int centre=NAME_W/2-(g_name_extent[id][0]+g_name_extent[id][1])/2;
    (void)sat_draw_sprite_scaled_screen(&g_name[id],(int16_t)((bg_cx+centre)*2),
        (int16_t)(plate_y+NAME_H/2),NAME_W*2,NAME_H,0);
}

static sat_result_t cache_portraits(void) {
    SAT_TRY(load_names());
    for(unsigned id=0;id<=SILHOUETTE;++id) {
        char name[12]={'P','O','R','T',(char)('0'+id/10u),(char)('0'+id%10u),'.','B','I','N',0,0};
        SAT_TRY(read_file(name,0,g_port_cache[id],PORT_FILE));
    }
    return SAT_OK;
}

static sat_result_t load_portrait(unsigned side,unsigned id) {
    const unsigned offset=side?SLOT_B-1u:SLOT_A-1u;
    __builtin_memcpy(g_io,g_port_cache[id],PORT_FILE);
    glow_palette(side,((unsigned)g_io[0]<<8)|g_io[1]);
    __builtin_memcpy(g_port_pal[side],g_io+PORT_HEADER,sizeof g_port_pal[side]);
    uint8_t* pixels=g_io+PORT_HEADER+PORT_PAL_WORDS*2;
    for(unsigned i=0;i<PORT_BYTES;++i)if(pixels[i])pixels[i]=(uint8_t)(pixels[i]+offset);
    return sat_vdp1_vram_write((uint32_t)g_port_tex[side].srca*8u,pixels,PORT_BYTES);
}

static void cell_frame(unsigned side,t3_select_cursor_t cursor,unsigned frame,int chosen) {
    const unsigned code=(chosen||(frame&8u))?(side?CODE_P2_HI:CODE_P1_HI):(side?CODE_P2:CODE_P1);
    const int x=(SEL_GRID_X+(int)cursor.column*SEL_PITCH-2)*2,y=SEL_GRID_Y+(int)cursor.row*SEL_ROW_PITCH-2;
    const int w=(SEL_CELL_W+4)*2,h=SEL_CELL_H+4;
    rect(x,y,w,3,code);rect(x,y+h-3,w,3,code);rect(x,y,4,h,code);rect(x+w-4,y,4,h,code);
}

typedef struct {int16_t x,y;int16_t life;} spark_t;
enum { SPARKS=8 };
static spark_t g_spark[SPARKS];


static void select_flow(uint8_t picks[2]) {
    pads_t pads;
    t3_select_cursor_t cursor[2]={t3_select_find(picks[0]),t3_select_find(picks[1])};
    unsigned chosen[2]={0,0},shown[2]={FIGHTERS+1,FIGHTERS+1},rest[2]={0,0},fade_in[2]={0,0};
    backdrop_show(0);
    SAT_PANIC_IF_ERROR(load_backdrop("SELBG.BIN"));
    SAT_PANIC_IF_ERROR(load_bitmap("TILEBG.BIN",TILE_BASE,TILE_BANK,MENU_BG_W,512,512));
    load_text_group(1);
    SAT_PANIC_IF_ERROR(cache_portraits());
    (void)sat_vdp2_layer_set_enabled(SAT_VDP2_NBG1,1);
    backdrop_show(1);
#if defined(T3_SELECT_CDDA_SECTORS)
    if(t3_select_cdda_start()==SAT_OK)t3_stage_cdda_mix();
#endif
    for(unsigned elapsed=0,t0=g_now;elapsed<SELECT_FRAMES&&!(chosen[0]&&chosen[1]);elapsed=g_now-t0) {
        frame_begin(&pads);
        fade(elapsed<FADE_FRAMES?-255+(int)(elapsed*255/FADE_FRAMES):0);
        if(pads.pad[1].connected) {
            for(unsigned side=0;side<2;++side) {
                if(chosen[side])continue;
                cursor[side]=steer(cursor[side],&pads.pad[side]);
                if(pads.pad[side].pressed&START_BUTTONS) {chosen[side]=1;sfx(SFX_PICK);}
            }
        } else if(!chosen[0]) {
            cursor[0]=steer(cursor[0],&pads.pad[0]);
            if(pads.pad[0].pressed&START_BUTTONS) {chosen[0]=1;sfx(SFX_PICK);}
        } else if(pads.pad[0].pressed&SAT_PAD_B) {
            chosen[0]=0;
        } else {
            cursor[1]=steer(cursor[1],&pads.pad[0]);
            if(pads.pad[0].pressed&START_BUTTONS) {chosen[1]=1;sfx(SFX_PICK);}
        }
        /* Until the first pick is made the CPU's side is the empty silhouette. */
        const int empty1=!pads.pad[1].connected&&!chosen[0];
        for(unsigned side=0;side<2;++side) {
            const unsigned id=(side&&empty1)?SILHOUETTE:t3_select_id(cursor[side]);
            const int bg_x=side?SEL_PORT_RIGHT_X:SEL_PORT_LEFT_X;
            if(id!=shown[side]) {
                if(++rest[side]>=PORT_REST_FRAMES) {
                    portrait_palette(side,0);
                    SAT_PANIC_IF_ERROR(load_portrait(side,id));
                    shown[side]=id;rest[side]=0;fade_in[side]=0;
                }
            } else rest[side]=0;
            if(shown[side]<=SILHOUETTE && fade_in[side]<=PORT_FADE_FRAMES)
                portrait_palette(side,2u*fade_in[side]++);
            if(shown[side]<=SILHOUETTE) {
                const int band=(SEL_GLOW_H+GLOW_BANDS-1)/GLOW_BANDS;
                for(unsigned i=0;i<GLOW_BANDS;++i)
                    rect((bg_x-2)*2,SEL_PORT_Y+(int)i*band,(MENU_PORT_W+4)*2,
                         i+1==GLOW_BANDS?SEL_GLOW_H-(int)i*band:band,(side?GLOW_P2:GLOW_P1)+i);
                (void)sat_draw_sprite_scaled_screen(&g_port_tex[side],(int16_t)((bg_x+MENU_PORT_W/2)*2),
                    (int16_t)(SEL_SPRITE_Y+MENU_PORT_H/2),PORT_STRIDE*2,MENU_PORT_H,0);
            }
            if(id!=SILHOUETTE)draw_name(id==20u?14u:id,bg_x+MENU_PORT_W/2,SEL_PLATE_Y);
        }
        /* Rising sparks: every 4th tick a 40-dot streak leaves a portrait, clipped above the plates. */
        if(!(elapsed&3u)) {
            static unsigned seed=12345u;
            seed=seed*1103515245u+12345u;
            for(unsigned i=0;i<SPARKS;++i)if(!g_spark[i].life) {
                const int bg_x=(elapsed&4u)?SEL_PORT_RIGHT_X:SEL_PORT_LEFT_X;
                g_spark[i]=(spark_t){(int16_t)((bg_x+(int)((seed>>16)%MENU_PORT_W))*2),(int16_t)SEL_PLATE_Y,24};
                break;
            }
        }
        for(unsigned i=0;i<SPARKS;++i)if(g_spark[i].life) {
            spark_t* spark=&g_spark[i];
            spark->y=(int16_t)(spark->y-5);
            --spark->life;
            if(spark->y<SEL_PORT_Y+40)spark->life=0;
            else {
                rect(spark->x,spark->y,2,40,spark->life>12?CODE_WHITE:CODE_GREY);
            }
        }
        /* The big gold timer between the portraits. */
        {
            const unsigned left=(SELECT_FRAMES-elapsed+SELECT_TIMER_UNIT-1u)/SELECT_TIMER_UNIT;
            for(unsigned digit=0;digit<2;++digit) {
                const unsigned value=digit?left%10u:(left/10u)%10u;
                (void)sat_draw_sprite_scaled_screen(&g_tx[TX_DIGIT0+value],
                    (int16_t)(MENU_BG_W+(int)digit*50-25),(int16_t)252,48,38,0);
            }
        }
        for(unsigned side=0;side<2;++side) {
            if(cursor[side].column>=T3_SELECT_DRAWN_COLUMNS)continue;
            if(side&&empty1)continue;
            if(!chosen[side]&&(elapsed&16u)&&!(side==0u&&!pads.pad[1].connected&&chosen[0]))continue;
            cell_frame(side,cursor[side],elapsed,(int)chosen[side]);
            pic_left(side?(pads.pad[1].connected?TX_TAG_2P:TX_TAG_CPU):TX_TAG_1P,
                     SEL_GRID_X+(int)cursor[side].column*SEL_PITCH+2,
                     SEL_GRID_Y+(int)cursor[side].row*SEL_ROW_PITCH+9);
        }
        if(empty1 && (elapsed&32u))pic_left(TX_INSERT_COIN,MENU_BG_W-128,SEL_PORT_Y-26);
        /* The steel tiles drift diagonally. */
        (void)sat_vdp2_layer_set_scroll(SAT_VDP2_NBG1,0,(int32_t)((elapsed*3u/2u)&511u)<<16);
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
    init_sfx();
    SAT_PANIC_IF_ERROR(read_text());
    load_text_group(0);
    if(sat_exec_arg(arg,sizeof arg,&arg_bytes)==SAT_OK && arg_bytes==BOOT_ARG_BYTES)__builtin_memcpy(g_arg,arg,sizeof g_arg);
    else g_arg[0]=0;                                  /* cold boot: the title first */
    unsigned mode=MODE_ARCADE;
    if(g_arg[0]==PHASE_RESULT) {result_flow();g_arg[0]=0;}
    if(g_arg[0]!=PHASE_SELECT && (mode=title_flow())!=MODE_DEMO)
    {
        /* bit 1 practice, bits 2-3 run mode (1 survival, 2 time attack); a fresh run starts at no wins. */
        const unsigned run=mode==MODE_SURVIVAL?1u:mode==MODE_TIME?2u:0u;
        g_arg[4]=(uint8_t)((g_arg[4]&1u)|(mode==MODE_PRACTICE?2u:0u)|(run<<2));
        g_arg[5]=g_arg[6]=g_arg[7]=0;
    }
    if(mode==MODE_DEMO) {
        /* The demonstration: two CPU fighters from a rotation, ended by any button. */
        static const uint8_t pairs[][2]={{1,8},{0,3},{7,4},{14,15},{5,2},{9,6}};
        const unsigned pair=(g_now/2u)%(sizeof pairs/sizeof pairs[0]);
        g_arg[0]=PHASE_DEMO;g_arg[1]=pairs[pair][0];g_arg[2]=pairs[pair][1];
        SAT_PANIC_IF_ERROR(sat_exec(g_volume,"0.BIN",g_arg,sizeof g_arg));
    }
    select_flow(&g_arg[1]);
    g_arg[0]=PHASE_FIGHT;
    SAT_PANIC_IF_ERROR(sat_exec(g_volume,"0.BIN",g_arg,sizeof g_arg));
    for(;;) {}
}
