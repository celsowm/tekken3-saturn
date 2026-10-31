#include "cold_data.h"
#if defined(T3_COLD_LOW_WRAM)
#include "disc.h"
#include "t3_local_divmot.h"
#if defined(T3_FIGHTER_PACKS)
#include "fighter_pack.h"
#endif
#if defined(T3_COLD_CAMERA_STREAMS)
#include "t3_local_camera.h"
#endif

enum { HEADER_BYTES=64u, CHUNK_BYTES=1048576u };
/* Version 2 also carries the opponent's character bank. */
#if defined(T3_LOCAL_COLD_SECOND_OFFSET)
#define COLD_VERSION 2u
#else
#define COLD_VERSION 1u
#endif

static uint32_t be32(const uint8_t* p) {
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}

static sat_result_t read_exact(sat_cdfs_volume_t* volume,const sat_cdfs_file_t* file,
                               uint32_t at,uint8_t* destination,uint32_t bytes) {
    for(uint32_t done=0;done<bytes;) {
        uint32_t part=bytes-done,read=0;
        if(part>CHUNK_BYTES)part=CHUNK_BYTES;
        const sat_result_t st=sat_cdfs_read_at(volume,file,at+done,destination+done,part,&read);
        if(st!=SAT_OK)return st;
        if(read!=part)return SAT_ERR_IO;
        done+=part;
    }
    return SAT_OK;
}

