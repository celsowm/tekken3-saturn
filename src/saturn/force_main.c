/* Tekken Force main program (docs/TEKKEN_FORCE_PLAN.md, Step 4): the real FORCE.BIN that runs from
 * the menu. Three fighter records (player + 2 enemies), three draw actors, the side-scrolling
 * camera, the level runner, the Force stage layer, and combat. Drops versus-only code.
 * Copyright (c) 2026 n0madic, PolyForm Noncommercial 1.0.0; see NOTICE.md. */
#include <stdint.h>

#include "saturn/app.h"
#include "saturn/color.h"
#include "saturn/input.h"
#include "saturn/saturn.h"
#include "saturn/vdp2.h"
#include "saturn/vdp2_layers.h"
#include "saturn/vdp2_special.h"
#include "saturn/vdp2_color_calc.h"
#include "saturn/render2d.h"
#include "saturn/sprite_batch.h"
#include "saturn/irq.h"
#include "saturn/parallel.h"
#include "disc.h"
#include "placement.h"
#include "psx_force_camera.h"
#include "psx_force_level.h"
#include "psx_force_walls.h"
#include "psx_force_target.h"
#include "psx_force_handoff.h"
#include "force_save.h"
#include "move_probe.h"
#include "characters.h"
#include "stage_vdp2.h"
#include "combat_events.h"
#include "boot_arg.h"
#include "saturn/exec.h"
#include "cold_data.h"
#include "fighter_pack.h"
#include "sfx.h"
#include "saturn/hud.h"
#include "tick_clock.h"

/* Temporary on-screen diagnostics while the Force renderer is brought up. */
#ifndef T3_FORCE_DEBUG_HUD
#define T3_FORCE_DEBUG_HUD 0
#endif

#ifndef T3_FRAME_COUNTER_BIAS
#define T3_FRAME_COUNTER_BIAS 0u
#endif
#ifndef T3_START_SEED
#define T3_START_SEED 1u
#endif
#ifndef T3_FORCE_POSE_CACHE_ENTRIES
#define T3_FORCE_POSE_CACHE_ENTRIES 32u
#endif

/* Force program uses 3 fighter records and 3 draw actors. */
#define T3_PSX_FIGHTERS 3
#define T3_DRAW_ACTORS 3
#define T3_FORCE_PROGRAM 1
#define T3_ACTOR_COUNT 3

/* Roster player plus two 342-face Force grunts (all four F21-F24 packs). */
#define T3_FORCE_ENEMY_FACE_COUNT 342u
#define T3_FORCE_SCENE_CAPACITY (T3_FACE_COUNT * 2u + 16u)
/* Faces without a material program (the floor, the items): at most 24 a frame on the probe replay
 * (g_force_probe_cold). The buffer is also the loaders' scratch; the largest is a voice (sfx.c). */
#define T3_FORCE_COLD_FACES 384u

#include "psx_match.h"
#include "psx_force_floor.h"
extern int t3_psx_match_init_failure(void);
#include "psx_ai_game.h"
#include "t3_local_divmot.h"
#include "divmot.h"
#include "t3_generated_character.h"
#include "fight.h"  /* for t3_match_t with T3_MAX_FIGHTERS=3 */

/* Static scene storage for 3 actors. */
static sat_scene3d_face_t g_force_scene_faces[T3_FORCE_COLD_FACES] T3_HOT_WRAM;
_Static_assert(sizeof(sat_scene3d_face_t)*T3_FORCE_COLD_FACES>=T3_ROSTER_MAX_VOICE_SAMPLES,"voice scratch");
static sat_scene3d_projected_face_t g_force_projected_faces[T3_FORCE_SCENE_CAPACITY] T3_HOT_WRAM;
/* Larger pose caches trade the ordering keys' Low WRAM space for High WRAM. */
#if T3_FORCE_POSE_CACHE_ENTRIES>=32
static uint32_t g_force_scene_keys[T3_FORCE_SCENE_CAPACITY] T3_HOT_WRAM;
#else
static uint32_t g_force_scene_keys[T3_FORCE_SCENE_CAPACITY] __attribute__((section(".wram_l")));
#endif
static uint16_t g_force_scene_order[T3_FORCE_SCENE_CAPACITY] T3_HOT_WRAM;

static sat_cdfs_volume_t* g_volume;

static t3_psx_ai_game_t g_ai;
/* Force-specific data buffers (loaded from disc, not cold data). */
static uint8_t g_force_tables[T3_FORCE_TABLE_BYTES] __attribute__((section(".wram_l"),aligned(4)));
static uint8_t g_force_script[T3_FORCE_SCRIPT_BYTES] __attribute__((section(".wram_l"),aligned(4)));
static uint8_t g_force_reaction[44] __attribute__((section(".wram_l"),aligned(4)));
static uint8_t g_level_header[64] __attribute__((section(".wram_l")));

/* Owned match state (3 fighters). */
static t3_psx_match_t g_owned_match T3_HOT_WRAM;
static t3_psx_match_config_t g_owned_config T3_HOT_WRAM;
#if defined(T3_PARALLEL_SKINNING)
#define T3_FORCE_BANK_STORAGE
#else
#define T3_FORCE_BANK_STORAGE __attribute__((section(".wram_l")))
#endif
static t3_divmot_t g_player_bank T3_FORCE_BANK_STORAGE;
static t3_divmot_t g_enemy_bank T3_FORCE_BANK_STORAGE;
/* The boss replaces the second pack in the arena. Hidden enemy records keep
 * their original bank and parts, so the simulation does not lose its state. */
#define T3_FORCE_RETAINED_BANK_BYTES 105788u /* the four grunt banks are 105,786 bytes (21..24.t3logic) */
static uint8_t g_force_grunt_bank[T3_FORCE_RETAINED_BANK_BYTES] __attribute__((aligned(4)));
static t3_psx_part_t g_force_grunt_parts[T3_PSX_DRAW_PARTS];
static t3_divmot_t g_boss_bank T3_FORCE_BANK_STORAGE;
static uint8_t g_boss_shapes[48] T3_FORCE_BANK_STORAGE;
static uint8_t g_boss_loaded;
static uint8_t g_run_bosses[4];
static t3_force_progress_t g_progress;
static sat_result_t g_save_result;
static uint8_t g_key_saved,g_run_end;
static uint32_t g_end_field;
static t3_divmot_t g_common_bank T3_FORCE_BANK_STORAGE;
static uint8_t g_body_shapes[2][48] T3_FORCE_BANK_STORAGE;
/* Fixed fallback for pairs whose live banks leave no arena space. */
static t3_psx_local_pose_entry_t g_force_pose_entries[16] __attribute__((section(".wram_l")));
static t3_psx_local_pose_cache_t g_force_pose_cache __attribute__((section(".wram_l")));
static t3_tick_clock_t g_force_clock;
static volatile uint32_t g_force_images;
static void init_force_pose_cache(void) {
    uint32_t bytes=0;
    t3_psx_local_pose_entry_t* entries=t3_fighter_pack_unused_arena(&bytes);
    /* The requested size, halved until the arena's free tail holds it (a boss pairing can leave none). */
    unsigned n=T3_FORCE_POSE_CACHE_ENTRIES;
    while(n>16u && (!entries || bytes/sizeof(*entries)<n))n>>=1;
    if(n>16u)t3_psx_local_pose_cache_init(&g_force_pose_cache,entries,n);
    else t3_psx_local_pose_cache_init(&g_force_pose_cache,g_force_pose_entries,16u);
}
#if T3_FORCE_INPUT_PROBE
/* Diagnostic checkpoints share the same input and state digest in every
 * candidate. Exclude pointers/padding so memory placement can be compared. */
static uint32_t g_force_probe_input_hash=2166136261u,g_force_probe_state_hash=2166136261u;
static uint32_t g_force_probe_start_field;
static volatile uint32_t g_force_probe_600[11];

static volatile uint32_t g_force_probe_skin_checked;
/* The most cold faces (no material program) and skipped faces in one frame: sizes T3_FORCE_COLD_FACES. */
static volatile uint32_t g_force_probe_cold[2];
static uint16_t force_probe_pad(uint32_t tick) {
    uint16_t held=(tick>=200u && tick<1800u)?0x2000u:0u;
    if(tick>=350u && tick%60u<10u)held|=0x80u;
    return held;
}
static uint32_t probe_word(uint32_t hash,uint32_t word) {return (hash^word)*16777619u;}
static void force_probe_commit(uint16_t held,uint16_t pressed) {
    const t3_psx_match_t* m=&g_owned_match;
    uint32_t h=g_force_probe_state_hash;
    g_force_probe_input_hash=probe_word(g_force_probe_input_hash,(uint32_t)held|((uint32_t)pressed<<16));
    h=probe_word(h,m->ticks);h=probe_word(h,m->camera.rand);
    h=probe_word(h,m->flow.state);h=probe_word(h,m->flow.counter);h=probe_word(h,m->flow.timer);
    for(unsigned i=0;i<3;++i) {
        const t3_psx_fighter_t* f=&m->fighter[i];
        h=probe_word(h,f->health_q16);h=probe_word(h,f->vm.slot);h=probe_word(h,f->vm.clock.pose_frame);
        h=probe_word(h,f->ground.root.x);h=probe_word(h,f->ground.root.y);h=probe_word(h,f->ground.root.z);
        h=probe_word(h,f->ground.heading);h=probe_word(h,f->parts_mask);
        for(unsigned j=0;j<T3_PSX_DRAW_PARTS;++j) {
            h=probe_word(h,f->world[j].origin.x);h=probe_word(h,f->world[j].origin.y);h=probe_word(h,f->world[j].origin.z);
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)h=probe_word(h,(uint16_t)f->world[j].rotation.m[r][c]);
        }
    }
    int32_t force[T3_FORCE_VECTOR_WORDS];
    t3_force_level_save(&m->force,force);
    for(unsigned i=0;i<T3_FORCE_VECTOR_WORDS;++i)h=probe_word(h,(uint32_t)force[i]);
    h=probe_word(h,m->force_cam.x);h=probe_word(h,m->force_cam.pitch);
    g_force_probe_state_hash=h;
    if(m->ticks==600u) {
        g_force_probe_600[1]=g_force_probe_input_hash;g_force_probe_600[2]=h;
        g_force_probe_600[3]=sat_field_count()-g_force_probe_start_field;
        g_force_probe_600[4]=g_force_clock.observed_fields;
        g_force_probe_600[5]=g_force_clock.discarded_fields;
        g_force_probe_600[6]=g_force_clock.scheduled_ticks;
        g_force_probe_600[7]=g_force_images;
        g_force_probe_600[8]=g_force_pose_cache.hits;g_force_probe_600[9]=g_force_pose_cache.misses;
        g_force_probe_600[10]=g_force_pose_cache.evictions;g_force_probe_600[0]=600u;
    }
}
#endif

/* Portable match for rendering (3 fighters) - mirrors owned match positions. */
static t3_match_t g_portable_match;

/* AI for two CPU enemies. */

static uint8_t g_cpu_mask = 6u;  /* enemies are fighters 1 and 2; fighter 0 is human */
static uint16_t g_cpu_difficulty = 1u;
static uint8_t g_cpu_level = 4u;

/* The view a frame is drawn with: the simulation's Force view, or on level 4 the same camera rolled by the level's
 * 0x82 (force_roll), as the original's CameraBuildView builds it. The simulation keeps its own unrolled view. */
static t3_psx_camera_matrix_t g_rolled_view;
static const t3_psx_camera_matrix_t* g_view = &g_owned_match.force_view;

/* VDP2 stage layer for the tile panorama. */
static sat_vdp2_layer_config_t g_stage_layer;

