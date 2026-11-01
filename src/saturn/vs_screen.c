/* The pre-fight VS screen: one 352x448 VDP2 bitmap at 2x zoom on a 704x448 display,
 * composed from the parts scripts/pack_vs_art.py bakes. It stays up, with VDP1 idle,
 * while the fight loads; the original also leaves its last frame up (modes.md).
 * Rules from Tekken3Remake docs/research/code/modes.md. PolyForm Noncommercial 1.0.0,
 * NOTICE.md. Required Notice: Copyright (c) 2026 n0madic. */
#include "vs_screen.h"
#include "disc.h"
#include "saturn/font.h"
#include "saturn/saturn.h"
#include "saturn/time.h"
#include "saturn/vdp2.h"
#include "saturn/vdp2_color_offset.h"
#include "saturn/vdp2_layers.h"
#include "saturn/video.h"
#include "vs_layout.h"

enum {
    SCREEN_W=704, SCREEN_H=448, STRIDE=512, BG_HEADER=12, BG_PALETTE=512, BANK=7,
    TEXT_BYTES=2+VS_TEXT_W*VS_TEXT_H, CAPTIONS=VS_FIGHTERS, CAPTION_STAGE=CAPTIONS, CAPTION_FIGHT=CAPTIONS+1,
    CAPTION_PRACTICE=CAPTIONS+2, DIGITS=CAPTIONS+3, CAPTION_SPACE=13, CREDIT_Y=438, CREDIT_GLYPH=8,
    MINIMUM_FIELDS=60, FADE_FIELDS=16,
    /* Bank B: the stage picture streams into bank A while this one is up. */
    BITMAP_BASE=0x40000,
    LAYERS=SAT_VDP2_LAYER_ALL
};

static sat_cdfs_volume_t* g_volume;
static uint32_t g_shown_at;

static sat_result_t read_part(const char* name,uint32_t offset,void* out,uint32_t bytes) {
    sat_cdfs_file_t file;
    uint32_t read=0;
    SAT_TRY(sat_cdfs_lookup(g_volume,name,&file));
    if(offset+bytes>file.size)return SAT_ERR_IO;
    SAT_TRY(sat_cdfs_read_at(g_volume,&file,offset,out,bytes,&read));
    return read==bytes?SAT_OK:SAT_ERR_IO;
}

static void paste(uint8_t* image,const uint8_t* picture,unsigned x,unsigned y,unsigned width,unsigned height,
                  unsigned stride) {
    for(unsigned row=0;row<height;++row)
        for(unsigned column=0;column<width;++column) {
            const uint8_t code=picture[row*stride+column];
            if(code)image[(y+row)*VS_W+x+column]=code;
        }
}

/* Returns the width of the text record pasted. */
static unsigned paste_text(uint8_t* image,uint8_t* record,unsigned index,unsigned x,unsigned y,sat_result_t* st) {
    if(*st!=SAT_OK)return 0;
    *st=read_part("VSTEXT.BIN",index*(uint32_t)TEXT_BYTES,record,TEXT_BYTES);
    if(*st!=SAT_OK)return 0;
    paste(image,record+2,x,y,record[0],record[1],VS_TEXT_W);
    return record[0];
}

static sat_result_t compose_panel(uint8_t* image,uint8_t* record,uint16_t* palette,unsigned picture,unsigned side,
                                  unsigned fighter) {
    const uint32_t at=(((uint32_t)picture*2u+side)*VS_FIGHTERS+fighter)*VS_PANEL_BYTES;
    SAT_TRY(read_part("VSPANEL.BIN",at,record,VS_PANEL_BYTES));
    const unsigned code=side?VS_SIDE_CODE_1:VS_SIDE_CODE_0;
    __builtin_memcpy(palette+code,record,VS_PANEL_COLOURS*2u);
    const uint8_t* codes=record+VS_PANEL_COLOURS*2u;
    uint8_t* to=image+(side?VS_PORTRAIT_Y1:VS_PORTRAIT_Y0)*VS_W+(side?VS_PORTRAIT_X1:VS_PORTRAIT_X0);
    for(unsigned row=0;row<VS_PANEL_H;++row,to+=VS_W,codes+=VS_PANEL_W)
        for(unsigned column=0;column<VS_PANEL_W;++column)to[column]=(uint8_t)(codes[column]+code);
    return SAT_OK;
}

static void credit(uint8_t* image) {
    static const char line[]="V0.1.2 MADE USING celsowm/LibSaturn";
    const unsigned length=sizeof line-1u;
    const unsigned left=(VS_W-length*CREDIT_GLYPH)/2u;
    for(unsigned i=0;i<length;++i) {
        const uint8_t* rows=sat_font_ascii_8x8_rows(line[i]);
        for(unsigned y=0;y<8u;++y)
            for(unsigned x=0;x<8u;++x)
                if(rows[y]&(0x80u>>x))image[(CREDIT_Y+y)*VS_W+left+i*CREDIT_GLYPH+x]=VS_CREDIT_CODE;
    }
}

static sat_result_t caption(uint8_t* image,uint8_t* record,t3_vs_kind_t kind,unsigned number) {
    sat_result_t st=SAT_OK;
    const unsigned label=kind==T3_VS_PRACTICE?CAPTION_PRACTICE:
        kind==T3_VS_ARCADE||kind==T3_VS_DEMO?CAPTION_STAGE:CAPTION_FIGHT;
    unsigned x=VS_CAPTION_X+paste_text(image,record,label,VS_CAPTION_X,VS_CAPTION_Y,&st)+CAPTION_SPACE;
    if(kind==T3_VS_PRACTICE)return st;
    if(number>99u)number=99u;
    if(number>9u)x+=paste_text(image,record,DIGITS+number/10u,x,VS_CAPTION_Y,&st)+1u;
    (void)paste_text(image,record,DIGITS+number%10u,x,VS_CAPTION_Y,&st);
    return st;
}

