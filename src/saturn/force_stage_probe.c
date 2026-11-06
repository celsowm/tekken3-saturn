/* Tekken Force stage-layer probe (docs/TEKKEN_FORCE_PLAN.md, phase 4): reads LEVn.BIN from the disc
 * (scripts/build_force_levels.py) and shows its panorama on NBG0 as 8x8-cell characters with 2-word
 * pattern names, scrolling it sideways like the camera does (one dot per 20 units of the camera's view x).
 * Copyright (c) 2026 n0madic, PolyForm Noncommercial 1.0.0; see NOTICE.md. */
#include <stdint.h>

#include "saturn/app.h"
#include "saturn/color.h"
#include "saturn/input.h"
#include "saturn/saturn.h"
#include "saturn/vdp2.h"
#include "saturn/vdp2_layers.h"
#include "saturn/vdp2_special.h"
#include "disc.h"

enum {
    HEADER_BYTES=64, TILE_BYTES=2048, CELLS_PER_TILE=64,
    NAMES_BASE=0x00000, CHAR_BASE=0x40000,      /* names in A0, character data in B0 and B1 */
    MAX_PALETTES=127, VIEW_W=320, VIEW_H=224
};
static uint8_t g_io[8192] __attribute__((aligned(4)));
static sat_cdfs_volume_t* g_volume;

