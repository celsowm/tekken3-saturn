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
       FILL_OFF_STAGE=17u, SHADE_BYTES=T3_LIGHT_TONES*LEVELS*2u };

typedef struct {
    volatile uint32_t* tables; /* two words per face: corners A B, C D; face f's table is CMDGRDA first + f */
    uint32_t faces;
    /* [tone][level], in VDP1 VRAM after the tables: Work RAM has no room for it. */
    volatile uint16_t (*shade)[LEVELS];
    const uint16_t* tones;
    uint8_t back[3],back_on;
} light_actor_t;

static const int32_t k_rigs[T3_ROSTER_LIGHT_RIG_COUNT][6]=T3_ROSTER_LIGHT_RIGS;
static const int32_t* g_rig=k_rigs[0];
static unsigned g_fill_on=1u;
/* Low Work RAM: High Work RAM is the Master stack's and the hot data's. */
static light_actor_t g_light[2] __attribute__((section(".wram_l")));

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
    a->tables=NULL;
    if(!pack->light_normal_count || pack->light_normal_count>MAX_NORMALS)
        return 0u;
    uint32_t offset;
    if(sat_vdp1_vram_reserve(pack->faces*8u+SHADE_BYTES,&offset)!=SAT_OK)return 0u;
    volatile uint32_t* tables=(volatile uint32_t*)sat_vdp1_vram_address(offset,pack->faces*8u+SHADE_BYTES);
    if(!tables || !offset || ((offset>>3)+pack->faces)>0xFFFFu)return 0u;
    a->shade=(volatile uint16_t (*)[LEVELS])(tables+2u*pack->faces);
    for(uint32_t f=0;f<2u*pack->faces;++f)tables[f]=0x42104210u; /* unchanged texels until the first relight */
    a->tones=pack->light_tones;a->back_on=0;
    build_shade(a,pack->light_tones);
    a->faces=pack->faces;
    a->tables=tables;
    return (uint16_t)(offset>>3);
}

void t3_fighter_light_relight(unsigned actor,const t3_psx_joint_t joints[T3_PSX_DRAW_PARTS]) {
    light_actor_t* a=&g_light[actor&1u];
    if(!a->tables)return;
    const t3_fighter_pack_t* pack=t3_fighter_pack(actor);
    /* Per part the light row in the part's own basis: R^T L. The fill light's
     * row (0, 1, 0) is the rotation's second row. */
    int32_t row[T3_PSX_DRAW_PARTS][3];
    for(unsigned j=0;j<T3_PSX_DRAW_PARTS;++j) {
        const int16_t (*m)[3]=joints[j].rotation.m;
        for(unsigned k=0;k<3u;++k)
            row[j][k]=(m[0][k]*g_rig[3]+m[1][k]*g_rig[4]+m[2][k]*g_rig[5])>>12;
    }
    /* Each normal's level, for this relight only. */
    uint8_t level[MAX_NORMALS];
    const t3_light_normal_t* n=pack->light_normals;
    for(uint32_t i=0;i<pack->light_normal_count;++i,++n) {
        const unsigned j=n->joint<T3_PSX_DRAW_PARTS?n->joint:0u;
        const int16_t* down=joints[j].rotation.m[1];
        const int32_t lit=(n->x*row[j][0]+n->y*row[j][1]+n->z*row[j][2])>>12;
        const int32_t fill=(n->x*down[0]+n->y*down[1]+n->z*down[2])>>12;
        uint32_t l=lit>0?((uint32_t)lit*(LIGHT_LEVELS-1u)+2048u)>>12:0u;
        uint32_t f=fill>0?((uint32_t)fill*(FILL_LEVELS-1u)+2048u)>>12:0u;
        if(l>=LIGHT_LEVELS)l=LIGHT_LEVELS-1u;
        if(f>=FILL_LEVELS)f=FILL_LEVELS-1u;
        level[i]=(uint8_t)(l*FILL_LEVELS+f);
    }
    const uint16_t* words=pack->light_faces;
    const uint16_t* corners=pack->light_corners;
    volatile uint32_t* out=a->tables;
    for(uint32_t f=0;f<a->faces;++f,out+=2) {
        const uint16_t word=words[f];
        const volatile uint16_t* shade=a->shade[T3_LIGHT_FACE_TONE(word)];
        const uint32_t first=shade[level[T3_LIGHT_FACE_NORMAL(word)]];
        if(word&T3_LIGHT_FACE_FLAT) {
            const uint32_t both=first<<16|first;
            out[0]=both;out[1]=both;
        } else {
            out[0]=first<<16|shade[level[corners[0]]];
            out[1]=(uint32_t)shade[level[corners[1]]]<<16|shade[level[corners[2]]];
            corners+=3;
        }
    }
}

/* Practice's FREEZE SIGNAL: lights an actor with this back colour (R, G, B bytes as the original's) instead of the
 * stage's, or with the stage's again when `on` is 0. The table is rebuilt in place: a relight running on the other
 * CPU at that moment mixes the two colours for one image. Nothing runs unless the colour changes. */
void t3_fighter_light_back(unsigned actor,int on,const uint8_t rgb[3]) {
    light_actor_t* a=&g_light[actor&1u];
    if(!a->tables || !a->tones)return;
    if(!on){if(!a->back_on)return;}
    else if(a->back_on && a->back[0]==rgb[0] && a->back[1]==rgb[1] && a->back[2]==rgb[2])return;
    a->back_on=(uint8_t)(on!=0);
    if(on){a->back[0]=rgb[0];a->back[1]=rgb[1];a->back[2]=rgb[2];}
    build_shade(a,a->tones);
}