/* The 3D scene camera built from the owned match's Force camera. */
static sat_camera3d_t g_scene_camera;

static sat_hud_t g_debug_hud;
static sat_ascii_font_t g_debug_font __attribute__((section(".wram_l")));   /* every glyph set by its init */
static sat_vdp1_texture_t g_loading_texture;
static sat_vdp1_texture_t g_item_picture,g_item_shadow;
static uint8_t g_item_shadow_slot;

/* Input state. */
static t3_input_t g_pad[3];

/* Level 4's vertical cell scroll table: one entry per plane cell column on screen (320 dots
 * at 368/320 texels each span at most 48), in VRAM A1; the pattern names fill A0's first
 * 0x10000 bytes. */
enum { STAGE_STAIR_TABLE = 0x20000u, STAGE_STAIR_COLUMNS = 48u };

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];
}

static int32_t s16le(const uint8_t* p) {
    return (int16_t)(uint16_t)(p[0] | p[1]<<8);
}

/* Saturn hardware bits are not t3_input_t bits. Preserve physical directions
 * until CpuOrPadInput knows the newly projected side of the current target. */
static uint16_t physical_pad(uint16_t held) {
    uint16_t out=0;
    if(held&SAT_PAD_UP)out|=0x1000u;
    if(held&SAT_PAD_RIGHT)out|=0x2000u;
    if(held&SAT_PAD_DOWN)out|=0x4000u;
    if(held&SAT_PAD_LEFT)out|=0x8000u;
    if(held&SAT_PAD_X)out|=0x80u;
    if(held&SAT_PAD_Y)out|=0x10u;
    if(held&SAT_PAD_A)out|=0x40u;
    if(held&SAT_PAD_B)out|=0x20u;
    if(held&SAT_PAD_L)out|=0x04u;
    if(held&SAT_PAD_START)out|=0x800u;
    return out;
}

static sat_result_t read_at(const char* name, uint32_t offset, void* dst, uint32_t bytes) {
    sat_cdfs_file_t file;
    uint32_t read = 0;
    SAT_TRY(sat_cdfs_lookup(g_volume, name, &file));
    if (offset + bytes > file.size) return SAT_ERR_IO;
    SAT_TRY(sat_cdfs_read_at(g_volume, &file, offset, dst, bytes, &read));
    return read == bytes ? SAT_OK : SAT_ERR_IO;
}

/* Private original PS1 font art, generated alongside the menu assets. It
 * lives in VDP1 with its own LUT, so stage and HUD palette uploads cannot
 * recolour the loading message while the CD is busy. */
static sat_result_t draw_loading(void) {
    const uint16_t width=(uint16_t)(g_loading_texture.width*320u/368u);
    const uint16_t height=(uint16_t)(g_loading_texture.height*224u/480u);
    return sat_draw_sprite_scaled_screen(&g_loading_texture,
        (int16_t)(302-width/2u),198,width,height,0);
}

static sat_result_t init_loading(void) {
    uint8_t* io=(uint8_t*)g_force_scene_faces;
    uint16_t palette[16],lut;
    SAT_TRY(read_at("LOAD.BIN",0,io,44));
    for(unsigned i=0;i<8;++i)if(io[i]!=(uint8_t)"T3LD0001"[i])return SAT_ERR_VERSION;
    const uint16_t width=(uint16_t)(io[8]<<8|io[9]);
    const uint16_t height=(uint16_t)(io[10]<<8|io[11]);
    if(!width || (width&7u) || !height || (uint32_t)width*height>8192u)return SAT_ERR_CAPACITY;
    for(unsigned i=0;i<16;++i)palette[i]=(uint16_t)(io[12+i*2]<<8|io[13+i*2]);
    SAT_TRY(read_at("LOAD.BIN",44,io,(uint32_t)width*height/2u));
    SAT_TRY(sat_vdp1_upload_lut(palette,&lut));
    SAT_TRY(sat_tex_upload_lut4_pixels(&g_loading_texture,io,width,height,lut));
    for(unsigned i=0;i<2;++i) {
        SAT_TRY(sat_begin_frame());
        SAT_TRY(draw_loading());
        SAT_TRY(sat_end_frame());
    }
    return SAT_OK;
}

/* Per-level original chicken and shadow. Keep picture colours in a VDP1
 * LUT. The shadow uses the final 16 entries of HUD bank 7, whose ASCII
 * glyphs only use entries 0/1. It must not overwrite bank 6: an INDEX8 boss
 * may own it. The panorama can fill all of banks 0..4. */
static sat_result_t init_force_items(void) {
    char name[]="ITEM1.BIN";
    uint8_t* io=(uint8_t*)g_force_scene_faces;
    uint16_t palette[16],lut;
    const uint32_t level=be32(g_level_header+8);
    if(level<1 || level>4)return SAT_ERR_VERSION;
    name[4]=(char)('0'+level);
    SAT_TRY(read_at(name,0,io,8272));
    if(__builtin_memcmp(io,"T3FI0001",8) || be32(io+8)!=0x00400080u ||
       be32(io+12)!=0x00400040u)return SAT_ERR_VERSION;
    for(unsigned i=0;i<16;++i)palette[i]=(uint16_t)(io[16+i*2]<<8|io[17+i*2]);
    SAT_TRY(sat_vdp1_upload_lut(palette,&lut));
    SAT_TRY(sat_tex_upload_lut4_pixels(&g_item_picture,io+80,64,128,lut));
    for(unsigned i=0;i<16;++i)palette[i]=(uint16_t)(io[48+i*2]<<8|io[49+i*2]);
    SAT_TRY(sat_vdp2_palette_upload(palette,16,7u*256u+240u));
    for(unsigned i=0;i<4096u;++i) {
        if(io[4176u+i]>15u)return SAT_ERR_VERSION;
        if(io[4176u+i])io[4176u+i]+=240u;
    }
    SAT_TRY(sat_tex_upload_indexed8_pixels(&g_item_shadow,io+4176,64,64,7));
    SAT_TRY(sat_vdp2_sprite_color_calc_configure_alpha(5));
    return sat_vdp2_sprite_color_calc_alpha_slot(128,&g_item_shadow_slot,NULL);
}

/* FUN_800B4374's two billboards share the fighters' painter. The original
 * projection returns SZ/4; t3_psx_camera_project returns SZ. Scale original
 * 368x480 framebuffer coordinates into the Saturn's 320x224 viewport. */
static sat_result_t draw_force_items(sat_scene_t* scene) {
    const t3_force_level_t* lv=&g_owned_match.force;
    if(lv->text_off || g_run_end)return SAT_OK;
    for(unsigned i=0;i<T3_FORCE_ITEMS;++i) {
        const t3_force_item_t* it=&lv->item[i];
        if(it->state!=1)continue;
        const int32_t point[3]={it->x,0x420,it->z};
        int32_t s[3];
        t3_psx_camera_project(g_view,point,s);
        if(s[2]*96<scene->faces.near_depth)continue;
        const int32_t d=s[2]>>2;
        if(d<=0)continue;
        const int32_t w=0x15E00/d,h=0x2BC00/d,left=s[0]-w/2,base=s[1]-h;
        for(unsigned shadow=0;shadow<2;++shadow) {
            const int32_t top=base-(shadow?w:h);
            const int32_t x0=(left-192)*320/368,x1=(left+w-192)*320/368;
            const int32_t y0=top*224/480-112,y1=base*224/480-112;
            if(x1<=-160 || x0>=160 || y1<=-112 || y0>=112 ||
               x0<-1024 || x1>1023 || y0<-1024 || y1>1023 || x0==x1 || y0==y1)continue;
            const sat_quad2_t quad={{x0,x1,x1,x0},{y0,y0,y1,y1}};
            const sat_scene3d_material_t material={.kind=SAT_SCENE3D_INDEXED_TEXTURED,
                .texture=shadow?&g_item_shadow:&g_item_picture,
                .color_calc_slot=shadow?g_item_shadow_slot:SAT_INDEXED_SOLID_OPAQUE};
            /* One original ordering-table bucket behind the picture. */
            SAT_TRY(sat_scene3d_faces_submit_projected_material(&scene->faces,&quad,
                s[2]*96+(shadow?32*96:0),&material,0));
        }
    }
    return SAT_OK;
}

/* The floor (stages.md, Floor): LEVn.BIN's floor-extra section holds the 10 x 10 descriptor resolved to a tile, a
 * palette and an orientation per entry, then the distinct 64 x 64 tiles and their palettes. The tiles are VDP1
 * textures (one palette per level); every frame draws the visible tiles of the world-anchored pattern as quads
 * through the Force view, behind everything else, cut at the backdrop's edge (z 0x9C4). */
#define T3_FORCE_FLOOR_TILES 16u
#define T3_FORCE_FLOOR_COLUMNS 7
#define T3_FORCE_FLOOR_ROWS 3
#define T3_FORCE_FLOOR_SHADES 4u
/* The spotlight floors (levels 3 and 4, stages.md kinds 3 and 4) brighten and darken per vertex; a projected quad takes
 * no Gouraud, so each tile picks one of four pre-darkened copies of its palette by the light at its centre. Kind 0
 * (levels 1 and 2) fades with distance instead: 128 + table[i] * shade >> 7, i = (beyond * 0x4000000 / 10,000) >> 16,
 * beyond the vertex depth less the camera's horizontal distance to the fighters (FloorDrawGrid). Its copies are darker
 * by 0, 1/12, 2/12 and 3/12 of the shade (the table runs 0 to 127); a row takes the copy whose step its depth reaches,
 * at beyond 1,309, 2,501 and 3,546 (scripts/force_floor_fade.py, from the table at 0x8001DF6C). The far row reaches
 * about 3,100. */
/* A tile and shade's command words, composed once: the floor writes its commands straight into the frame's list. What a
 * frame reads stays in high work RAM: low work RAM is the slower bank, and these lines are cold at every frame. */
static sat_sprite_frame_t g_floor_frame[T3_FORCE_FLOOR_TILES][T3_FORCE_FLOOR_SHADES];
static uint8_t g_floor_spot;
static uint8_t g_floor_fade;
static uint8_t g_floor_entry[100][4];
/* Per entry, the grid vertex (0..3, v0..v3) of each corner in the order the quad takes them: tl, tr, br, bl. */
static uint8_t g_floor_corner[100][4];
static uint8_t g_floor_ready;

