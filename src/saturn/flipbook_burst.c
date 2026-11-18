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
#if defined(T3_FIGHTER_PACKS)
#include "fighter_pack.h"
#endif
#include "move_probe.h"
#include "psx_particle_projection.h"
#include "psx_effect_uv.h"
#include "psx_viewport.h"
#ifndef T3_PARTICLE_CLUT_FLIP
#define T3_PARTICLE_CLUT_FLIP 0u
#endif

enum { SETS=3, MAX_FRAMES=30, FRAME_BYTES=512, HEADER_BYTES=16+32, SYSTEM_SET=2 };
/* Set 0 and 1 are the two fighters' character flipbooks, set 2 the system burst
 * (the records' set 3); all three share CRAM bank 5, 16 entries each after the
 * transparent entry 0 (PS1 colour i of a set is entry 1 + 16 * set + i). Banks 6 and 7 hold the stage's
 * floor and panorama palettes (stage_vdp2.c); the fighters' banks are claimed in the
 * SDK's registry, so a clash with them fails the upload at boot. */
#if !defined(T3_FIGHTER_PACKS)
static const char* const g_file[SETS]={"FLIPC0.BIN","FLIPC1.BIN","FLIPBOOK.BIN"};
#endif
/* The last ten frames of a record fade (modulation byte 0x80 - ((frame - frames + 10) * 0x9999 >> 12),
 * 119 down to 33 of 128). VDP1 colour-bank sprites cannot be shaded, so three banks approximate the
 * eleven modulation levels. The sprite picks the nearest bank; same texels, no extra VRAM.
 * A frame is LUT4 (512 bytes of VDP1 VRAM, not INDEXED8's 1024): each set has a table of
 * colour-bank codes per fade bank, which carries the VDP2 addition as a bank selector does. */
enum { FADE_BANKS=3 };
/* The pool's fire, smoke, cloud and gas flipbooks (FIRE.BIN, scripts/extract_psx_fire_flipbook.py): 53 cells in LUT4 beside the
 * bursts, their five palettes after the bursts' in CRAM bank 5 (entries 49 to 128), drawn as the same additive sprite. */
enum { FIRE_CLUTS=5,FIRE_CELLS_MAX=56,FIRE_CRAM=1+16*SETS,FIRE_CRAM_END=FIRE_CRAM+16*FIRE_CLUTS,FIRE_CLUT_BASE=0x7DC6,
       FIRE_HEADER=16,FIRE_MODEL_BYTES=32 };
static const uint8_t g_fade_bank[FADE_BANKS]={5,4,3};
static const uint8_t g_fade_scale[FADE_BANKS]={128,85,47}; /* of 128 */
static uint16_t g_palette[256];
static sat_vdp1_texture_t g_frame_texture[SETS][MAX_FRAMES];
static uint16_t g_lut[SETS][FADE_BANKS];
static uint8_t g_frames[SETS];
static sat_vdp1_texture_t g_fire_texture[FIRE_CELLS_MAX];
static t3_psx_particle_model_t g_fire_model[T3_FM_COUNT];
static uint16_t g_fire_lut[FIRE_CLUTS];
/* The same palettes as plain RGB tables, for the clouds and the gas: the PS1 draws those on its 50 % page, (B + F) / 2, which the
 * VDP2 additive path cannot give; a VDP1 mesh sprite (a checkerboard of the sprite and what is behind it, the VDP2 floor too)
 * averages the two on screen. */
static uint16_t g_fire_mesh_lut[FIRE_CLUTS];
static uint8_t g_fire_cell[T3_UV_END],g_fire_ready;
static uint8_t g_ready;
volatile uint32_t g_t3_flipbook_drawn,g_t3_fire_drawn;
volatile uint32_t g_t3_flipbook_seen[4]; /* diagnostic: active records, projection failures, off-screen, images */
volatile int32_t g_t3_flipbook_last[10]; /* diagnostic: last sprite as drawn */

static int magic_ok(const uint8_t* b) {
    static const char want[8]="T3FB002";
    for(unsigned i=0;i<8;++i)if(b[i]!=(uint8_t)want[i])return 0;
    return 1;
}
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0]|(p[1]<<8)); }

