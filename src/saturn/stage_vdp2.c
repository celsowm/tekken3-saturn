#include "stage_vdp2.h"
#include "saturn/color.h"
#include "saturn/vdp2_layers.h"
#if defined(T3_STAGE_ADD_PROBE)
#include "saturn/vdp2_compose.h"
#endif
#include "saturn/vdp2_floor.h"
#include "saturn/video.h"
#include "saturn/vdp1.h"
#include "saturn/cdfs.h"
#include "saturn/parallel.h"
#if defined(T3_PARTICLE_MASK_TIMING) || defined(T3_PARTICLE_PUBLISH_AUDIT)
#include "saturn/time.h"
#endif
#include "disc.h"
#include "stage_backdrop.h"
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION) && !defined(T3_STAGE_NO_VIEWPORT_SHIFT)
#include "psx_viewport.h"
#endif
#if defined(T3_STAGE_BITMAP_FLOOR)
#include "stage_floor_bitmap.h"
#endif
#include "t3_local_stage.h"
#if defined(T3_PARTICLE_POSITION_PROBE)
#include "psx_particle_tile.h"
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
#include "psx_face_coverage.h"
#endif
#endif

enum {
    WIDTH=320, HEIGHT=224,
    CHARS=0x40000, NAMES=0x60000,
    PARAM_A=0x3c000, COEFFICIENT_A=0x3c200,
    /* One entry (x, y, increment) per line, written in VBlank. */
    LINE_SCROLL=0x3d000,
    PALETTE=6, PICTURE_PALETTE=7,
    PICTURE_BYTES=T3_STAGE_COLUMNS*T3_STAGE_ROWS,
    LINE_WORDS=HEIGHT*6
};
static sat_vdp2_rotation_params_t g_params __attribute__((section(".wram_l")));
/* Rebuilt every image: High WRAM, where an access costs a quarter of Low's. */
#if !defined(T3_PARTICLE_ORDERED_MASK_PROBE)
static sat_vdp2_line_scroll_entry_t g_lines[HEIGHT];
#endif
static t3_stage_backdrop_line_t g_backdrop[HEIGHT];
static sat_fx16_t g_wall_depth[HEIGHT]
#if defined(T3_PARTICLE_GENERAL_MASK_PROBE)
    __attribute__((section(".wram_l")))
#endif
    ;
/* The VRAM words of one image's mapping, in the order of the fixed tables
 * they replace. Two slots: an image's tables are filled while the previous
 * image may still wait for its swap. */
typedef struct {
    uint16_t lines[LINE_WORDS];
    uint16_t params[SAT_VDP2_ROTATION_PARAM_WORDS];
    uint32_t coefficients[HEIGHT
#if defined(T3_STAGE_ORIGINAL_VERTICAL_PROBE)
        +4
#endif
    ];
} tables_t;
static tables_t g_tables[2] __attribute__((section(".wram_l"),aligned(4)));
static uint32_t g_slot;
/* Per slot, the VDP1 list whose swap publishes it; 0 for none. */
static volatile uint32_t g_pending[2];
volatile uint32_t g_t3_vdp2_floor_prepared, g_t3_vdp2_floor_presented;

static sat_cdfs_volume_t* g_volume;
#if defined(T3_VDP1_FLOOR_PROBE)
static sat_vdp1_texture_t g_floor_texture;
#if defined(T3_VDP1_FLOOR_SUBDIVIDE_PROBE)
static sat_vdp1_texture_t g_floor_quarters[4];
const sat_vdp1_texture_t* t3_stage_floor_quarter(unsigned index) {return index<4?&g_floor_quarters[index]:NULL;}
#endif
static uint8_t g_floor_sqrt_before[640];
const sat_vdp1_texture_t* t3_stage_floor_texture(void) {return &g_floor_texture;}
const uint8_t* t3_stage_floor_sqrt_before(void) {return g_floor_sqrt_before;}
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
static uint8_t g_floor_fade[1024],g_floor_correction[255*3];
uint16_t t3_stage_floor_shade(uint32_t depth,uint32_t midpoint_distance) {
    uint32_t delta=depth>midpoint_distance?depth-midpoint_distance:0;
    uint32_t index=(delta*(0x4000000u/10000u))>>16;
    if(index>1023)index=1023;
    /* shade = -51; arithmetic right shift rounds negative values down. */
    unsigned level=128u-((g_floor_fade[index]*51u+127u)>>7);
    const uint8_t* c=&g_floor_correction[level*3u];
    return sat_gouraud_rgb((int8_t)c[0],(int8_t)c[1],(int8_t)c[2]);
}
#endif
#endif
#if defined(T3_STAGE_STACK_PROBE)
volatile uint32_t g_t3_stage_stack_min[2] __attribute__((section(".wram_l")));
void t3_stage_stack_sample(unsigned which) {
    uint32_t sp;
    __asm__ volatile("mov r15,%0":"=r"(sp));
    volatile uint32_t* min=sat_parallel_uncached_address((const void*)g_t3_stage_stack_min);
    if(which<2 && sp>=0x06000000u && sp<0x06001000u && (!min[which] || sp<min[which]))min[which]=sp;
}
#endif
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
/* Reuse the stage upload buffer after initialization. The two 80-cell
 * particle slots and stage lines extend the union beyond the upload buffer;
 * the linker enforces the Low WRAM limit. Slots live through their swaps. */