__attribute__((optimize("Os")))
static sat_result_t init_force_floor(void) {
    char name[]="LEV1.BIN";
    uint8_t* io=(uint8_t*)g_force_scene_faces;
    const uint32_t level=be32(g_level_header+8);
    const uint32_t at=be32(g_level_header+52),tiles=be32(g_level_header+56),palettes=be32(g_level_header+60);
    g_floor_ready=0;
    if(level<1 || level>4)return SAT_ERR_VERSION;
    if(!at || !tiles)return SAT_OK;            /* an older level file: no floor */
    if(tiles>T3_FORCE_FLOOR_TILES || !palettes || palettes>16u)return SAT_ERR_VERSION;
    name[3]=(char)('0'+level);
    SAT_TRY(read_at(name,at,g_floor_entry,sizeof g_floor_entry));
    uint8_t tile_palette[T3_FORCE_FLOOR_TILES]={0};
    for(unsigned k=0;k<100u;++k) {
        if(g_floor_entry[k][0]>=tiles || g_floor_entry[k][1]>=palettes)return SAT_ERR_VERSION;
        tile_palette[g_floor_entry[k][0]]=g_floor_entry[k][1];
        t3_force_floor_quad_order(g_floor_entry[k][2],g_floor_corner[k]);
    }
    g_floor_spot=level==3u?3u:level==4u?4u:0u;
    uint16_t shade_scale[T3_FORCE_FLOOR_SHADES]={256,180,115,64};  /* of 256 */
    g_floor_fade=0;
    if(!g_floor_spot) {
        uint8_t kind_shade[4];   /* descriptor 0x324: s16 kind, s16 shade, little-endian */
        SAT_TRY(read_at(name,be32(g_level_header+40)+0x324u,kind_shade,4u));
        const int32_t shade=(int16_t)(kind_shade[2]|kind_shade[3]<<8);
        if(!(kind_shade[0]|kind_shade[1]) && shade<0 && shade>=-128) {
            for(unsigned sh=1;sh<T3_FORCE_FLOOR_SHADES;++sh)shade_scale[sh]=(uint16_t)(256+shade*(int32_t)sh/6);
            g_floor_fade=1;
        }
#if defined(T3_FORCE_NO_FLOOR_FADE) && T3_FORCE_NO_FLOOR_FADE
        g_floor_fade=(uint8_t)(kind_shade[0]&0u);   /* A/B control: the same code, the fade off at run time */
#endif
    }
    uint16_t lut[16][T3_FORCE_FLOOR_SHADES];
    SAT_TRY(read_at(name,at+400u+tiles*2048u,io,palettes*32u));
    for(unsigned p=0;p<palettes;++p)
        for(unsigned sh=0;sh<T3_FORCE_FLOOR_SHADES;++sh) {
            uint16_t palette[16];
            for(unsigned i=0;i<16u;++i) {
                const uint16_t c=(uint16_t)(io[p*32u+i*2u]<<8|io[p*32u+i*2u+1u]);
                if(!c){palette[i]=0;continue;}
                const unsigned r=(c&31u)*shade_scale[sh]>>8,g=(c>>5&31u)*shade_scale[sh]>>8,b=(c>>10&31u)*shade_scale[sh]>>8;
                palette[i]=(uint16_t)(0x8000u|b<<10|g<<5|r);
            }
            SAT_TRY(sat_vdp1_upload_lut(palette,&lut[p][sh]));
        }
    for(unsigned t=0;t<tiles;t+=4u) {
        const unsigned n=tiles-t<4u?tiles-t:4u;
        SAT_TRY(read_at(name,at+400u+t*2048u,io,n*2048u));
        for(unsigned k=0;k<n;++k) {
            sat_vdp1_texture_t tex;
            SAT_TRY(sat_tex_upload_lut4_pixels(&tex,io+k*2048u,64,64,lut[tile_palette[t+k]][0]));
            for(unsigned sh=0;sh<T3_FORCE_FLOOR_SHADES;++sh) {
                tex.palette=lut[tile_palette[t+k]][sh];
                SAT_TRY(sat_sprite_frame_init(&g_floor_frame[t+k][sh],&tex,0,0,0));
            }
        }
    }
#if defined(T3_FORCE_NO_FLOOR) && T3_FORCE_NO_FLOOR
    g_floor_ready=0;   /* A/B control: the same code, the floor switched off at run time */
#else
    g_floor_ready=1;
#endif
    return SAT_OK;
}

static int32_t floor_div(int32_t a,int32_t b) {return a/b-((a%b!=0)&&((a<0)!=(b<0)));}

__attribute__((optimize("Os")))
static sat_result_t draw_force_floor(sat_scene_t* scene) {
    if(!g_floor_ready || g_owned_match.force.text_off || g_run_end)return SAT_OK;
    const int32_t first=floor_div(g_owned_match.force_cam.x,T3_FORCE_FLOOR_TILE)-T3_FORCE_FLOOR_COLUMNS/2;
    /* The descriptor entry of tile (first + c, r) is row r (mod 10) * 10 + (first + c + 5) mod 10. */
    static const uint8_t row_base[10]={0,10,20,30,40,50,60,70,80,90};
    const unsigned base_col=(unsigned)(((first+5)%T3_FORCE_FLOOR_PATTERN+T3_FORCE_FLOOR_PATTERN)%T3_FORCE_FLOOR_PATTERN);
    /* The grid's vertices, projected once: (columns + 1) x (rows + 1). A vertex is unusable behind the near plane or
     * outside the quad coordinate range. */
    int16_t gx[T3_FORCE_FLOOR_ROWS+1][T3_FORCE_FLOOR_COLUMNS+1],gy[T3_FORCE_FLOOR_ROWS+1][T3_FORCE_FLOOR_COLUMNS+1];
    uint8_t ok[T3_FORCE_FLOOR_ROWS+1][T3_FORCE_FLOOR_COLUMNS+1];
    /* The view has no yaw or roll: a row of the grid has one screen y and a screen x linear in world x. Each row's y,
     * x at the camera and x per world unit (16.16) come from two projections whenever the camera's height, depth or
     * pitch changes (rarely); a frame is then plain multiplies. */
    static struct {int32_t y,z,pitch,valid;int32_t ry[T3_FORCE_FLOOR_ROWS+1],rx[T3_FORCE_FLOOR_ROWS+1],slope[T3_FORCE_FLOOR_ROWS+1],depth[T3_FORCE_FLOOR_ROWS+1];}
        cache;
    const t3_force_camera_t* cam=&g_owned_match.force_cam;
    if(!cache.valid || cache.y!=cam->y || cache.z!=cam->z || cache.pitch!=cam->pitch) {
        for(int32_t r=0;r<=T3_FORCE_FLOOR_ROWS;++r) {
            const int32_t z=T3_FORCE_FLOOR_EDGE-r*T3_FORCE_FLOOR_TILE;
            const int32_t p0[3]={cam->x,0,z},p1[3]={cam->x+8*T3_FORCE_FLOOR_TILE,0,z};
            int32_t s0[3],s1[3];
            t3_psx_camera_project(&g_owned_match.force_view,p0,s0);
            t3_psx_camera_project(&g_owned_match.force_view,p1,s1);
            cache.ry[r]=s0[1];cache.rx[r]=s0[0];cache.depth[r]=s0[2];
            cache.slope[r]=(int32_t)(((int64_t)(s1[0]-s0[0])<<16)/(8*T3_FORCE_FLOOR_TILE));
        }
        cache.y=cam->y;cache.z=cam->z;cache.pitch=cam->pitch;cache.valid=1;
    }
    /* PS1 to Saturn screen scale as 16.16 multipliers (320/368 and 224/480): no division per vertex. */
    enum { FLOOR_SCALE_X = 56988, FLOOR_SCALE_Y = 30583 };
    for(int32_t r=0;r<=T3_FORCE_FLOOR_ROWS;++r) {
        const int32_t y=((cache.ry[r]*FLOOR_SCALE_Y)>>16)-112;
        const uint8_t row_ok=(uint8_t)(cache.depth[r]*96>=scene->faces.near_depth && y>=-1024 && y<=1023);
        /* x is linear in world x along a row: Saturn x in 32.32 at the first column, then a fixed step (the PS1 16.16 x
         * scaled once per row, so a column is one add and its high word). */
        const int64_t px=((int64_t)(cache.rx[r]-192)<<16)+(int64_t)(first*T3_FORCE_FLOOR_TILE-cam->x)*cache.slope[r];
        int64_t sx=px*FLOOR_SCALE_X;
        const int64_t step=(int64_t)T3_FORCE_FLOOR_TILE*cache.slope[r]*FLOOR_SCALE_X;
        for(int32_t c=0;c<=T3_FORCE_FLOOR_COLUMNS;++c,sx+=step) {
            const int64_t wide=sx>>32;
            const int32_t x=wide<-2048?-2048:wide>2047?2047:(int32_t)wide;
            ok[r][c]=(uint8_t)(row_ok && x>=-1024 && x<=1023);
            gx[r][c]=(int16_t)x;gy[r][c]=(int16_t)y;
        }
    }
    if(g_view!=&g_owned_match.force_view) {
        /* Level 4's rolled view: a row has no single screen y, so every vertex is projected. */
        for(int32_t r=0;r<=T3_FORCE_FLOOR_ROWS;++r)for(int32_t c=0;c<=T3_FORCE_FLOOR_COLUMNS;++c) {
            const int32_t p[3]={(first+c)*T3_FORCE_FLOOR_TILE,0,T3_FORCE_FLOOR_EDGE-r*T3_FORCE_FLOOR_TILE};
            int32_t s[3];
            t3_psx_camera_project(g_view,p,s);
            const int32_t x=(s[0]-192)*320/368,y=s[1]*224/480-112;
            ok[r][c]=(uint8_t)(s[2]*96>=scene->faces.near_depth && x>=-1024 && x<=1023 && y>=-1024 && y<=1023);
            gx[r][c]=(int16_t)x;gy[r][c]=(int16_t)y;
        }
    }

    uint8_t row_shade[T3_FORCE_FLOOR_ROWS]={0};
    if(g_floor_fade) {
        const t3_vec3i_t root=g_owned_match.fighter[0].ground.root;
        const int32_t dx=(cam->x-root.x)>>2,dz=(cam->z-root.z)>>2;   /* quarters: the squares stay in 32 bits */
        const int32_t distance=(int32_t)sat_isqrt32((uint32_t)(dx*dx+dz*dz))<<2;
        for(int32_t r=0;r<T3_FORCE_FLOOR_ROWS;++r) {
            const int32_t beyond=(cache.depth[r]+cache.depth[r+1])/2-distance;
            row_shade[r]=(uint8_t)(beyond>=3546?3u:beyond>=2501?2u:beyond>=1309?1u:0u);
        }
    }
    /* Straight to VDP1, ahead of the scene: the floor is behind everything, which the painter would also find, at a
     * cost per face (docs/TEKKEN_FORCE_PLAN.md, floor A/B). The commands are sat_draw_sprite_distorted's, written into a
     * window of the list without its per-call checks. */
    sat_sprite_batch_window_t window;
    SAT_TRY(sat_sprite_batch_begin(&window,T3_FORCE_FLOOR_ROWS*T3_FORCE_FLOOR_COLUMNS,0u));
    sat_sprite_batch_window_t win=window;
    for(int32_t r=0;r<T3_FORCE_FLOOR_ROWS;++r)
        for(int32_t c=0;c<T3_FORCE_FLOOR_COLUMNS;++c) {
            /* v0 (x0, z1), v1 (x1, z1), v2 (x0, z0), v3 (x1, z0) */
            if(!ok[r][c] || !ok[r][c+1] || !ok[r+1][c] || !ok[r+1][c+1])continue;
            const int16_t vx[4]={gx[r][c],gx[r][c+1],gx[r+1][c],gx[r+1][c+1]};
            const int16_t vy[4]={gy[r][c],gy[r][c+1],gy[r+1][c],gy[r+1][c+1]};
            int16_t lo_x=vx[0],hi_x=vx[0],lo_y=vy[0],hi_y=vy[0];
            for(unsigned k=1;k<4u;++k) {
                if(vx[k]<lo_x)lo_x=vx[k];
                if(vx[k]>hi_x)hi_x=vx[k];
                if(vy[k]<lo_y)lo_y=vy[k];
                if(vy[k]>hi_y)hi_y=vy[k];
            }
            if(hi_x<=-160 || lo_x>=160 || hi_y<=-112 || lo_y>=112)continue;
            unsigned col=base_col+(unsigned)c;
            if(col>=T3_FORCE_FLOOR_PATTERN)col-=T3_FORCE_FLOOR_PATTERN;
            const uint8_t* entry=g_floor_entry[row_base[r]+col];
            const uint8_t* corner=g_floor_corner[row_base[r]+col];
            int16_t qx[4],qy[4];
            for(unsigned k=0;k<4u;++k){qx[k]=vx[corner[k]];qy[k]=vy[corner[k]];}

            unsigned shade=row_shade[r];
            if(g_floor_spot) {
                /* FUN_80049AE0: K / r^2 from one light (the player's root; level 4: the camera's x at z -2000), 128 is
                 * neutral; brighter than that cannot be shown, so it stays at 1. */
                const int32_t lx=g_floor_spot==3u?g_owned_match.fighter[0].ground.root.x:g_owned_match.force_cam.x;
                const int32_t lz=g_floor_spot==3u?g_owned_match.fighter[0].ground.root.z:-2000;
                /* in sixteenths of a unit, so the squares and K / 256 stay in 32 bits */
                const int32_t dx=((first+c)*T3_FORCE_FLOOR_TILE+T3_FORCE_FLOOR_TILE/2-lx)>>4;
                const int32_t dz=(T3_FORCE_FLOOR_EDGE-r*T3_FORCE_FLOOR_TILE-T3_FORCE_FLOOR_TILE/2-lz)>>4;
                const uint32_t d2=(uint32_t)(dx*dx+dz*dz)+1u;
                const uint32_t level=(g_floor_spot==3u?2000000u:1620000u)/d2;   /* 128 = neutral */
                shade=level>=109?0u:level>=73?1u:level>=45?2u:3u;
            }
            /* The corners go in the order the painter's opaque textured path sends them. */
            sat_sprite_batch_emit_distorted(&win,&g_floor_frame[entry[0]][shade],qx,qy);
        }
    SAT_TRY(sat_sprite_batch_end(&win,NULL));

    return SAT_OK;
}

