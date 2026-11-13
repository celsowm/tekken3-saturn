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

/* FHUD.BIN's HUD pieces (extract_force_hud.py), its glyphs from GLYPH on, then the keys, the name and
 * the two composed labels. */
enum { CAP_LEFT=0, CAP_RIGHT, HEALTH, DAMAGE, EMPTY, PLATE, DIGIT=6, GLYPH=16, MAX_PIECES=128,
       KEY=MAX_PIECES, NAME=KEY+3, LABEL_SCORE, LABEL_HI, SEPARATORS, TEXTURES, FONTS=3, COLOURS=16 };
/* print_text's fonts (hud.md): glyph width, height and advance. */
static const struct {uint8_t width,height,advance;} FONT[FONTS]={{10,16,9},{14,24,13},{24,40,22}};
static sat_vdp1_texture_t g_tex[TEXTURES];
static uint8_t g_glyph[FONTS][96];          /* the piece of each character from ' ', 0 for none */
static uint16_t g_palette[FONTS][COLOURS];  /* the text colours FHUD.BIN carries */
static uint16_t g_palettes[FONTS];          /* which of them it carries (bits) */
static uint16_t g_name_width;
/* The mode line's two labels (font 0), each composed once into one texture: one sprite, not one per
 * letter. */
static const char* const LABEL_TEXT[2]={"SCORE","HI-SCORE"};
static uint16_t g_label_width[2];
static sat_result_t make_separators(uint8_t* scratch,unsigned bytes);

static unsigned le16(const uint8_t* p) {return (unsigned)p[0]|(unsigned)p[1]<<8;}
static unsigned be16(const uint8_t* p) {return (unsigned)p[0]<<8|(unsigned)p[1];}

static unsigned glyph_of(unsigned font,char c) {
    const unsigned k=(unsigned char)c;
    return font<FONTS && k>=32u && k<128u?g_glyph[font][k-32u]:0u;
}

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

/* Lays `text`'s font-0 glyphs an advance apart into `out` (padded texels wide, LUT4, left pixel in the
 * high nibble); a later glyph's opaque texels win where two overlap. */