static sat_result_t load_set(unsigned set,void* scratch,uint32_t scratch_bytes) {
    sat_cdfs_volume_t* volume;sat_cdfs_file_t file;uint32_t read=0,size;
    SAT_TRY(t3_disc_volume(&volume));
#if defined(T3_FIGHTER_PACKS)
    /* A fighter's set is a section of its pack. */
    if(set<SYSTEM_SET) {
        size=t3_fighter_pack_section_bytes(set,T3_PACK_FLIPBOOK);
        if(size>scratch_bytes || size<HEADER_BYTES)return SAT_ERR_VERSION;
        SAT_TRY(t3_fighter_pack_read(set,T3_PACK_FLIPBOOK,0,scratch,size));
    } else
#endif
    {
#if defined(T3_FIGHTER_PACKS)
        SAT_TRY(sat_cdfs_lookup(volume,"FLIPBOOK.BIN",&file));
#else
        SAT_TRY(sat_cdfs_lookup(volume,g_file[set],&file));
#endif
        size=file.size;
        if(size>scratch_bytes || size<HEADER_BYTES)return SAT_ERR_VERSION;
        SAT_TRY(sat_cdfs_read_at(volume,&file,0,scratch,size,&read));
        if(read!=size)return SAT_ERR_IO;
    }
    const uint8_t* b=scratch;
    const unsigned frames=le16(b+8);
    if(!magic_ok(b) || !frames || frames>MAX_FRAMES || le16(b+10)!=32 || le16(b+12)!=32 || le16(b+14)!=16 ||
       size!=HEADER_BYTES+frames*FRAME_BYTES)return SAT_ERR_VERSION;
    /* Texel 0 of a sprite is transparent. Black adds nothing to the image below, so the PS1's zero
     * and STP-black colours become texel 0 and leave the VDP1 pixels under the quad alone; the
     * others take texels 1 to 15 in order (every set has a black: at most 15 remain). */
    uint8_t map[16],codes[16]={0};
    unsigned texels=1;
    for(unsigned i=0;i<16;++i) {
        const uint16_t v=le16(b+16+2*i);
        map[i]=0u;
        if(!(v&0x7fffu))continue;
        if(texels>15u)return SAT_ERR_VERSION;
        codes[texels]=(uint8_t)(1u+16u*set+i);
        map[i]=(uint8_t)texels++;
        g_palette[1u+16u*set+i]=(uint16_t)(0x8000u|(v&0x7fffu));
    }
    for(unsigned bank=0;bank<FADE_BANKS;++bank)
        SAT_TRY(sat_vdp1_upload_color_calc_lut(codes,g_fade_bank[bank],SAT_VDP2_SPRITE_ADD,&g_lut[set][bank]));
    if(set+1u==SETS) {
        for(unsigned bank=0;bank<FADE_BANKS;++bank) {
            uint16_t faded[256]={0};
            for(unsigned i=1;i<FIRE_CRAM_END;++i) {
                const uint16_t v=g_palette[i];
                unsigned c[3];
                for(unsigned k=0;k<3;++k)c[k]=(((v>>(5u*k))&31u)*g_fade_scale[bank]+64u)>>7;
                faded[i]=(uint16_t)(0x8000u|c[0]|(c[1]<<5)|(c[2]<<10));
            }
            SAT_TRY(sat_palette_upload_indexed8(faded,g_fade_bank[bank]));
        }
    }
    uint8_t pixels[FRAME_BYTES];
    for(unsigned f=0;f<frames;++f) {
        const uint8_t* src=b+HEADER_BYTES+f*FRAME_BYTES;
        for(unsigned i=0;i<FRAME_BYTES;++i)pixels[i]=(uint8_t)((map[src[i]>>4]<<4)|map[src[i]&15u]);
        SAT_TRY(sat_tex_upload_lut4_pixels(&g_frame_texture[set][f],pixels,32,32,g_lut[set][0]));
    }
    g_frames[set]=(uint8_t)frames;
    return SAT_OK;
}