static union {
    uint16_t load_words[8192];
    struct { t3_particle_tile_probe_t tiles[2]; sat_vdp2_line_scroll_entry_t lines[HEIGHT]; } live;
} g_stage_boot_or_particles __attribute__((section(".wram_l"),aligned(4)));
_Static_assert(sizeof(g_stage_boot_or_particles.live)<=24576u,"live stage data exceeds particle budget");
_Static_assert(0x64080u+T3_PARTICLE_PROBE_MAX_TILES*128u<=0x80000u,"particle characters exceed VDP2 VRAM");
#define g_load_words (g_stage_boot_or_particles.load_words)
#define g_probe_tiles (g_stage_boot_or_particles.live.tiles)
#define g_lines (g_stage_boot_or_particles.live.lines)
#else
static uint16_t g_load_words[8192] __attribute__((section(".wram_l"),aligned(4)));
#endif
static sat_camera3d_t g_previous_camera;
static int32_t g_previous_center[2],g_previous_turn;
static uint8_t g_valid,g_dirty;
static int same_camera(const sat_camera3d_t* camera) {
    if(camera->near_z!=g_previous_camera.near_z || camera->far_z!=g_previous_camera.far_z ||
       camera->eye.x!=g_previous_camera.eye.x || camera->eye.y!=g_previous_camera.eye.y ||
       camera->eye.z!=g_previous_camera.eye.z)return 0;
    for(unsigned i=0;i<16;++i)
        if(camera->view_proj.m[i]!=g_previous_camera.view_proj.m[i])return 0;
    return 1;
}
volatile uint32_t g_t3_vdp2_floor_updates, g_t3_stage_boot_step;
#if defined(T3_PARTICLE_SWAP_PROBE)
/* Master fills one image while the other may still await a VDP1 swap. */
#if defined(T3_PARTICLE_POSITION_PROBE)
#if !defined(T3_PARTICLE_ORDERED_MASK_PROBE)
static t3_particle_tile_probe_t g_probe_tiles[2] __attribute__((aligned(4)));
#endif
static uint8_t g_probe_old_x[T3_PARTICLE_PROBE_MAX_TILES],g_probe_old_y[T3_PARTICLE_PROBE_MAX_TILES];
static unsigned g_probe_old_count;
static t3_psx_particle_quad_t g_probe_quad[2];
t3_psx_particle_quad_t g_t3_particle_probe_quad;
volatile uint32_t g_t3_particle_swap_tile_count;
#else
typedef union { uint32_t aligned[128]; uint16_t pixels[256]; } probe_tiles_t;
static probe_tiles_t g_probe_tiles[2] __attribute__((aligned(4)));
#endif
static uint32_t g_probe_frame[2],g_probe_clut[2],g_probe_pattern[2],g_probe_slot;
#if defined(T3_PARTICLE_MULTI_PROBE)
static uint32_t g_probe_particle_count[2];
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
/* Bounds of allocated cells in the image currently being prepared. */
static int g_particle_left,g_particle_right,g_particle_top,g_particle_bottom;
/* Index of the allocated cell at each 8x8 screen cell, 0xFF when none, so a
 * face visits only the cells under its bounds instead of every allocated one. */
static uint8_t g_particle_cell[28][40];
#endif
volatile uint32_t g_t3_particle_swap_particles;
volatile uint32_t g_t3_particle_multi_fail_tiles,g_t3_particle_multi_fail_particles;
t3_psx_particle_quad_t g_t3_particle_multi_fail_quad;
#endif
static volatile uint32_t g_probe_pending[2];
static uint8_t g_probe_dirty;
volatile uint32_t g_t3_particle_swap_prepared,g_t3_particle_swap_published;
volatile uint32_t g_t3_particle_swap_frame,g_t3_particle_swap_serial;
volatile uint32_t g_t3_particle_swap_clut,g_t3_particle_swap_pattern;
#if defined(T3_PARTICLE_MULTI_PROBE)
sat_result_t t3_stage_particle_multi_begin(uint32_t frame) {
    unsigned slot=g_probe_slot;
    if(g_probe_pending[slot])return SAT_ERR_BUSY;
    g_probe_tiles[slot].count=0;g_probe_particle_count[slot]=0;
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    g_particle_left=320;g_particle_right=-1;g_particle_top=224;g_particle_bottom=-1;
    for(unsigned i=0;i<sizeof g_particle_cell;++i)((uint8_t*)g_particle_cell)[i]=0xFFu;
#endif
    g_probe_frame[slot]=frame;g_probe_clut[slot]=0;g_probe_pattern[slot]=0xff;
    g_probe_quad[slot].visible=0;return SAT_OK;
}
sat_result_t t3_stage_particle_multi_add(const t3_psx_particle_quad_t* quad,const uint8_t* pattern,const uint16_t palette[16]) {
    unsigned slot=g_probe_slot;
    const t3_particle_tile_status_t result=t3_particle_tile_accumulate_status(&g_probe_tiles[slot],quad,pattern,palette);
    if(result!=T3_PARTICLE_TILE_OK) {
        g_t3_particle_multi_fail_tiles=g_probe_tiles[slot].count;
        g_t3_particle_multi_fail_particles=g_probe_particle_count[slot];
        g_t3_particle_multi_fail_quad=*quad;
        return result==T3_PARTICLE_TILE_CAPACITY?SAT_ERR_CAPACITY:SAT_ERR_UNSUPPORTED;
    }
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    /* Accumulation may compact the list, so rebuild the conservative bound. */
    const t3_particle_tile_probe_t* tiles=&g_probe_tiles[slot];
    g_particle_left=320;g_particle_right=-1;g_particle_top=224;g_particle_bottom=-1;
    for(unsigned i=0;i<sizeof g_particle_cell;++i)((uint8_t*)g_particle_cell)[i]=0xFFu;
    for(unsigned i=0;i<tiles->count;++i) {
        const int x=tiles->tile_x[i]*8,y=tiles->tile_y[i]*8;
        g_particle_cell[tiles->tile_y[i]][tiles->tile_x[i]]=(uint8_t)i;
        if(x<g_particle_left)g_particle_left=x;
        if(x+7>g_particle_right)g_particle_right=x+7;
        if(y<g_particle_top)g_particle_top=y;
        if(y+7>g_particle_bottom)g_particle_bottom=y+7;
    }
#endif
    ++g_probe_particle_count[slot];return SAT_OK;
}
sat_result_t t3_stage_particle_multi_occlude(unsigned x,unsigned y,const uint8_t coverage[8]) {
    return t3_particle_tile_occlude(&g_probe_tiles[g_probe_slot],x,y,coverage)?SAT_OK:SAT_ERR_INVALID_ARG;
}
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
volatile uint32_t g_t3_ordered_mask_tiles,g_t3_ordered_mask_index8_tiles,g_t3_ordered_mask_lut4_tiles;
#if defined(T3_PARTICLE_CELL_CHECK)
volatile uint32_t g_t3_cell_check_faces,g_t3_cell_check_mismatch,g_t3_cell_check_visited;
#endif
#if defined(T3_PARTICLE_GENERAL_MASK_PROBE)
volatile uint32_t g_t3_ordered_general_tiles __attribute__((section(".wram_l")));
#if defined(T3_PARTICLE_MASK_TIMING)
volatile uint32_t g_t3_ordered_general_ticks __attribute__((section(".wram_l")));
#endif
#endif
volatile uint32_t g_t3_ordered_mask_cell_drops;
/* A face the mask cannot describe removes the particle pixels in the cells it
 * touches (a missing spark piece, never one drawn over a nearer fighter)
 * instead of blanking the whole effect image. */