/* Upload one 64x64 tile (64 cells, 8x8 each) to VRAM. */
static sat_result_t upload_tile(unsigned tile, const uint8_t* texels) {
    uint16_t cell[16];
    for (unsigned cy = 0; cy < 8; ++cy) {
        for (unsigned cx = 0; cx < 8; ++cx) {
            for (unsigned row = 0; row < 8; ++row) {
                const uint8_t* p = texels + (cy*8u + row)*32u + cx*4u;
                cell[row*2] = (uint16_t)(p[0]<<8 | p[1]);
                cell[row*2+1] = (uint16_t)(p[2]<<8 | p[3]);
            }
            SAT_TRY(sat_vdp2_vram_write_words((0x40000 + (tile*64u + cy*8u + cx)*32u)/2u, cell, 16));
        }
    }
    return SAT_OK;
}

/* Initialize the owned match config for 3 fighters (player + 2 enemies). */
static sat_result_t init_match_config(uint8_t level, uint8_t player_char,
                                      uint32_t start_score, uint32_t start_hiscore, int32_t start_health,
                                      uint32_t start_seed) {
    const t3_fighter_pack_t* player_pack = t3_fighter_pack(0);
    const t3_fighter_pack_t* enemy_pack = t3_fighter_pack(1); /* shared grunt motion/parts/radii */

    g_owned_config = (t3_psx_match_config_t){0};

    const int boss_at_start=level>3u;
    if(!boss_at_start) {
        if(enemy_pack->bank_bytes>sizeof g_force_grunt_bank)return SAT_ERR_CAPACITY;
        __builtin_memcpy(g_force_grunt_bank,enemy_pack->bank,enemy_pack->bank_bytes);
    }
    __builtin_memcpy(g_force_grunt_parts,enemy_pack->parts,sizeof g_force_grunt_parts);
    if(!t3_divmot_open(&g_player_bank,player_pack->bank,player_pack->bank_bytes) ||
       !t3_divmot_open(&g_enemy_bank,boss_at_start?enemy_pack->bank:g_force_grunt_bank,enemy_pack->bank_bytes) ||
       !t3_divmot_open(&g_common_bank,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES))
        return SAT_ERR_VERSION;
    t3_divmot_t* banks[3]={&g_player_bank,&g_enemy_bank,&g_common_bank};
    for(unsigned i=0;i<3;++i) {
        if(!t3_divmot_set_common_branches(banks[i],t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES))
            return SAT_ERR_VERSION;
        banks[i]->builtin_attacks=t3_local_combat_attacks;
    }
    g_player_bank.linked=&g_enemy_bank;
    g_enemy_bank.linked=&g_player_bank;
    g_owned_config.bank = &g_player_bank;
    g_owned_config.common = &g_common_bank;
    g_owned_config.actor_bank[0] = &g_player_bank;
    g_owned_config.actor_bank[1] = &g_enemy_bank;
    g_owned_config.actor_bank[2] = &g_enemy_bank;

    /* Character IDs: player is chosen character, enemies are character 21 (Force grunts). */
    g_owned_config.character[0] = player_char;
    g_owned_config.character[1] = boss_at_start?(uint8_t)enemy_pack->id:21;
    g_owned_config.character[2] = 21;

    g_owned_config.scale_percent = T3_KMD_SCALE_PERCENT;
    g_owned_config.actor_scale_percent[0]=player_pack->scale_percent;
    g_owned_config.actor_scale_percent[1]=g_owned_config.actor_scale_percent[2]=enemy_pack->scale_percent;
    /* FUN_800519B4: Force player health is 130 (see remake fight_simulation). */
    g_owned_config.max_health_q16 = 130 * 65536;

    /* Ground tables from cold load. */
    if (!t3_psx_ground_tables_bind((t3_psx_trig_t){t3_local_trig_blob, T3_LOCAL_TRIG_BYTES},
            t3_local_ground_blob, T3_LOCAL_GROUND_BYTES, &g_owned_config.ground_tables)) {
        return SAT_ERR_VERSION;
    }

    g_owned_config.spline = (t3_motion_tables_t){t3_local_spline_blob, T3_LOCAL_SPLINE_BYTES};
    g_owned_config.sequences = t3_local_sequences;
    g_owned_config.sequence_count = T3_LOCAL_SEQUENCE_COUNT;

    g_owned_config.enable_body = 1;
    g_owned_config.enable_combat = 1;
    g_owned_config.all_attacks = 1;
    g_owned_config.enable_rounds = 2;  /* original frame API */

    /* Force-specific config. */
    g_owned_config.start_force = 1;
    /* Force uses the original placement/setup path, which is entered through
     * the match's intro initializer even though its camera intro is skipped. */
    g_owned_config.start_intro = 1;
    g_owned_config.start_played = 1;
    g_owned_config.start_seed=start_seed;
    init_force_pose_cache();
    g_owned_config.local_pose_cache=&g_force_pose_cache;
    g_owned_config.force_tables = g_force_tables;
    g_owned_config.force_script = g_force_script;
    g_owned_config.force_trig = t3_local_trig_blob;
    g_owned_config.force_level = level;
    g_owned_config.forced_reaction = g_force_reaction;

    /* Combat tables from cold load. */
    g_owned_config.reactions = t3_local_combat_reactions;
    g_owned_config.reactions_size = T3_LOCAL_COMBAT_REACTION_BYTES;
    g_owned_config.close_reactions = t3_local_combat_close;
    g_owned_config.close_reactions_size = T3_LOCAL_COMBAT_CLOSE_BYTES;
    g_owned_config.throw_victim_slots = t3_local_combat_throws;
    g_owned_config.throw_victim_slots_size = T3_LOCAL_COMBAT_THROW_BYTES;
    g_owned_config.reversal_lists = t3_local_reversal_lists;
    g_owned_config.reversal_lists_size = T3_LOCAL_REVERSAL_BYTES;

    /* Body tables from cold load. */
    g_owned_config.body_tables = t3_local_body_blob;
    g_owned_config.body_tables_size = T3_LOCAL_BODY_BYTES;
    g_owned_config.ik = (t3_psx_ik_tables_t){{t3_local_trig_blob, T3_LOCAL_TRIG_BYTES}, t3_local_ik_blob, T3_LOCAL_IK_BYTES};
#if defined(T3_LOCAL_RSQRT_BYTES) && T3_LOCAL_RSQRT_BYTES > 0
    g_owned_config.rsqrt = (t3_psx_rsqrt_t){t3_local_rsqrt_blob, T3_LOCAL_RSQRT_BYTES};
#endif

    /* Parts and body shapes from fighter packs. */
    g_owned_config.parts[0] = player_pack->parts;
    g_owned_config.parts[1] = g_force_grunt_parts;
    g_owned_config.parts[2] = g_force_grunt_parts;
    /* Sphere-joint indices are common, radii belong to each costume. The
     * body blob starts with 384 bytes of separation tables, not shapes. */
    for(unsigned i=0;i<2;++i) {
        const t3_fighter_pack_t* pack=t3_fighter_pack(i);
        __builtin_memcpy(g_body_shapes[i],t3_local_body_blob+384u,32u);
        for(unsigned k=0;k<8;++k) {
            g_body_shapes[i][32u+2u*k]=(uint8_t)pack->radii[k];
            g_body_shapes[i][33u+2u*k]=(uint8_t)((uint16_t)pack->radii[k]>>8);
        }
    }
    g_owned_config.actor_body_shapes[0]=g_body_shapes[0];
    g_owned_config.actor_body_shapes[1]=g_owned_config.actor_body_shapes[2]=g_body_shapes[1];

    /* Force camera config (uses the side-scrolling camera, not the fight camera). */
    g_owned_config.camera = NULL;  /* Force uses its own camera */

    /* AI for enemies (fighters 1 and 2). */
    static const uint8_t force_ai_level[4]={3,4,5,5};
    g_cpu_level=level<4u?force_ai_level[level]:0u;
    if (!t3_psx_ai_game_init(&g_ai, t3_local_ai_tables, T3_LOCAL_AI_TABLE_BYTES, g_cpu_mask,
            g_cpu_difficulty, g_cpu_level)) {
        return SAT_ERR_CAPACITY;
    }
    g_ai.game_mode = 8;   /* mode 8, Tekken Force */
    g_ai.force_layout = 1; /* two enemy AI slots both target the player */
    /* Force AI: enemies share bank type 20, both target player (fighter 0). */
    t3_psx_ai_game_place_banks(&g_ai, player_pack->bank_type, enemy_pack->bank_type, 0);
    g_ai.observe = NULL;  /* no profiling in production */
    g_owned_config.ai = &g_ai;

    /* Sound tables from cold load. */
    g_owned_config.sound_tables = (t3_psx_sound_tables_t){t3_local_sound_tables, T3_LOCAL_SOUND_TABLE_BYTES};
    g_owned_config.sound_voice_set[0] = player_char;
    g_owned_config.sound_voice_set[1] = (uint8_t)enemy_pack->voice_set;
    g_owned_config.sound_voice_set[2] = 21;
#if defined(T3_SFX)
    g_owned_config.sound_sink = t3_combat_events_sound;   /* the original's sound decisions: swings, hits, voices */
    t3_combat_events_connect_sound();
#endif

    /* Spawn positions: player at origin, enemies off-screen initially. */
    g_owned_config.spawn[0] = (t3_psx_spawn_t){{0,0,0}, {0,-1027,0}, 16384u, 16384u};
    g_owned_config.spawn[1] = (t3_psx_spawn_t){{5000,0,0}, {5000,-1027,0}, 49151u, 49151u};
    g_owned_config.spawn[2] = (t3_psx_spawn_t){{6000,0,0}, {6000,-1027,0}, 49151u, 49151u};

    /* Carry the previous level's score, hi-score and health into the start. */
    g_owned_config.force_carry_score = start_score;
    g_owned_config.force_carry_hiscore = start_hiscore;
    g_owned_config.force_carry_health = start_health;

    /* Initialize the match. */
    if (!t3_psx_match_init(&g_owned_match, &g_owned_config)) {
        /* Temporary diagnostic: the panic screen prints the raw failure code
         * (-7 = reset_fighters, -107.. = fighter i move load, -207.. = pair distance). */
        return (sat_result_t)(-(int32_t)t3_psx_match_init_failure());
    }

    return SAT_OK;
}

