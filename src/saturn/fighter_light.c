/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Fighter lighting: a C adaptation of the light rig and colour rule that pinned
 * Tekken3Remake documents in docs/research/code/stages.md (Lighting) for
 * DrawSkinnedPart's NCCT. Changes: the colour of a face's corner is quantised
 * to 16 main-light and 8 fill-light levels and applied as an additive VDP1
 * Gouraud offset scaled by the face's tone class instead of multiplying each
 * texel; no flash, freeze or burn colours.
 *
 * The original lights a normal as base * (back + light * max(N.L, 0) +
 * fill * max(N.down, 0)), each term per channel (fill is half the light,
 * none on stage 17); 128 leaves a texel unchanged. Here the main and fill
 * terms come from the PS1 part rotations, as the GTE's light matrix does. */
#include "fighter_light.h"
#include "fighter_pack.h"
#include "saturn/vdp1.h"

enum { LIGHT_LEVELS=16u, FILL_LEVELS=8u, LEVELS=LIGHT_LEVELS*FILL_LEVELS, MAX_NORMALS=T3_ROSTER_LIGHT_NORMALS,
       FILL_OFF_STAGE=17u, SHADE_BYTES=T3_LIGHT_TONES*LEVELS*2u,
       /* A slice of a relight: parts up to this many normals, or this many shaded faces; a small slice. */
       SLICE_NORMALS=64u, SLICE_FACES=96u, SMALL_NORMALS=24u, SMALL_FACES=32u };

typedef struct {
    /* In VDP1 VRAM: [tone][level] Gouraud words, then two words per face (corners A B, C D); face f's
     * table is CMDGRDA first + f. Work RAM has no room for the shade table. */
    volatile uint16_t (*shade)[LEVELS];
    const uint16_t* tones;
    uint32_t walk,changed;      /* a bit per part: this relight walks it; its last walk changed a level */
    uint16_t face;              /* the shaded faces done */
    uint8_t part;               /* the parts done; T3_PSX_DRAW_PARTS + 1 once the relight is complete */
    uint8_t back[3],back_on;
    uint8_t all;                /* the next relight writes every table: the shade table changed */
} light_actor_t;

/* A part's normals for fighter_light_sh2.s, which holds the field order. */
typedef struct {
    uint8_t* stream;
    uint32_t normals;
    const int16_t* rows;            /* the light row, then the fill row */
    uint8_t* level;                 /* per normal: level * 2, bit 0 set when its last walk changed it */
    volatile uint8_t* shade;        /* the stream's table offsets start here */
    uint32_t all;                   /* 1: every normal counts as changed */
} light_part_job_t;
_Static_assert(sizeof(light_part_job_t)==24u && T3_LIGHT_TONES==16u && LEVELS==128u,"fighter_light_sh2.s holds these");
/* Lights a part's normals and writes the corner colours and tables of those whose level changed; how many did. */
uint32_t t3_fighter_light_part_sh2(const light_part_job_t* job);
/* Write the tables of the shaded faces (of four corners, of three) that have a corner the part walks changed. */
void t3_fighter_light_shaded4_sh2(const uint16_t* faces,uint32_t count,const uint8_t* stream,volatile uint8_t* shade);
void t3_fighter_light_shaded3_sh2(const uint16_t* faces,uint32_t count,const uint8_t* stream,volatile uint8_t* shade);

static const int32_t k_rigs[T3_ROSTER_LIGHT_RIG_COUNT][6]=T3_ROSTER_LIGHT_RIGS;
static const int32_t* g_rig=k_rigs[0];
static unsigned g_fill_on=1u;
/* Low Work RAM: High Work RAM is the Master stack's and the hot data's. */
static light_actor_t g_light[2] __attribute__((section(".wram_l")));
/* Per part the light row and the fill row its last walk used, in 1/121 and 1/113: a normal's dot
 * product with them (its bytes are 1/127) is its level in 1/1024, its fill level in 1/2048. */
static int8_t g_rows[2][T3_PSX_DRAW_PARTS][6];