static void drop_cell(t3_particle_tile_probe_t* tiles,unsigned i) {
    for(unsigned j=0;j<64;++j)tiles->pixels[i][j]=0;
    ++g_t3_ordered_mask_cell_drops;
}
sat_result_t t3_stage_particle_multi_face(const sat_model_texture_asset_t* texture,const int16_t x[4],const int16_t y[4]) {
    if(!x || !y)return SAT_ERR_INVALID_ARG;
    int left=x[0],right=x[0],top=y[0],bottom=y[0];
    for(unsigned i=1;i<4;++i) {
        if(x[i]<left)left=x[i];
        if(x[i]>right)right=x[i];
        if(y[i]<top)top=y[i];
        if(y[i]>bottom)bottom=y[i];
    }
    if(right<g_particle_left || left>g_particle_right ||
       bottom<g_particle_top || top>g_particle_bottom)return SAT_OK;
    t3_particle_tile_probe_t* tiles=&g_probe_tiles[g_probe_slot];
    uint8_t unpacked[64];const uint8_t* pixels=NULL;
    /* Visit the cells under the face's bounds; a face spanning more cells than
     * are allocated walks the allocated list instead. */
    int cx0=left<0?0:left>>3,cx1=right>319?39:right>>3;
    int cy0=top<0?0:top>>3,cy1=bottom>223?27:bottom>>3;
    const unsigned span=(unsigned)(cx1-cx0+1)*(unsigned)(cy1-cy0+1);
    const int by_cell=span<tiles->count;
#if defined(T3_PARTICLE_CELL_CHECK)
    /* Diagnostic: the cell walk must visit exactly the allocated cells that
     * overlap the face's bounds, the set the full list scan used to visit. */
    unsigned expected=0,expected_sum=0,visited=0,visited_sum=0;
    for(unsigned i=0;i<tiles->count;++i) {
        const int tx=tiles->tile_x[i],ty=tiles->tile_y[i];
        if(!((int)tx*8>right || (int)tx*8+7<left || (int)ty*8>bottom || (int)ty*8+7<top)){++expected;expected_sum+=i;}
    }
#endif
    int cx=cx0,cy=cy0;
    for(unsigned n=0;;++n) {
        unsigned i,tx,ty;
        if(by_cell) {
            if(cy>cy1)break;
            i=g_particle_cell[cy][cx];tx=(unsigned)cx;ty=(unsigned)cy;
            if(++cx>cx1){cx=cx0;++cy;}
            if(i==0xFFu)continue;
        } else {
            if(n>=tiles->count)break;
            i=n;tx=tiles->tile_x[i];ty=tiles->tile_y[i];
            if((int)tx*8>right || (int)tx*8+7<left || (int)ty*8>bottom || (int)ty*8+7<top)continue;
        }
#if defined(T3_PARTICLE_CELL_CHECK)
        ++visited;visited_sum+=i;
#endif
        /* Allocated cells can contain only transparent pixels or have been
         * cleared by earlier faces. Such cells cannot require masking. */
        if(!t3_particle_tile_intersects(tiles,i,left,top,right,bottom))continue;
        if(!texture || !texture->pixels || texture->width!=8u || texture->height!=8u || texture->pixel_count!=64u) {
            drop_cell(tiles,i);continue;
        }
        uint8_t mask[8];
        if(!pixels) {
            pixels=texture->pixels;
            if(texture->flags & SAT_MODEL_TEXTURE_LUT4) {
                for(unsigned j=0;j<64;++j) {
                    uint8_t v=texture->pixels[j/2u];unpacked[j]=(uint8_t)((j&1u)?v&15u:v>>4u);
                }
                pixels=unpacked;
            }
        }
        if(!t3_face_quad_tile_coverage(pixels,0,x,y,tx,ty,mask)) {
#if defined(T3_PARTICLE_GENERAL_MASK_PROBE)
#if defined(T3_PARTICLE_MASK_TIMING)
            uint16_t started=sat_time_frc();
#endif
            int ok=t3_face_general_tile_coverage(pixels,0,x,y,tx,ty,mask);
#if defined(T3_PARTICLE_MASK_TIMING)
            g_t3_ordered_general_ticks+=(uint16_t)(sat_time_frc()-started);
#endif
            if(!ok){drop_cell(tiles,i);continue;}
            ++g_t3_ordered_general_tiles;
#else
            drop_cell(tiles,i);continue;
#endif
        }
        if(!t3_particle_tile_occlude_index(tiles,i,mask))return SAT_ERR_INVALID_ARG;
        ++g_t3_ordered_mask_tiles;
        if(texture->flags & SAT_MODEL_TEXTURE_LUT4)++g_t3_ordered_mask_lut4_tiles;
        else ++g_t3_ordered_mask_index8_tiles;
    }
#if defined(T3_PARTICLE_CELL_CHECK)
    ++g_t3_cell_check_faces;
    if(visited!=expected || visited_sum!=expected_sum)++g_t3_cell_check_mismatch;
    else g_t3_cell_check_visited+=visited;
#endif
    return SAT_OK;
}
#endif
void t3_stage_particle_multi_end(void) {g_probe_dirty=1;++g_t3_particle_swap_prepared;}
#endif
sat_result_t t3_stage_particle_probe_prepare(const uint8_t* pattern,const uint16_t palette[16],uint32_t frame,uint16_t clut,uint8_t pattern_id
#if defined(T3_PARTICLE_POSITION_PROBE)
    ,const t3_psx_particle_quad_t* quad
#endif
) {
    const unsigned slot=g_probe_slot;
    if(g_probe_pending[slot] || (pattern && !palette))return SAT_ERR_BUSY;
#if defined(T3_PARTICLE_POSITION_PROBE)
    if(pattern) {
        if(!t3_particle_tile_probe(&g_probe_tiles[slot],quad,pattern,palette))return SAT_ERR_CAPACITY;
        g_probe_quad[slot]=*quad;
    } else {g_probe_tiles[slot].count=0;g_probe_quad[slot].visible=0;}
#else
    uint16_t* cells=g_probe_tiles[slot].pixels;
    for(unsigned ty=0;ty<2;++ty)for(unsigned tx=0;tx<2;++tx)
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
            unsigned pixel=(ty*8u+y)*16u+tx*8u+x;
            uint16_t color=0;
            if(pattern) {
                uint8_t pair=pattern[pixel/2u];
                color=palette[(pixel&1u)?(pair&15u):(pair>>4u)];
            }
            cells[(ty*2u+tx)*64u+y*8u+x]=color?(uint16_t)(0x8000u|(color&0x7fffu)):0;
        }