static sat_result_t configure_layer(void) {
    sat_vdp2_layer_config_t layer;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&layer);
    layer.bitmap=1;
    layer.bitmap_size=SAT_VDP2_BITMAP_512X512;
    layer.color_mode=SAT_VDP2_COLOR_MODE_256;
    layer.palette=BANK;
    layer.priority=7;
    layer.transparent=0;
    for(unsigned i=0;i<4;++i)layer.plane_address[i]=BITMAP_BASE;
    SAT_TRY(sat_vdp2_layer_configure(&layer));
    SAT_TRY(sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0,0x20000u,0x10000u));
    return sat_vdp2_layer_set_scroll(SAT_VDP2_NBG0,0,0);
}

sat_result_t t3_vs_screen_show(void* scratch,uint32_t capacity,const uint8_t picks[2],t3_vs_kind_t kind,unsigned number) {
    static const uint8_t pictures[]={0,1,1,0,0};
    static const char* const files[]={"VSBG0.BIN","VSBG1.BIN","VSBG1.BIN","VSBG2.BIN","VSBG3.BIN"};
    enum { IMAGE_BYTES=VS_W*VS_H, BACKGROUND_BYTES=BG_HEADER+BG_PALETTE+IMAGE_BYTES };
    const uint32_t record_bytes=(uint32_t)VS_PANEL_BYTES>(uint32_t)TEXT_BYTES?(uint32_t)VS_PANEL_BYTES:(uint32_t)TEXT_BYTES;
    if(capacity<(uint32_t)BACKGROUND_BYTES+record_bytes+STRIDE+4u)return SAT_ERR_CAPACITY;
    SAT_TRY(t3_disc_volume(&g_volume));
    /* The VDP1 buffers hold whatever the last program left; the sprite layer must stay empty. */
    SAT_TRY(sat_begin_frame());
    SAT_TRY(sat_end_frame());
    uint8_t* file=scratch;
    SAT_TRY(read_part(files[kind],0,file,BACKGROUND_BYTES));
    for(unsigned i=0;i<8u;++i)if(file[i]!=(uint8_t)"T3BG0001"[i])return SAT_ERR_VERSION;
    uint16_t palette[256];
    __builtin_memcpy(palette,file+BG_HEADER,sizeof palette);
    uint8_t* image=file+BG_HEADER+BG_PALETTE;
    /* A record follows the image; the row buffer follows the largest record. */
    uint8_t* record=image+IMAGE_BYTES;
    for(unsigned side=0;side<2u;++side) {
        SAT_TRY(compose_panel(image,record,palette,pictures[kind],side,picks[side]));
        sat_result_t st=SAT_OK;
        (void)paste_text(image,record,picks[side],g_vs_name_x[side][picks[side]],side?VS_NAME_Y1:VS_NAME_Y0,&st);
        SAT_TRY(st);
    }
    SAT_TRY(caption(image,record,kind,number));
    credit(image);
    const sat_video_mode_t mode={SCREEN_W,SCREEN_H,SAT_VIDEO_INTERLACE_DOUBLE,0};
    SAT_TRY(sat_video_set_display_mode(&mode));
    SAT_TRY(configure_layer());
    SAT_TRY(sat_vdp2_palette_upload(palette,256,BANK*256));
    uint16_t* row=(uint16_t*)(record+((record_bytes+3u)&~3u));
    for(unsigned y=0;y<STRIDE;++y) {
        __builtin_memset(row,0,STRIDE);
        if(y<VS_H)__builtin_memcpy(row,image+y*VS_W,VS_W);
        SAT_TRY(sat_vdp2_vram_write_words(BITMAP_BASE/2u+y*(STRIDE/2u),row,STRIDE/2u));
    }
    SAT_TRY(sat_vdp2_back_color_set(SAT_COLOR_BLACK));
    SAT_TRY(sat_vdp2_layer_set_enabled(SAT_VDP2_NBG0,1));
    SAT_TRY(sat_vdp2_layers_commit());
    g_shown_at=sat_field_count();
    return SAT_OK;
}

sat_result_t t3_vs_screen_finish(void) {
    while(sat_field_count()-g_shown_at<MINIMUM_FIELDS)SAT_TRY(sat_wait_vblank());
    SAT_TRY(sat_vdp2_color_offset_enable(LAYERS,SAT_VDP2_COLOR_OFFSET_A));
    for(unsigned f=0;f<=FADE_FIELDS;++f) {
        const int16_t level=(int16_t)(-(int)(f*255u/FADE_FIELDS));
        const sat_vdp2_color_offset_t offset={level,level,level};
        SAT_TRY(sat_wait_vblank());
        SAT_TRY(sat_vdp2_color_offset_set(SAT_VDP2_COLOR_OFFSET_A,&offset));
    }
    SAT_TRY(sat_vdp2_layer_set_enabled(SAT_VDP2_NBG0,0));
    SAT_TRY(sat_vdp2_layers_commit());
    SAT_TRY(sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0,0x10000u,0x10000u));
    const sat_video_mode_t mode={sat_video_width(),sat_video_height(),sat_video_interlace_mode(),0};
    return sat_video_set_display_mode(&mode);
}

sat_result_t t3_vs_screen_release(void) {
    const sat_vdp2_color_offset_t none={0,0,0};
    SAT_TRY(sat_vdp2_color_offset_set(SAT_VDP2_COLOR_OFFSET_A,&none));
    return sat_vdp2_color_offset_disable(LAYERS);
}