/* Initialize the portable match for rendering (mirrors owned match). */
static void init_portable_match(void) {
    __builtin_memset(&g_portable_match, 0, sizeof(g_portable_match));
    for (unsigned i = 0; i < 3; ++i) {
        t3_history_reset(&g_portable_match.input_histories[i]);
        g_portable_match.fighters[i].health = T3_MAX_HEALTH;
    }
    g_portable_match.fighters[0].facing = 1;
    g_portable_match.fighters[1].facing = -1;
    g_portable_match.fighters[2].facing = -1;
    g_portable_match.round = 1;
}

/* Update the portable match from the owned match for rendering. */
static void update_portable_match(void) {
    /* The owned world is PS1 units; the skin's world is PS1 units * 96, and
     * t3_characters_render places `fighter->x * 256`. Both fighters' portable
     * x/z therefore carry root * 3/8 (as move_probe's owned view does). The
     * Force camera is absolute (it follows the player), so no centre offset. */
    for (unsigned i = 0; i < 3; ++i) {
        const t3_psx_fighter_t* src = &g_owned_match.fighter[i];
        t3_fighter_t* dst = &g_portable_match.fighters[i];

        dst->x = (int32_t)(((int64_t)src->ground.root.x) * 3 / 8);
        dst->z = (int32_t)(-((int64_t)src->ground.root.z) * 3 / 8);
        dst->facing = (src->ground.facing >= 0x2000u && src->ground.facing < 0xE000u) ? 1 : -1;
        if (i == 0 && g_owned_match.cur_opponent[0] < 3u) {
            const t3_psx_fighter_t* enemy = &g_owned_match.fighter[g_owned_match.cur_opponent[0]];
            dst->facing = enemy->ground.root.x >= src->ground.root.x ? 1 : -1;
        } else if (i != 0) {
            dst->facing = g_owned_match.fighter[0].ground.root.x >= src->ground.root.x ? 1 : -1;
        }
        dst->health = (int16_t)(g_owned_config.max_health_q16 > 0
            ? (int64_t)src->health_q16 * T3_MAX_HEALTH / g_owned_config.max_health_q16
            : 0);
        dst->guarding = src->ground.guard != 0u;
        dst->crouching = (uint8_t)((src->ground.state & 7u) == 1u);
        dst->stun = src->ground.recover_frames;
        dst->jump_frames = 0;
        dst->attack = 0;  /* clip selection falls back to position for now */
    }
    g_portable_match.total_ticks = g_owned_match.ticks;
    g_portable_match.timer = (uint16_t)(g_owned_match.flow.timer / 60);
    g_portable_match.round = 1;  /* Force has one long round */
}

static void update_draw_view(void) {
    if (!g_owned_match.force_roll) { g_view = &g_owned_match.force_view; return; }
    const t3_force_camera_t* c = &g_owned_match.force_cam;
    const t3_psx_camera_view_t v = {c->pitch>>6, c->yaw>>6, g_owned_match.force_roll, c->x, c->y, c->z, c->h};
    const t3_psx_trig_t trig = {t3_local_trig_blob, T3_LOCAL_TRIG_BYTES};
    g_view = t3_psx_camera_build_view_trig(&v, &trig, &g_rolled_view) ? &g_rolled_view : &g_owned_match.force_view;
}

/* Build the Saturn scene camera from the owned match's original Force view.
 * The mapping is the same one move_probe uses for the fight camera: the PS1
 * world is (x, -y, -z) at 96 units each, forward is view row 2 and up is
 * -row 1. */
static sat_result_t update_force_scene_camera(void) {
    const t3_force_camera_t* c = &g_owned_match.force_cam;
    const t3_psx_camera_matrix_t* m = g_view;
    const int32_t h = m->h;

    if (h <= 0 || h > 0x10000) return SAT_ERR_INVALID_ARG;
    /* The skinned vertices are rebased on the Force camera (see
     * t3_characters_set_force_origin), so the eye sits at the origin in x/z
     * and only its height remains. */
    g_scene_camera.eye = (sat_vec3_t){0, (sat_fx16_t)(-(int64_t)c->y * 96), 0};
    g_scene_camera.target = (sat_vec3_t){
        (sat_fx16_t)(g_scene_camera.eye.x + m->m[2][0] * 64),
        (sat_fx16_t)(g_scene_camera.eye.y - m->m[2][1] * 64),
        (sat_fx16_t)(g_scene_camera.eye.z - m->m[2][2] * 64)};
    g_scene_camera.up = (sat_vec3_t){-m->m[1][0] * 16, m->m[1][1] * 16, m->m[1][2] * 16};
    g_scene_camera.fov_y = (sat_fx16_t)sat_atan2_16(240 * 4096, 0x1BD2 * h) * 720;
    g_scene_camera.aspect = (sat_fx16_t)(((int64_t)184 * 0x1BD2 << 16) / (240 * 4096));
    g_scene_camera.near_z = SAT_FX16_ONE / 4;
    g_scene_camera.far_z = SAT_FX16_ONE * 40;
    return sat_camera3d_update(&g_scene_camera);
}

/* The panorama in the original's units (modes.md "Panorama", force_sim.background_draw): one map
 * texel per pixel of the PS1's 368 x 480 screen, shown on 320 x 224 like the floor
 * (x' = (x - 192) * 320/368 + 160, y' = y * 224/480), so NBG0 steps 368/320 texels per dot and
 * 480/224 per line, and the Saturn's left edge is PS1 x 8. */
static sat_result_t configure_stage_layer(void) {
    SAT_TRY(sat_vdp2_layer_configure(&g_stage_layer));
    return sat_vdp2_layer_set_zoom(SAT_VDP2_NBG0, (uint32_t)(0x10000u * 320u / 368u), (uint32_t)(0x10000u * 224u / 480u));
}

/* Update the VDP2 stage layer scroll from the Force camera: one texel per 20 units of the
 * camera's view-space x (modulo the map width), and the map's top edge at the projection of
 * (camera x, 0, 2,500) minus 64 x its height. */
static void update_stage_layer_scroll(uint32_t level_width_tiles) {
    const int32_t width = (int32_t)(level_width_tiles * 64u);
    int32_t camera_x = g_owned_match.force_cam.x - g_owned_match.force.origin;
    if (g_view != &g_owned_match.force_view)   /* rolled: the view-space x also takes the camera's height */
        camera_x = (g_view->m[0][0] * camera_x + g_view->m[0][1] * g_owned_match.force_cam.y) >> 12;
    int32_t dots = (camera_x / 20) % width;
    if (dots < 0) dots += width;
    /* The view has no yaw and a fixed roll, so that y changes only with the camera's height, depth or pitch. */
    static int32_t top, top_y, top_z, top_pitch = -1;
    const t3_force_camera_t* cam = &g_owned_match.force_cam;
    if (cam->y != top_y || cam->z != top_z || cam->pitch != top_pitch) {
        const int32_t edge[3] = {cam->x, 0, T3_FORCE_FLOOR_EDGE};
        int32_t screen[3];
        t3_psx_camera_project(g_view, edge, screen);
        top = screen[1] - (int32_t)(be32(g_level_header+16) * 64u);
        top_y = cam->y; top_z = cam->z; top_pitch = cam->pitch;
    }
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_scroll(SAT_VDP2_NBG0, (dots + 8) << 16, -top << 16));
    if (!g_stage_layer.vertical_cell_scroll) return;
    /* Level 4's staircase: the map starts tan(0xD8) * (184 + scroll % 192) lower at PS1 x 0
     * and climbs by rise = 192 * tan at every 3-column repetition (map dot multiple of 192).
     * The VDP2 reads one entry per plane cell column, from the first one on screen, and the
     * steps fall on cell edges. The plane rows above and below the 5-row map are blank black
     * cells, standing in for the original's black ground and gate tiles. */
    const int32_t sin = s16le(t3_local_trig_blob + 2u*0xD8u), cos = s16le(t3_local_trig_blob + 2u*0xD8u + 0x800u);
    const int32_t rise = (0xC0000 / cos) * sin;
    const int32_t first = (((184 + dots % 192) << 12) / cos) * sin;
    const int32_t column = (dots + 8) / 8, block = dots / 192;
    int32_t v[STAGE_STAIR_COLUMNS];
    for (int32_t i = 0; i < (int32_t)STAGE_STAIR_COLUMNS; ++i) {
        const int32_t dy = (first - ((column + i) * 8 / 192 - block) * rise) >> 12;
        v[i] = -dy << 16;
    }
    SAT_PANIC_IF_ERROR(sat_vdp2_vertical_cell_scroll_write(SAT_VDP2_NBG0, 0, v, STAGE_STAIR_COLUMNS));
}


static void save_force_progress(void) {
    const t3_force_level_t* lv=&g_owned_match.force;
    if(g_progress.hiscore<lv->hiscore_run)g_progress.hiscore=lv->hiscore_run;
    unsigned keys=lv->key_count;
    if(keys<(unsigned)lv->keys)keys=(unsigned)lv->keys;
    if(keys>3u)keys=3u;
    if(g_progress.keys<keys)g_progress.keys=(uint8_t)keys;
    if(lv->mode_flags&T3_FORCE_FLAG_SAVED_DOCTOR)g_progress.doctor=1;
    g_save_result=t3_force_save_store(&g_progress);
}

static uint32_t result_ticks(void) {
    const uint32_t refresh=sat_video_refresh_hz();
    return refresh?(uint32_t)((uint64_t)(sat_field_count()-g_end_field)*60u/refresh):0u;
}

/* A lost run only: a cleared one shows the Tekken Force result in MENU.BIN (menu_main.c). */
static sat_result_t draw_run_result(void) {
    return sat_hud_text_centered(&g_debug_hud,"GAME OVER",160,94);
}

/* Screen-space presentation reads the runner after all simulation ticks for
 * this image. Health uses each record's maximum (grunts have much less than
 * the player); healing may temporarily put the player above that maximum. */
