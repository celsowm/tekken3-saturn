/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Uses particle texture conventions from pinned Tekken3Remake effects
 * research. Diagnostic nearest sampling is not an original GPU port. */
#include "psx_particle_tile.h"
#include "psx_viewport.h"
#define map_x t3_psx_viewport_x
int t3_particle_screen_bounds(const t3_psx_particle_quad_t* q,t3_particle_bounds_t* b) {
    if(!q || !b || !q->visible)return 0;
    int left=map_x(q->xy[0][0]),right=left,top=t3_psx_viewport_y(q->xy[0][1]),bottom=top;
    for(unsigned i=1;i<4;++i) {
        int x=map_x(q->xy[i][0]),y=t3_psx_viewport_y(q->xy[i][1]);
        if(x<left)left=x;
        if(x>right)right=x;
        if(y<top)top=y;
        if(y>bottom)bottom=y;
    }
    /* One pixel of slack covers endpoint rounding in the diagnostic mapper. */
    --left;--top;++right;++bottom;
    if(right<0 || bottom<0 || left>319 || top>223)return 0;
    if(left<0)left=0;
    if(top<0)top=0;
    if(right>319)right=319;
    if(bottom>223)bottom=223;
    *b=(t3_particle_bounds_t){(int16_t)left,(int16_t)top,(int16_t)right,(int16_t)bottom};
    return 1;
}
int t3_particle_bounds_intersect(const t3_particle_bounds_t* a,const t3_particle_bounds_t* b) {
    return a && b && a->left<=b->right && b->left<=a->right &&
           a->top<=b->bottom && b->top<=a->bottom;
}
int t3_particle_tile_probe(t3_particle_tile_probe_t* out,const t3_psx_particle_quad_t* q,
                           const uint8_t pattern[128],const uint16_t palette[16]) {
    if(!out || !q || !pattern || !palette || !q->visible)return 0;
    if(q->xy[0][1]!=q->xy[1][1] || q->xy[2][1]!=q->xy[3][1] ||
       q->xy[0][0]!=q->xy[2][0] || q->xy[1][0]!=q->xy[3][0])return 0;
    const int left=map_x(q->xy[0][0]),right=map_x(q->xy[1][0]);
    const int top=t3_psx_viewport_y(q->xy[0][1]),bottom=t3_psx_viewport_y(q->xy[2][1]);
    const int width=right-left,height=bottom-top;
    if(width<=0 || height<=0 || width>32 || height>32)return 0;
    const int x0=left<0?0:left,y0=top<0?0:top,x1=right>320?320:right,y1=bottom>224?224:bottom;
    if(x0>=x1 || y0>=y1){out->count=0;return 1;}
    const int tx0=x0/8,ty0=y0/8,tx1=(x1-1)/8,ty1=(y1-1)/8;
    const unsigned count=(unsigned)((tx1-tx0+1)*(ty1-ty0+1));
    if(count>T3_PARTICLE_PROBE_MAX_TILES)return 0;
    unsigned tile=0;
    for(int ty=ty0;ty<=ty1;++ty)for(int tx=tx0;tx<=tx1;++tx) {
        out->tile_x[tile]=(uint8_t)tx;out->tile_y[tile]=(uint8_t)ty;
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            int px=tx*8+x,py=ty*8+y;uint16_t color=0;
            if(px>=x0 && px<x1 && py>=y0 && py<y1) {
                unsigned u=(unsigned)((px-left)*16/width),v=(unsigned)((py-top)*16/height);
                unsigned at=v*16u+u;uint8_t pair=pattern[at/2u];
                color=palette[(at&1u)?pair&15u:pair>>4u];
            }
            out->pixels[tile][y*8+x]=color?(uint16_t)(0x8000u|(color&0x7fffu)):0;
        }
        ++tile;
    }
    out->count=tile;return 1;
}
static unsigned find_tile(const t3_particle_tile_probe_t* out,unsigned x,unsigned y) {
    for(unsigned i=0;i<out->count;++i)if(out->tile_x[i]==x && out->tile_y[i]==y)return i;
    return out->count;
}
int t3_particle_tile_intersects(const t3_particle_tile_probe_t* out,unsigned tile,
                                int left,int top,int right,int bottom) {
    if(!out || out->count>T3_PARTICLE_PROBE_MAX_TILES || tile>=out->count || left>right || top>bottom)return 0;
    const int ox=(int)out->tile_x[tile]*8,oy=(int)out->tile_y[tile]*8;
    for(int y=0;y<8;++y) {
        if(oy+y<top || oy+y>bottom)continue;
        for(int x=0;x<8;++x)
            if(ox+x>=left && ox+x<=right && out->pixels[tile][y*8+x])return 1;
    }
    return 0;
}
int t3_particle_tile_occlude_index(t3_particle_tile_probe_t* out,unsigned tile,
                                   const uint8_t coverage[8]) {
    if(!out || !coverage || out->count>T3_PARTICLE_PROBE_MAX_TILES || tile>=out->count)return 0;
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
        if(coverage[y]&(0x80u>>x))out->pixels[tile][y*8u+x]=0;
    return 1;
}
int t3_particle_tile_occlude(t3_particle_tile_probe_t* out,unsigned tile_x,unsigned tile_y,
                             const uint8_t coverage[8]) {
    if(!out || !coverage || out->count>T3_PARTICLE_PROBE_MAX_TILES || tile_x>=40u || tile_y>=28u)return 0;
    unsigned tile=find_tile(out,tile_x,tile_y);
    return tile==out->count?1:t3_particle_tile_occlude_index(out,tile,coverage);
}
static uint16_t add_rgb(uint16_t a,uint16_t b) {
    unsigned r=(a&31u)+(b&31u),g=((a>>5)&31u)+((b>>5)&31u),z=((a>>10)&31u)+((b>>10)&31u);
    if(r>31)r=31;
    if(g>31)g=31;
    if(z>31)z=31;
    return (uint16_t)(0x8000u|(z<<10)|(g<<5)|r);
}
#if defined(T3_PARTICLE_BOX_FILTER)
/* Area average of the palette colours over a pattern's footprint (transparent texels count
 * as black), the way a downscaled PS1 frame averages the sprite's texels. */