#endif
    g_probe_frame[slot]=frame;g_probe_clut[slot]=clut;g_probe_pattern[slot]=pattern_id;g_probe_dirty=1;++g_t3_particle_swap_prepared;
    return SAT_OK;
}
#endif

static const t3_stage_backdrop_t g_cylinder={T3_STAGE_BOX_HALF,T3_STAGE_BAKE_HEIGHT,T3_STAGE_TAN_TOP,
    T3_STAGE_ROWS_PER_TAN,T3_STAGE_COLUMNS,T3_STAGE_ROWS,WIDTH,HEIGHT};

static void copy_vram(uint32_t address,const void* source,uint32_t bytes) {
    volatile uint32_t* to=(volatile uint32_t*)(0x25E00000u+address);
    const uint32_t* from=(const uint32_t*)source;
    for(uint32_t i=0;i<bytes/4;++i)to[i]=from[i];
}
#if defined(T3_PARTICLE_PUBLISH_AUDIT)
/* calls, entered in VBlank, exited in VBlank, maximum FRT ticks, cells at
 * maximum, and maximum ticks with a simultaneous backdrop update. */
volatile uint32_t g_t3_particle_publish_audit[6];
#endif

/* The swap handler: in the VBlank in which the list showing an image is
 * swapped in, its tables replace the ones the VDP2 reads, so the panorama
 * and floor change with that image's sprites and the CPU never waits. VBlank
 * leaves time for the copy, and only CPU writes are used: the program may be
 * in the middle of an SCU-DMA. */