void t3_fighter_light_stage(unsigned stage) {
    g_rig=k_rigs[stage<T3_ROSTER_LIGHT_RIG_COUNT?stage:0u];
    g_fill_on=stage!=FILL_OFF_STAGE;
}

/* A Gouraud channel: 16 plus the tone's change under `colour` (128 = none). */
static uint16_t channel(int32_t tone,int32_t colour) {
    if(colour>255)colour=255;
    const int32_t change=tone*(colour-128);
    int32_t g=16+(change>=0?(change+64)/128:-((-change+64)/128));
    return (uint16_t)(g<0?0:g>31?31:g);
}

static void build_shade(light_actor_t* a,const uint16_t* tones) {
    int32_t back[3]={g_rig[0]>>4,g_rig[0]>>4,g_rig[0]>>4};
    if(a->back_on)for(unsigned c=0;c<3u;++c)back[c]=a->back[c];   /* practice's freeze signal (FUN_8003AA6C) */
    for(unsigned t=0;t<T3_LIGHT_TONES;++t)
        for(unsigned l=0;l<LIGHT_LEVELS;++l)
            for(unsigned f=0;f<FILL_LEVELS;++f) {
                uint16_t word=0u;
                for(unsigned c=0;c<3u;++c) {
                    const int32_t base=(g_rig[1]>>(8u*c))&255,light=(g_rig[2]>>(8u*c))&255;
                    const int32_t fill=g_fill_on?light>>1:0;
                    /* base * (back + light*l/15 + fill*f/7) / 256, over 105 = 15*7. */
                    const int32_t sum=back[c]*105+light*(int32_t)l*7+fill*(int32_t)f*15;
                    const int32_t colour=base*sum/(256*105);
                    word|=(uint16_t)(channel((tones[t]>>(5u*c))&31,colour)<<(5u*c));
                }
                a->shade[t][l*FILL_LEVELS+f]=word;
            }
}

uint16_t t3_fighter_light_load(unsigned actor) {
    light_actor_t* a=&g_light[actor&1u];
    const t3_fighter_pack_t* pack=t3_fighter_pack(actor);
    a->shade=NULL;
    if(!pack->light_normal_count || pack->light_normal_count>MAX_NORMALS)
        return 0u;
    const uint32_t bytes=SHADE_BYTES+pack->faces*8u;
    uint32_t offset;
    if(sat_vdp1_vram_reserve(bytes,&offset)!=SAT_OK)return 0u;
    volatile uint16_t (*shade)[LEVELS]=(volatile uint16_t (*)[LEVELS])sat_vdp1_vram_address(offset,bytes);
    const uint32_t first=(offset+SHADE_BYTES)>>3;
    if(!shade || (offset&7u) || first+pack->faces>0xFFFFu)return 0u;
    volatile uint32_t* tables=(volatile uint32_t*)(void*)shade[T3_LIGHT_TONES];
    for(uint32_t f=0;f<2u*pack->faces;++f)tables[f]=0x42104210u; /* unchanged texels until the first relight */
    a->tones=pack->light_tones;a->back_on=0;
    a->shade=shade;
    build_shade(a,pack->light_tones);
    a->all=1u;a->changed=0u;a->part=T3_PSX_DRAW_PARTS+1u;
    return (uint16_t)first;
}

int t3_fighter_light_begin(unsigned actor,const t3_psx_joint_t joints[T3_PSX_DRAW_PARTS]) {
    light_actor_t* a=&g_light[actor&1u];
    if(!a->shade)return 0;
    /* Per part the light row in the part's own basis: R^T L. The fill light's row (0, 1, 0) is the
     * rotation's second row. A part is walked when a row moved, or to clear what its last walk marked. */
    int8_t (*rows)[6]=g_rows[actor&1u];
    uint32_t walk=a->changed;
    for(unsigned j=0;j<T3_PSX_DRAW_PARTS;++j) {
        const int16_t (*m)[3]=joints[j].rotation.m;
        for(unsigned k=0;k<3u;++k) {
            const int32_t lit=(m[0][k]*g_rig[3]+m[1][k]*g_rig[4]+m[2][k]*g_rig[5])>>12;
            const int8_t now[2]={(int8_t)((lit*1935+32768)>>16),(int8_t)((m[1][k]*1806+32768)>>16)};
            for(unsigned r=0;r<2u;++r)
                if(rows[j][3u*r+k]!=now[r]){rows[j][3u*r+k]=now[r];walk|=1u<<j;}
        }
    }
    if(a->all)walk=(1u<<T3_PSX_DRAW_PARTS)-1u;
    a->walk=walk;a->part=0u;a->face=0u;
    return walk!=0u;
}