static uint16_t box_color(const uint8_t pattern[128],const uint16_t palette[16],
                          unsigned u0,unsigned u1,unsigned v0,unsigned v1) {
    if(u1<=u0)u1=u0+1u;
    if(v1<=v0)v1=v0+1u;
    unsigned r=0,g=0,b=0;const unsigned n=(u1-u0)*(v1-v0);
    for(unsigned v=v0;v<v1 && v<16u;++v)for(unsigned u=u0;u<u1 && u<16u;++u) {
        const unsigned at=v*16u+u;const uint8_t pair=pattern[at/2u];
        const uint16_t c=palette[(at&1u)?pair&15u:pair>>4u];
        if(c){r+=c&31u;g+=(c>>5)&31u;b+=(c>>10)&31u;}
    }
    r=(r+n/2u)/n;g=(g+n/2u)/n;b=(b+n/2u)/n;
    return (r|g|b)?(uint16_t)(0x8000u|(b<<10)|(g<<5)|r):0u;
}
static unsigned map_hi(const uint8_t map[32],int index,int size) {
    return index+1<size?map[index+1]:16u;
}
#endif
static int tile_has_color(int tx,int ty,int x0,int y0,int x1,int y1,
                          int left,int top,int width,int height,const uint8_t u_map[32],const uint8_t v_map[32],
                          const uint8_t pattern[128],const uint16_t palette[16]) {
    int start_x=tx*8>x0?tx*8:x0,end_x=tx*8+8<x1?tx*8+8:x1;
    int start_y=ty*8>y0?ty*8:y0,end_y=ty*8+8<y1?ty*8+8:y1;
    for(int y=start_y;y<end_y;++y)for(int x=start_x;x<end_x;++x) {
#if defined(T3_PARTICLE_BOX_FILTER)
        if(box_color(pattern,palette,u_map[x-left],map_hi(u_map,x-left,width),
                     v_map[y-top],map_hi(v_map,y-top,height)))return 1;
#else
        unsigned u=u_map[x-left],v=v_map[y-top],at=v*16u+u;
        uint8_t pair=pattern[at/2u];
        if(palette[(at&1u)?pair&15u:pair>>4u])return 1;
#endif
    }
    return 0;
}
/* floor(16*i/size), for i in [0,size). The bounded accumulator avoids
 * division for every sampled pixel on SH-2. */