static sat_result_t draw_force_hud(void) {
    const t3_force_level_t* lv=&g_owned_match.force;
    const t3_psx_fighter_t* player=&g_owned_match.fighter[0];
    const sat_hud_t* hud=&g_debug_hud;
    const uint32_t health=player->health_q16>0?(uint32_t)player->health_q16:0u;
    if(!g_run_end) {
    if(lv->level>3)SAT_TRY(sat_hud_text(hud,"FINAL STAGE",8,6));
    else SAT_TRY(sat_hud_value(hud,"STAGE ",(uint32_t)lv->level+1u,8,6));
    SAT_TRY(sat_hud_value(hud,"TIME ",g_owned_match.flow.timer>0?
        ((uint32_t)g_owned_match.flow.timer+59u)/60u:0u,208,6));
    SAT_TRY(sat_hud_bar(hud,8,18,152,7,health>(uint32_t)player->health_max?(uint32_t)player->health_max:health,(uint32_t)player->health_max,
        SAT_RGB555(8,2,2),SAT_RGB555(3,28,7)));
    SAT_TRY(sat_hud_value(hud,"SCORE ",lv->score,8,30));
    SAT_TRY(sat_hud_value(hud,"HI ",lv->hiscore_run,176,30));
    for(unsigned i=1;i<3;++i) {
        const t3_psx_fighter_t* enemy=&g_owned_match.fighter[i];
        if(!t3_psx_match_fighter_shown(&g_owned_match,i) || enemy->health_q16<=0)continue;
        SAT_TRY(sat_hud_bar(hud,8+(int)(i-1u)*160,44,144,4,
            (uint32_t)enemy->health_q16,(uint32_t)enemy->health_max,
            SAT_RGB555(5,2,2),SAT_RGB555(29,20,3)));
        SAT_TRY(sat_hud_text(hud,t3_fighter_name(t3_characters_force_model(i)),
            8+(int)(i-1u)*160,50));
    }
    for(unsigned i=0;i<T3_FORCE_ITEMS;++i) {
        const t3_force_item_t* it=&lv->item[i];
        if(it->state==2)SAT_TRY(sat_hud_text(hud,"LIFE UP!",
            ((int16_t)it->popup_x-192)*320/368+160,
            ((int16_t)it->popup_y>>4)*224/480));
    }
    for(unsigned i=0;i<T3_FORCE_SLOTS;++i) {
        const t3_force_slot_t* slot=&lv->slot[i];
        if(slot->state==6 && lv->clock<(int16_t)slot->popup_until && slot->type>=0 && slot->type<=4) {
            const unsigned at=0x128u+4u*(unsigned)slot->type+2u;
            const uint32_t seconds=g_force_tables[at]|(uint32_t)g_force_tables[at+1]<<8;
            const int x=((int16_t)slot->popup_x-192)*320/368+160;
            const int y=((int16_t)slot->popup_y>>4)*224/480;
            SAT_TRY(sat_hud_value(hud,"+",seconds,x,y));
            SAT_TRY(sat_hud_text(hud," SEC",x+16,y));
        }
    }
#if T3_FORCE_BOSS_PROBE
    SAT_TRY(sat_hud_text_centered(hud,"BOSS BOUNDARY PROBE",160,184));
#endif
#if T3_FORCE_INPUT_PROBE
    SAT_TRY(sat_hud_text_centered(hud,"INPUT REPLAY",160,196));
#endif
    SAT_TRY(sat_hud_value(hud,"KEYS ",(uint32_t)lv->keys,8,210));
    SAT_TRY(sat_hud_value(hud,"DEFEATED ",lv->kills,152,210));
    }
    if(g_run_end) {
        SAT_TRY(draw_run_result());
    } else if(lv->state==7) {
        SAT_TRY(sat_draw_rect_screen(24,68,272,132,SAT_RGB555(0,0,0)));
        SAT_TRY(sat_hud_text_centered(hud,"STAGE CLEAR",160,76));
        if(lv->tally>=30)SAT_TRY(sat_hud_value(hud,"STAGE BONUS",
            ((uint32_t)lv->level+1u)*5000u,32,96));
        if(lv->tally>=60)SAT_TRY(sat_hud_value(hud,"CLEAR BONUS",(uint32_t)lv->clear_bonus,32,112));
        if(lv->tally>=90)SAT_TRY(sat_hud_value(hud,"CLEAR TIME",(uint32_t)lv->clear_time/60u,32,128));
        if(lv->tally>=120) {
            if(lv->key_earned)SAT_TRY(sat_hud_text_centered(hud,"YOU GOT A KEY!",160,152));
            else if(lv->final_challenge)SAT_TRY(sat_hud_text_centered(hud,"CHALLENGE FINAL STAGE!",160,152));
            else if(lv->level<3 && lv->bonus_time!=0)
                SAT_TRY(sat_hud_value(hud,"BONUS TIME",(uint32_t)lv->bonus_time/60u,32,152));
            SAT_TRY(sat_hud_text_centered(hud,"PRESS START",160,180));
        }
    } else if(lv->state==3 || lv->state==4) {
        SAT_TRY(draw_loading());
    }
#if T3_FORCE_CLEAR_PROBE
    SAT_TRY(sat_hud_text_centered(hud,"CLEAR PATH PROBE",160,196));
#endif
#if T3_FORCE_RESCUE_PROBE
    SAT_TRY(sat_hud_text_centered(hud,"RESCUE PATH PROBE",160,196));
#endif
#if T3_FORCE_ITEM_PROBE
    SAT_TRY(sat_hud_text_centered(hud,"PICKUP PATH PROBE",160,196));
#endif
#if T3_FORCE_VARIANT_PROBE
    SAT_TRY(sat_hud_text_centered(hud,"VARIANT PATH PROBE",160,184));
    SAT_TRY(sat_hud_value(hud,"E1 MODEL ",t3_characters_force_model(1),8,196));
    SAT_TRY(sat_hud_value(hud,"E2 MODEL ",t3_characters_force_model(2),168,196));
#endif
    if(g_save_result!=SAT_OK)SAT_TRY(sat_hud_text_centered(hud,"PROGRESS NOT SAVED",160,202));
    return SAT_OK;
}

static sat_result_t load_stage_layer(const char* lev_name, int stairs, int tile_only) {
        const uint32_t width = be32(g_level_header+12);
        const uint32_t height = be32(g_level_header+16);
        const uint32_t tiles = be32(g_level_header+20);
        const uint32_t palettes = be32(g_level_header+24);
        const uint32_t cells_at = be32(g_level_header+28);
        const uint32_t tiles_at = be32(g_level_header+32);
        const uint32_t palettes_at = be32(g_level_header+36);

        if (be32(g_level_header) != 0x54334C46u || !width || width > 16u || height > 8u || palettes > 80u) {
            return SAT_ERR_IO;
        }

        /* Read cell map (tile index + palette per 64x64 texel cell). */
        /* The face queue is unused during loading; reuse it for staging
         * rather than reserving another 8.5 KB for the lifetime of the game. */
        uint8_t* io=(uint8_t*)g_force_scene_faces;
        uint8_t* cells=io+8192u;
        SAT_TRY(read_at(lev_name, cells_at, cells, width * height * 4u));

        /* Read all palettes in one go (16 colours each, palette p at CRAM p*16). */
        SAT_TRY(read_at(lev_name, palettes_at, io, palettes * 32u));
        for (unsigned p = 0; p < palettes; ++p) {
            uint16_t pal[16];
            for (unsigned i = 0; i < 16; ++i) pal[i] = (uint16_t)(io[p*32u+i*2u]<<8 | io[p*32u+i*2u+1u]);
            SAT_TRY(sat_vdp2_palette_upload(pal, 16, (uint16_t)(p*16u)));
        }

        /* Read and upload tiles (4 per read). */
        for (unsigned t = 0; t < tiles; t += 4u) {
            const unsigned n = tiles - t < 4u ? tiles - t : 4u;
            SAT_TRY(read_at(lev_name, tiles_at + t * 2048u, io, n * 2048u));
            for (unsigned k = 0; k < n; ++k) SAT_TRY(upload_tile(t + k, io + k * 2048u));
        }

        /* One blank cell (code 0) in the black of palette 113: bank 7 entry 16, which
         * the text font (entries 0/1) and the item shadow (240..255) leave free. */
        {
            static const uint16_t blank[16] = {0};
            const uint16_t black = SAT_COLOR_BLACK;
            SAT_TRY(sat_vdp2_vram_write_words((0x40000 + tiles * 64u * 32u)/2u, blank, 16));
            SAT_TRY(sat_vdp2_palette_upload(&black, 1, 7u*256u + 16u));
        }

        /* Pattern names: two planes of 2x1 pages of 64x64 cells side by side (2,048 dots), 4 bytes
         * per name (2-word names), the map repeated across them: the scroll stays below the map
         * width, and a 368-texel view past it must still show the map, also on maps narrower than
         * the screen (level 4's 192) or not dividing 1,024 (levels 2 and 3). */
        for (unsigned row = 0; row < 64; ++row) {
            for (unsigned page = 0; page < 4; ++page) {
                uint16_t names[128];   /* one page row */
                for (unsigned i = 0; i < 64; ++i) {
                    uint16_t chr = (uint16_t)(0x40000/32u + tiles * 64u);
                    uint16_t pal = 113u;
                    if (row < height * 8u) {
                        const unsigned map_col = (page*64u + i) % (width * 8u);
                        const uint8_t* c = cells + ((row/8u)*width + map_col/8u)*4u;
                        chr = (uint16_t)(0x40000/32u + (unsigned)(c[0]<<8 | c[1]) * 64u + (row%8u)*8u + map_col%8u);
                        pal = (uint16_t)(c[2]<<8 | c[3]);
                    }
                    const uint32_t word = sat_vdp2_pattern_name_2word(chr, (uint8_t)pal, 0, 0, 0, 0);
                    names[2u*i] = (uint16_t)(word>>16);
                    names[2u*i+1u] = (uint16_t)word;
                }
                SAT_TRY(sat_vdp2_vram_write_words((page*0x4000u + row*256u)/2u, names, 128));
            }
        }

        /* Configure NBG0 for the panorama layer. */
        sat_vdp2_layer_config_default(SAT_VDP2_NBG0, &g_stage_layer);
        g_stage_layer.bitmap = 0;
        g_stage_layer.color_mode = SAT_VDP2_COLOR_MODE_16;
        g_stage_layer.char_size = SAT_VDP2_CHAR_SIZE_1X1;
        g_stage_layer.pattern_name_words = 2;
        g_stage_layer.plane_pages_x = 2;
        g_stage_layer.plane_pages_y = 1;
        /* The Doctor B. level (0x800B70F4) draws no map, only a black tile down to the horizon - 16 over the black
         * clear colour (stages.md "Clear colour"): the layer is hidden over the black back screen. */
        g_stage_layer.priority = tile_only ? 0 : 2;
        /* Opaque: the PS1 skips only texels of colour 0x0000, and no level tile has one
         * (scripts/render_force_panorama.py), while VDP2 would skip every index 0. */
        g_stage_layer.transparent = 0;
        g_stage_layer.char_bank_mask = (uint8_t)(SAT_VDP2_BANK_B0 | SAT_VDP2_BANK_B1);
        /* Planes A and C at 0, B and D at 0x8000: 2,048 x 1,024 dots, the rows repeating. */
        for (unsigned i = 0; i < 4; ++i) g_stage_layer.plane_address[i] = (i & 1u) * 0x8000u;
        g_stage_layer.vertical_cell_scroll = (uint8_t)(stairs != 0);
        g_stage_layer.vertical_cell_scroll_address = STAGE_STAIR_TABLE;
        SAT_TRY(configure_stage_layer());
        /* Sprites (fighters and the debug text) must draw over the panorama:
         * VDP2 sprite type 0 takes their priority from PRISA, and the default
         * is below NBG0's 2. */
        SAT_TRY(sat_vdp2_sprite_set_priority(5));
    return SAT_OK;
}

/* Only presentation and immutable assets reload. The owned match, VM frames,
 * RNG, health, AI state and runner remain untouched across NOW LOADING. */