static sat_result_t load_fire(void* scratch,uint32_t scratch_bytes) {
    sat_cdfs_volume_t* volume;sat_cdfs_file_t file;uint32_t read=0;
    static const char want[8]="T3FR001";
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"FIRE.BIN",&file));
    if(file.size>scratch_bytes || file.size<FIRE_HEADER)return SAT_ERR_VERSION;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,scratch,file.size,&read));
    if(read!=file.size)return SAT_ERR_IO;
    const uint8_t* b=scratch;
    for(unsigned i=0;i<8u;++i)if(b[i]!=(uint8_t)want[i])return SAT_ERR_VERSION;
    const unsigned cells=le16(b+8),frames=le16(b+10),models=le16(b+12);
    const uint8_t* palettes=b+FIRE_HEADER;
    const uint8_t* model=palettes+32u*FIRE_CLUTS;
    const uint8_t* frame_cell=model+FIRE_MODEL_BYTES*models;
    const uint8_t* size=frame_cell+frames;
    const uint8_t* texels=size+cells;
    if(cells>FIRE_CELLS_MAX || frames!=T3_UV_END-1u || models!=T3_FM_COUNT-1u)return SAT_ERR_VERSION;
    for(unsigned c=0;c<FIRE_CLUTS;++c) {
        uint8_t codes[16]={0};
        for(unsigned i=0;i<16;++i) {
            g_palette[FIRE_CRAM+16u*c+i]=(uint16_t)(0x8000u|(le16(palettes+32u*c+2u*i)&0x7fffu));
            /* CLUTs 0x7DC8 and 0x7DC9 are stored with texel values 0 and 15 swapped (the tool's header) */
            if(i)codes[i]=(uint8_t)(FIRE_CRAM+16u*c+((c==2u||c==3u)&&i==15u?0u:i));
        }
        SAT_TRY(sat_vdp1_upload_color_calc_lut(codes,g_fade_bank[0],SAT_VDP2_SPRITE_ADD,&g_fire_lut[c]));
        uint16_t rgb[16]={0};   /* index 0 stays transparent */
        for(unsigned i=1;i<16u;++i)rgb[i]=g_palette[FIRE_CRAM+16u*c+i];
        SAT_TRY(sat_vdp1_upload_lut(rgb,&g_fire_mesh_lut[c]));
    }
    for(unsigned m=0;m<models;++m)for(unsigned i=0;i<4u;++i)for(unsigned j=0;j<4u;++j)
        g_fire_model[m+1u].corners[i][j]=(int16_t)le16(model+FIRE_MODEL_BYTES*m+8u*i+2u*j);
    for(unsigned f=0;f<frames;++f) {
        if(frame_cell[f]>=cells)return SAT_ERR_VERSION;
        g_fire_cell[f+1u]=frame_cell[f];
    }
    for(unsigned c=0;c<cells;++c) {
        const unsigned w=size[c];
        if(w!=16u && w!=32u)return SAT_ERR_VERSION;
        SAT_TRY(sat_tex_upload_lut4_pixels(&g_fire_texture[c],texels,w,w,g_fire_lut[0]));
        texels+=w*w/2u;
    }
    g_fire_ready=1;
    return SAT_OK;
}

sat_result_t t3_flipbook_burst_init(void* scratch,uint32_t scratch_bytes) {
    if(!scratch || scratch_bytes<T3_FLIPBOOK_FILE_BYTES)return SAT_ERR_INVALID_ARG;
    SAT_TRY(load_fire(scratch,scratch_bytes));   /* first: the last set's palette upload carries the fire palettes */
    for(unsigned set=0;set<SETS;++set)SAT_TRY(load_set(set,scratch,scratch_bytes));
    g_ready=1;
    return SAT_OK;
}

static sat_result_t draw_ball_glow(void);
/* The fire objects of one pass. The PS1 sorts gas behind the fighters' tail, so the gas is drawn in the world pass before the
 * fighters' faces (behind=1); the flames and clouds follow in the overlay. */
