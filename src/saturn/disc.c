#include "disc.h"
#include "saturn/cd_block.h"

static sat_cdfs_volume_t g_volume __attribute__((section(".wram_l")));
static sat_cd_block_t g_cd __attribute__((section(".wram_l")));
static sat_cd_device_t g_device __attribute__((section(".wram_l")));
static uint8_t g_mounted;

sat_result_t t3_disc_volume(sat_cdfs_volume_t** out_volume) {
    sat_result_t st;
    if(!out_volume)return SAT_ERR_INVALID_ARG;
    if(!g_mounted) {
        if((st=sat_cd_block_init(&g_cd,SAT_CD_BLOCK_DEFAULT_TIMEOUT))!=SAT_OK)return st;
        if((st=sat_cd_block_bind_device(&g_cd,&g_device,0))!=SAT_OK)return st;
        if((st=sat_cdfs_mount(&g_volume,&g_device))!=SAT_OK)return st;
        g_mounted=1;
    }
    *out_volume=&g_volume;
    return SAT_OK;
}
