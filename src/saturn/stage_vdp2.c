#include "stage_vdp2.h"
#include "saturn/color.h"
#include "saturn/vdp2_layers.h"
#include "saturn/vdp2_floor.h"
#include "saturn/video.h"
#include "saturn/cdfs.h"
#include "disc.h"
#include "t3_local_stage.h"

enum {
    WIDTH=320, HEIGHT=224,
    CHARS=0x40000, NAMES=0x60000,
    PARAM_A=0x3c000, PARAM_B=0x3c100,
    COEFFICIENT_A=0x3c200, COEFFICIENT_B=0x3c600,
    PALETTE=6
};
static uint32_t g_coefficients[HEIGHT] __attribute__((section(".wram_l")));
static sat_vdp2_rotation_params_t g_params __attribute__((section(".wram_l")));
static uint32_t g_table_index;
volatile uint32_t g_t3_vdp2_floor_prepared, g_t3_vdp2_floor_presented;

static sat_cdfs_volume_t* g_volume;
static uint16_t g_load_words[8192] __attribute__((section(".wram_l"),aligned(4)));
static sat_camera3d_t g_previous_camera __attribute__((section(".wram_l")));
static int32_t g_previous_center[2];
static uint8_t g_valid,g_dirty;
static int same_camera(const sat_camera3d_t* camera) {
    if(camera->near_z!=g_previous_camera.near_z || camera->far_z!=g_previous_camera.far_z)return 0;
    for(unsigned i=0;i<16;++i)
        if(camera->view_proj.m[i]!=g_previous_camera.view_proj.m[i])return 0;
    return 1;
}
volatile uint32_t g_t3_vdp2_floor_updates, g_t3_stage_boot_step;

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
    if(header[0]!=0x54335354u || header[1]!=1 || header[2]!=512 || header[3]!=HEIGHT ||
       header[6]!=2048 || header[7]!=16 || header[8]!=2048 || header[9]!=4096 ||
       header[10]!=6144 || header[11]!=512*HEIGHT*2)return SAT_ERR_VERSION;
    g_t3_stage_boot_step=6;
    if((st=read_part(&file,2048,2048))!=SAT_OK)return st;
    g_t3_stage_boot_step=7;
    if((st=sat_vdp2_vram_write_words(CHARS/2,g_load_words,1024))!=SAT_OK)return st;
    g_t3_stage_boot_step=8;
    if((st=read_part(&file,4096,32))!=SAT_OK)return st;
    g_t3_stage_boot_step=9;
    if((st=sat_vdp2_palette_upload(g_load_words,16,PALETTE*256))!=SAT_OK)return st;
    for(unsigned y=0;y<64;++y) {
        for(unsigned x=0;x<64;++x)g_load_words[x]=(uint16_t)((y%8)*8+x%8);
        g_t3_stage_boot_step=10;
        if((st=sat_vdp2_vram_write_words(NAMES/2+y*64,g_load_words,64))!=SAT_OK)return st;
    }
    /* Upload once at boot, keeping all picture bytes outside the resident
     * application. The unused rows 240..255 of this 512x256 bitmap hold
     * the two rotation/coefficient pairs; NBG0 displays only rows 0..223. */
    for(uint32_t done=0;done<512u*HEIGHT*2;) {
        uint32_t bytes=512u*HEIGHT*2-done;
        if(bytes>sizeof(g_load_words))bytes=sizeof(g_load_words);
        g_t3_stage_boot_step=11;
        if((st=read_part(&file,6144+done,bytes))!=SAT_OK)return st;
        g_t3_stage_boot_step=12;
        if((st=sat_vdp2_vram_write_words(done/2,g_load_words,bytes/2))!=SAT_OK)return st;
        done+=bytes;
    }
    sat_vdp2_layer_config_t background;
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&background);
    background.bitmap=1; background.bitmap_size=SAT_VDP2_BITMAP_512X256;
    background.color_mode=SAT_VDP2_COLOR_MODE_32768;
    background.priority=1; background.transparent=0;
    background.plane_address[0]=0;
    g_t3_stage_boot_step=13;
    if((st=sat_vdp2_layer_configure(&background))!=SAT_OK)return st;
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
    return sat_vdp2_sprite_set_priority(6);
}

sat_result_t t3_stage_vdp2_prepare(const sat_camera3d_t* camera,int32_t center_x,int32_t center_z) {
    if(!camera)return SAT_ERR_INVALID_ARG;
    ++g_t3_vdp2_floor_prepared;
    if(g_valid && center_x==g_previous_center[0] && center_z==g_previous_center[1] &&
       same_camera(camera))return SAT_OK;
    /* The floor map repeats every 1,800 PS1 units in 64 dots. The skin's
     * world is 96 fx16 per PS1 unit around the fighters' centre, x and -z. */
    const sat_vdp2_floor_t floor={WIDTH,HEIGHT,0,(int32_t)((int64_t)64*65536*65536/(1800*96)),
        (int32_t)(((int64_t)center_x*64*SAT_FX16_ONE/1800)%(64*SAT_FX16_ONE)),
        -(int32_t)(((int64_t)center_z*64*SAT_FX16_ONE/1800)%(64*SAT_FX16_ONE)),64*SAT_FX16_ONE};
    const sat_result_t st=sat_vdp2_floor_from_view(&floor,&camera->view_proj,&camera->eye,
        camera->near_z,camera->far_z,&g_params,g_coefficients);
    if(st!=SAT_OK)return st;
    g_params.coefficient_address=g_table_index?COEFFICIENT_B:COEFFICIENT_A;
    g_previous_camera=*camera;g_previous_center[0]=center_x;g_previous_center[1]=center_z;
    g_valid=g_dirty=1;
    return SAT_OK;
}

sat_result_t t3_stage_vdp2_present(void) {
    sat_result_t st;
    ++g_t3_vdp2_floor_presented;
    if(!g_dirty)return SAT_OK;
    /* Unchanged mapping keeps CPU/VDP1 overlap. A changed mapping waits
     * for the matching complete VDP1 list before publishing in VBlank. */
    if((st=sat_wait_vblank())!=SAT_OK)return st;
    if((st=sat_vdp2_rotation_write_coefficients(g_params.coefficient_address,
            g_coefficients,HEIGHT))!=SAT_OK)return st;
    if((st=sat_vdp2_rotation_write_params(g_table_index?PARAM_B:PARAM_A,&g_params))!=SAT_OK)return st;
    if((st=sat_vdp2_layers_commit())!=SAT_OK)return st;
    g_table_index^=1;
    g_dirty=0;
    ++g_t3_vdp2_floor_updates;
    return SAT_OK;
}