static void particle_sample_map(uint8_t map[32],unsigned size) {
    unsigned value=0,remainder=0;
    for(unsigned i=0;i<size;++i) {
        map[i]=(uint8_t)value;remainder+=16u;
        while(remainder>=size){remainder-=size;++value;}
    }
}
static int tile_nonempty(const t3_particle_tile_probe_t* out,unsigned tile) {
    for(unsigned i=0;i<64;++i)if(out->pixels[tile][i])return 1;
    return 0;
}
t3_particle_tile_status_t t3_particle_tile_accumulate_status(t3_particle_tile_probe_t* out,const t3_psx_particle_quad_t* q,
                                const uint8_t pattern[128],const uint16_t palette[16]) {
    if(!out || !q || !pattern || !palette || !q->visible || out->count>T3_PARTICLE_PROBE_MAX_TILES)return 0;
    if(q->xy[0][1]!=q->xy[1][1] || q->xy[2][1]!=q->xy[3][1] ||
       q->xy[0][0]!=q->xy[2][0] || q->xy[1][0]!=q->xy[3][0])return 0;
    for(unsigned i=0;i<16;++i)if(palette[i] && !(palette[i]&0x8000u))return 0;
    const int left=map_x(q->xy[0][0]),right=map_x(q->xy[1][0]);
    const int top=t3_psx_viewport_y(q->xy[0][1]),bottom=t3_psx_viewport_y(q->xy[2][1]);
    const int width=right-left,height=bottom-top;
    if(width<=0 || height<=0 || width>32 || height>32)return 0;
    const int x0=left<0?0:left,y0=top<0?0:top,x1=right>320?320:right,y1=bottom>224?224:bottom;
    if(x0>=x1 || y0>=y1)return 1;
    uint8_t u_map[32],v_map[32];
    particle_sample_map(u_map,(unsigned)width);particle_sample_map(v_map,(unsigned)height);
    const int tx0=x0/8,ty0=y0/8,tx1=(x1-1)/8,ty1=(y1-1)/8;
    /* A <=32px sprite intersects at most 5x5 cells. Cache whether a cell
     * contributes color across capacity preflight, compaction and drawing. */
    uint8_t color_cache[25];for(unsigned i=0;i<25;++i)color_cache[i]=2;
    uint8_t tile_cache[25];int compacted=0;
    unsigned needed=0;
    for(int ty=ty0;ty<=ty1;++ty)for(int tx=tx0;tx<=tx1;++tx) {
        const unsigned at=(unsigned)(ty-ty0)*5u+(unsigned)(tx-tx0);
        const unsigned tile=find_tile(out,(unsigned)tx,(unsigned)ty);
        tile_cache[at]=tile==out->count?255u:(uint8_t)tile;
        if(tile==out->count) {
            color_cache[at]=(uint8_t)tile_has_color(tx,ty,x0,y0,x1,y1,left,top,width,height,u_map,v_map,pattern,palette);
            needed+=color_cache[at];
        }
    }
    if(needed>T3_PARTICLE_PROBE_MAX_TILES-out->count) {
        /* First prove that compaction will fit, without changing the canvas.
         * An empty existing cell can itself be required by this sprite. */
        unsigned live=0,new_needed=0;
        for(unsigned i=0;i<out->count;++i)live+=(unsigned)tile_nonempty(out,i);
        for(int ty=ty0;ty<=ty1;++ty)for(int tx=tx0;tx<=tx1;++tx) {
            unsigned tile=find_tile(out,(unsigned)tx,(unsigned)ty);
            if(tile==out->count || !tile_nonempty(out,tile)) {
                const unsigned at=(unsigned)(ty-ty0)*5u+(unsigned)(tx-tx0);
                if(color_cache[at]==2)color_cache[at]=(uint8_t)tile_has_color(tx,ty,x0,y0,x1,y1,left,top,width,height,u_map,v_map,pattern,palette);
                new_needed+=color_cache[at];
            }
        }
        if(new_needed>T3_PARTICLE_PROBE_MAX_TILES-live)return T3_PARTICLE_TILE_CAPACITY;
        unsigned dest=0;
        for(unsigned i=0;i<out->count;++i)if(tile_nonempty(out,i)) {
            if(i!=dest) {
                out->tile_x[dest]=out->tile_x[i];out->tile_y[dest]=out->tile_y[i];
                for(unsigned p=0;p<64;++p)out->pixels[dest][p]=out->pixels[i][p];
            }
            ++dest;
        }
        out->count=dest;
        compacted=1;
    }
    for(int ty=ty0;ty<=ty1;++ty)for(int tx=tx0;tx<=tx1;++tx) {
        const unsigned at=(unsigned)(ty-ty0)*5u+(unsigned)(tx-tx0);
        unsigned tile=compacted?find_tile(out,(unsigned)tx,(unsigned)ty):
            tile_cache[at]==255u?out->count:tile_cache[at];
        if(tile==out->count) {
            if(color_cache[at]==2)color_cache[at]=(uint8_t)tile_has_color(tx,ty,x0,y0,x1,y1,left,top,width,height,u_map,v_map,pattern,palette);
            if(!color_cache[at])continue;
            out->tile_x[tile]=(uint8_t)tx;out->tile_y[tile]=(uint8_t)ty;
            for(unsigned i=0;i<64;++i)out->pixels[tile][i]=0;
            ++out->count;
        }
        for(int y=0;y<8;++y)for(int x=0;x<8;++x) {
            const int px=tx*8+x,py=ty*8+y;
            if(px<x0 || px>=x1 || py<y0 || py>=y1)continue;
#if defined(T3_PARTICLE_BOX_FILTER)
            const uint16_t color=box_color(pattern,palette,u_map[px-left],map_hi(u_map,px-left,width),
                                           v_map[py-top],map_hi(v_map,py-top,height));
#else
            const unsigned u=u_map[px-left],v=v_map[py-top],at=v*16u+u;
            const uint8_t pair=pattern[at/2u];const uint16_t color=palette[(at&1u)?pair&15u:pair>>4u];
#endif
            if(color)out->pixels[tile][y*8+x]=add_rgb(out->pixels[tile][y*8+x],color);
        }
    }
    return 1;
}
int t3_particle_tile_accumulate(t3_particle_tile_probe_t* out,const t3_psx_particle_quad_t* q,
                                const uint8_t pattern[128],const uint16_t palette[16]) {
    return t3_particle_tile_accumulate_status(out,q,pattern,palette)==T3_PARTICLE_TILE_OK;
}
