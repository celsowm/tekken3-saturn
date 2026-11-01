/* The pre-fight VS screen: one 352x448 VDP2 bitmap at 2x zoom on a 704x448 display,
 * composed by vs_screen_compose.c. It stays up, with VDP1 idle, while the fight loads;
 * the original also leaves its last frame up (modes.md).
 * Rules from Tekken3Remake docs/research/code/modes.md. PolyForm Noncommercial 1.0.0,
 * NOTICE.md. Required Notice: Copyright (c) 2026 n0madic. */
#include "vs_screen.h"
#include "saturn/saturn.h"
#include "saturn/time.h"
#include "saturn/vdp2.h"
#include "saturn/vdp2_color_offset.h"
#include "saturn/vdp2_layers.h"
#include "saturn/video.h"

enum {
    SCREEN_W=704, SCREEN_H=448,
    MINIMUM_FIELDS=60, FADE_FIELDS=16,
    LAYERS=SAT_VDP2_LAYER_ALL
};

static uint32_t g_shown_at;

static sat_result_t configure_layer(void) {
    sat_vdp2_layer_config_t layer;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&layer);
    layer.bitmap=1;
    layer.bitmap_size=SAT_VDP2_BITMAP_512X512;
    layer.color_mode=SAT_VDP2_COLOR_MODE_256;
    layer.palette=T3_VS_PALETTE_BANK;
    layer.priority=7;
    layer.transparent=0;
    for(unsigned i=0;i<4;++i)layer.plane_address[i]=T3_VS_BITMAP_BASE;
    SAT_TRY(sat_vdp2_layer_configure(&layer));
    SAT_TRY(sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0,0x20000u,0x10000u));
    return sat_vdp2_layer_set_scroll(SAT_VDP2_NBG0,0,0);
}

sat_result_t t3_vs_screen_resume(void) {
    /* The VDP1 buffers hold whatever the last program left; the sprite layer must stay empty. */
    SAT_TRY(sat_begin_frame());
    SAT_TRY(sat_end_frame());
    const sat_video_mode_t mode={SCREEN_W,SCREEN_H,SAT_VIDEO_INTERLACE_DOUBLE,0};
    SAT_TRY(sat_video_set_display_mode(&mode));
    SAT_TRY(configure_layer());
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