static void publish(uint32_t serial,void* user) {
    (void)user;
#if defined(T3_PARTICLE_PUBLISH_AUDIT)
    const volatile uint16_t* tvstat=(const volatile uint16_t*)0x25f80004u;
    const uint16_t start_time=sat_time_frc(),start_blank=*tvstat&8u;
    unsigned audit_count=0,audit_particle=0,audit_stage=0;
#endif
#if defined(T3_PARTICLE_SWAP_PROBE)
    for(unsigned s=0;s<2;++s)if(g_probe_pending[s]==serial) {
#if defined(T3_PARTICLE_POSITION_PROBE)
        volatile uint32_t* names=(volatile uint32_t*)0x25e60000u;
        for(unsigned i=0;i<g_probe_old_count;++i)names[g_probe_old_y[i]*64u+g_probe_old_x[i]]=0x00003200u;
        const unsigned count=g_probe_tiles[s].count;
#if defined(T3_PARTICLE_PUBLISH_AUDIT)
        audit_particle=1;audit_count=count;
#endif
        if(count)copy_vram(0x64080u,g_probe_tiles[s].pixels,count*128u);
        for(unsigned i=0;i<count;++i) {
            unsigned x=g_probe_tiles[s].tile_x[i],y=g_probe_tiles[s].tile_y[i];
            names[y*64u+x]=0x00003204u+i*4u;
            g_probe_old_x[i]=(uint8_t)x;g_probe_old_y[i]=(uint8_t)y;
        }
        g_probe_old_count=count;g_t3_particle_swap_tile_count=count;
#if defined(T3_PARTICLE_MULTI_PROBE)
        g_t3_particle_swap_particles=g_probe_particle_count[s];
#endif
        g_t3_particle_probe_quad=g_probe_quad[s];
#else
        copy_vram(0x64080u,g_probe_tiles[s].aligned,sizeof g_probe_tiles[s]);
#endif
        g_t3_particle_swap_frame=g_probe_frame[s];g_t3_particle_swap_serial=serial;
        g_t3_particle_swap_clut=g_probe_clut[s];g_t3_particle_swap_pattern=g_probe_pattern[s];
        g_probe_pending[s]=0;++g_t3_particle_swap_published;
    }
#endif
    for(unsigned s=0;s<2;++s) {
        if(g_pending[s]!=serial)continue;
#if defined(T3_PARTICLE_PUBLISH_AUDIT)
        audit_stage=1;
#endif
        copy_vram(LINE_SCROLL,g_tables[s].lines,sizeof g_tables[s].lines);
        /* The vertical viewport experiment uses four extra RAM scratch
         * entries, but VDP2 consumes only one coefficient per display line. */
        copy_vram(COEFFICIENT_A,g_tables[s].coefficients,HEIGHT*sizeof g_tables[s].coefficients[0]);
        copy_vram(PARAM_A,g_tables[s].params,sizeof g_tables[s].params);
        g_pending[s]=0;
        ++g_t3_vdp2_floor_updates;
    }
#if defined(T3_PARTICLE_PUBLISH_AUDIT)
    if(audit_particle) {
        const uint32_t elapsed=(uint16_t)(sat_time_frc()-start_time);
        ++g_t3_particle_publish_audit[0];
        if(start_blank)++g_t3_particle_publish_audit[1];
        if(*tvstat&8u)++g_t3_particle_publish_audit[2];
        if(elapsed>g_t3_particle_publish_audit[3]) {
            g_t3_particle_publish_audit[3]=elapsed;g_t3_particle_publish_audit[4]=audit_count;
        }
        if(audit_stage && elapsed>g_t3_particle_publish_audit[5])g_t3_particle_publish_audit[5]=elapsed;
    }
#endif
}

static sat_result_t read_part(const sat_cdfs_file_t* file,uint32_t at,uint32_t bytes) {
    uint32_t read=0;
    const sat_result_t st=sat_cdfs_read_at(g_volume,file,at,g_load_words,bytes,&read);
    return st!=SAT_OK?st:read==bytes?SAT_OK:SAT_ERR_IO;
}

