/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * The layout, colours and rules follow the pinned Tekken3Remake (hud.md, modes.md, mode_hud_view.gd,
 * force_sim.py progress_draw/key_icons), PolyForm Noncommercial 1.0.0; see NOTICE.md. Changes: VDP1
 * sprites and polygons in the Saturn's 320 x 224 image (psx_viewport.h); the progress bar's additive
 * part is a half-transparent polygon at twice its colour. */
#include "force_hud.h"
#include "disc.h"
#include "fighter_pack.h"
#include "psx_viewport.h"
#include "saturn/cdfs.h"
#include "saturn/color.h"
#include "saturn/render3d.h"
#include "saturn/vdp1.h"

/* FHUD.BIN's order (extract_force_hud.py), then the three keys. */
enum { CAP_LEFT=0, CAP_RIGHT, HEALTH, DAMAGE, EMPTY, PLATE, DIGIT=6, GLYPH=16, PIECES=34,
       KEY=PIECES, NAME=KEY+3, LABEL_SCORE, LABEL_HI, TEXTURES, PALETTES=3, GLYPH_W=10, GLYPH_H=16,
       ADVANCE=9 };
static const char MODE_CHARS[]="SCOREHI-0123456789";
_Static_assert(sizeof MODE_CHARS-1==PIECES-GLYPH,"FHUD.BIN glyph order");
static sat_vdp1_texture_t g_tex[TEXTURES];
static uint16_t g_palette[PALETTES];   /* font 0 colours 5, 2, 6 */
static uint16_t g_name_width;
/* The two labels, each composed once into one texture: one sprite instead of one per letter. */
static const char* const LABEL_TEXT[2]={"SCORE","HI-SCORE"};
static uint16_t g_label_width[2];

static unsigned le16(const uint8_t* p) {return (unsigned)p[0]|(unsigned)p[1]<<8;}
static unsigned be16(const uint8_t* p) {return (unsigned)p[0]<<8|(unsigned)p[1];}

static sat_result_t read_file(const char* name,uint8_t* out,unsigned capacity,uint32_t* size) {
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,name,&file));
    if(file.size>capacity)return SAT_ERR_CAPACITY;
    uint32_t read=0;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,out,file.size,&read));
    *size=read;
    return read==file.size?SAT_OK:SAT_ERR_IO;
}

static sat_result_t upload(sat_vdp1_texture_t* tex,const uint16_t* colours,const uint8_t* texels,
                           unsigned padded,unsigned height) {
    uint16_t lut;
    SAT_TRY(sat_vdp1_upload_lut(colours,&lut));
    return sat_tex_upload_lut4_pixels(tex,texels,(uint16_t)padded,(uint16_t)height,lut);
}

/* Lays `text`'s glyphs ADVANCE texels apart into `out` (padded texels wide, LUT4, left pixel in the
 * high nibble); a later glyph's opaque texels win where two overlap. */
static void compose(const char* text,const uint8_t* const* glyph,const unsigned* glyph_padded,
                    uint8_t* out,unsigned padded) {
    for(unsigned i=0;i<padded*GLYPH_H/2u;++i)out[i]=0;
    for(unsigned pen=0;*text;++text,pen+=ADVANCE) {
        unsigned g=0;
        while(MODE_CHARS[g]!=*text)++g;
        for(unsigned y=0;y<GLYPH_H;++y)for(unsigned x=0;x<GLYPH_W;++x) {
            const uint8_t b=glyph[g][y*glyph_padded[g]/2u+x/2u];
            const unsigned nib=x&1u?b&15u:b>>4,to=pen+x;
            if(!nib)continue;
            uint8_t* o=&out[y*padded/2u+to/2u];
            *o=(uint8_t)(to&1u?(*o&0xF0u)|nib:(*o&0x0Fu)|nib<<4);
        }
    }
}

