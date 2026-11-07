#include "stage_vdp2.h"
#include "saturn/color.h"
#include "saturn/vdp2_layers.h"
#include "saturn/vdp2_color_calc.h"
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
#include "stage_format.h"
#if defined(T3_PARTICLE_POSITION_PROBE)
#include "psx_particle_tile.h"
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
#include "psx_face_coverage.h"
#endif
#endif

enum {
    WIDTH=320, HEIGHT=224,
    FLOOR_BITMAP=0x40000,
    PARAM_A=0x3c000, COEFFICIENT_A=0x3c200,
    /* One entry (x, y, increment) per line, written in VBlank. */
    LINE_SCROLL=0x3d000,
    PALETTE=6, PICTURE_PALETTE=7,
    CHUNK_BYTES=8192,
    LINE_WORDS=HEIGHT*6
};
static sat_vdp2_rotation_params_t g_params __attribute__((section(".wram_l")));
/* Rebuilt every image: High WRAM, where an access costs a quarter of Low's. */
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
/* A 16.16 line-scroll value as its table word pair (integer bits masked,
 * the fraction's top 8 bits in bits 15-8), high word first. */
static inline uint32_t line_word(int32_t value,uint32_t integer_mask) {
    return ((((uint32_t)value>>16)&integer_mask)<<16)|((uint32_t)value&0xFF00u);
}
static uint32_t g_slot;
/* Per slot, the VDP1 list whose swap publishes it; 0 for none. */
static volatile uint32_t g_pending[2];
volatile uint32_t g_t3_vdp2_floor_prepared, g_t3_vdp2_floor_presented;

static sat_cdfs_volume_t* g_volume;
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
 * particle slots may extend the union beyond the upload buffer; the linker
 * enforces the Low WRAM limit. Slots live through their swaps. */
static union {
    uint16_t load_words[8192];
    struct { t3_particle_tile_probe_t tiles[2]; } live;
} g_stage_boot_or_particles __attribute__((section(".wram_l"),aligned(4)));
_Static_assert(sizeof(g_stage_boot_or_particles.live)<=24576u,"live stage data exceeds particle budget");
_Static_assert(0x64080u+T3_PARTICLE_PROBE_MAX_TILES*128u<=0x80000u,"particle characters exceed VDP2 VRAM");
#define g_load_words (g_stage_boot_or_particles.load_words)
#define g_probe_tiles (g_stage_boot_or_particles.live.tiles)
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
#if defined(T3_STAGE_SPLIT_PROBE)
/* W6.1 of docs/POST_VRAM_FRAME_RATE_PLAN.md: FRT (/128) per stage component.
 * [0] panorama lines, [1] line-table encode, [2] floor coefficients,
 * [3] rotation parameters, [4] prepared stages. Read from a save state. */
volatile uint32_t g_t3_stage_split[5];
#define STAGE_SPLIT_START(name) const uint16_t name=sat_time_frc()
#define STAGE_SPLIT_ADD(slot,name) (g_t3_stage_split[slot]+=(uint16_t)(sat_time_frc()-(name)))
#define STAGE_SPLIT_COUNT() (++g_t3_stage_split[4])
#else
#define STAGE_SPLIT_START(name) do{}while(0)
#define STAGE_SPLIT_ADD(slot,name) do{}while(0)
#define STAGE_SPLIT_COUNT() do{}while(0)
#endif
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
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    const unsigned old_count=g_probe_tiles[slot].count,old_compactions=g_probe_tiles[slot].compactions;
#endif
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    /* g_particle_cell is exact for the list on entry (it is updated below),
     * so cells are found by one read instead of a scan (W3.4). */
    const t3_particle_tile_status_t result=t3_particle_tile_accumulate_mapped(&g_probe_tiles[slot],quad,pattern,palette,
        (const uint8_t (*)[40])g_particle_cell);
#else
    const t3_particle_tile_status_t result=t3_particle_tile_accumulate_status(&g_probe_tiles[slot],quad,pattern,palette);