sat_result_t t3_stage_vdp2_init(void) {
    sat_result_t st;
    sat_cdfs_file_t file;
    g_t3_stage_boot_step=1;
    if((st=t3_disc_volume(&g_volume))!=SAT_OK)return st;
    g_t3_stage_boot_step=4;
    if((st=sat_cdfs_lookup(g_volume,"PAUL.STG",&file))!=SAT_OK)return st;
    if(file.size!=T3_STAGE_FILE_BYTES)return SAT_ERR_VERSION;
    g_t3_stage_boot_step=5;
    if((st=read_part(&file,0,64))!=SAT_OK)return st;
    uint32_t header[16];
    for(unsigned i=0;i<16;++i)header[i]=((uint32_t)g_load_words[2*i]<<16)|g_load_words[2*i+1];
    /* Version 2 (scripts/build_local_paul_stage.py): the floor tile, its
     * palette, and the panorama as one 8-bit cylinder with its palette. */
    if(header[0]!=0x54335354u || header[1]!=2 || header[2]!=T3_STAGE_COLUMNS || header[3]!=T3_STAGE_ROWS ||
       header[4]!=64 || header[5]!=64 || header[6]!=2048 || header[7]!=16 || header[8]!=2048 ||
       header[9]!=4096 || header[10]!=6144 || header[11]!=T3_STAGE_PICTURE_COLOURS ||
       header[12]!=8192 || header[13]!=PICTURE_BYTES)return SAT_ERR_VERSION;
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
    if(header[14]!=0x46414445u)return SAT_ERR_VERSION;
#endif
#if defined(T3_VDP1_FLOOR_PROBE)
    if(header[15]!=0x53515254u)return SAT_ERR_VERSION;
#endif
    g_t3_stage_boot_step=6;
    if((st=read_part(&file,2048,2048))!=SAT_OK)return st;
#if defined(T3_VDP1_FLOOR_PROBE)
    /* Preserve one linear 64x64 copy while read_part replaces the source. */
    for(unsigned y=0;y<64;++y) {
        uint8_t row[256];
        if(!t3_stage_floor_bitmap_row((const uint8_t*)g_load_words,y,row))return SAT_ERR_VERSION;
        for(unsigned x=0;x<32;++x)((uint8_t*)(g_load_words+1024))[y*32+x]=row[x];
    }
#endif
    g_t3_stage_boot_step=7;
#if defined(T3_STAGE_BITMAP_FLOOR)
    /* Expand the loaded cells one row at a time into B0. The scratch row
     * remains separate from the immutable source until all uploads finish. */
    uint16_t bitmap_row[128];
    for(unsigned y=0;y<256;++y) {
        if(!t3_stage_floor_bitmap_row((const uint8_t*)g_load_words,y,(uint8_t*)bitmap_row))return SAT_ERR_VERSION;
        if((st=sat_vdp2_vram_write_words(CHARS/2+y*128u,bitmap_row,128))!=SAT_OK)return st;
    }
#else
    if((st=sat_vdp2_vram_write_words(CHARS/2,g_load_words,1024))!=SAT_OK)return st;
#endif
    g_t3_stage_boot_step=8;
    if((st=read_part(&file,4096,32))!=SAT_OK)return st;
#if defined(T3_VDP1_FLOOR_PROBE)
    uint16_t floor_lut[16],floor_lut_handle;
    for(unsigned i=0;i<16;++i)floor_lut[i]=g_load_words[i]|0x8000u;
    if((st=sat_vdp1_upload_lut(floor_lut,&floor_lut_handle))!=SAT_OK)return st;
    if((st=sat_tex_upload_lut4_pixels(&g_floor_texture,(const uint8_t*)(g_load_words+1024),64,64,floor_lut_handle))!=SAT_OK)return st;
#if defined(T3_VDP1_FLOOR_SUBDIVIDE_PROBE)
    for(unsigned q=0;q<4;++q) {
        uint8_t* quarter=(uint8_t*)(g_load_words+2048);
        const uint8_t* full=(const uint8_t*)(g_load_words+1024);
        for(unsigned y=0;y<32;++y)for(unsigned x=0;x<16;++x)
            quarter[y*16+x]=full[(y+(q>>1)*32)*32+x+(q&1u)*16];
        if((st=sat_tex_upload_lut4_pixels(&g_floor_quarters[q],quarter,32,32,floor_lut_handle))!=SAT_OK)return st;
    }
#endif
#endif
    g_t3_stage_boot_step=9;
    if((st=sat_vdp2_palette_upload(g_load_words,16,PALETTE*256))!=SAT_OK)return st;
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
    if((st=read_part(&file,4128,1024+255*3))!=SAT_OK)return st;
    for(unsigned i=0;i<1024;++i)g_floor_fade[i]=((const uint8_t*)g_load_words)[i];
    for(unsigned i=0;i<255*3;++i)g_floor_correction[i]=((const uint8_t*)g_load_words)[1024+i];
#endif
    if((st=read_part(&file,6144,T3_STAGE_PICTURE_COLOURS*2))!=SAT_OK)return st;
    if((st=sat_vdp2_palette_upload(g_load_words,T3_STAGE_PICTURE_COLOURS,PICTURE_PALETTE*256))!=SAT_OK)return st;
#if defined(T3_VDP1_FLOOR_PROBE)
    if((st=read_part(&file,T3_STAGE_FILE_BYTES-2048u,640))!=SAT_OK)return st;
    for(unsigned i=0;i<640;++i)g_floor_sqrt_before[i]=((const uint8_t*)g_load_words)[i];
#endif
#if !defined(T3_STAGE_BITMAP_FLOOR)
    for(unsigned y=0;y<64;++y) {
        for(unsigned x=0;x<64;++x)g_load_words[x]=(uint16_t)((y%8)*8+x%8);
        g_t3_stage_boot_step=10;
        if((st=sat_vdp2_vram_write_words(NAMES/2+y*64,g_load_words,64))!=SAT_OK)return st;
    }
#endif
    /* Upload once at boot, keeping all picture bytes outside the resident
     * application. Rows 240..255 of this 1024x256 bitmap hold the rotation
     * and coefficient tables and the line scroll table; the line scroll
     * never shows them. */
    for(uint32_t done=0;done<PICTURE_BYTES;) {
        uint32_t bytes=PICTURE_BYTES-done;
        if(bytes>sizeof(g_load_words))bytes=sizeof(g_load_words);
        g_t3_stage_boot_step=11;
        if((st=read_part(&file,8192+done,bytes))!=SAT_OK)return st;
        g_t3_stage_boot_step=12;
        if((st=sat_vdp2_vram_write_words(done/2,g_load_words,bytes/2))!=SAT_OK)return st;
        done+=bytes;
    }
    sat_vdp2_layer_config_t background;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&background);
    background.bitmap=1; background.bitmap_size=SAT_VDP2_BITMAP_1024X256;
    background.color_mode=SAT_VDP2_COLOR_MODE_256;
    background.palette=PICTURE_PALETTE;
    background.priority=1; background.transparent=1;
    background.plane_address[0]=0;
    g_t3_stage_boot_step=13;
    if((st=sat_vdp2_layer_configure(&background))!=SAT_OK)return st;
    const sat_vdp2_line_scroll_config_t lines={SAT_VDP2_NBG0,1,1,1,0,0,0,LINE_SCROLL};
    if((st=sat_vdp2_layer_line_scroll_enable(&lines))!=SAT_OK)return st;
#if defined(T3_STAGE_BITMAP_FLOOR)
    const sat_vdp2_rotation_bitmap_config_t config={
        .color_mode=SAT_VDP2_COLOR_MODE_16,.height_512=0,
#if defined(T3_VDP1_FLOOR_PROBE)
        .priority=0,
#else
        .priority=2,
#endif
        .transparent=0,
        .palette=PALETTE,.over=SAT_VDP2_ROTATION_OVER_REPEAT,.coefficients=1,
        .coefficient_mode=SAT_VDP2_COEFFICIENT_KXKY,.bitmap_address=CHARS,.table_address=PARAM_A};
    g_t3_stage_boot_step=14;
    if((st=sat_vdp2_rotation_bitmap_configure(&config))!=SAT_OK)return st;