static sat_result_t draw_fire(const t3_psx_particle_view_t* viewp,unsigned behind) {
    const t3_particle_image_t* image=&g_t3_particle_image;
    const t3_psx_particle_view_t view=*viewp;
    if(g_fire_ready) {
        const unsigned buffer=(image->frame_counter+T3_PARTICLE_CLUT_FLIP)&1u;
        for(unsigned i=0;i<image->sprites.count;++i) {
            const t3_psx_particle_snapshot_item_t* item=&image->sprites.item[i];
            const unsigned model=T3_PSX_SNAPSHOT_FIRE_MODEL(item),clut=(unsigned)item->clut[buffer]-FIRE_CLUT_BASE;
            if(!item->frame || !(item->flags&0x40u) || item->frame>=T3_UV_END || !model || model>=T3_FM_COUNT || clut>=FIRE_CLUTS)continue;
            if((model==T3_FM_GAS_A || model==T3_FM_GAS_B)!=(behind!=0))continue;
            t3_psx_particle_quad_t q;
            if(!t3_psx_particle_project(&view,item->position,&g_fire_model[model],item->flags,&q) || !q.visible)continue;
            sat_distorted_sprite_cmd_t cmd={0};
            static const uint8_t order[4]={0,1,3,2};   /* the PS1's top left, top right, bottom left, bottom right to the perimeter */
            int lo_x=1000,hi_x=-1000,lo_y=1000,hi_y=-1000;
            for(unsigned k=0;k<4u;++k) {
                const int x=t3_psx_viewport_x(q.xy[order[k]][0])-160,y=t3_psx_viewport_y(q.xy[order[k]][1])-112;
                cmd.x[k]=(int16_t)x;cmd.y[k]=(int16_t)y;
                if(x<lo_x)lo_x=x;if(x>hi_x)hi_x=x;if(y<lo_y)lo_y=y;if(y>hi_y)hi_y=y;
            }
            if(hi_x<-160 || lo_x>160 || hi_y<-112 || lo_y>112)continue;
            cmd.texture=&g_fire_texture[g_fire_cell[item->frame]];
            if(model==T3_FM_CLOUD || model==T3_FM_GAS_A || model==T3_FM_GAS_B) {   /* the PS1's 50 % page: a mesh sprite averages */
                cmd.palette_override=g_fire_mesh_lut[clut];
                cmd.flags=SAT_SPRITE_FLAG_MESH;
                SAT_TRY(sat_draw_sprite_distorted(&cmd));
            } else {
                cmd.palette_override=g_fire_lut[clut];
                SAT_TRY(sat_draw_sprite_distorted_color_calc(&cmd,SAT_VDP2_SPRITE_ADD));
            }
            ++g_t3_fire_drawn;
        }
    }
    return SAT_OK;
}
sat_result_t t3_flipbook_burst_draw_behind(void) {
    if(!g_ready)return SAT_OK;
    t3_psx_particle_view_t view;
    if(!t3_psx_particle_build_view(&g_t3_particle_image.camera,t3_move_probe_particle_trig(),&view))return SAT_OK;
    return draw_fire(&view,1);
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
        cmd.palette_override=g_lut[set][fade>6?2:fade>2?1:0];
        SAT_TRY(sat_draw_sprite_distorted_color_calc(&cmd,SAT_VDP2_SPRITE_ADD));
        ++g_t3_flipbook_drawn;
        g_t3_flipbook_last[0]=x0;g_t3_flipbook_last[1]=y0;g_t3_flipbook_last[2]=x1;g_t3_flipbook_last[3]=y1;
        g_t3_flipbook_last[4]=(int32_t)r->frame;g_t3_flipbook_last[5]=q.depth[0];g_t3_flipbook_last[6]=q.xy[0][0];
        g_t3_flipbook_last[7]=q.xy[0][1];g_t3_flipbook_last[8]=(int32_t)image->frame_counter;
    }
    SAT_TRY(draw_ball_glow());
    SAT_TRY(draw_fire(&view,0));
    return SAT_OK;
}
/* The glow is asked for while the HUD is drawn and drawn with the other flipbook sprites, in t3_flipbook_burst_draw. */
static struct { int cx,cy,half_w,half_h;unsigned kind,tick; } g_glow;

sat_result_t t3_flipbook_burst_ball_glow(int cx,int cy,int half_w,int half_h,unsigned kind,unsigned tick) {
    g_glow.cx=cx;g_glow.cy=cy;g_glow.half_w=half_w;g_glow.half_h=half_h;g_glow.kind=kind;g_glow.tick=tick;

    return SAT_OK;
}
static sat_result_t draw_ball_glow(void) {
    const int cx=g_glow.cx,cy=g_glow.cy,half_w=g_glow.half_w,half_h=g_glow.half_h;
    const unsigned kind=g_glow.kind,tick=g_glow.tick;
    g_glow.kind=0;
    if(!g_fire_ready || (kind!=6u && kind!=7u && kind!=9u))return SAT_OK;
    sat_distorted_sprite_cmd_t cmd={0};
    if(kind==9u) {   /* a flame: type 6's grow (7 frames), then its loop */
        const unsigned frame=tick<8u?T3_UV_FIREBALL_GROW+tick:T3_UV_FIREBALL_LOOP+(tick&7u);
        cmd.texture=&g_fire_texture[g_fire_cell[frame]];cmd.palette_override=g_fire_lut[2];
    } else {         /* the hitter's character flipbook, frames 9 to 20 in a loop (FUN_80073F78) */
        const unsigned set=kind-6u,frame=8u+tick%12u;
        if(g_frames[set]<=frame)return SAT_OK;
        cmd.texture=&g_frame_texture[set][frame];cmd.palette_override=g_lut[set][0];
    }
    cmd.x[0]=cmd.x[3]=(int16_t)(cx-half_w);cmd.x[1]=cmd.x[2]=(int16_t)(cx+half_w);
    cmd.y[0]=cmd.y[1]=(int16_t)(cy-half_h);cmd.y[2]=cmd.y[3]=(int16_t)(cy+half_h);

    return sat_draw_sprite_distorted_color_calc(&cmd,SAT_VDP2_SPRITE_ADD);
}
uint32_t t3_flipbook_burst_drawn(void) { return g_t3_flipbook_drawn; }
#endif
