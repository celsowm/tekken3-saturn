#include "fighter_pack.h"
#include "disc.h"
#include "placement.h"
#include "saturn/cdfs.h"

#ifndef T3_FORCE_PACK_COUNT
#define T3_FORCE_PACK_COUNT T3_ROSTER_COUNT
#endif

enum { HEADER_WORDS=64u, CHUNK_BYTES=1048576u, MAGIC=0x54334650u, VERSION=6u, DIRECTORY_WORD=26u,
       LIGHT_WORD=DIRECTORY_WORD+2u*T3_PACK_SECTIONS };
_Static_assert(sizeof(t3_light_normal_t)==8u,"pack lit normals");
_Static_assert(sizeof(t3_hand_variant_row_t)==24u,"pack hand variant rows");
_Static_assert(sizeof(t3_psx_projected_part_t)==22u*sizeof(uint32_t),"pack projected rows");
_Static_assert(sizeof(t3_psx_part_t)==16u,"pack parts");
_Static_assert(sizeof(t3_psx_camera_choice_t)==8u,"pack camera choices");
_Static_assert(sizeof(t3_psx_camera_stream_t)==16u,"pack camera streams");
_Static_assert((T3_ROSTER_STORE_BYTES&15u)==0u,"each actor's store starts aligned");

typedef struct {
    sat_cdfs_file_t file;
    uint32_t section[T3_PACK_SECTIONS][2];
#if defined(T3_FORCE_PROGRAM)
    uint32_t base;
#endif
} pack_file_t;

/* The player and primary enemy/boss hot block and compact mesh stay in High
 * WRAM. Force's additional regular enemy uses a reserved Low WRAM arena tail. */
static uint8_t g_store[2][T3_ROSTER_STORE_BYTES] __attribute__((aligned(16)));
static uint8_t g_arena[T3_ROSTER_ARENA_BYTES] __attribute__((section(".wram_l"),aligned(16)));
#if defined(T3_FORCE_PROGRAM)
#define PACK_SLOTS 3u
static unsigned g_pack_count=2u;
static uint32_t g_arena_live_bytes;
static uint8_t g_arena_loaded;
/* The extra enemy's hot block and compact mesh use the arena's unused tail.
 * All live banks/camera streams are checked against this reserved range. */
#else
#define PACK_SLOTS 2u
#endif
static pack_file_t g_file[PACK_SLOTS];
static t3_fighter_pack_t g_pack[PACK_SLOTS];

static unsigned pack_index(unsigned actor) {
#if defined(T3_FORCE_PROGRAM)
    if(actor==2u && g_pack_count==3u)return 2u;
#endif
    return actor&1u;
}
static uint32_t arena_capacity(void) {
#if defined(T3_FORCE_PROGRAM)
    if(g_pack_count==3u)return T3_ROSTER_ARENA_BYTES-T3_ROSTER_STORE_BYTES;
#endif
    return T3_ROSTER_ARENA_BYTES;
}
static uint8_t* pack_store(unsigned actor) {
#if defined(T3_FORCE_PROGRAM)
    if(actor==2u)return g_arena+T3_ROSTER_ARENA_BYTES-T3_ROSTER_STORE_BYTES;
#endif
    return g_store[actor];
}

static uint32_t fnv1a(const uint8_t* bytes,uint32_t count) {
    uint32_t hash=2166136261u;
    for(uint32_t i=0;i<count;++i)hash=(hash^bytes[i])*16777619u;
    return hash;
}
#if defined(T3_FORCE_PROGRAM)
static int same_bytes(const void* first,const void* second,unsigned bytes) {
    const uint8_t* a=first;const uint8_t* b=second;
    uint8_t different=0;
    for(unsigned i=0;i<bytes;++i)different|=a[i]^b[i];
    return different==0;
}
#endif

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

static sat_result_t open_pack_source(unsigned actor,unsigned id,const char* source,uint32_t base,uint32_t limit) {
    uint32_t w[HEADER_WORDS];
    sat_cdfs_volume_t* volume;
    pack_file_t* file=&g_file[actor];
    t3_fighter_pack_t* pack=&g_pack[actor];
    if(id>=T3_FORCE_PACK_COUNT)return SAT_ERR_INVALID_ARG;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,source,&file->file));
    if(base>file->file.size || (limit && limit>file->file.size-base))return SAT_ERR_VERSION;
#if defined(T3_FORCE_PROGRAM)
    file->base=base;