#else
    sat_vdp2_rotation_config_t config;
    sat_vdp2_rotation_config_default(&config);
    config.color_mode=SAT_VDP2_COLOR_MODE_16;
    config.char_bank_mask=SAT_VDP2_BANK_B0;
    config.name_bank_mask=SAT_VDP2_BANK_B1;
    config.char_base_address=CHARS;
    config.table_address=PARAM_A;
    config.coefficients=1; config.transparent=0;
    config.palette=PALETTE;
    config.over=SAT_VDP2_ROTATION_OVER_REPEAT;
    config.priority=2;
    for(unsigned p=0;p<16;++p)config.plane_address[p]=NAMES;
    g_t3_stage_boot_step=14;
    if((st=sat_vdp2_rotation_configure(&config))!=SAT_OK)return st;
#endif
#if defined(T3_STAGE_ADD_PROBE)
    /* Controlled RGB tests in B1, available with the bitmap floor. Cell 0
     * is transparent. Either a fixed additive rectangle or four cells of
     * original spark texture are shown above VDP1; neither is a renderer. */
    enum { TEST_NAMES=0x60000, TEST_CHARS=0x64000 };
#if defined(T3_PARTICLE_TEXTURE_PROBE)
    for(unsigned i=0;i<320;++i)g_load_words[i]=0;
    if((st=sat_vdp2_vram_write_words(TEST_CHARS/2,g_load_words,320))!=SAT_OK)return st;
#else
    for(unsigned i=0;i<64;++i)g_load_words[i]=0;
    for(unsigned i=64;i<128;++i)g_load_words[i]=0x8000u|(9u<<10)|(7u<<5)|5u;
    if((st=sat_vdp2_vram_write_words(TEST_CHARS/2,g_load_words,128))!=SAT_OK)return st;
#endif
    for(unsigned y=0;y<64;++y) {
        for(unsigned x=0;x<64;++x) {
            g_load_words[2*x]=0;
#if defined(T3_PARTICLE_TEXTURE_PROBE)
#if defined(T3_PARTICLE_POSITION_PROBE)
            unsigned cell=0;
#else
            unsigned cell=(x>=10 && x<12 && y>=8 && y<10)?1u+(y-8u)*2u+x-10u:0u;
#endif
            g_load_words[2*x+1]=(uint16_t)(TEST_CHARS/32u+4u*cell);
#else
            g_load_words[2*x+1]=(uint16_t)(TEST_CHARS/32u+((x>=10 && x<20 && y>=7 && y<19)?4u:0u));
#endif
        }
        if((st=sat_vdp2_vram_write_words(TEST_NAMES/2+y*128u,g_load_words,128))!=SAT_OK)return st;
    }
    sat_vdp2_layer_config_t test_layer;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG1,&test_layer);
    test_layer.pattern_name_words=2;test_layer.color_mode=SAT_VDP2_COLOR_MODE_32768;
    test_layer.char_bank_mask=SAT_VDP2_BANK_B1;test_layer.priority=7;test_layer.transparent=1;
    for(unsigned p=0;p<4;++p)test_layer.plane_address[p]=TEST_NAMES;
    if((st=sat_vdp2_layer_configure(&test_layer))!=SAT_OK)return st;
    if((st=sat_vdp2_screen_color_calc_set(SAT_VDP2_SCREEN_NBG1,1,0))!=SAT_OK)return st;
    if((st=sat_vdp2_color_calc_set_add(1))!=SAT_OK)return st;
#endif
    /* From here on the VDP2 reads parameter table A and coefficient table A,
     * and only the swap handler rewrites them. Until the first image every
     * line is transparent. */
    for(unsigned y=0;y<HEIGHT;++y)g_tables[0].coefficients[y]=sat_vdp2_rotation_coefficient(0,1);
    if((st=sat_vdp2_rotation_write_coefficients(COEFFICIENT_A,g_tables[0].coefficients,HEIGHT))!=SAT_OK)
        return st;
    g_params=(sat_vdp2_rotation_params_t){0};
    g_params.coefficient_address=COEFFICIENT_A;g_params.coefficient_step=SAT_FX16_ONE;
    if((st=sat_vdp2_rotation_write_params(PARAM_A,&g_params))!=SAT_OK)return st;
    if((st=sat_vdp2_layers_commit())!=SAT_OK)return st;
    if((st=sat_vdp1_set_swap_handler(publish,NULL))!=SAT_OK)return st;
    return sat_vdp2_sprite_set_priority(6);
}

sat_result_t t3_stage_vdp2_prepare(const sat_camera3d_t* camera,int32_t center_x,int32_t center_z,int32_t turn) {
#if defined(T3_STAGE_STACK_PROBE)
    t3_stage_stack_sample(0);
#endif
    if(!camera)return SAT_ERR_INVALID_ARG;
    ++g_t3_vdp2_floor_prepared;
#if !defined(T3_STAGE_BENCH_EVERY_FRAME)
    if(g_valid && center_x==g_previous_center[0] && center_z==g_previous_center[1] &&
       turn==g_previous_turn && same_camera(camera))return SAT_OK;
#endif
    /* The panorama follows the fighters' centre, turned by the original's
     * backdrop angle; the floor ends where its lines meet the walls. */
    const int32_t eye[3]={camera->eye.x,camera->eye.y,camera->eye.z};
#if defined(T3_STAGE_ORIGINAL_VERTICAL_PROBE)
    sat_mat4_t backdrop_view=camera->view_proj;
    for(unsigned i=4;i<8;++i)backdrop_view.m[i]=(int32_t)((int64_t)backdrop_view.m[i]*15/14);
    if(!t3_stage_backdrop_lines_offset(&g_cylinder,backdrop_view.m,eye,turn,1,2,g_backdrop,g_wall_depth))
        return SAT_ERR_UNSUPPORTED;
#else
    if(!t3_stage_backdrop_lines(&g_cylinder,camera->view_proj.m,eye,turn,1,g_backdrop,g_wall_depth))
        return SAT_ERR_UNSUPPORTED;
#endif
    for(unsigned y=0;y<HEIGHT;++y) {
        int32_t column=g_backdrop[y].x;
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION) && !defined(T3_STAGE_NO_VIEWPORT_SHIFT)
        /* Stage rays previously assumed x=160; the mapped original GTE
         * center is x=167. Sample the same ray seven pixels earlier. */
        column=(column-(t3_psx_viewport_x(192)-WIDTH/2)*(int32_t)g_backdrop[y].zoom)
            &((T3_STAGE_COLUMNS<<16)-1);
#endif
        g_lines[y]=(sat_vdp2_line_scroll_entry_t){column,g_backdrop[y].y,g_backdrop[y].zoom};
    }
    /* The floor map repeats every 1,800 PS1 units in 64 dots. The skin's
     * world is 96 fx16 per PS1 unit around the fighters' centre, x and -z. */
    sat_vdp2_floor_t floor={WIDTH,HEIGHT,0,(int32_t)((int64_t)64*65536*65536/(1800*96)),
        (int32_t)(((int64_t)center_x*64*SAT_FX16_ONE/1800)%(64*SAT_FX16_ONE)),
        -(int32_t)(((int64_t)center_z*64*SAT_FX16_ONE/1800)%(64*SAT_FX16_ONE)),64*SAT_FX16_ONE,
        g_wall_depth,1};
    tables_t* tables=&g_tables[g_slot];
