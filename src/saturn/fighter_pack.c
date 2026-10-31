#include "fighter_pack.h"
#include "disc.h"
#include "placement.h"
#include "saturn/cdfs.h"

enum { HEADER_WORDS=64u, CHUNK_BYTES=1048576u, MAGIC=0x54334650u, VERSION=3u, DIRECTORY_WORD=26u };
_Static_assert(sizeof(t3_psx_projected_part_t)==22u*sizeof(uint32_t),"pack projected rows");
_Static_assert(sizeof(t3_psx_part_t)==16u,"pack parts");
_Static_assert(sizeof(t3_psx_camera_choice_t)==8u,"pack camera choices");
_Static_assert(sizeof(t3_psx_camera_stream_t)==16u,"pack camera streams");
_Static_assert((T3_ROSTER_STORE_BYTES&15u)==0u,"each actor's store starts aligned");

static const char* const g_names[T3_ROSTER_COUNT]={
    "PAUL","LAW","LEI","KING","YOSHIMITSU","NINA","HWOARANG","XIAOYU","EDDY","JIN",
    "JULIA","KUMA","BRYAN","HEIHACHI","OGRE","MOKUJIN","GUN JACK","GON","ANNA","DOCTOR B","TRUE OGRE"};

typedef struct {
    sat_cdfs_file_t file;
    uint32_t section[T3_PACK_SECTIONS][2];
} pack_file_t;

/* The hot block and the compacted mesh stay for the match; rendering reads
 * them every image, so they live in High WRAM. */
static uint8_t g_store[2][T3_ROSTER_STORE_BYTES] __attribute__((aligned(16)));
static uint8_t g_arena[T3_ROSTER_ARENA_BYTES] __attribute__((section(".wram_l"),aligned(16)));
static pack_file_t g_file[2];
static t3_fighter_pack_t g_pack[2];

const char* t3_fighter_name(unsigned id) {return id<T3_ROSTER_COUNT?g_names[id]:"";}

static uint32_t fnv1a(const uint8_t* bytes,uint32_t count) {
    uint32_t hash=2166136261u;
    for(uint32_t i=0;i<count;++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}

static sat_result_t read_exact(const sat_cdfs_file_t* file,uint32_t at,void* out,uint32_t bytes) {
    sat_cdfs_volume_t* volume;
    SAT_TRY(t3_disc_volume(&volume));
    for(uint32_t done=0;done<bytes;) {
        uint32_t part=bytes-done,read=0;
        if(part>CHUNK_BYTES)part=CHUNK_BYTES;
        SAT_TRY(sat_cdfs_read_at(volume,file,at+done,(uint8_t*)out+done,part,&read));
        if(read!=part)return SAT_ERR_IO;
        done+=part;
    }
    return SAT_OK;
}

static sat_result_t open_pack(unsigned actor,unsigned id) {
    static const char digits[]="0123456789";
    char path[]="F00.BIN";
    uint32_t w[HEADER_WORDS];
    sat_cdfs_volume_t* volume;
    pack_file_t* file=&g_file[actor];
    t3_fighter_pack_t* pack=&g_pack[actor];
    if(id>=T3_ROSTER_COUNT)return SAT_ERR_INVALID_ARG;
    path[1]=digits[id/10u];path[2]=digits[id%10u];
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,path,&file->file));
    SAT_TRY(read_exact(&file->file,0,w,sizeof w));
    if(w[0]!=MAGIC || w[1]!=VERSION || w[2]!=id)return SAT_ERR_VERSION;
    for(unsigned s=0;s<T3_PACK_SECTIONS;++s) {
        file->section[s][0]=w[DIRECTORY_WORD+2u*s];
        file->section[s][1]=w[DIRECTORY_WORD+2u*s+1u];
        if(file->section[s][0]+file->section[s][1]>file->file.size)return SAT_ERR_VERSION;
    }
    const uint32_t hot=file->section[T3_PACK_HOT][1];
    if(w[3]>=256u || w[9]>T3_ROSTER_TEXTURES || w[7]>T3_ROSTER_VERTICES || w[8]>T3_ROSTER_FACES ||
       w[18]>T3_ROSTER_VOICES || w[24]>T3_ROSTER_PROJECTED_VERTICES ||
       hot+w[19]+16u>T3_ROSTER_STORE_BYTES)return SAT_ERR_CAPACITY;
    uint8_t* store=g_store[actor];
    SAT_TRY(read_exact(&file->file,file->section[T3_PACK_HOT][0],store,hot));
    *pack=(t3_fighter_pack_t){
        .id=id,.bank_type=w[3],.voice_set=w[4],.scale_percent=w[5],.name_width=w[6],
        .vertices=w[7],.faces=w[8],.textures=w[9],.palettes=w[10],
        .voice_count=w[18],.compact_bytes=w[19],.bank_fnv1a=w[20],
        .name=g_names[id],
        .parts=(const t3_psx_part_t*)(const void*)(store+w[11]),
        .radii=(const int16_t*)(const void*)(store+w[12]),
        .choices=(const t3_psx_camera_choice_t*)(const void*)(store+w[13]),.choice_count=w[14],
        .double_sided=store+w[17]};
    pack->streams.streams=(const t3_psx_camera_stream_t*)(const void*)(store+w[15]);
    pack->streams.stream_count=w[16];
    pack->bank_bytes=file->section[T3_PACK_BANK][1];
    /* The rows hold hot offsets until now; zero marks an absent array. */
    t3_psx_projected_part_t* projected=(t3_psx_projected_part_t*)(void*)(store+w[21]);
    for(uint32_t p=0;p<w[22];++p) {
        t3_psx_projected_part_t* row=&projected[p];
#define T3_RELOCATE(field,type) if(row->field)row->field=(type)(const void*)(store+(uintptr_t)row->field)
        T3_RELOCATE(scratch_import,const uint8_t*);
        T3_RELOCATE(global_import,const uint8_t*);
        T3_RELOCATE(positions,const int16_t(*)[3]);
        for(unsigned k=0;k<6u;++k)T3_RELOCATE(post[k],const uint16_t*);
        T3_RELOCATE(map,const uint16_t*);
        T3_RELOCATE(logical_map,const uint16_t*);
#undef T3_RELOCATE
    }
    pack->projected=projected;pack->projected_count=w[22];
    pack->projected_faces=(const uint16_t(*)[4])(const void*)(store+w[23]);
    pack->projected_vertices=w[24];
    pack->face_ot=(const uint8_t(*)[2])(const void*)(store+w[25]);
    return SAT_OK;
}

