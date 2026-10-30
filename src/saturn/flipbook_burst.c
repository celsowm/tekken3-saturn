/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Flipbook draw adapted from the pinned Tekken3Remake effects research and the
 * decompiled FUN_80077158 (docs/PARTICLE_INTEGRATION_PLAN.md, section 48). See NOTICE.md. */
#include "flipbook_burst.h"

#if defined(T3_FLIPBOOK_BURST)
#include "saturn/cdfs.h"
#include "saturn/vdp1.h"
#include "saturn/vdp1_color_calc.h"
#include "disc.h"
#include "move_probe.h"
#include "psx_particle_projection.h"
#include "psx_viewport.h"

enum { FRAMES=27, FRAME_BYTES=512, PALETTE_BANK=7, HEADER_BYTES=16+32 };
static sat_vdp1_texture_t g_frame_texture[FRAMES];
static uint8_t g_ready;
volatile uint32_t g_t3_flipbook_drawn;
volatile uint32_t g_t3_flipbook_seen[4]; /* diagnostic: active records, projection failures, off-screen, images */
volatile int32_t g_t3_flipbook_last[10]; /* diagnostic: last sprite as drawn */

static int magic_ok(const uint8_t* b) {
    static const char want[8]="T3FB002";
    for(unsigned i=0;i<8;++i)if(b[i]!=(uint8_t)want[i])return 0;
    return 1;
}
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0]|(p[1]<<8)); }

sat_result_t t3_flipbook_burst_init(void* scratch,uint32_t scratch_bytes) {
    sat_cdfs_volume_t* volume;sat_cdfs_file_t file;uint32_t read=0;
    if(!scratch || scratch_bytes<T3_FLIPBOOK_FILE_BYTES)return SAT_ERR_INVALID_ARG;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"FLIPBOOK.BIN",&file));
    if(file.size!=T3_FLIPBOOK_FILE_BYTES)return SAT_ERR_VERSION;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,scratch,file.size,&read));
    if(read!=file.size)return SAT_ERR_IO;
    const uint8_t* b=scratch;
    if(!magic_ok(b) || le16(b+8)!=FRAMES || le16(b+10)!=32 || le16(b+12)!=32 || le16(b+14)!=16)
        return SAT_ERR_VERSION;
    /* Texel 0 of an indexed8 sprite is transparent, so PS1 colour i is entry i+1;
     * the PS1's own transparent (zero) colours stay entry 0. */
    uint16_t palette[256]={0};uint8_t map[16];
    for(unsigned i=0;i<16;++i) {
        const uint16_t v=le16(b+16+2*i);
        map[i]=v?(uint8_t)(i+1u):0u;
        if(v)palette[i+1u]=(uint16_t)(0x8000u|(v&0x7fffu));
    }
    SAT_TRY(sat_palette_upload_indexed8(palette,PALETTE_BANK));
    uint8_t pixels[FRAME_BYTES*2];
    for(unsigned f=0;f<FRAMES;++f) {
        const uint8_t* src=b+HEADER_BYTES+f*FRAME_BYTES;
        for(unsigned i=0;i<FRAME_BYTES;++i) {
            pixels[2*i]=map[src[i]>>4];pixels[2*i+1]=map[src[i]&15u];
        }
        SAT_TRY(sat_tex_upload_indexed8_pixels(&g_frame_texture[f],pixels,32,32,PALETTE_BANK));
    }
    g_ready=1;
    return SAT_OK;
}

sat_result_t t3_flipbook_burst_draw(void) {
    if(!g_ready)return SAT_OK;
    const t3_particle_image_t* image=&g_t3_particle_image;
    t3_psx_particle_view_t view;
    if(!t3_psx_particle_build_view(&image->camera,t3_move_probe_particle_trig(),&view))return SAT_OK;
    ++g_t3_flipbook_seen[3];
    static const t3_psx_particle_model_t centre={{{0,0,0},{0,0,0},{0,0,0},{0,0,0}}};
    for(unsigned i=0;i<T3_PSX_FLIPBOOK_RECORDS;++i) {
        const t3_psx_flipbook_t* r=&image->flipbook[i];
        if(!r->active || r->set!=3u || r->frame<1u || r->frame>FRAMES)continue;
        ++g_t3_flipbook_seen[0];
        /* The ground is never passed: y is clamped before the divide. */
        int32_t p[3]={r->pos[0],r->pos[1]>-32000?-32000:r->pos[1],r->pos[2]};
        for(unsigned k=0;k<3;++k)p[k]/=128;
        t3_psx_particle_quad_t q;
        if(!t3_psx_particle_project(&view,p,&centre,0,&q) || !q.depth[0]) {++g_t3_flipbook_seen[1];continue;}
        int32_t w=377184/(int32_t)q.depth[0],h=655684/(int32_t)q.depth[0];
        if(w>368)w=368;
        if(h>480)h=480;
        const int32_t left=q.xy[0][0]-w/2,top=q.xy[0][1]-h/2;
        const int x0=t3_psx_viewport_x(left)-160,x1=t3_psx_viewport_x(left+w)-160;
        const int y0=t3_psx_viewport_y(top)-112,y1=t3_psx_viewport_y(top+h)-112;
        if(x1<=x0 || y1<=y0 || x1<-160 || x0>160 || y1<-112 || y0>112) {++g_t3_flipbook_seen[2];continue;}
        sat_distorted_sprite_cmd_t cmd={0};
        const int near_x=r->mirror?x1:x0,far_x=r->mirror?x0:x1;
        cmd.x[0]=(int16_t)near_x;cmd.y[0]=(int16_t)y0;
        cmd.x[1]=(int16_t)far_x;cmd.y[1]=(int16_t)y0;
        cmd.x[2]=(int16_t)far_x;cmd.y[2]=(int16_t)y1;
        cmd.x[3]=(int16_t)near_x;cmd.y[3]=(int16_t)y1;
        cmd.texture=&g_frame_texture[r->frame-1u];
        SAT_TRY(sat_draw_sprite_distorted_color_calc(&cmd,SAT_VDP2_SPRITE_ADD));
        ++g_t3_flipbook_drawn;
        g_t3_flipbook_last[0]=x0;g_t3_flipbook_last[1]=y0;g_t3_flipbook_last[2]=x1;g_t3_flipbook_last[3]=y1;
        g_t3_flipbook_last[4]=(int32_t)r->frame;g_t3_flipbook_last[5]=q.depth[0];g_t3_flipbook_last[6]=q.xy[0][0];
        g_t3_flipbook_last[7]=q.xy[0][1];g_t3_flipbook_last[8]=(int32_t)image->frame_counter;
    }
    return SAT_OK;
}
uint32_t t3_flipbook_burst_drawn(void) { return g_t3_flipbook_drawn; }
#endif