#endif
    if(!limit)limit=file->file.size-base;
    SAT_TRY(read_exact(&file->file,base,w,sizeof w));
    if(w[0]!=MAGIC || w[1]!=VERSION || w[2]!=id)return SAT_ERR_VERSION;
    for(unsigned s=0;s<T3_PACK_SECTIONS;++s) {
        file->section[s][0]=w[DIRECTORY_WORD+2u*s];
        file->section[s][1]=w[DIRECTORY_WORD+2u*s+1u];
        if(file->section[s][0]>limit || file->section[s][1]>limit-file->section[s][0])return SAT_ERR_VERSION;
    }
    const uint32_t hot=file->section[T3_PACK_HOT][1];
    if(w[3]>=256u || w[9]>T3_ROSTER_TEXTURES || w[7]>T3_ROSTER_VERTICES || w[8]>T3_ROSTER_FACES ||
       w[18]>T3_ROSTER_VOICES || w[24]>T3_ROSTER_PROJECTED_VERTICES ||
       hot+w[19]+16u>T3_ROSTER_STORE_BYTES)return SAT_ERR_CAPACITY;
#if defined(T3_FORCE_PROGRAM)
    if(actor==2u && hot+w[19]+16u+T3_FORCE_PACK_SCRATCH_BYTES>T3_ROSTER_STORE_BYTES)
        return SAT_ERR_CAPACITY;
#endif
    uint8_t* store=pack_store(actor);
    SAT_TRY(read_exact(&file->file,base+file->section[T3_PACK_HOT][0],store,hot));
    *pack=(t3_fighter_pack_t){
        .id=id,.bank_type=w[3],.voice_set=w[4],.scale_percent=w[5],.name_width=w[6],
        .vertices=w[7],.faces=w[8],.textures=w[9],.palettes=w[10],
        .voice_count=w[18],.compact_bytes=w[19],.bank_fnv1a=w[20],
        .name=t3_fighter_name(id),
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
    /* Lighting data exists with the LUT4 textures only; zero normals mark it absent. */
    const uint32_t* light=w+LIGHT_WORD;
    pack->light_normal_count=light[0];
    if(light[0]) {
        pack->light_normals=(const t3_light_normal_t*)(const void*)(store+light[1]);
        pack->light_faces=(const uint16_t*)(const void*)(store+light[2]);
        pack->light_corners=(const uint16_t*)(const void*)(store+light[3]);
        pack->light_tones=(const uint16_t*)(const void*)(store+light[4]);
    }
    /* The hand rows hold hot offsets until relocated, as the projected rows. */
    t3_hand_variant_row_t* hands=(t3_hand_variant_row_t*)(void*)(store+light[6]);
    pack->hand_row_count=light[5];
    for(uint32_t r=0;r<light[5];++r) {
        t3_hand_variant_row_t* row=&hands[r];
        if(row->block>=w[22])return SAT_ERR_VERSION;
        row->vertices=(const uint16_t*)(const void*)(store+(uintptr_t)row->vertices);
        row->records=(const uint16_t*)(const void*)(store+(uintptr_t)row->records);
        row->coords=(const int16_t(*)[3])(const void*)(store+(uintptr_t)row->coords);
        row->positions=(const int16_t(*)[3])(const void*)(store+(uintptr_t)row->positions);
    }
    pack->hand_rows=hands;
    pack->shadow_faces=light[7];
    pack->shadow=store+light[8];
    pack->body_shapes = NULL;  /* TODO: add body_shapes to pack format */
    if(light[7] && light[8]+2u>hot)return SAT_ERR_VERSION;
    return SAT_OK;
}

static sat_result_t open_pack(unsigned actor,unsigned id) {
    if(id>=T3_FORCE_PACK_COUNT)return SAT_ERR_INVALID_ARG;
    char path[]="F00.BIN";
    path[1]=(char)('0'+id/10u);path[2]=(char)('0'+id%10u);
    return open_pack_source(actor,id,path,0,0);
}

#if defined(T3_FORCE_PROGRAM)
sat_result_t t3_fighter_packs_open_boss(unsigned player,unsigned character,unsigned costume) {
    g_arena_loaded=0;
    g_pack_count=2u;
    SAT_TRY(open_pack(0,player));
    if(!costume)return open_pack(1,character);
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    uint32_t header[3],entries[84][4];
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"BOSSPAK.BIN",&file));
    SAT_TRY(read_exact(&file,0,header,sizeof header));
    if(header[0]!=0x54334250u || header[1]!=0x30303031u || header[2]>84u)return SAT_ERR_VERSION;
    SAT_TRY(read_exact(&file,12u,entries,header[2]*sizeof entries[0]));
    for(unsigned i=0;i<header[2];++i) {
        const uint32_t* entry=entries[i];
        if(entry[0]==character && entry[1]==costume)
            return open_pack_source(1,character,"BOSSPAK.BIN",entry[2],entry[3]);
    }
    return SAT_ERR_NOT_FOUND;
}