sat_result_t t3_force_hud_init(void* scratch,unsigned bytes) {
    uint8_t* data=scratch;
    uint16_t lut[16];
    uint32_t size;
    const uint8_t* glyph[PIECES-GLYPH];
    unsigned glyph_padded[PIECES-GLYPH];
    SAT_TRY(read_file("FHUD.BIN",data,bytes,&size));
    static const uint8_t magic[8]={'T','3','F','H','U','D','1',0};
    for(unsigned j=0;j<8;++j)if(size<12u || data[j]!=magic[j])return SAT_ERR_VERSION;
    if(le16(data+8)!=PIECES || le16(data+10)!=PALETTES)return SAT_ERR_VERSION;
    uint32_t at=12;
    for(unsigned i=0;i<PIECES;++i) {
        if(at+40u>size)return SAT_ERR_VERSION;
        const unsigned padded=le16(data+at+2),height=le16(data+at+4),count=le16(data+at+6);
        if(!padded || padded&7u || count!=padded*height/2u || at+40u+count>size)return SAT_ERR_VERSION;
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(data+at+8u+2u*j);
        SAT_TRY(upload(&g_tex[i],lut,data+at+40u,padded,height));
        if(i>=GLYPH) {
            if(height!=GLYPH_H || padded<GLYPH_W)return SAT_ERR_VERSION;
            glyph[i-GLYPH]=data+at+40u;
            glyph_padded[i-GLYPH]=padded;
        }
        at+=40u+count;
    }
    /* The glyphs share colour 5's table; the palette picks each label's colour when drawn. */
    for(unsigned l=0;l<2u;++l) {
        unsigned n=0;
        while(LABEL_TEXT[l][n])++n;
        const unsigned padded=(ADVANCE*(n-1u)+GLYPH_W+7u)&~7u;
        if(size+padded*GLYPH_H/2u>bytes)return SAT_ERR_CAPACITY;
        compose(LABEL_TEXT[l],glyph,glyph_padded,data+size,padded);
        SAT_TRY(upload(&g_tex[LABEL_SCORE+l],lut,data+size,padded,GLYPH_H));
        g_label_width[l]=(uint16_t)padded;
    }
    for(unsigned i=0;i<PALETTES;++i,at+=32u) {
        if(at+32u>size)return SAT_ERR_VERSION;
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(data+at+2u*j);
        SAT_TRY(sat_vdp1_upload_lut(lut,&g_palette[i]));
    }
    if(at!=size)return SAT_ERR_VERSION;
    /* KEYS.BIN (build_force_icons.py): big-endian count, then per key width, height, 16 colours and
     * 4-bit texels; the 20-texel rows pad to 24 for VDP1. */
    SAT_TRY(read_file("KEYS.BIN",data,bytes,&size));
    if(size<4u || be16(data)!=0 || be16(data+2)!=3u)return SAT_ERR_VERSION;
    at=4;
    for(unsigned k=0;k<3u;++k) {
        if(at+36u>size)return SAT_ERR_VERSION;
        const unsigned width=be16(data+at),height=be16(data+at+2),row=width/2u,padded=(width+7u)&~7u;
        if(!width || width&1u || height>64u || at+36u+row*height>size || bytes<size+padded*height/2u)
            return SAT_ERR_VERSION;
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)be16(data+at+4u+2u*j);
        uint8_t* texels=data+size;
        for(unsigned y=0;y<height;++y)for(unsigned x=0;x<padded/2u;++x)
            texels[y*padded/2u+x]=x<row?data[at+36u+y*row+x]:0u;
        SAT_TRY(upload(&g_tex[KEY+k],lut,texels,padded,height));
        at+=36u+row*height;
    }
    /* The player's name sheet: big-endian width, padded width, height, then the left-aligned side's
     * colours and texels (build_fighter_packs.py names). */
    const uint32_t name_bytes=t3_fighter_pack_section_bytes(0,T3_PACK_NAMES);
    if(name_bytes<8u || name_bytes>bytes)return SAT_ERR_CAPACITY;
    SAT_TRY(t3_fighter_pack_read(0,T3_PACK_NAMES,0,data,name_bytes));
    const unsigned padded=be16(data+2),height=be16(data+4);
    if(!padded || padded&7u || 8u+2u*(32u+padded*height/2u)!=name_bytes)return SAT_ERR_VERSION;
    g_name_width=(uint16_t)be16(data);
    for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(data+8u+2u*j);
    return upload(&g_tex[NAME],lut,data+40u,padded,height);
}

#define X(x) t3_psx_viewport_x(x)
#define Y(y) t3_psx_viewport_y(y)

