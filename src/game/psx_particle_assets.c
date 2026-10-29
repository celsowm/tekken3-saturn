/* Asset layout derived from pinned Tekken3Remake effects research.
 * PolyForm Noncommercial 1.0.0; Copyright (c) 2026 n0madic; see NOTICE.md. */
#include "psx_particle_assets.h"

enum { HEADER=16, PATTERNS=2, PATTERN_BYTES=128, PALETTES=37,
       PALETTE_BYTES=68, MODELS=5, MODEL_BYTES=32,
       PALETTE_OFFSET=HEADER+PATTERNS*PATTERN_BYTES,
       MODEL_OFFSET=PALETTE_OFFSET+PALETTES*PALETTE_BYTES,
       FILE_BYTES=MODEL_OFFSET+MODELS*MODEL_BYTES };
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | ((unsigned)p[1]<<8)); }
static uint16_t clut_id(unsigned i) {
    return (uint16_t)(i<5 ? 0x7dc6u+i : i<21 ? 0x7f50u+i-5 : 0x7f90u+i-21);
}
int t3_psx_particle_assets_open(t3_psx_particle_assets_t* out,const void* data,size_t size) {
    static const uint8_t magic[8]={'T','3','F','X','0','0','1',0};
    const uint8_t* p=(const uint8_t*)data;
    if(!out || !p || size!=FILE_BYTES ||
       le16(p+8)!=PATTERNS || le16(p+10)!=PALETTES ||
       le16(p+12)!=MODELS || le16(p+14)!=16) return 0;
    for(unsigned i=0;i<8;++i) if(p[i]!=magic[i]) return 0;
    for(unsigned i=0;i<PALETTES;++i) {
        const uint8_t* q=p+PALETTE_OFFSET+i*PALETTE_BYTES;
        if(le16(q)!=clut_id(i) || le16(q+2)!=0) return 0;
        for(unsigned j=0;j<16;++j) {
            uint16_t original=le16(q+4+2*j);
            uint16_t converted=original ? (uint16_t)(0x8000u|(original&0x7fffu)) : 0;
            if(le16(q+36+2*j)!=converted) return 0;
        }
    }
    out->data=p;
    return 1;
}
const uint8_t* t3_psx_particle_pattern(const t3_psx_particle_assets_t* a,unsigned pattern) {
    return a && a->data && pattern<PATTERNS ? a->data+HEADER+pattern*PATTERN_BYTES : NULL;
}
int t3_psx_particle_palette(const t3_psx_particle_assets_t* a,uint16_t clut,uint16_t out[16]) {
    if(!a || !a->data || !out) return 0;
    for(unsigned i=0;i<PALETTES;++i) if(clut_id(i)==clut) {
        const uint8_t* p=a->data+PALETTE_OFFSET+i*PALETTE_BYTES+4;
        for(unsigned j=0;j<16;++j) out[j]=le16(p+2*j);
        return 1;
    }
    return 0;
}
int t3_psx_particle_model(const t3_psx_particle_assets_t* a,unsigned model,t3_psx_particle_model_t* out) {
    if(!a || !a->data || !out || model==0 || model>MODELS) return 0;
    const uint8_t* p=a->data+MODEL_OFFSET+(model-1)*MODEL_BYTES;
    for(unsigned i=0;i<4;++i) for(unsigned j=0;j<4;++j) {
        unsigned u=le16(p+8*i+2*j);
        out->corners[i][j]=(int16_t)(u<0x8000u ? (int)u : (int)u-65536);
    }
    return 1;
}