int t3_fighter_light_step(unsigned actor,int small) {
    light_actor_t* a=&g_light[actor&1u];
    if(!a->shade || a->part>T3_PSX_DRAW_PARTS)return 0;
    const t3_fighter_pack_t* pack=t3_fighter_pack(actor);
    if(a->part<T3_PSX_DRAW_PARTS) {
        const uint32_t slice=small?SMALL_NORMALS:SLICE_NORMALS;
        for(uint32_t normals=0;a->part<T3_PSX_DRAW_PARTS && normals<slice;++a->part) {
            const unsigned j=a->part;
            const t3_light_part_t* part=&pack->light_parts[j];
            if(!(a->walk&(1u<<j)) || !part->normals)continue;
            int16_t rows[6];
            for(unsigned k=0;k<6u;++k)rows[k]=g_rows[actor&1u][j][k];
            const light_part_job_t job={.stream=pack->light_stream+part->stream,.normals=part->normals,.rows=rows,
                .level=pack->light_state+part->first,.shade=(volatile uint8_t*)a->shade,.all=a->all};
            if(t3_fighter_light_part_sh2(&job))a->changed|=1u<<j;
            else a->changed&=~(1u<<j);
            normals+=part->normals;
        }
        if(a->part<T3_PSX_DRAW_PARTS)return 1;
        a->all=0u;
        if(!a->walk || !(pack->light_shaded[0]+pack->light_shaded[1])){a->part=T3_PSX_DRAW_PARTS+1u;return 0;}
        return 1;
    }
    /* The faces of four corners, then those of three. */
    const uint16_t* shaded=pack->light_shaded;
    const uint32_t four=shaded[0],faces=four+shaded[1];
    uint32_t count=(a->face<four?four:faces)-a->face;
    if(count>(small?SMALL_FACES:SLICE_FACES))count=small?SMALL_FACES:SLICE_FACES;
    if(a->face<four)
        t3_fighter_light_shaded4_sh2(shaded+2u+5u*a->face,count,pack->light_stream,(volatile uint8_t*)a->shade);
    else
        t3_fighter_light_shaded3_sh2(shaded+2u+5u*four+4u*(a->face-four),count,pack->light_stream,
                                     (volatile uint8_t*)a->shade);
    a->face=(uint16_t)(a->face+count);
    if(a->face<faces)return 1;
    a->part=T3_PSX_DRAW_PARTS+1u;
    return 0;
}

/* Practice's FREEZE SIGNAL: lights an actor with this back colour (R, G, B bytes as the original's) instead of the
 * stage's, or with the stage's again when `on` is 0. The table is rebuilt in place: a relight under way mixes the two
 * colours until the next one. Nothing runs unless the colour changes. */
void t3_fighter_light_back(unsigned actor,int on,const uint8_t rgb[3]) {
    light_actor_t* a=&g_light[actor&1u];
    if(!a->shade || !a->tones)return;
    if(!on){if(!a->back_on)return;}
    else if(a->back_on && a->back[0]==rgb[0] && a->back[1]==rgb[1] && a->back[2]==rgb[2])return;
    a->back_on=(uint8_t)(on!=0);
    if(on){a->back[0]=rgb[0];a->back[1]=rgb[1];a->back[2]=rgb[2];}
    build_shade(a,a->tones);
    a->all=1u;
}