static uint32_t fnv1a(uint32_t hash,const uint8_t* bytes,uint32_t count) {
    for(uint32_t i=0;i<count;++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}

sat_result_t t3_cold_data_load(void) {
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    uint8_t header[HEADER_BYTES] __attribute__((aligned(4)));
    sat_result_t st;
    if((st=t3_disc_volume(&volume))!=SAT_OK)return st;
    if((st=sat_cdfs_lookup(volume,"MOTION.BIN",&file))!=SAT_OK)return st;
    if(file.size!=T3_LOCAL_COLD_FILE_BYTES)return SAT_ERR_VERSION;
    if((st=read_exact(volume,&file,0,header,HEADER_BYTES))!=SAT_OK)return st;
    if(be32(header)!=0x54334D4Fu || be32(header+4)!=COLD_VERSION ||
       be32(header+8)!=T3_LOCAL_DIVMOT_BANK_BYTES || be32(header+12)!=T3_LOCAL_DIVMOT_COMMON_BYTES ||
       be32(header+16)!=T3_LOCAL_COLD_BANK_OFFSET || be32(header+20)!=T3_LOCAL_COLD_COMMON_OFFSET ||
       be32(header+24)!=T3_LOCAL_COLD_FNV1A)return SAT_ERR_VERSION;
#if defined(T3_LOCAL_COLD_SECOND_OFFSET)
    if(be32(header+28)!=T3_LOCAL_SECOND_BANK_BYTES || be32(header+32)!=T3_LOCAL_COLD_SECOND_OFFSET)
        return SAT_ERR_VERSION;
#endif
#if !defined(T3_FIGHTER_PACKS)
    if((st=read_exact(volume,&file,T3_LOCAL_COLD_BANK_OFFSET,t3_local_bank,
                      T3_LOCAL_DIVMOT_BANK_BYTES))!=SAT_OK)return st;
#endif
    if((st=read_exact(volume,&file,T3_LOCAL_COLD_COMMON_OFFSET,t3_local_common,
                      T3_LOCAL_DIVMOT_COMMON_BYTES))!=SAT_OK)return st;
#if defined(T3_LOCAL_COLD_SECOND_OFFSET)
    if((st=read_exact(volume,&file,T3_LOCAL_COLD_SECOND_OFFSET,t3_local_second_bank,
                      T3_LOCAL_SECOND_BANK_BYTES))!=SAT_OK)return st;
#endif
#if defined(T3_FIGHTER_PACKS)
    uint32_t hash=fnv1a(2166136261u,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES);
    if(hash!=T3_LOCAL_COLD_FNV1A)return SAT_ERR_IO;
    return t3_fighter_packs_load_banks();
#else
    uint32_t hash=fnv1a(2166136261u,t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES);
    hash=fnv1a(hash,t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES);
#if defined(T3_LOCAL_COLD_SECOND_OFFSET)
    hash=fnv1a(hash,t3_local_second_bank,T3_LOCAL_SECOND_BANK_BYTES);
#endif
    if(hash!=T3_LOCAL_COLD_FNV1A)return SAT_ERR_IO;
#if defined(T3_COLD_CAMERA_STREAMS)
    if((st=sat_cdfs_lookup(volume,"CAMERA.BIN",&file))!=SAT_OK)return st;
    if(file.size!=T3_LOCAL_COLD_CAMERA_BYTES)return SAT_ERR_VERSION;
    if((st=read_exact(volume,&file,0,t3_local_camera_encoded,
                      T3_LOCAL_COLD_CAMERA_PRIMARY_BYTES))!=SAT_OK)return st;
#if defined(T3_LOCAL_COLD_CAMERA_SECOND_BYTES)
    if((st=read_exact(volume,&file,T3_LOCAL_COLD_CAMERA_PRIMARY_BYTES,
                      t3_local_second_camera_encoded,T3_LOCAL_COLD_CAMERA_SECOND_BYTES))!=SAT_OK)return st;
#endif
    hash=fnv1a(2166136261u,t3_local_camera_encoded,T3_LOCAL_COLD_CAMERA_PRIMARY_BYTES);
#if defined(T3_LOCAL_COLD_CAMERA_SECOND_BYTES)
    hash=fnv1a(hash,t3_local_second_camera_encoded,T3_LOCAL_COLD_CAMERA_SECOND_BYTES);
#endif
    if(hash!=T3_LOCAL_COLD_CAMERA_FNV1A)return SAT_ERR_IO;
#endif
    return SAT_OK;
#endif
}
volatile uint32_t g_t3_cold_verify_passes __attribute__((used));
#if defined(T3_VERIFY_COLD) && defined(T3_FIGHTER_PACKS)
#error "T3_VERIFY_COLD covers the embedded bank layout"
#endif
#if defined(T3_VERIFY_COLD)
enum { VERIFY_SLICE_BYTES=4096u };
sat_result_t t3_cold_data_verify_slice(void) {
    /* The banks in digest order: bank, common, then the optional second. */
    static const struct {const uint8_t* data;uint32_t bytes;} parts[]={
        {t3_local_bank,T3_LOCAL_DIVMOT_BANK_BYTES},{t3_local_common,T3_LOCAL_DIVMOT_COMMON_BYTES},
#if defined(T3_LOCAL_COLD_SECOND_OFFSET)
        {t3_local_second_bank,T3_LOCAL_SECOND_BANK_BYTES},
#endif
    };
    static uint32_t part_index,position,hash=2166136261u;
    uint32_t budget=VERIFY_SLICE_BYTES;
    while(budget && part_index<sizeof(parts)/sizeof(parts[0])) {
        uint32_t part=parts[part_index].bytes-position;
        if(part>budget)part=budget;
        hash=fnv1a(hash,parts[part_index].data+position,part);
        position+=part;budget-=part;
        if(position==parts[part_index].bytes){++part_index;position=0;}
    }
    if(part_index<sizeof(parts)/sizeof(parts[0]))return SAT_OK;
    const sat_result_t st=hash==T3_LOCAL_COLD_FNV1A?SAT_OK:SAT_ERR_IO;
    part_index=position=0;hash=2166136261u;++g_t3_cold_verify_passes;
    return st;
}
#else
sat_result_t t3_cold_data_verify_slice(void) {return SAT_OK;}
#endif
#else
volatile uint32_t g_t3_cold_verify_passes __attribute__((used));
sat_result_t t3_cold_data_load(void) {return SAT_OK;}
sat_result_t t3_cold_data_verify_slice(void) {return SAT_OK;}
#endif