/* One piece over original x0..x1 and y0..y1 (heights halve, as the 480-line frame does). */
static sat_result_t draw(unsigned piece,int x0,int y0,int x1,int y1,uint16_t palette) {
    const int x=X(x0),y=Y(y0),w=X(x1)-x,h=Y(y1)-y;
    if(w<=0 || h<=0)return SAT_OK;
    return sat_draw_sprite_scaled_screen(&g_tex[piece],(int16_t)(x+w/2),(int16_t)(y+h/2),
                                         (uint16_t)w,(uint16_t)h,palette);
}

sat_result_t t3_force_hud_bar(int x,int y,int width,int fill,int recent) {
    /* mode_hud_view.gd _bar: 6 x 24 caps; the parts run left from x + width - 2: the health, the
     * recent damage beyond it, the empty rest down to x + 2. */
    const int end=x+width-2,full=width-4;
    if(full<=0)return SAT_OK;
    if(fill>full)fill=full;
    if(recent>full)recent=full;
    if(recent<fill)recent=fill;
    SAT_TRY(draw(CAP_LEFT,x,y,x+6,y+24,0));
    SAT_TRY(draw(CAP_RIGHT,x+width-6,y,x+width,y+24,0));
    if(full>recent)SAT_TRY(draw(EMPTY,x+2,y,end-recent,y+24,0));
    if(recent>fill)SAT_TRY(draw(DAMAGE,end-recent,y,end-fill,y+24,0));
    if(fill>0)SAT_TRY(draw(HEALTH,end-fill,y,end,y+24,0));
    return SAT_OK;
}

sat_result_t t3_force_hud_player_name(void) {
    /* The plate quad from 4 pixels left of the name to 4 right, rows y + 9 to y + 19; the 16-row name. */
    SAT_TRY(draw(PLATE,10,66+9,14+g_name_width+4,66+19,0));
    const int padded=(g_name_width+7)&~7;
    return draw(NAME,14,66,14+padded,66+16,0);
}

sat_result_t t3_force_hud_timer(unsigned seconds) {
    if(seconds>99u)seconds=99u;
    SAT_TRY(draw(DIGIT+seconds/10u,169,22,169+16,22+46,0));
    return draw(DIGIT+seconds%10u,184,22,184+16,22+46,0);
}

/* print_text with font 0 (advance 9, 10 x 16 glyphs): a space advances the pen. */
static int text(const char* s,int pen,uint16_t palette,sat_result_t* st) {
    for(;*s;++s,pen+=9) {
        if(*s==' ')continue;
        unsigned g=0;
        while(MODE_CHARS[g] && MODE_CHARS[g]!=*s)++g;
        if(*st==SAT_OK && MODE_CHARS[g])*st=draw(GLYPH+g,pen,24,pen+10,24+16,palette);
    }
    return pen;
}

/* %8D: the value right-aligned in eight places (values over 99,999,999 print 99999999). */
static sat_result_t number(uint32_t value,int pen) {
    char digits[9];
    if(value>99999999u)value=99999999u;
    int at=8;
    digits[at]=0;
    do {digits[--at]=(char)('0'+value%10u);value/=10u;} while(value && at);
    sat_result_t st=SAT_OK;
    text(digits+at,pen+9*at,g_palette[2],&st);
    return st;
}

sat_result_t t3_force_hud_scores(uint32_t score,uint32_t hiscore) {
    /* "SCORE " at 19 and "HI-SCORE " at 208 (the space advances the pen), each as its composed label. */
    SAT_TRY(draw(LABEL_SCORE,19,24,19+g_label_width[0],24+GLYPH_H,g_palette[0]));
    SAT_TRY(number(score,19+6*ADVANCE));
    SAT_TRY(draw(LABEL_HI,208,24,208+g_label_width[1],24+GLYPH_H,g_palette[1]));
    return number(hiscore,208+9*ADVANCE);
}

sat_result_t t3_force_hud_keys(unsigned keys) {
    for(unsigned k=0;k<3u && k<keys;++k)SAT_TRY(draw(KEY+k,20*(int)k,420,20*(int)k+20,420+36,0));
    return SAT_OK;
}

/* Polygons take the VDP1's native coordinates: the 320 x 224 screen's centre is 0, 0, and both
 * corners are inside. */
#define NX(x) (X(x)-160)
#define NY(y) (Y(y)-112)

