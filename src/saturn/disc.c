#include "disc.h"
#include "saturn/cd_block.h"

/* Sound commands occupy the last Low WRAM margin. The mount record is
 * master-only and fits High WRAM after pruning unreachable sound scripts;
 * retain the linker's full 16 KiB stack reservation. */
static sat_cdfs_volume_t g_volume;
/* sat_exec stages the next program in Work RAM Low, so the volume, device and
 * CD block it reads through must live elsewhere. */
static sat_cd_block_t g_cd;
static sat_cd_device_t g_device;
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

sat_result_t t3_disc_block(sat_cd_block_t** out_block) {
    sat_cdfs_volume_t* volume;
    if(!out_block)return SAT_ERR_INVALID_ARG;
    SAT_TRY(t3_disc_volume(&volume));
    *out_block=&g_cd;
    return SAT_OK;
}