static uint32_t be32(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

static sat_result_t read_at(const char* name,uint32_t offset,void* dst,uint32_t bytes) {
    sat_cdfs_file_t file;
    uint32_t read=0;
    SAT_TRY(sat_cdfs_lookup(g_volume,name,&file));
    if(offset+bytes>file.size)return SAT_ERR_IO;
    SAT_TRY(sat_cdfs_read_at(g_volume,&file,offset,dst,bytes,&read));
    return read==bytes?SAT_OK:SAT_ERR_IO;
}

/* One tile is 64 x 64 texels in 4 bits (32 bytes a row); a cell is 8 rows of 4 bytes. */
static sat_result_t upload_tile(unsigned tile,const uint8_t* texels) {
    uint16_t cell[16];
    for(unsigned cy=0;cy<8;++cy)
        for(unsigned cx=0;cx<8;++cx) {
            for(unsigned row=0;row<8;++row) {
                const uint8_t* p=texels+(cy*8u+row)*32u+cx*4u;
                cell[row*2]=(uint16_t)(p[0]<<8|p[1]);
                cell[row*2+1]=(uint16_t)(p[2]<<8|p[3]);
            }
            SAT_TRY(sat_vdp2_vram_write_words((CHAR_BASE+(tile*CELLS_PER_TILE+cy*8u+cx)*32u)/2u,cell,16));
        }
    return SAT_OK;
}

int main(void) {
    sat_video_config_t video={VIEW_W,VIEW_H,SAT_VIDEO_AUTO,0};
    sat_vdp2_layer_config_t layer;
    uint8_t header[HEADER_BYTES];
    static uint8_t cells[128*64*4];     /* the map: (tile, palette) per 64 x 64 texel cell */
    const char* name="LEV1.BIN";
    SAT_PANIC_IF_ERROR(sat_init(&video));
    SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(SAT_COLOR_RED));
    (void)sat_vdp2_layers_commit();
    SAT_PANIC_IF_ERROR(sat_begin_frame());
    SAT_PANIC_IF_ERROR(sat_end_frame());
    SAT_PANIC_IF_ERROR(t3_disc_volume(&g_volume));
    SAT_PANIC_IF_ERROR(read_at(name,0,header,HEADER_BYTES));
    const uint32_t width=be32(header+12),height=be32(header+16),tiles=be32(header+20),palettes=be32(header+24);
    const uint32_t cells_at=be32(header+28),tiles_at=be32(header+32),palettes_at=be32(header+36);
    if(be32(header)!=0x54334C46u || width>16u || height>8u || palettes>MAX_PALETTES || CHAR_BASE/32u+tiles*CELLS_PER_TILE+1u>0x7FFFu)
        SAT_PANIC_IF_ERROR(SAT_ERR_IO);
    SAT_PANIC_IF_ERROR(read_at(name,cells_at,cells,width*height*4u));
    /* Palettes: 16 colours each, palette number p at CRAM colour p * 16 (one read for all of them). */
    SAT_PANIC_IF_ERROR(read_at(name,palettes_at,g_io,palettes*32u));
    for(unsigned p=0;p<palettes;++p) {
        uint16_t pal[16];
        for(unsigned i=0;i<16;++i)pal[i]=(uint16_t)(g_io[p*32u+i*2u]<<8|g_io[p*32u+i*2u+1u]);
        SAT_PANIC_IF_ERROR(sat_vdp2_palette_upload(pal,16,(uint16_t)(p*16u)));
    }
    /* Characters: every tile's 64 cells (four tiles to a read), then one blank cell (code 0 is transparent). */
    for(unsigned t=0;t<tiles;t+=4u) {
        const unsigned n=tiles-t<4u?tiles-t:4u;
        SAT_PANIC_IF_ERROR(read_at(name,tiles_at+t*TILE_BYTES,g_io,n*TILE_BYTES));
        for(unsigned k=0;k<n;++k)SAT_PANIC_IF_ERROR(upload_tile(t+k,g_io+k*TILE_BYTES));
    }
    {
        static const uint16_t blank[16]={0};
        SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words((CHAR_BASE+tiles*CELLS_PER_TILE*32u)/2u,blank,16));
    }
    /* Pattern names. The plane is 2 x 1 pages of 64 x 64 cells (columns 0..63, then 64..127), 4 bytes a name; the
     * map is width x height tiles of 8 x 8 cells, the rest of the plane is the blank cell. */
    for(unsigned row=0;row<64;++row)
        for(unsigned col=0;col<128;++col) {
            uint16_t chr=(uint16_t)(CHAR_BASE/32u+tiles*CELLS_PER_TILE),pal=0;
            if(col<width*8u && row<height*8u) {
                const uint8_t* c=cells+((row/8u)*width+col/8u)*4u;
                chr=(uint16_t)(CHAR_BASE/32u+(unsigned)(c[0]<<8|c[1])*CELLS_PER_TILE+(row%8u)*8u+col%8u);
                pal=(uint16_t)(c[2]<<8|c[3]);
            }
            const uint32_t word=sat_vdp2_pattern_name_2word(chr,(uint8_t)pal,0,0,0,0);
            const uint16_t pair[2]={(uint16_t)(word>>16),(uint16_t)word};
            SAT_PANIC_IF_ERROR(sat_vdp2_vram_write_words((NAMES_BASE+(col/64u)*0x4000u+(row*64u+col%64u)*4u)/2u,pair,2));
        }
    SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(SAT_COLOR_GREEN));
    (void)sat_vdp2_layers_commit();
    sat_vdp2_layer_config_default(SAT_VDP2_NBG0,&layer);
    layer.bitmap=0;
    layer.color_mode=SAT_VDP2_COLOR_MODE_16;
    layer.char_size=SAT_VDP2_CHAR_SIZE_1X1;
    layer.pattern_name_words=2;
    layer.plane_pages_x=2;layer.plane_pages_y=1;
    layer.priority=2;
    layer.transparent=1;
    layer.char_bank_mask=(uint8_t)(SAT_VDP2_BANK_B0|SAT_VDP2_BANK_B1);
    /* 2-word names carry the whole character number: character n is at VRAM n * 0x20. */
    for(unsigned i=0;i<4;++i)layer.plane_address[i]=NAMES_BASE;
    SAT_PANIC_IF_ERROR(sat_vdp2_layer_configure(&layer));
    SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(SAT_COLOR_BLACK));
    /* The camera scrolls one dot per 20 units of its view x: here 40 units a frame, 2 dots. */
    for(uint32_t frame=0;;++frame) {
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        SAT_PANIC_IF_ERROR(sat_vdp2_layer_set_scroll(SAT_VDP2_NBG0,(int32_t)((frame*2u)%(width*64u))<<16,(int32_t)((height*64u>VIEW_H?height*64u-VIEW_H:0u)<<16)));
        (void)sat_vdp2_layers_commit();
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        SAT_PANIC_IF_ERROR(sat_end_frame());
    }
}
