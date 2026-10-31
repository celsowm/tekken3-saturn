/* Phase 0 spike: can a 704x448 double-density screen carry a VDP2 bitmap backdrop (zoomed 2x wide),
 * VDP1 8-bit sprites and text over it, and a colour-offset fade? Written after MENU.BIN was built
 * (the plan asked for it first); it records the answer as a reproducible Ymir run.
 *
 *   cmake -DT3_HIRES_SPIKE=ON ...; run build/.../tekken3_spike_hires.app.bin on the probe and read
 *   the screenshot: a gradient bitmap, a white box, "SPIKE" text and a fade from white over 60 fields. */
#include <stdint.h>
#include "saturn/app.h"
#include "saturn/color.h"
#include "saturn/font.h"
#include "saturn/saturn.h"
#include "saturn/vdp1.h"
#include "saturn/vdp2.h"
#include "saturn/vdp2_color_offset.h"
#include "saturn/vdp2_layers.h"
#include "saturn/video.h"

enum { W=704, H=448, BANK=7, BITMAP_W=352 };
static sat_ascii_font_t g_font;

int main(void) {
    sat_video_config_t video={W,H,SAT_VIDEO_AUTO,0};
    sat_vdp2_layer_config_t layer;
    uint16_t palette[256]={0},row[256];
    SAT_PANIC_IF_ERROR(sat_init(&video));
    for(unsigned i=1;i<256;++i)palette[i]=(uint16_t)(0x8000u|((i>>3)<<10)|((i>>4)<<5)|(31u-(i>>3)));
    palette[255]=SAT_COLOR_WHITE;palette[254]=SAT_COLOR_BLACK;
    SAT_PANIC_IF_ERROR(sat_vdp2_palette_upload(palette,256,0));
    SAT_PANIC_IF_ERROR(sat_vdp2_palette_upload(palette,256,BANK*256));
    SAT_PANIC_IF_ERROR(sat_vdp2_sprite_palette_bank_set(BANK));
    for(unsigned y=0;y<512;++y) {                         /* a 352 wide horizontal gradient, 512x512 stride */
        for(unsigned x=0;x<256;++x) {
            const unsigned a=(2*x<BITMAP_W)?(2*x*255/BITMAP_W):0u,b=(2*x+1<BITMAP_W)?((2*x+1)*255/BITMAP_W):0u;
            row[x]=(uint16_t)((a<<8)|b);
        }
        SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words(y*512u/2u,row,256u));
    }
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&layer);
    layer.bitmap=1;layer.bitmap_size=SAT_VDP2_BITMAP_512X512;layer.color_mode=SAT_VDP2_COLOR_MODE_256;
    layer.palette=0;layer.priority=1;layer.transparent=0;
    for(unsigned i=0;i<4;++i)layer.plane_address[i]=0;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&layer));
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0,0x20000u,0x10000u));
    SAT_PANIC_IF_ERROR(sat_vdp2_color_offset_enable(SAT_VDP2_LAYER_NBG0|SAT_VDP2_LAYER_SPRITE,SAT_VDP2_COLOR_OFFSET_A));
    for(unsigned glyph=0;glyph<SAT_ASCII_FONT_GLYPH_COUNT;++glyph) {
        uint8_t tall[16*16]={0};
        SAT_PANIC_IF_ERROR(sat_font_pack_8x8_glyph_indexed8(tall,16,16,0,0,sat_font_ascii_8x8_rows((char)(glyph+32u)),2));
        for(unsigned i=0;i<sizeof tall;++i)if(tall[i])tall[i]=254;
        SAT_PANIC_IF_ERROR(sat_tex_upload_indexed8(&g_font.glyphs[glyph],tall,16,16,palette,BANK));
    }
    for(unsigned frame=0;;++frame) {
        const int level=frame<60?255-(int)(frame*255/60):0;
        const sat_vdp2_color_offset_t offset={(int16_t)level,(int16_t)level,(int16_t)level};
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        (void)sat_vdp2_color_offset_set(SAT_VDP2_COLOR_OFFSET_A,&offset);
        (void)sat_vdp2_layers_commit();
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        (void)sat_draw_rect_screen(200,150,300,120,255);
        (void)sat_ascii_font_draw_text_screen_centered_indexed8(&g_font,"SPIKE 704x448",352,200,14,0,0);
        SAT_PANIC_IF_ERROR(sat_end_frame());
    }
}