sat_result_t t3_fighter_packs_open(unsigned first,unsigned second) {
    SAT_TRY(open_pack(0,first));
    return open_pack(1,second);
}

const t3_fighter_pack_t* t3_fighter_pack(unsigned actor) {return &g_pack[actor&1u];}

int t3_fighter_packs_share_bank(void) {return g_pack[0].bank_type==g_pack[1].bank_type;}

uint32_t t3_fighter_pack_section_bytes(unsigned actor,unsigned section) {
    return g_file[actor&1u].section[section][1];
}

sat_result_t t3_fighter_pack_read(unsigned actor,unsigned section,uint32_t at,void* out,uint32_t bytes) {
    const pack_file_t* file=&g_file[actor&1u];
    if(section>=T3_PACK_SECTIONS || at>file->section[section][1] ||
       bytes>file->section[section][1]-at)return SAT_ERR_INVALID_ARG;
    return read_exact(&file->file,file->section[section][0]+at,out,bytes);
}

uint8_t* t3_fighter_pack_arena(void) {return g_arena;}

void* t3_fighter_pack_asset_storage(unsigned actor,uint32_t* capacity) {
    const uint32_t hot=(t3_fighter_pack_section_bytes(actor,T3_PACK_HOT)+15u)&~15u;
    *capacity=T3_ROSTER_STORE_BYTES-hot;
    return g_store[actor&1u]+hot;
}

sat_result_t t3_fighter_packs_load_banks(void) {
    uint32_t at=0;
    for(unsigned actor=0;actor<2u;++actor) {
        t3_fighter_pack_t* pack=&g_pack[actor];
        if(actor && t3_fighter_packs_share_bank()) {
            if(g_pack[0].bank_fnv1a!=pack->bank_fnv1a)return SAT_ERR_VERSION;
            pack->bank=g_pack[0].bank;
        } else {
            if(at+pack->bank_bytes>T3_ROSTER_ARENA_BYTES)return SAT_ERR_CAPACITY;
            SAT_TRY(t3_fighter_pack_read(actor,T3_PACK_BANK,0,g_arena+at,pack->bank_bytes));
            if(fnv1a(g_arena+at,pack->bank_bytes)!=pack->bank_fnv1a)return SAT_ERR_IO;
            pack->bank=g_arena+at;
            at=(at+pack->bank_bytes+15u)&~15u;
        }
    }
    for(unsigned actor=0;actor<2u;++actor) {
        t3_fighter_pack_t* pack=&g_pack[actor];
        const uint32_t bytes=t3_fighter_pack_section_bytes(actor,T3_PACK_CAMERA);
        if(at+bytes>T3_ROSTER_ARENA_BYTES)return SAT_ERR_CAPACITY;
        SAT_TRY(t3_fighter_pack_read(actor,T3_PACK_CAMERA,0,g_arena+at,bytes));
        pack->streams.encoded=g_arena+at;
        pack->streams.encoded_size=bytes;
        at=(at+bytes+15u)&~15u;
    }
    return SAT_OK;
}