static void compose(const char* text,const uint8_t* const* texels,const uint16_t* pitch,
                    uint8_t* out,unsigned padded) {
    const unsigned w=FONT[0].width,h=FONT[0].height;
    for(unsigned i=0;i<padded*h/2u;++i)out[i]=0;
    for(unsigned pen=0;*text;++text,pen+=FONT[0].advance) {
        const unsigned g=glyph_of(0,*text);
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x) {
            const uint8_t b=texels[g][y*pitch[g]/2u+x/2u];
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
    const uint8_t* texels[MAX_PIECES];
    uint16_t pitch[MAX_PIECES];
    SAT_TRY(read_file("FHUD.BIN",data,bytes,&size));
    static const uint8_t magic[8]={'T','3','F','H','U','D','2',0};
    for(unsigned j=0;j<8;++j)if(size<12u || data[j]!=magic[j])return SAT_ERR_VERSION;
    const unsigned pieces=le16(data+8),palettes=le16(data+10);
    if(pieces<=GLYPH || pieces>MAX_PIECES)return SAT_ERR_VERSION;
    __builtin_memset(g_glyph,0,sizeof g_glyph);
    g_palettes[0]=g_palettes[1]=g_palettes[2]=0;
    uint32_t at=12;
    for(unsigned i=0;i<pieces;++i) {
        if(at+44u>size)return SAT_ERR_VERSION;
        const unsigned padded=le16(data+at+2),height=le16(data+at+4),count=le16(data+at+6);
        const unsigned font=data[at+8],c=data[at+9];
        if(!padded || padded&7u || count!=padded*height/2u || at+44u+count>size)return SAT_ERR_VERSION;
        if((i<GLYPH)!=(font==0xFFu))return SAT_ERR_VERSION;
        if(i>=GLYPH) {
            if(font>=FONTS || c<33u || c>=128u || height!=FONT[font].height || padded<FONT[font].width)
                return SAT_ERR_VERSION;
            g_glyph[font][c-32u]=(uint8_t)i;
        }
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(data+at+12u+2u*j);
        texels[i]=data+at+44u;
        pitch[i]=(uint16_t)padded;
        SAT_TRY(upload(&g_tex[i],lut,texels[i],padded,height));
        at+=44u+count;
    }
    for(unsigned i=0;i<palettes;++i,at+=36u) {
        if(at+36u>size)return SAT_ERR_VERSION;
        const unsigned font=data[at],colour=data[at+1];
        if(font>=FONTS || colour>=COLOURS)return SAT_ERR_VERSION;
        for(unsigned j=0;j<16;++j)lut[j]=(uint16_t)le16(data+at+4u+2u*j);
        SAT_TRY(sat_vdp1_upload_lut(lut,&g_palette[font][colour]));
        g_palettes[font]|=(uint16_t)(1u<<colour);
    }
    if(at!=size)return SAT_ERR_VERSION;
    for(unsigned l=0;l<2u;++l) {
        unsigned n=0;
        for(;LABEL_TEXT[l][n];++n)if(!glyph_of(0,LABEL_TEXT[l][n]))return SAT_ERR_VERSION;
        const unsigned padded=(FONT[0].advance*(n-1u)+FONT[0].width+7u)&~7u;
        if(size+padded*FONT[0].height/2u>bytes)return SAT_ERR_CAPACITY;
        compose(LABEL_TEXT[l],texels,pitch,data+size,padded);
        SAT_TRY(upload(&g_tex[LABEL_SCORE+l],lut,data+size,padded,FONT[0].height));
        g_label_width[l]=(uint16_t)padded;
    }
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
    SAT_TRY(upload(&g_tex[NAME],lut,data+40u,padded,height));
    return make_separators(data,bytes);
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

/* print_text (FUN_8004D15C) of an already formatted line: a space advances the pen; a character
 * without a glyph or a colour FHUD.BIN lacks draws nothing. Returns the pen after the line. */
static int print(unsigned font,unsigned colour,int pen,int y,const char* s,sat_result_t* st) {
    const int w=FONT[font].width,h=FONT[font].height;
    const int known=colour<COLOURS && (g_palettes[font]>>colour&1u);
    for(;*s;++s,pen+=FONT[font].advance) {
        const unsigned g=glyph_of(font,*s);
        if(g && known && *st==SAT_OK)*st=draw(g,pen,y,pen+w,y+h,g_palette[font][colour]);
    }
    return pen;
}

/* A %D or %d value into `out` (at least 10 bytes): with `width` and a shorter value, spaces before it
 * (they advance the pen) or zeros with `zeros`; a longer value keeps its last `width` digits. Values
 * above 99,999,999 print 99999999. */
static const char* format(char* out,uint32_t value,unsigned width,int zeros) {
    if(value>99999999u)value=99999999u;
    char* p=out+9;
    *p=0;
    unsigned n=0;
    do {*--p=(char)('0'+value%10u);value/=10u;++n;} while(value);
    if(width && n>width)return out+9-width;
    for(;width && n<width;++n)*--p=zeros?'0':' ';
    return p;
}

sat_result_t t3_force_hud_scores(uint32_t score,uint32_t hiscore) {
    /* ModeCaption mode 10: "%c%f%H%VSCORE %c%8D" at 19 and the same for HI-SCORE at 208, y 24, the
     * labels composed (colours 5 and 2), the values in colour 6. */
    char buffer[10];
    sat_result_t st=SAT_OK;
    SAT_TRY(draw(LABEL_SCORE,19,24,19+g_label_width[0],24+FONT[0].height,g_palette[0][5]));
    print(0,6,19+6*FONT[0].advance,24,format(buffer,score,8,0),&st);
    SAT_TRY(st);
    SAT_TRY(draw(LABEL_HI,208,24,208+g_label_width[1],24+FONT[0].height,g_palette[0][2]));
    print(0,6,208+9*FONT[0].advance,24,format(buffer,hiscore,8,0),&st);
    return st;
}

sat_result_t t3_force_hud_life_up(int x,int y,int32_t clock) {
    sat_result_t st=SAT_OK;
    print(0,clock&2?2u:5u,x,y,"LIFE UP!",&st);
    return st;
}

sat_result_t t3_force_hud_seconds(int x,int y,unsigned seconds) {
    /* "+" (font 0, colour 5) 4 below, the number (font 1, colour 5), "SEC" (font 0, colour 6) 6 below. */
    char buffer[10];
    sat_result_t st=SAT_OK;
    x=print(0,5,x,y+4,"+",&st);
    x=print(1,5,x,y,format(buffer,seconds,0,0),&st);
    print(0,6,x,y+6,"SEC",&st);
    return st;
}

sat_result_t t3_force_hud_loading(void) {
    sat_result_t st=SAT_OK;
    print(0,6,0xF2,0x1A6,"NOW LOADING...",&st);
    return st;
}

sat_result_t t3_force_hud_bonus_left(int x,int y,int32_t frames) {
    /* "%2D\"%02D": seconds and hundredths (FUN_800B1778 state 6, colour 5). */
    char buffer[10];
    sat_result_t st=SAT_OK;
    const int32_t seconds=frames/60%60,hundredths=(int32_t)((int64_t)frames*100/60%100);
    x=print(0,5,x,y,format(buffer,(uint32_t)seconds,2,0),&st);
    x=print(0,5,x,y,"\"",&st);
    print(0,5,x,y,format(buffer,(uint32_t)hundredths,2,1),&st);
    return st;
}

sat_result_t t3_force_hud_stage_clear(void) {
    sat_result_t st=SAT_OK;
    print(2,8,41,112,"STAGE CLEAR!!",&st);
    return st;
}

/* m'ss"cc of a frame count, as tekken_force's tally prints it. */
static int clock_text(unsigned colour,int x,int y,int32_t t,int32_t cs,sat_result_t* st) {
    char buffer[10];
    x=print(1,colour,x,y,format(buffer,(uint32_t)(t/3600%60),2,1),st);
    x=print(1,colour,x,y,"'",st);
    x=print(1,colour,x,y,format(buffer,(uint32_t)(t/60-60*(t/3600)),2,1),st);
    x=print(1,colour,x,y,"\"",st);
    return print(1,colour,x,y,format(buffer,(uint32_t)cs,2,1),st);
}

sat_result_t t3_force_hud_tally(const t3_force_hud_tally_t* t) {
    /* FUN_800B1778 state 7 (font 1, x 0x2F): the lines 30 frames apart at y 0xB4, 0xE8, 0x11C, 0x150. */
    char buffer[10];
    sat_result_t st=SAT_OK;
    int x;
    if(t->frame>=0x1E) {
        x=print(1,4,0x2F,0xB4,"STAGE BONUS : ",&st);
        print(1,4,x,0xB4,format(buffer,t->stage_bonus,5,0),&st);
    }
    if(t->frame>=0x3C) {
        x=print(1,1,0x2F,0xE8,"CLEAR BONUS : ",&st);
        print(1,1,x,0xE8,format(buffer,t->clear_bonus,5,0),&st);
    }
    int32_t cs=0;
    if(t->frame>=0x5A) {
        cs=(int32_t)((int64_t)t->clear_time*100/60%100);
        x=print(1,5,0x2F,0x11C,"CLEAR TIME : ",&st);
        clock_text(5,x,0x11C,t->clear_time,cs,&st);
    }
    if(t->frame<0x78)return st;
    switch(t->last) {
    case T3_FORCE_TALLY_BONUS_TIME: {
        /* The hundredths add up with the clear time's to a whole second. */
        int32_t c=(int32_t)((int64_t)t->bonus_time*100/60%100);
        if(cs+c<100)c=100-cs;
        x=print(1,6,0x2F,0x150,"BONUS TIME : ",&st);
        clock_text(6,x,0x150,t->bonus_time,c,&st);
        break;}
    case T3_FORCE_TALLY_KEY:
        print(1,t->frame&2?6u:2u,0x56,0x150,"YOU GOT A KEY!!",&st);
        break;
    case T3_FORCE_TALLY_FINAL:
        print(1,t->frame&4?6u:2u,0x22,0x150,"CHALLENGE FINAL STAGE!!",&st);
        break;
    default:break;
    }
    return st;
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

/* The five separators (LINE_G2, white at the top to grey) never move: one sprite of them, its rows
 * shading through LUT entries 1 to `height`. */
static sat_result_t make_separators(uint8_t* scratch,unsigned bytes) {
    const unsigned left=(unsigned)X(0x3F),height=(unsigned)(Y(BAR_BOTTOM)-Y(BAR_TOP));
    const unsigned padded=((unsigned)X(0x3F+4*SEGMENT)-left+1u+7u)&~7u;
    if(height<2u || height>15u || padded*height/2u>bytes)return SAT_ERR_CAPACITY;
    uint16_t lut[16]={0};
    for(unsigned r=0;r<height;++r) {
        const unsigned v=31u-15u*r/(height-1u);
        lut[1u+r]=SAT_RGB555(v,v,v);
    }
    for(unsigned i=0;i<padded*height/2u;++i)scratch[i]=0;
    for(int k=0;k<5;++k) {
        const unsigned x=(unsigned)X(0x3F+SEGMENT*k)-left;
        for(unsigned r=0;r<height;++r) {
            uint8_t* o=&scratch[r*padded/2u+x/2u];
            *o=(uint8_t)(x&1u?(*o&0xF0u)|(1u+r):(*o&0x0Fu)|(1u+r)<<4);
        }
    }
    return upload(&g_tex[SEPARATORS],lut,scratch,padded,height);
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
    const sat_vdp1_texture_t* lines=&g_tex[SEPARATORS];
    SAT_TRY(sat_draw_sprite_scaled_screen(lines,(int16_t)(X(0x3F)+lines->width/2),
        (int16_t)(Y(BAR_TOP)+lines->height/2),lines->width,lines->height,0));
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
