/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Uses original particle tables and projection adapted from the pinned
 * Tekken3Remake effects research. See NOTICE.md. */
#include "particle_prepare.h"
#ifndef T3_PARTICLE_CLUT_FLIP
#define T3_PARTICLE_CLUT_FLIP 0u
#endif
#include "move_probe.h"
#include "original_hud.h"
#include "disc.h"
#include "saturn/cdfs.h"
#if defined(T3_PARTICLE_TEXTURE_PROBE)
#include "saturn/vdp2.h"
#endif
#include "placement.h"
#if defined(T3_PARTICLE_SWAP_PROBE)
#include "stage_vdp2.h"
#endif
static t3_psx_particle_assets_t g_assets;
#if defined(T3_ORIGINAL_FIGHTER_OT)
t3_prepared_particle_image_t g_t3_prepared_particles __attribute__((section(".wram_l")));
#else
t3_prepared_particle_image_t g_t3_prepared_particles T3_HOT_WRAM;
#endif
volatile uint32_t g_t3_particle_prepare_images,g_t3_particle_prepare_peak;
sat_result_t t3_particle_prepare_init(void) {
    sat_cdfs_volume_t* volume;sat_cdfs_file_t file;uint32_t read=0;
    /* The asset reader keeps pointers into this buffer, so it stays resident. */
    static uint8_t effects_file[3072] __attribute__((aligned(4)));
    void* buffer=effects_file;const unsigned capacity=sizeof effects_file;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"EFFECTS.BIN",&file));
    if(file.size>capacity)return SAT_ERR_VERSION;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,buffer,file.size,&read));
    if(read!=file.size)return SAT_ERR_IO;
    if(!t3_psx_particle_assets_open(&g_assets,buffer,file.size))return SAT_ERR_VERSION;
#if defined(T3_PARTICLE_TEXTURE_PROBE) && !defined(T3_PARTICLE_SWAP_PROBE)
    /* Diagnostic: native RGB cells for the original unscaled 16x16 normal
     * spark and CLUT 0x7F94, at screen (80,64). Zero word is transparent;
     * 0x8000 is visible black. Hardware color calculation adds over VDP1. */
    const uint8_t* pattern=t3_psx_particle_pattern(&g_assets,0);
    uint16_t palette[16],cell[64];
    if(!pattern || !t3_psx_particle_palette(&g_assets,0x7f94,palette))return SAT_ERR_VERSION;
    SAT_TRY(sat_vdp2_wait_vblank_start());
    for(unsigned ty=0;ty<2;++ty)for(unsigned tx=0;tx<2;++tx) {
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            unsigned pixel=(ty*8u+y)*16u+tx*8u+x;
            uint8_t pair=pattern[pixel/2u];
            unsigned index=(pixel&1u)?(pair&15u):(pair>>4u);
            uint16_t color=palette[index];
            cell[y*8u+x]=color?(uint16_t)(0x8000u|(color&0x7fffu)):0;
        }
        unsigned tile=1u+ty*2u+tx;
        SAT_TRY(sat_vdp2_vram_write_words((0x64000u+tile*128u)/2u,cell,64));
    }
#endif
    return SAT_OK;
}
sat_result_t t3_particle_prepare_image(void) {
    t3_psx_particle_view_t view;
    const t3_particle_image_t* image=&g_t3_particle_image;
    const unsigned count=image->sprites.count;
    if(count>T3_PSX_EFFECT_CAPACITY ||
       !t3_psx_particle_build_view(&image->camera,t3_move_probe_particle_trig(),&view))return SAT_ERR_VERIFY_FAILED;
    unsigned visible=0;
    for(unsigned i=0;i<count;++i) {
        t3_psx_particle_draw_t* draw=&g_t3_prepared_particles.draw[i];
        if(!t3_psx_particle_prepare(&image->sprites.item[i],&g_assets,&view,(image->frame_counter+T3_PARTICLE_CLUT_FLIP)&1u,draw))
            return SAT_ERR_VERIFY_FAILED;
        visible+=draw->quad.visible;
    }
    g_t3_prepared_particles.count=count;g_t3_prepared_particles.visible=visible;
    g_t3_prepared_particles.frame_counter=image->frame_counter;
#if defined(T3_PARTICLE_MULTI_PROBE) && !defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    SAT_TRY(t3_stage_particle_multi_begin(image->frame_counter));
    for(unsigned i=0;i<count;++i)if(g_t3_prepared_particles.draw[i].quad.visible) {
        const t3_psx_particle_draw_t* d=&g_t3_prepared_particles.draw[i];
        const uint8_t* pattern=t3_psx_particle_pattern(&g_assets,d->pattern);uint16_t palette[16];
        if(!pattern || !t3_psx_particle_palette(&g_assets,d->clut,palette))return SAT_ERR_VERIFY_FAILED;
        SAT_TRY(t3_stage_particle_multi_add(&d->quad,pattern,palette));
    }
    t3_stage_particle_multi_end();
#elif defined(T3_PARTICLE_SWAP_PROBE) && !defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    const uint8_t* pattern=NULL;uint16_t palette[16],clut=0;uint8_t pattern_id=0xff;
#if defined(T3_PARTICLE_POSITION_PROBE)
    const t3_psx_particle_quad_t* quad=NULL;
#endif
    for(unsigned i=0;i<count;++i)if(g_t3_prepared_particles.draw[i].quad.visible) {
        const t3_psx_particle_draw_t* d=&g_t3_prepared_particles.draw[i];
        pattern_id=d->pattern;clut=d->clut;
#if defined(T3_PARTICLE_POSITION_PROBE)
        quad=&d->quad;
#endif
        pattern=t3_psx_particle_pattern(&g_assets,pattern_id);
        if(!pattern || !t3_psx_particle_palette(&g_assets,d->clut,palette))return SAT_ERR_VERIFY_FAILED;
        break;
    }
    SAT_TRY(t3_stage_particle_probe_prepare(pattern,pattern?palette:NULL,image->frame_counter,clut,pattern_id
#if defined(T3_PARTICLE_POSITION_PROBE)
        ,quad
#endif
    ));
#endif
    if(count>g_t3_particle_prepare_peak)g_t3_particle_prepare_peak=count;
    ++g_t3_particle_prepare_images;
    return SAT_OK;
}

#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
sat_result_t t3_particle_prepare_add(const t3_psx_particle_draw_t* draw) {
    if(!draw || !draw->quad.visible)return SAT_ERR_INVALID_ARG;
    const uint8_t* pattern=t3_psx_particle_pattern(&g_assets,draw->pattern);
    uint16_t palette[16];
    if(!pattern || !t3_psx_particle_palette(&g_assets,draw->clut,palette))return SAT_ERR_VERIFY_FAILED;
    return t3_stage_particle_multi_add(&draw->quad,pattern,palette);
}
#endif
