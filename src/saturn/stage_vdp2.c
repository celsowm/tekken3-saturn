#include "stage_vdp2.h"
#include "saturn/color.h"
#include "saturn/vdp2_layers.h"
#include "saturn/vdp2_floor.h"
#include "saturn/video.h"
#include "saturn/vdp1.h"
#include "saturn/cdfs.h"
#include "saturn/parallel.h"
#include "disc.h"
#include "stage_backdrop.h"
#include "t3_local_stage.h"

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
static sat_vdp2_line_scroll_entry_t g_lines[HEIGHT];
static t3_stage_backdrop_line_t g_backdrop[HEIGHT];
static sat_fx16_t g_wall_depth[HEIGHT];
/* The VRAM words of one image's mapping, in the order of the fixed tables
 * they replace. Two slots: an image's tables are filled while the previous
 * image may still wait for its swap. */
typedef struct {
    uint16_t lines[LINE_WORDS];
    uint16_t params[SAT_VDP2_ROTATION_PARAM_WORDS];
    uint32_t coefficients[HEIGHT];
} tables_t;
static tables_t g_tables[2] __attribute__((section(".wram_l"),aligned(4)));
static uint32_t g_slot;
/* Per slot, the VDP1 list whose swap publishes it; 0 for none. */
static volatile uint32_t g_pending[2];
volatile uint32_t g_t3_vdp2_floor_prepared, g_t3_vdp2_floor_presented;

static sat_cdfs_volume_t* g_volume;
static uint16_t g_load_words[8192] __attribute__((section(".wram_l"),aligned(4)));
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

static const t3_stage_backdrop_t g_cylinder={T3_STAGE_BOX_HALF,T3_STAGE_BAKE_HEIGHT,T3_STAGE_TAN_TOP,
    T3_STAGE_ROWS_PER_TAN,T3_STAGE_COLUMNS,T3_STAGE_ROWS,WIDTH,HEIGHT};

static void copy_vram(uint32_t address,const void* source,uint32_t bytes) {
    volatile uint32_t* to=(volatile uint32_t*)(0x25E00000u+address);
    const uint32_t* from=(const uint32_t*)source;
    for(uint32_t i=0;i<bytes/4;++i)to[i]=from[i];
}

/* The swap handler: in the VBlank in which the list showing an image is
 * swapped in, its tables replace the ones the VDP2 reads, so the panorama
 * and floor change with that image's sprites and the CPU never waits. VBlank
 * leaves time for the copy, and only CPU writes are used: the program may be
 * in the middle of an SCU-DMA. */
static void publish(uint32_t serial,void* user) {
    (void)user;
    for(unsigned s=0;s<2;++s) {
        if(g_pending[s]!=serial)continue;
        copy_vram(LINE_SCROLL,g_tables[s].lines,sizeof g_tables[s].lines);
        copy_vram(COEFFICIENT_A,g_tables[s].coefficients,sizeof g_tables[s].coefficients);
        copy_vram(PARAM_A,g_tables[s].params,sizeof g_tables[s].params);
        g_pending[s]=0;
        ++g_t3_vdp2_floor_updates;
    }
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
    g_t3_stage_boot_step=6;
    if((st=read_part(&file,2048,2048))!=SAT_OK)return st;
    g_t3_stage_boot_step=7;
    if((st=sat_vdp2_vram_write_words(CHARS/2,g_load_words,1024))!=SAT_OK)return st;
    g_t3_stage_boot_step=8;
    if((st=read_part(&file,4096,32))!=SAT_OK)return st;
    g_t3_stage_boot_step=9;
    if((st=sat_vdp2_palette_upload(g_load_words,16,PALETTE*256))!=SAT_OK)return st;
    if((st=read_part(&file,6144,T3_STAGE_PICTURE_COLOURS*2))!=SAT_OK)return st;
    if((st=sat_vdp2_palette_upload(g_load_words,T3_STAGE_PICTURE_COLOURS,PICTURE_PALETTE*256))!=SAT_OK)return st;
    for(unsigned y=0;y<64;++y) {
        for(unsigned x=0;x<64;++x)g_load_words[x]=(uint16_t)((y%8)*8+x%8);
        g_t3_stage_boot_step=10;
        if((st=sat_vdp2_vram_write_words(NAMES/2+y*64,g_load_words,64))!=SAT_OK)return st;
    }
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
    if(!camera)return SAT_ERR_INVALID_ARG;
    ++g_t3_vdp2_floor_prepared;
#if !defined(T3_STAGE_BENCH_EVERY_FRAME)
    if(g_valid && center_x==g_previous_center[0] && center_z==g_previous_center[1] &&
       turn==g_previous_turn && same_camera(camera))return SAT_OK;
#endif
    /* The panorama follows the fighters' centre, turned by the original's
     * backdrop angle; the floor ends where its lines meet the walls. */
    const int32_t eye[3]={camera->eye.x,camera->eye.y,camera->eye.z};
    if(!t3_stage_backdrop_lines(&g_cylinder,camera->view_proj.m,eye,turn,1,g_backdrop,g_wall_depth))
        return SAT_ERR_UNSUPPORTED;
    for(unsigned y=0;y<HEIGHT;++y)
        g_lines[y]=(sat_vdp2_line_scroll_entry_t){g_backdrop[y].x,g_backdrop[y].y,g_backdrop[y].zoom};
    /* The floor map repeats every 1,800 PS1 units in 64 dots. The skin's
     * world is 96 fx16 per PS1 unit around the fighters' centre, x and -z. */
    const sat_vdp2_floor_t floor={WIDTH,HEIGHT,0,(int32_t)((int64_t)64*65536*65536/(1800*96)),
        (int32_t)(((int64_t)center_x*64*SAT_FX16_ONE/1800)%(64*SAT_FX16_ONE)),
        -(int32_t)(((int64_t)center_z*64*SAT_FX16_ONE/1800)%(64*SAT_FX16_ONE)),64*SAT_FX16_ONE,
        g_wall_depth,1};
    tables_t* tables=&g_tables[g_slot];
    sat_result_t st=sat_vdp2_floor_from_view(&floor,&camera->view_proj,&camera->eye,
        camera->near_z,camera->far_z,&g_params,tables->coefficients);
    if(st!=SAT_OK)return st;
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
    if(!g_dirty)return SAT_OK;
    /* The next sat_end_frame submits list serial+1; its swap publishes the
     * slot. The other slot's image was submitted earlier and that submit
     * waited for the swap before it, so at most these two are pending. */
    g_pending[g_slot]=sat_vdp1_list_serial()+1u;
    g_slot^=1;
    g_dirty=0;
    return SAT_OK;
}