static sat_result_t load_force_boss(const t3_force_handoff_t* start) {
    uint8_t entry[2];
    const uint32_t key=(uint32_t)start->character*4u+start->costume;
    SAT_TRY(read_at("BOSS.BIN",key*8u+(uint32_t)start->level*2u,entry,2));
    if(entry[0]>=T3_ROSTER_COUNT || entry[1]>3u)return SAT_ERR_VERSION;
    if(start->level<4u)g_run_bosses[start->level]=(uint8_t)(entry[0]*4u+entry[1]);
    SAT_TRY(sat_shutdown());
    const sat_video_config_t video={320,224,SAT_VIDEO_AUTO,0};
    SAT_TRY(sat_init(&video));
    SAT_TRY(init_loading());
    /* sat_init resets registers, but retains VDP2 VRAM/CRAM. Reuse the
     * already loaded panorama rather than rereading and uploading the level. */
    SAT_TRY(configure_stage_layer());
    SAT_TRY(sat_vdp2_sprite_set_priority(5));
    SAT_TRY(t3_fighter_packs_open_boss(start->character,entry[0],entry[1]));
    SAT_TRY(t3_characters_force_reload());
    SAT_TRY(t3_characters_reset());
    SAT_TRY(sat_ascii_font_init_8x8_indexed8(&g_debug_font,SAT_COLOR_WHITE,SAT_COLOR_BLACK,7u));
    SAT_TRY(sat_hud_init(&g_debug_hud,&g_debug_font,7u,8u));
    SAT_TRY(init_force_items());
    SAT_TRY(init_force_floor());
    SAT_TRY(t3_cold_data_load());
    const t3_fighter_pack_t* player=t3_fighter_pack(0);
    const t3_fighter_pack_t* boss=t3_fighter_pack(1);
    if(!t3_divmot_open(&g_player_bank,player->bank,player->bank_bytes) ||
       !t3_divmot_open(&g_boss_bank,boss->bank,boss->bank_bytes) ||
       !t3_divmot_set_common_branches(&g_player_bank,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES) ||
       !t3_divmot_set_common_branches(&g_boss_bank,t3_local_common_branches,T3_LOCAL_COMMON_BRANCH_BYTES))return SAT_ERR_VERSION;
    g_player_bank.linked=&g_enemy_bank;
    g_player_bank.builtin_attacks=g_boss_bank.builtin_attacks=t3_local_combat_attacks;
    __builtin_memcpy(g_boss_shapes,t3_local_body_blob+384u,32u);
    for(unsigned k=0;k<8;++k) {
        g_boss_shapes[32+2*k]=(uint8_t)boss->radii[k];
        g_boss_shapes[33+2*k]=(uint8_t)((uint16_t)boss->radii[k]>>8);
    }
    g_owned_config.force_boss_bank=&g_boss_bank;
    g_owned_config.force_boss_parts=boss->parts;
    g_owned_config.force_boss_shapes=g_boss_shapes;
    g_owned_config.force_boss_scale=boss->scale_percent;
    g_owned_config.force_boss_character=(uint8_t)boss->id;
    g_owned_config.force_boss_voice=(uint8_t)boss->voice_set;
    init_force_pose_cache();
    t3_psx_ai_game_place_banks(&g_ai,player->bank_type,boss->bank_type,0);
    SAT_TRY(t3_sfx_init(g_force_scene_faces,sizeof g_force_scene_faces));
    g_boss_loaded=1;
    return SAT_OK;
}

/* Main entry point. */
int main(void) {
    extern uint8_t __t3_sdk_hot_start[],__t3_sdk_hot_end[];
    for(uint8_t* byte=__t3_sdk_hot_start;byte<__t3_sdk_hot_end;++byte)*byte=0;
    sat_video_config_t video = {320, 224, SAT_VIDEO_AUTO, 0};
    uint8_t arg[SAT_EXEC_ARG_MAX];
    uint32_t arg_bytes = 0;
    t3_force_handoff_t start = {.seed=T3_START_SEED,.bosses={255,255,255,255}};
    char lev_name[] = "LEV1.BIN";

    SAT_PANIC_IF_ERROR(sat_init(&video));
    SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(SAT_COLOR_BLACK));
    SAT_PANIC_IF_ERROR(sat_begin_frame());
    SAT_PANIC_IF_ERROR(sat_end_frame());
    SAT_PANIC_IF_ERROR(t3_disc_volume(&g_volume));
    SAT_PANIC_IF_ERROR(init_loading());

    /* Read hand-off from menu (level, score, health, etc.). */
    if (sat_exec_arg(arg, sizeof arg, &arg_bytes) == SAT_OK && t3_force_handoff_unpack(arg, arg_bytes, &start)) {
        lev_name[3] = (char)('1' + (start.level<4u?start.level:3u));
    } else {
        g_save_result=t3_force_save_load(&g_progress);
        start.keys=g_progress.keys;start.hiscore=g_progress.hiscore;
        if(g_progress.doctor)start.flags=T3_FORCE_FLAG_DOCTOR_UNLOCKED;
    }
    g_progress=(t3_force_progress_t){start.hiscore,start.keys,(start.flags&T3_FORCE_FLAG_DOCTOR_UNLOCKED)!=0};
    __builtin_memcpy(g_run_bosses,start.bosses,sizeof g_run_bosses);

#if T3_FORCE_BOSS_PROBE
    start.character=T3_FORCE_BOSS_PROBE_CHARACTER;
#endif

#if T3_FORCE_RESCUE_PROBE
    start.level=4;
    lev_name[3]='4';
#endif
#if T3_FORCE_VARIANT_PROBE
    start.level=T3_FORCE_VARIANT_PROBE_LEVEL;
    lev_name[3]=(char)('1'+start.level);
#endif

    /* Read level header (needed for stage layer). */
    SAT_PANIC_IF_ERROR(read_at(lev_name, 0, g_level_header, 64));

    /* The staircase is level 4's; the Doctor B. level (after level 4) shows no map. */
    SAT_PANIC_IF_ERROR(load_stage_layer(lev_name, start.level==3u, start.level>3u));

    /* Read TABLES.BIN from disc (Force level runner tables). */
    SAT_PANIC_IF_ERROR(read_at("TABLES.BIN", 0, g_force_tables, T3_FORCE_TABLE_BYTES));
    if(!t3_force_tables_valid(g_force_tables,sizeof g_force_tables))SAT_PANIC_IF_ERROR(SAT_ERR_VERSION);

    /* Read level script from LEVn.BIN. */
    SAT_PANIC_IF_ERROR(read_at(lev_name, be32(g_level_header+44), g_force_script, T3_FORCE_SCRIPT_BYTES));
#if T3_FORCE_VARIANT_PROBE
    /* Keep the natural script parked until the labelled probe begins. */
    __builtin_memset(g_force_script,0,sizeof g_force_script);
    g_force_script[3]=0x7f;
#endif

    /* Open fighter packs before cold_data_load: it loads their bank sections into the arena. */
    if(start.level>3u) {
        /* Only the earned fifth stage starts directly against Doctor B.
         * The fourth stage still runs its own scrolling/staircase script. */
        SAT_PANIC_IF_ERROR(t3_fighter_packs_open_boss(start.character,19u,0u));
        g_boss_loaded=1;
    } else {
        const unsigned key_at=0x1A0u+2u*start.level;
        SAT_PANIC_IF_ERROR(t3_fighter_packs_open_force(start.character,
            g_force_tables[key_at],g_force_tables[key_at+1u]));
    }

    /* Model decoding borrows the same arena that holds motion banks during
     * play. Compact and upload all actors before loading those banks. */
    SAT_PANIC_IF_ERROR(t3_characters_init(NULL, NULL));
    SAT_PANIC_IF_ERROR(t3_characters_reset());
    SAT_PANIC_IF_ERROR(sat_ascii_font_init_8x8_indexed8(&g_debug_font,SAT_COLOR_WHITE,SAT_COLOR_BLACK,7u));
    SAT_PANIC_IF_ERROR(sat_hud_init(&g_debug_hud,&g_debug_font,7u,8u));
    SAT_PANIC_IF_ERROR(init_force_items());
    SAT_PANIC_IF_ERROR(init_force_floor());

    /* Load cold data (common DIVMOT from MOTION.BIN and the opened packs' banks). */
    SAT_PANIC_IF_ERROR(t3_cold_data_load());
    SAT_PANIC_IF_ERROR(read_at("psx_forced_reaction.bin", 0, g_force_reaction, sizeof g_force_reaction));

    /* Initialize match config. */
    SAT_PANIC_IF_ERROR(init_match_config(start.level, start.character,
        start.score, start.hiscore, start.health,start.seed));

    /* The level runner itself is initialised by t3_psx_match_init (start_force). */
    g_owned_match.force.keys = start.keys;
    g_owned_match.force.key_count=start.keys;
    g_owned_match.force.hiscore_flag=start.new_record;
    g_owned_match.force.mode_flags = start.flags;
    g_owned_match.force.kills = start.kills;
    if(start.level>3u) {
        g_owned_match.flow.timer=0xE10;
        SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_enabled(SAT_VDP2_NBG0,0));
    } else if (start.timer > 0) g_owned_match.flow.timer = start.timer;

    /* Initialize portable match for rendering. */
    init_portable_match();

    /* Initialize scene for rendering 3 actors. */
    sat_scene_t scene;
    SAT_PANIC_IF_ERROR(sat_scene_init(&scene, g_force_scene_faces, g_force_scene_keys, g_force_scene_order, T3_FORCE_SCENE_CAPACITY));

    SAT_PANIC_IF_ERROR(sat_scene3d_faces_use_projected_storage(
        &scene.faces,g_force_projected_faces,T3_FORCE_SCENE_CAPACITY));
    scene.faces.cold_capacity=T3_FORCE_COLD_FACES;

    /* Initialize Force-specific systems. */
    SAT_PANIC_IF_ERROR(t3_sfx_init(g_force_scene_faces, sizeof g_force_scene_faces));
    t3_combat_events_reset();

    /* Main loop. */
    uint32_t previous_field=sat_field_count();
#if T3_FORCE_INPUT_PROBE
    g_force_probe_start_field=previous_field;