static sat_result_t quad(int x0,int x1,int y0,int y1,uint16_t colour,uint16_t flags) {
    const int a=NX(x0),b=NX(x1)-1,t=NY(y0),u=NY(y1)-1;
    if(b<a || u<t)return SAT_OK;
    const sat_quad2_t q={{(int16_t)a,(int16_t)b,(int16_t)b,(int16_t)a},{(int16_t)t,(int16_t)t,(int16_t)u,(int16_t)u}};
    return sat_draw_quad2_polygon_effects(&q,colour,flags);
}

/* BAR_COLOURS (force.ovl 0x800B69FC) at 5 bits: one per level. */
static const uint8_t SEGMENT_RGB[4][3]={{0,8,16},{0,16,4},{16,16,0},{16,2,0}};
enum { BAR_LEFT=0x40, SEGMENT=0x3C, BAR_TOP=0x1B4, BAR_BOTTOM=0x1C4, POINTER_Y=0x1A8 };

static uint16_t segment(unsigned i,int scale) {
    int c[3];
    for(unsigned k=0;k<3;++k) {
        c[k]=SEGMENT_RGB[i][k]*scale/128;
        if(c[k]>31)c[k]=31;
    }
    return SAT_RGB555(c[0],c[1],c[2]);
}

sat_result_t t3_force_hud_progress(int level,int mark,int sine) {
    /* Lit segments are their colour half over the scene, later ones and the current one past the
     * marker the scene halved (black, half-transparent); the current one up to the marker adds its
     * colour times the pulse (128..255). */
    int pulse=((sine+0x1000)>>6)+0x80;
    if(pulse>0xFF)pulse=0xFF;
    const uint16_t half=SAT_SPRITE_FLAG_HALF_TRANSPARENT,black=SAT_RGB555(0,0,0);
    const int x0=BAR_LEFT+SEGMENT*(level<4?level:4);
    for(int i=0;i<4 && i<level;++i) {
        const int x=BAR_LEFT+SEGMENT*i;
        SAT_TRY(quad(x,x+SEGMENT,BAR_TOP,BAR_BOTTOM,segment((unsigned)i,128),half));
    }
    if(level<4) {
        SAT_TRY(quad(x0,mark,BAR_TOP,BAR_BOTTOM,segment((unsigned)level,2*pulse),half));
        /* The current segment past the marker and the later ones: one run, the scene halved. */
        SAT_TRY(quad(mark,BAR_LEFT+4*SEGMENT,BAR_TOP,BAR_BOTTOM,black,half));
    }
    /* The Doctor B. level: the short fifth piece pulses instead. */
    if(level==4)SAT_TRY(quad(x0,x0+0xC,BAR_TOP,BAR_BOTTOM,segment(3u,2*pulse),half));
    /* The separators shade from white at the top to grey (LINE_G2); the marker is white. */
    const int16_t top=(int16_t)NY(BAR_TOP),bottom=(int16_t)(NY(BAR_BOTTOM)-1);
    const uint16_t fade[4]={sat_gouraud_rgb(15,15,15),sat_gouraud_rgb(15,15,15),
                            sat_gouraud_rgb(0,0,0),sat_gouraud_rgb(0,0,0)};
    for(int k=0;k<5;++k) {
        const int16_t x=(int16_t)NX(0x3F+SEGMENT*k);
        const sat_quad2_t line={{x,x,x,x},{top,top,bottom,bottom}};
        SAT_TRY(sat_draw_quad2_polygon_gouraud(&line,SAT_RGB555(16,16,16),fade));
    }
    const int16_t m=(int16_t)NX(mark);
    const sat_quad2_t marker={{m,m,m,m},{top,top,bottom,bottom}};
    SAT_TRY(sat_draw_quad2_polygon(&marker,SAT_RGB555(31,31,31)));
    /* The pointer: a triangle from POINTER_Y down to the marker's top, blue above and white at the
     * tip, half as wide as twelve times the sine (both divisions truncate). */
    const int s=sine*12/4096/2;
    const sat_quad2_t tri={{(int16_t)NX(mark-s),(int16_t)NX(mark+s),m,m},
                           {(int16_t)NY(POINTER_Y),(int16_t)NY(POINTER_Y),top,top}};
    const uint16_t shade[4]={sat_gouraud_rgb(-16,0,15),sat_gouraud_rgb(-16,0,15),
                             sat_gouraud_rgb(15,15,15),sat_gouraud_rgb(15,15,15)};
    return sat_draw_quad2_polygon_gouraud(&tri,SAT_RGB555(16,16,16),shade);
}