#if defined(T3_STAGE_ORIGINAL_VERTICAL_PROBE)
    /* Generate a 228-line virtual view, then display lines 4..227. Its
     * center is 114-4=110 and the original 240-line half-height becomes
     * 114; the row-1 focal scale restores the original pixel focal length. */
    sat_mat4_t floor_view=camera->view_proj;
    /* SDK reads each far value before writing the corresponding coefficient.
     * Use one array in place to avoid 1.8 KiB on the Slave's small stack. */
    sat_fx16_t* far_lines=(sat_fx16_t*)(void*)tables->coefficients;
    for(unsigned i=4;i<8;++i)floor_view.m[i]=(int32_t)((int64_t)floor_view.m[i]*20/19);
    for(unsigned i=0;i<4;++i)far_lines[i]=0;
    for(unsigned i=0;i<HEIGHT;++i)far_lines[i+4]=g_wall_depth[i];
    floor.height=HEIGHT+4;floor.far_lines=far_lines;
    sat_result_t st=sat_vdp2_floor_from_view(&floor,&floor_view,&camera->eye,
        camera->near_z,camera->far_z,&g_params,tables->coefficients);
    if(st!=SAT_OK)return st;
    for(unsigned i=0;i<HEIGHT;++i)tables->coefficients[i]=tables->coefficients[i+4];
    g_params.yst+=4*g_params.dyst;
#else
    sat_result_t st=sat_vdp2_floor_from_view(&floor,&camera->view_proj,&camera->eye,
        camera->near_z,camera->far_z,&g_params,tables->coefficients);
    if(st!=SAT_OK)return st;
#endif
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION) && !defined(T3_STAGE_NO_VIEWPORT_SHIFT)
    g_params.xst-=(t3_psx_viewport_x(192)-WIDTH/2)*SAT_FX16_ONE;
#endif
    g_params.coefficient_address=COEFFICIENT_A;
    if((st=sat_vdp2_rotation_encode_params(&g_params,tables->params))!=SAT_OK)return st;
    if(sat_vdp2_line_scroll_encode(SAT_VDP2_NBG0,g_lines,HEIGHT,tables->lines,LINE_WORDS)!=LINE_WORDS)
        return SAT_ERR_UNSUPPORTED;
    g_previous_camera=*camera;g_previous_center[0]=center_x;g_previous_center[1]=center_z;
    g_previous_turn=turn;
    g_valid=g_dirty=1;
    return SAT_OK;
}

sat_result_t t3_stage_vdp2_sync(void) {
    /* Everything prepare writes, for the CPU that did not run it. */
    const struct {const void* address;uint32_t size;} range[]={
        {&g_params,sizeof g_params},{g_lines,sizeof g_lines},{g_backdrop,sizeof g_backdrop},
        {g_wall_depth,sizeof g_wall_depth},{&g_tables[g_slot],sizeof g_tables[g_slot]},
        {&g_previous_camera,sizeof g_previous_camera},{g_previous_center,sizeof g_previous_center},
        {&g_previous_turn,sizeof g_previous_turn},{&g_valid,sizeof g_valid},{&g_dirty,sizeof g_dirty},
        {(const void*)&g_t3_vdp2_floor_prepared,sizeof g_t3_vdp2_floor_prepared},
    };
    for(unsigned i=0;i<sizeof range/sizeof *range;++i) {
        const sat_result_t st=sat_parallel_cache_sync_range(range[i].address,range[i].size);
        if(st!=SAT_OK)return st;
    }
    return SAT_OK;
}

sat_result_t t3_stage_vdp2_present(void) {
    ++g_t3_vdp2_floor_presented;
#if defined(T3_PARTICLE_SWAP_PROBE)
    if(g_probe_dirty) {
        g_probe_pending[g_probe_slot]=sat_vdp1_list_serial()+1u;
        g_probe_slot^=1u;g_probe_dirty=0;
    }
#endif
    if(!g_dirty)return SAT_OK;
    /* The next sat_end_frame submits list serial+1; its swap publishes the
     * slot. The other slot's image was submitted earlier and that submit
     * waited for the swap before it, so at most these two are pending. */
    g_pending[g_slot]=sat_vdp1_list_serial()+1u;
    g_slot^=1;
    g_dirty=0;
    return SAT_OK;
}
