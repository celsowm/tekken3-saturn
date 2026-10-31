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

enum { SETS=3, MAX_FRAMES=30, FRAME_BYTES=512, HEADER_BYTES=16+32, SYSTEM_SET=2 };
/* Set 0 and 1 are the two fighters' character flipbooks, set 2 the system burst
 * (the records' set 3); all three share CRAM bank 5, 16 entries each after the
 * transparent entry 0 (indexed8 texels are 1 + 16 * set + colour). Banks 6 and 7 hold the stage's
 * floor and panorama palettes (stage_vdp2.c); the fighters' banks are claimed in the
 * SDK's registry, so a clash with them fails the upload at boot. */
static const char* const g_file[SETS]={"FLIPC0.BIN","FLIPC1.BIN","FLIPBOOK.BIN"};
/* The last ten frames of a record fade (modulation byte 0x80 - ((frame - frames + 10) * 0x9999 >> 12),
 * 119 down to 33 of 128). VDP1 colour-bank sprites cannot be shaded, so three banks approximate the
 * eleven modulation levels. The sprite picks the nearest bank; same texels, no extra VRAM. */
enum { FADE_BANKS=3 };
static const uint8_t g_fade_bank[FADE_BANKS]={5,4,3};
static const uint8_t g_fade_scale[FADE_BANKS]={128,85,47}; /* of 128 */
static uint16_t g_palette[256];
static sat_vdp1_texture_t g_frame_texture[SETS][MAX_FRAMES];
static uint8_t g_frames[SETS];
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

static sat_result_t load_set(unsigned set,void* scratch,uint32_t scratch_bytes) {
    sat_cdfs_volume_t* volume;sat_cdfs_file_t file;uint32_t read=0;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,g_file[set],&file));
    if(file.size>scratch_bytes || file.size<HEADER_BYTES)return SAT_ERR_VERSION;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,scratch,file.size,&read));
    if(read!=file.size)return SAT_ERR_IO;
    const uint8_t* b=scratch;
    const unsigned frames=le16(b+8);
    if(!magic_ok(b) || !frames || frames>MAX_FRAMES || le16(b+10)!=32 || le16(b+12)!=32 || le16(b+14)!=16 ||
       file.size!=HEADER_BYTES+frames*FRAME_BYTES)return SAT_ERR_VERSION;
    /* Texel 0 of an indexed8 sprite is transparent, so PS1 colour i is entry i+1;
     * black adds nothing to the image below, so the PS1's zero and STP-black
     * colours stay transparent (entry 0) and leave the VDP1 pixels under the quad alone. */
    uint8_t map[16];
    for(unsigned i=0;i<16;++i) {
        const uint16_t v=le16(b+16+2*i);
        map[i]=(v&0x7fffu)?(uint8_t)(1u+16u*set+i):0u;
        if(v&0x7fffu)g_palette[1u+16u*set+i]=(uint16_t)(0x8000u|(v&0x7fffu));
    }
    if(set+1u==SETS) {
        for(unsigned bank=0;bank<FADE_BANKS;++bank) {
            uint16_t faded[256]={0};
            for(unsigned i=1;i<=16u*SETS;++i) {
                const uint16_t v=g_palette[i];
                unsigned c[3];
                for(unsigned k=0;k<3;++k)c[k]=(((v>>(5u*k))&31u)*g_fade_scale[bank]+64u)>>7;
                faded[i]=(uint16_t)(0x8000u|c[0]|(c[1]<<5)|(c[2]<<10));
            }
            SAT_TRY(sat_palette_upload_indexed8(faded,g_fade_bank[bank]));
        }
    }
    uint8_t pixels[FRAME_BYTES*2];
    for(unsigned f=0;f<frames;++f) {
        const uint8_t* src=b+HEADER_BYTES+f*FRAME_BYTES;
        for(unsigned i=0;i<FRAME_BYTES;++i) {
            pixels[2*i]=map[src[i]>>4];pixels[2*i+1]=map[src[i]&15u];
        }
        SAT_TRY(sat_tex_upload_indexed8_pixels(&g_frame_texture[set][f],pixels,32,32,g_fade_bank[0]));
    }
    g_frames[set]=(uint8_t)frames;
    return SAT_OK;
}

sat_result_t t3_flipbook_burst_init(void* scratch,uint32_t scratch_bytes) {
    if(!scratch || scratch_bytes<T3_FLIPBOOK_FILE_BYTES)return SAT_ERR_INVALID_ARG;
    for(unsigned set=0;set<SETS;++set)SAT_TRY(load_set(set,scratch,scratch_bytes));
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
        const unsigned set=r->set==3u?SYSTEM_SET:r->set;
        if(!r->active || set>=SETS || r->frame<1u || r->frame>g_frames[set])continue;
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
        cmd.texture=&g_frame_texture[set][r->frame-1u];
        /* The nearest of 128, 85 and 47 for the original modulation at each fade step. */
        const int fade=(int)r->frame-(int)r->frames+10;
        cmd.palette_override=g_fade_bank[fade>6?2:fade>2?1:0];
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