unsigned t3_fighter_packs_count(void) {return g_pack_count;}
void* t3_fighter_pack_force_scratch(void) {
    return g_pack_count==3u?g_arena+T3_ROSTER_ARENA_BYTES-T3_FORCE_PACK_SCRATCH_BYTES:NULL;
}
void* t3_fighter_pack_unused_arena(uint32_t* bytes) {
    *bytes=g_arena_loaded?arena_capacity()-g_arena_live_bytes:0u;
    return *bytes?g_arena+g_arena_live_bytes:NULL;
}
sat_result_t t3_fighter_packs_open_force(unsigned player,unsigned first_key,unsigned second_key) {
    g_arena_loaded=0;
    if(first_key>3u || second_key>3u || first_key==second_key)return SAT_ERR_INVALID_ARG;
    g_pack_count=3u;
    SAT_TRY(open_pack(0,player));
    SAT_TRY(open_pack(1,21u+first_key));
    SAT_TRY(open_pack(2,21u+second_key));
    /* These shared simulation rules were verified across all four packs;
     * fail if private regenerated data changes that contract. */
    if(g_pack[1].bank_type!=g_pack[2].bank_type ||
       g_pack[1].bank_bytes!=g_pack[2].bank_bytes ||
       g_pack[1].bank_fnv1a!=g_pack[2].bank_fnv1a ||
       g_pack[1].scale_percent!=g_pack[2].scale_percent ||
       !same_bytes(g_pack[1].parts,g_pack[2].parts,22u*sizeof(t3_psx_part_t)) ||
       !same_bytes(g_pack[1].radii,g_pack[2].radii,8u*sizeof(int16_t)))return SAT_ERR_VERSION;
    return SAT_OK;
}
#endif

sat_result_t t3_fighter_packs_open(unsigned first,unsigned second) {
#if defined(T3_FORCE_PROGRAM)
    g_pack_count=2u;
    g_arena_loaded=0;
#endif
    SAT_TRY(open_pack(0,first));
    return open_pack(1,second);
}

const t3_fighter_pack_t* t3_fighter_pack(unsigned actor) {return &g_pack[pack_index(actor)];}

int t3_fighter_packs_share_bank(void) {return g_pack[0].bank_type==g_pack[1].bank_type;}

uint32_t t3_fighter_pack_section_bytes(unsigned actor,unsigned section) {
    return g_file[pack_index(actor)].section[section][1];
}

sat_result_t t3_fighter_pack_read(unsigned actor,unsigned section,uint32_t at,void* out,uint32_t bytes) {
    const pack_file_t* file=&g_file[pack_index(actor)];
    if(section>=T3_PACK_SECTIONS || at>file->section[section][1] ||
       bytes>file->section[section][1]-at)return SAT_ERR_INVALID_ARG;
    uint32_t offset=file->section[section][0]+at;
#if defined(T3_FORCE_PROGRAM)
    offset+=file->base;
#endif
    return read_exact(&file->file,offset,out,bytes);
}

uint8_t* t3_fighter_pack_arena(void) {return g_arena;}

void* t3_fighter_pack_asset_storage(unsigned actor,uint32_t* capacity) {
    const uint32_t hot=(t3_fighter_pack_section_bytes(actor,T3_PACK_HOT)+15u)&~15u;
    *capacity=T3_ROSTER_STORE_BYTES-hot;
#if defined(T3_FORCE_PROGRAM)
    if(pack_index(actor)==2u)*capacity-=T3_FORCE_PACK_SCRATCH_BYTES;
#endif
    return pack_store(pack_index(actor))+hot;
}

sat_result_t t3_fighter_packs_load_banks(void) {
    uint32_t at=0;
    for(unsigned actor=0;actor<2u;++actor) {
        t3_fighter_pack_t* pack=&g_pack[actor];
        if(actor && t3_fighter_packs_share_bank()) {
            if(g_pack[0].bank_fnv1a!=pack->bank_fnv1a)return SAT_ERR_VERSION;
            pack->bank=g_pack[0].bank;
        } else {
            if(at+pack->bank_bytes>arena_capacity())return SAT_ERR_CAPACITY;
            SAT_TRY(t3_fighter_pack_read(actor,T3_PACK_BANK,0,g_arena+at,pack->bank_bytes));
            if(fnv1a(g_arena+at,pack->bank_bytes)!=pack->bank_fnv1a)return SAT_ERR_IO;
            pack->bank=g_arena+at;
            at=(at+pack->bank_bytes+15u)&~15u;
        }
    }
    for(unsigned actor=0;actor<2u;++actor) {
        t3_fighter_pack_t* pack=&g_pack[actor];
        const uint32_t bytes=t3_fighter_pack_section_bytes(actor,T3_PACK_CAMERA);
        if(at+bytes>arena_capacity())return SAT_ERR_CAPACITY;
        SAT_TRY(t3_fighter_pack_read(actor,T3_PACK_CAMERA,0,g_arena+at,bytes));
        pack->streams.encoded=g_arena+at;
        pack->streams.encoded_size=bytes;
        at=(at+bytes+15u)&~15u;
    }
#if defined(T3_FORCE_PROGRAM)
    if(g_pack_count==3u)g_pack[2].bank=g_pack[1].bank;
    g_arena_live_bytes=at;g_arena_loaded=1;
#endif
    return SAT_OK;
}