#endif
    uint16_t pending_pressed=0;
    uint32_t published_field=0;
    /* The first image needs a tick behind it: one field must pass before the clock is read. */
    SAT_PANIC_IF_ERROR(sat_wait_vblank());
    for (;;) {
#if T3_FORCE_VARIANT_PROBE
        static unsigned variant_probe_phase;
        if(variant_probe_phase<3u && g_owned_match.ticks>=200u+360u*variant_probe_phase &&
           g_owned_match.flow.state==T3_ROUND_FIGHT) {
            /* Diagnostic: release both slots and feed the real runner two
             * spawn commands, alternating requested variants each time.
             * Neutral CPU input isolates model changes from combat. */
            const unsigned swapped=(variant_probe_phase&1u)^1u;
            g_owned_config.ai=NULL;
            __builtin_memset(g_force_script,0,sizeof g_force_script);
            for(unsigned i=0;i<2;++i) {
                uint8_t* record=g_force_script+20u*i;
                const uint16_t x=(uint16_t)(i?2000:-2000);
                record[4]=1;record[6]=(uint8_t)x;record[7]=(uint8_t)(x>>8);
                record[14]=(uint8_t)(i^swapped);
                g_owned_match.force.slot[i].state=1;
                g_owned_match.fighter[i+1].health_q16=0;
                g_owned_match.fighter[i+1].body.active=0;
                g_owned_match.fighter[i+1].body.invulnerable=1;
            }
            g_force_script[43]=0x7f;
            g_owned_match.force.state=0;g_owned_match.force.script_ptr=0;
            ++variant_probe_phase;
        }
#endif
#if T3_FORCE_ITEM_PROBE
        static uint8_t item_probe_phase;
        t3_force_item_t* probe_item=&g_owned_match.force.item[0];
        const t3_vec3i_t probe_player=g_owned_match.fighter[0].ground.root;
        if(item_probe_phase==0 && g_owned_match.ticks>=200u &&
           g_owned_match.flow.state==T3_ROUND_FIGHT) {
            /* Hold a visible item for 160 ticks, then put it in pickup
             * range to exercise the real heal/popup without pad injection. */
            *probe_item=(t3_force_item_t){.state=1,.x=probe_player.x+1500,.z=probe_player.z};
            g_owned_match.fighter[0].health_q16=80*65536;
            item_probe_phase=1;
        } else if(item_probe_phase==1 && g_owned_match.ticks>=360u) {
            probe_item->x=probe_player.x;probe_item->z=probe_player.z;
            item_probe_phase=2;
        }
#endif
#if T3_FORCE_RESCUE_PROBE
        static uint8_t rescue_probe_started;
        if(!rescue_probe_started && start.level==4u && g_owned_match.ticks>=200u &&
           g_owned_match.flow.state==T3_ROUND_FIGHT) {
            rescue_probe_started=1;
            /* A controlled KO verifies the original round-end/tally path,
             * not the player's ability to win the complete mode. */
            g_owned_match.fighter[1].health_q16=0;
            g_owned_match.fighter[1].last_attacker=0;
        }
#endif
#if T3_FORCE_CLEAR_PROBE
        static uint8_t clear_probe_started;
        if(!clear_probe_started && start.level<4u && g_owned_match.ticks>=200u && g_owned_match.flow.state==T3_ROUND_FIGHT) {
            clear_probe_started=1;
            g_owned_match.force.level=3;
            g_owned_match.force.state=6;
            g_owned_match.force.flag_70e8=1;
            g_owned_match.level_ended=1;
            g_owned_match.force.fight_start_timer=g_owned_match.flow.timer;
            g_owned_match.flow.state=T3_ROUND_STAGE_CLEAR;
            g_owned_match.flow.counter=0;
            g_owned_match.flow.match_result=1;
            g_owned_match.round_actor[0].won=1;
        }
#endif
#if T3_FORCE_BOSS_PROBE
        if(!g_boss_loaded && g_owned_match.ticks>=200u && g_owned_match.flow.state==T3_ROUND_FIGHT) {
            /* The real boundary occurs in a live fight, after RoundIntro has
             * enabled the input owners. Keep that setup in the diagnostic. */
            g_owned_match.force.state=5;
            g_owned_match.flow.state=T3_ROUND_AREA_CHANGE;
        }
#endif
        if(!g_boss_loaded && g_owned_match.flow.state==T3_ROUND_AREA_CHANGE) {
            SAT_PANIC_IF_ERROR(load_force_boss(&start));
            /* Loading is not simulation time; the original area transition
             * also pauses the fight while resources are read from the disc. */
            /* Wait for the next field before rendering: at a fresh boss
             * boundary no camera view has been composed by a fight tick yet. */
            previous_field=sat_field_count();
            SAT_PANIC_IF_ERROR(sat_wait_vblank());
            continue;
        }

        /* The player is pad 0; the two enemies are driven by the Force AI bus. */
        sat_pad_state_t pad0;
        SAT_PANIC_IF_ERROR(sat_input_poll());
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(start.pad_side, 0, &pad0));
#if T3_FORCE_INPUT_PROBE
        /* Input is generated per simulation tick below. Render cadence must
         * not change press duration or which logic tick first sees a press. */
        pad0.held=0;pad0.pressed=0;
#endif
        pending_pressed|=physical_pad(pad0.pressed);
        g_pad[0] = (t3_input_t){0, 0};
        g_pad[1] = (t3_input_t){0, 0};
        g_pad[2] = (t3_input_t){0, 0};

        t3_force_level_t* lv=&g_owned_match.force;
        if(!g_run_end && g_owned_match.flow.state==T3_ROUND_END) {
            t3_force_handoff_t next;
            const int outcome=t3_force_run_finish(&start,lv,g_owned_match.round_actor[0].won==1,
                g_owned_match.fighter[0].health_q16,g_owned_match.camera.rand,&next);
            __builtin_memcpy(next.bosses,g_run_bosses,sizeof next.bosses);
            if(outcome==T3_FORCE_RUN_NEXT) {
                uint8_t out[T3_FORCE_HANDOFF_BYTES];
                t3_force_handoff_pack(&next,out);
                SAT_PANIC_IF_ERROR(sat_exec(g_volume,"FORCE.BIN",out,sizeof out));
            }
            save_force_progress();
            if(outcome==T3_FORCE_RUN_RESULT) {
                /* The result screen is the menu's, in the original art; it shows the stored high score. */
                uint8_t out[T3_FORCE_HANDOFF_BYTES];
                next.hiscore=g_progress.hiscore;
                t3_force_handoff_pack(&next,out);
                SAT_PANIC_IF_ERROR(sat_exec(g_volume,"MENU.BIN",out,sizeof out));
            }
            g_run_end=1u;
            g_end_field=sat_field_count();
        }
        if(g_run_end && result_ticks()>=121u) {
            uint8_t out[BOOT_ARG_BYTES]={PHASE_MENU};
            SAT_PANIC_IF_ERROR(sat_exec(g_volume,"MENU.BIN",out,sizeof out));
        }

        /* Simulation is 60 Hz independently of display work and PAL timing.
         * A new press is consumed once, even when this image catches up. */
        const uint32_t field=sat_field_count();
        const uint32_t ticks=g_run_end?0u:t3_tick_clock_advance(&g_force_clock,field-previous_field,
            sat_video_refresh_hz(),T3_HZ);
        previous_field=field;
        for(uint32_t tick=0;tick<ticks;++tick) {
        uint16_t held=physical_pad(pad0.held),pressed=tick?0:pending_pressed;
#if T3_FORCE_INPUT_PROBE
        held=force_probe_pad(g_owned_match.ticks+1u);
        pressed=held&~force_probe_pad(g_owned_match.ticks);
#endif
        if (!t3_psx_match_step_original_in_place(&g_owned_match, g_pad[0], g_pad[1],
                &(t3_psx_match_frame_t){
                    .frame_counter = g_owned_match.ticks + 1u,
                    .physical_inputs = 1,
                    .physical = {held,0,0},
                    .pressed = {pressed,0,0},
                    .third = g_pad[2]
                })) {
            SAT_PANIC_IF_ERROR(SAT_ERR_INVALID_ARG);
        }
#if T3_FORCE_INPUT_PROBE
        force_probe_commit(held,pressed);
#endif
        for(unsigned event=0;event<lv->event_count;++event) {
            if(lv->events[event].kind==T3_FORCE_EVENT_SAVE_PENDING && !g_key_saved) {
                save_force_progress();g_key_saved=1;
            }
            if(lv->events[event].kind==T3_FORCE_EVENT_SOUND)t3_sfx_play_code((uint16_t)lv->events[event].a);
        }
        if(g_owned_match.flow.state==T3_ROUND_AREA_CHANGE)break;
        }
        if(ticks)pending_pressed=0;
        /* The ticks and the image's preparation up to the characters' tick are CPU work only: they run while the last
         * image is still on its way to the screen. */
        /* Build the scene camera from the match's Force camera and update the
         * tile panorama scroll. The characters are rebased on the camera so
         * their original joint origins stay in the skin bridge's bounds. */
        t3_characters_set_force_origin(g_owned_match.force_cam.x, g_owned_match.force_cam.z);
        update_draw_view();
        SAT_PANIC_IF_ERROR(update_force_scene_camera());

        /* Publish the owned match's original pose for each draw actor. */
        for (unsigned i = 0; i < 3; ++i) {
            const t3_psx_fighter_t* f = &g_owned_match.fighter[i];
            if(i && !g_boss_loaded) {
                /* Verified Tekken3Remake enemy_key(level, variant): the
                 * runner's costume key selects one of the resident models. */
                const unsigned key=lv->fighter[i-1u].key;
                if(key>=0x54u && key<=0x57u)
                    SAT_PANIC_IF_ERROR(t3_characters_force_bind_enemy(i,key-0x54u));
            }
            t3_characters_set_force_pose(i, f->world, f->parts_mask,
                t3_psx_match_fighter_shown(&g_owned_match, i));
        }

        /* Update portable match for rendering. */
        update_portable_match();

        /* Update characters (clip selection, animation advance). */
        SAT_PANIC_IF_ERROR(t3_characters_tick(&g_portable_match));

        /* The VDP1 (1-cycle mode) takes the last list at the next field start, and then leaves the bank this image is
         * built in. When the ticks have run past that field start there is nothing to wait for. Level 4's staircase
         * is written to VDP2 VRAM directly, so it keeps the VBlank. The scroll reaches the VDP2 at a VBlank either
         * way (sat_vdp2_layers_commit). */
        if(sat_field_count()==published_field || g_stage_layer.vertical_cell_scroll)
            SAT_PANIC_IF_ERROR(sat_wait_vblank());
        update_stage_layer_scroll(be32(g_level_header+12));
        SAT_PANIC_IF_ERROR(sat_vdp2_layers_commit());

        /* Begin frame and draw. */
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        SAT_PANIC_IF_ERROR(sat_scene_begin(&scene, &g_scene_camera, SAT_FX16_ONE / 4,
            320, 224, 0));

        /* The floor first (direct VDP1 commands), then the characters, then the scene. */
        SAT_PANIC_IF_ERROR(draw_force_floor(&scene));
        if(!g_run_end)SAT_PANIC_IF_ERROR(t3_characters_render(&scene, &g_portable_match));
#if T3_FORCE_INPUT_PROBE
        if(g_owned_match.ticks>=600u && !g_force_probe_skin_checked) {
            uint32_t checked;
            SAT_PANIC_IF_ERROR(t3_characters_force_check_skin(&checked));
            g_force_probe_skin_checked=checked;
        }
#endif
        SAT_PANIC_IF_ERROR(draw_force_items(&scene));
#if T3_FORCE_INPUT_PROBE
        if(scene.faces.cold_count>g_force_probe_cold[0])g_force_probe_cold[0]=scene.faces.cold_count;
        g_force_probe_cold[1]=scene.faces.skipped_faces;
#endif
        SAT_PANIC_IF_ERROR(sat_scene_flush(&scene));
        SAT_PANIC_IF_ERROR(draw_force_hud());

#if T3_FORCE_DEBUG_HUD
        sat_hud_reset_cursor(&g_debug_hud, 4, 54);
        SAT_PANIC_IF_ERROR(sat_hud_line(&g_debug_hud,"pad",pad0.held));
        SAT_PANIC_IF_ERROR(sat_hud_line(&g_debug_hud,"dir",t3_history_at(&g_owned_match.fighter[0].input,0).direction));
        SAT_PANIC_IF_ERROR(sat_hud_line(&g_debug_hud, "tick", g_owned_match.ticks));
        SAT_PANIC_IF_ERROR(sat_hud_line(&g_debug_hud, "camx", (uint32_t)(g_owned_match.force_cam.x + 100000)));
        SAT_PANIC_IF_ERROR(sat_hud_line(&g_debug_hud, "px", (uint32_t)(g_owned_match.fighter[0].ground.root.x + 100000)));
        SAT_PANIC_IF_ERROR(sat_hud_line(&g_debug_hud,"ex",(uint32_t)(g_owned_match.fighter[1].ground.root.x+100000)));
        SAT_PANIC_IF_ERROR(sat_hud_line(&g_debug_hud,"rdy",(uint32_t)t3_characters_force_pose_ready(0)*4u+
            (uint32_t)t3_characters_force_pose_ready(1)*2u+(uint32_t)t3_characters_force_pose_ready(2)));

#endif

        SAT_PANIC_IF_ERROR(sat_end_frame());
        /* The combat sounds start with the image that shows their ticks: it is on screen from the next field. */
        {
            t3_combat_event_t sounds[32];
            unsigned sound_count=0;
            while(sound_count<32u && t3_combat_events_pop(&sounds[sound_count]))++sound_count;
            t3_sfx_play_events(sounds,sound_count);
        }
        published_field=sat_field_count();
        ++g_force_images;
    }
}