#endif
    if(result!=T3_PARTICLE_TILE_OK) {
        g_t3_particle_multi_fail_tiles=g_probe_tiles[slot].count;
        g_t3_particle_multi_fail_particles=g_probe_particle_count[slot];
        g_t3_particle_multi_fail_quad=*quad;
        return result==T3_PARTICLE_TILE_CAPACITY?SAT_ERR_CAPACITY:SAT_ERR_UNSUPPORTED;
    }
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    /* New cells extend the map. Compaction renumbers the list and can shrink
     * the conservative bound, so only then rebuild it from scratch. */
    const t3_particle_tile_probe_t* tiles=&g_probe_tiles[slot];
    unsigned first_new=old_count;
    if(tiles->compactions!=old_compactions) {
        first_new=0;
        g_particle_left=320;g_particle_right=-1;g_particle_top=224;g_particle_bottom=-1;
        for(unsigned i=0;i<sizeof g_particle_cell;++i)((uint8_t*)g_particle_cell)[i]=0xFFu;
    }
    for(unsigned i=first_new;i<tiles->count;++i) {
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
#if defined(T3_PARTICLE_COMPOSE_CHECK) || defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
/* Diagnostic: every cell's position and pixels, in list order. */
uint32_t t3_stage_particle_multi_checksum(void) {
    const t3_particle_tile_probe_t* tiles=&g_probe_tiles[g_probe_slot];
    uint32_t sum=tiles->count;
    for(unsigned i=0;i<tiles->count;++i) {
        sum=sum*31u+tiles->tile_x[i]+tiles->tile_y[i]*64u;
        for(unsigned p=0;p<64;++p)sum=sum*31u+tiles->pixels[i][p];
    }
    return sum;
}
#if defined(T3_MIXED_OT_LIVE_ENTRIES_CHECK)
unsigned t3_stage_particle_multi_cell_count(void) {return g_probe_tiles[g_probe_slot].count;}
#endif
#endif
volatile uint32_t g_t3_hud_occluded_cells;
int t3_stage_particle_multi_has_cell(unsigned x,unsigned y) {
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    return x<40u && y<28u && g_particle_cell[y][x]!=0xFFu;
#else
    (void)x;(void)y;return 1;
#endif
}
sat_result_t t3_stage_particle_multi_occlude(unsigned x,unsigned y,const uint8_t coverage[8]) {
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    if(x>=40u || y>=28u || !coverage)return SAT_ERR_INVALID_ARG;
    const unsigned index=g_particle_cell[y][x];
    if(index==0xFFu)return SAT_OK;
    ++g_t3_hud_occluded_cells;
    return t3_particle_tile_occlude_index(&g_probe_tiles[g_probe_slot],index,coverage)?SAT_OK:SAT_ERR_INVALID_ARG;
#else
    return t3_particle_tile_occlude(&g_probe_tiles[g_probe_slot],x,y,coverage)?SAT_OK:SAT_ERR_INVALID_ARG;
#endif
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
#if defined(T3_PARTICLE_DROP_SELFTEST)
        /* Diagnostic: take the unsupported-face branch for every 40th visit. */
        {static unsigned visits;if(++visits%40u==0u){drop_cell(tiles,i);continue;}}
#endif
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

static t3_stage_backdrop_t g_cylinder={0,T3_STAGE_BAKE_HEIGHT,T3_STAGE_TAN_TOP,
    T3_STAGE_ROWS_PER_TAN,T3_STAGE_COLUMNS,T3_STAGE_ROWS,WIDTH,HEIGHT};
static sat_cdfs_file_t g_file;
static uint16_t g_back,g_shadow,g_floor_level=128;

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

static sat_result_t read_part(const sat_cdfs_file_t* file,uint32_t at,uint16_t* out,uint32_t bytes) {
    uint32_t read=0;
    const sat_result_t st=sat_cdfs_read_at(g_volume,file,at,out,bytes,&read);
    return st!=SAT_OK?st:read==bytes?SAT_OK:SAT_ERR_IO;
}

static uint32_t head_word(unsigned word) {
    return ((uint32_t)g_load_words[2*word]<<16)|g_load_words[2*word+1];
}

/* Streams one section of the file to VDP2 VRAM through the upper half of g_load_words. */
static sat_result_t stream_to_vram(uint32_t at,uint32_t bytes,uint32_t address) {
    uint16_t* const chunk=g_load_words+CHUNK_BYTES/2;
    for(uint32_t done=0;done<bytes;) {
        uint32_t part=bytes-done;
        if(part>CHUNK_BYTES)part=CHUNK_BYTES;
        g_t3_stage_boot_step=11;
        SAT_TRY(read_part(&g_file,at+done,chunk,part));
        g_t3_stage_boot_step=12;
        SAT_TRY(sat_vdp2_vram_write_words((address+done)/2,chunk,part/2));
        done+=part;
    }
    return SAT_OK;
}

/* The head stays in g_load_words for t3_stage_vdp2_init, and the picture goes
 * straight to VDP2 bank A. In a True Ogre fight there is no panorama: its
 * colours stay black, and the floor takes the spotlight's brightness. */
sat_result_t t3_stage_vdp2_load(unsigned stage,int true_ogre) {
    sat_result_t st;
    char name[]="STG00.BIN";
    name[3]=(char)('0'+stage/10u);name[4]=(char)('0'+stage%10u);
    g_t3_stage_boot_step=1;
    if((st=t3_disc_volume(&g_volume))!=SAT_OK)return st;
    g_t3_stage_boot_step=4;
    if((st=sat_cdfs_lookup(g_volume,name,&g_file))!=SAT_OK)return st;
    if(g_file.size!=T3_STAGE_FILE_BYTES)return SAT_ERR_VERSION;
    g_t3_stage_boot_step=5;
    if((st=read_part(&g_file,0,g_load_words,T3_STAGE_HEAD_BYTES))!=SAT_OK)return st;
    if(head_word(T3_STAGE_WORD_MAGIC)!=T3_STAGE_MAGIC || head_word(T3_STAGE_WORD_VERSION)!=T3_STAGE_VERSION ||
       head_word(T3_STAGE_WORD_NUMBER)!=stage)return SAT_ERR_VERSION;
    g_cylinder.box_half=(int32_t)head_word(T3_STAGE_WORD_BOX_HALF);
    g_back=(uint16_t)head_word(T3_STAGE_WORD_BACK);
    g_shadow=(uint16_t)head_word(T3_STAGE_WORD_SHADOW);
    if(true_ogre) {
        g_back=SAT_RGB555(0,0,0);
        g_floor_level=(uint16_t)head_word(T3_STAGE_WORD_OGRE_LEVEL);
        for(unsigned i=0;i<256u;++i)g_load_words[T3_STAGE_PICTURE_PALETTE_AT/2+i]=0;
    }
    /* Rows 240..255 of this 1024x256 bitmap hold the rotation and coefficient
     * tables and the line scroll table; the line scroll never shows them. */
    return stream_to_vram(T3_STAGE_PICTURE_AT,T3_STAGE_PICTURE_BYTES,0);
}

uint16_t t3_stage_vdp2_shadow(void) {return g_shadow;}
uint16_t t3_stage_vdp2_back(void) {return g_back;}

sat_result_t t3_stage_vdp2_init(void) {
    sat_result_t st;
    uint16_t* const floor_palette=g_load_words+T3_STAGE_FLOOR_PALETTE_AT/2;
    for(unsigned i=0;g_floor_level!=128u && i<256u;++i) {
        const uint32_t c=floor_palette[i];
        floor_palette[i]=(uint16_t)((c&0x8000u)|(((c&0x1Fu)*g_floor_level/128u)&0x1Fu)|
            (((((c>>5)&0x1Fu)*g_floor_level/128u)&0x1Fu)<<5)|(((((c>>10)&0x1Fu)*g_floor_level/128u)&0x1Fu)<<10));
    }
    g_t3_stage_boot_step=9;
    if((st=sat_vdp2_palette_upload(floor_palette,256,PALETTE*256))!=SAT_OK)return st;
    if((st=sat_vdp2_palette_upload(g_load_words+T3_STAGE_PICTURE_PALETTE_AT/2,256,PICTURE_PALETTE*256))!=SAT_OK)return st;
    if((st=sat_vdp2_back_color_set(g_back))!=SAT_OK)return st;
    /* Bank B belongs to the VS screen until now, so the floor is read here. */
    g_t3_stage_boot_step=7;
    if((st=stream_to_vram(T3_STAGE_FLOOR_AT,T3_STAGE_FLOOR_BYTES,FLOOR_BITMAP))!=SAT_OK)return st;
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
    /* Index 0 is a floor texel the original leaves undrawn: stage 11's grates. */
    const sat_vdp2_rotation_bitmap_config_t config={
        .color_mode=SAT_VDP2_COLOR_MODE_256,.height_512=0,
        .priority=2,
        .transparent=1,
        .palette=PALETTE,.over=SAT_VDP2_ROTATION_OVER_REPEAT,
        .coefficients_a={.enabled=1,.mode=SAT_VDP2_COEFFICIENT_KXKY},
        .bitmap_address=FLOOR_BITMAP,.table_address=PARAM_A};
    g_t3_stage_boot_step=14;
    if((st=sat_vdp2_rotation_bitmap_configure(&config))!=SAT_OK)return st;
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
    {
        /* prepare packs NBG0's (x, y, increment) entries itself; they must be
         * the words the SDK's encoder writes for this layer. */
        const sat_vdp2_line_scroll_entry_t probe={0x12345678,-0x2468ACE,0x2ABCDu};
        uint16_t words[6];
        const uint32_t packed[3]={line_word(probe.x,0x7FFu),line_word(probe.y,0x7FFu),
                                  line_word((int32_t)probe.zoom,0x7u)};
        if(sat_vdp2_line_scroll_encode(SAT_VDP2_NBG0,&probe,1,words,6)!=6)return SAT_ERR_UNSUPPORTED;
        for(unsigned i=0;i<3u;++i)
            if(words[2*i]!=(uint16_t)(packed[i]>>16) || words[2*i+1]!=(uint16_t)packed[i])
                return SAT_ERR_UNSUPPORTED;
    }
    /* From here on the VDP2 reads parameter table A and coefficient table A,
     * and only the swap handler rewrites them. Until the first image every
     * line is transparent. */
    for(unsigned y=0;y<HEIGHT;++y)g_tables[0].coefficients[y]=sat_vdp2_rotation_coefficient(0,1);
    if((st=sat_vdp2_rotation_write_coefficients(COEFFICIENT_A,g_tables[0].coefficients,HEIGHT))!=SAT_OK)
        return st;
    g_params=(sat_vdp2_rotation_params_t){0};
    g_params.coefficient_address=COEFFICIENT_A;g_params.coefficient_step=SAT_FX16_ONE;
    /* Parameter set A is the only one read; the swap handler keeps it fresh. */
    if((st=sat_vdp2_rotation_write_params(PARAM_A,&g_params,&g_params))!=SAT_OK)return st;
    if((st=sat_vdp2_layers_commit())!=SAT_OK)return st;
    if((st=sat_vdp1_set_swap_handler(publish,NULL))!=SAT_OK)return st;
#if defined(T3_FLIPBOOK_BURST)
    /* The hit burst is an additive sprite: colour calculation at priority 6,
     * faded sprites at 5, above the panorama (1) and floor (0/2) layers. */
    sat_vdp2_sprite_color_calc_config_t burst_cc={.enabled=1,.normal_priority=6};
    return sat_vdp2_sprite_color_calc_configure(&burst_cc);
#else
    return sat_vdp2_sprite_set_priority(6);
#endif
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
    STAGE_SPLIT_COUNT();
    /* The panorama follows the fighters' centre, turned by the original's
     * backdrop angle; the floor ends where its lines meet the walls. */
    const int32_t eye[3]={camera->eye.x,camera->eye.y,camera->eye.z};
    STAGE_SPLIT_START(pano_started);
#if defined(T3_STAGE_ORIGINAL_VERTICAL_PROBE)
    sat_mat4_t backdrop_view=camera->view_proj;
    for(unsigned i=4;i<8;++i)backdrop_view.m[i]=(int32_t)((int64_t)backdrop_view.m[i]*15/14);
    if(!t3_stage_backdrop_lines_offset(&g_cylinder,backdrop_view.m,eye,turn,1,2,g_backdrop,g_wall_depth))
        return SAT_ERR_UNSUPPORTED;
#else
    if(!t3_stage_backdrop_lines(&g_cylinder,camera->view_proj.m,eye,turn,1,g_backdrop,g_wall_depth))
        return SAT_ERR_UNSUPPORTED;
#endif
    STAGE_SPLIT_ADD(0,pano_started);
    tables_t* tables=&g_tables[g_slot];
    /* Straight into the slot's Low WRAM words, one store per word pair. */
    STAGE_SPLIT_START(lines_started);
    uint32_t* line_words=(uint32_t*)(void*)tables->lines;
    for(unsigned y=0;y<HEIGHT;++y,line_words+=3) {
        int32_t column=g_backdrop[y].x;
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION) && !defined(T3_STAGE_NO_VIEWPORT_SHIFT)
        /* Stage rays previously assumed x=160; the mapped original GTE
         * center is x=167. Sample the same ray seven pixels earlier. */
        column=(column-(t3_psx_viewport_x(192)-WIDTH/2)*(int32_t)g_backdrop[y].zoom)
            &((T3_STAGE_COLUMNS<<16)-1);
#endif
        line_words[0]=line_word(column,0x7FFu);
        line_words[1]=line_word(g_backdrop[y].y,0x7FFu);
        line_words[2]=line_word((int32_t)g_backdrop[y].zoom,0x7u);
    }
    STAGE_SPLIT_ADD(1,lines_started);
    /* A floor tile is 64 dots and 1,800 PS1 units. The skin's world is 96 fx16
     * per PS1 unit around the fighters' centre, x and -z. The bitmap's repeat
     * differs per axis, so the map position wraps below, not through `period`. */
    sat_vdp2_floor_t floor={WIDTH,HEIGHT,0,(int32_t)((int64_t)64*65536*65536/(1800*96)),
        (int32_t)(((int64_t)center_x*64*SAT_FX16_ONE/1800)%(T3_STAGE_FLOOR_W*SAT_FX16_ONE)),
        -(int32_t)(((int64_t)center_z*64*SAT_FX16_ONE/1800)%(T3_STAGE_FLOOR_H*SAT_FX16_ONE)),0,
        g_wall_depth,1};
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
    STAGE_SPLIT_START(floor_started);
    sat_result_t st=sat_vdp2_floor_from_view(&floor,&floor_view,&camera->eye,
        camera->near_z,camera->far_z,&g_params,tables->coefficients);
    STAGE_SPLIT_ADD(2,floor_started);
    if(st!=SAT_OK)return st;
    for(unsigned i=0;i<HEIGHT;++i)tables->coefficients[i]=tables->coefficients[i+4];
    g_params.yst+=4*g_params.dyst;
#else
    STAGE_SPLIT_START(floor_started);
    sat_result_t st=sat_vdp2_floor_from_view(&floor,&camera->view_proj,&camera->eye,
        camera->near_z,camera->far_z,&g_params,tables->coefficients);
    STAGE_SPLIT_ADD(2,floor_started);
    if(st!=SAT_OK)return st;
#endif
    g_params.mx&=(int32_t)(T3_STAGE_FLOOR_W*SAT_FX16_ONE-1);
    g_params.my&=(int32_t)(T3_STAGE_FLOOR_H*SAT_FX16_ONE-1);
#if defined(T3_ORIGINAL_FIGHTER_PROJECTION) && !defined(T3_STAGE_NO_VIEWPORT_SHIFT)
    g_params.xst-=(t3_psx_viewport_x(192)-WIDTH/2)*SAT_FX16_ONE;
#endif
    g_params.coefficient_address=COEFFICIENT_A;
    STAGE_SPLIT_START(params_started);
    st=sat_vdp2_rotation_encode_params(&g_params,tables->params);
    STAGE_SPLIT_ADD(3,params_started);
    if(st!=SAT_OK)return st;
    g_previous_camera=*camera;g_previous_center[0]=center_x;g_previous_center[1]=center_z;
    g_previous_turn=turn;
    g_valid=g_dirty=1;
    return SAT_OK;
}

sat_result_t t3_stage_vdp2_sync(void) {
    /* Everything prepare writes, for the CPU that did not run it. */
    const struct {const void* address;uint32_t size;} range[]={
        {&g_params,sizeof g_params},{g_backdrop,sizeof g_backdrop},
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
