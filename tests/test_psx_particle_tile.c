#include "psx_particle_tile.h"
#include "psx_face_coverage.h"
#include "psx_viewport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int mx(int x){int n=x*20;return (n+(n<0?-11:11))/23;}
static uint16_t composite(uint16_t a,uint16_t b) {
    unsigned r=(a&31u)+(b&31u),g=((a>>5)&31u)+((b>>5)&31u),z=((a>>10)&31u)+((b>>10)&31u);
    return (uint16_t)((r>31?31:r)|((g>31?31:g)<<5)|((z>31?31:z)<<10));
}
static void check_layered_mask(void) {
    uint32_t rng=0x5ad193u;
    t3_psx_particle_quad_t q={0};q.visible=1;
    q.xy[0][0]=q.xy[2][0]=92;q.xy[1][0]=q.xy[3][0]=101;
    q.xy[0][1]=q.xy[1][1]=20;q.xy[2][1]=q.xy[3][1]=36;
    for(unsigned trial=0;trial<100;++trial) {
        t3_particle_tile_probe_t overlay={0};uint16_t base[64],expected[64];
        for(unsigned i=0;i<64;++i){rng=rng*1664525u+1013904223u;base[i]=expected[i]=(uint16_t)((rng>>8)&0x7fffu);}
        for(unsigned event=0;event<30;++event) {
            rng=rng*1664525u+1013904223u;
            if(rng&1u) {
                uint8_t mask[8];uint16_t fighter=(uint16_t)((rng>>8)&0x7fffu);
                for(unsigned y=0;y<8;++y){rng=rng*1664525u+1013904223u;mask[y]=(uint8_t)(rng>>24);}
                assert(t3_particle_tile_occlude(&overlay,10,0,mask));
                for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x)
                    if(mask[y]&(0x80u>>x))base[y*8+x]=expected[y*8+x]=fighter;
            } else {
                uint8_t texture[128];uint16_t palette[16]={0};memset(texture,0x11,sizeof texture);
                uint16_t color=(uint16_t)((rng>>8)&0x7fffu);palette[1]=color|0x8000u;
                assert(t3_particle_tile_accumulate(&overlay,&q,texture,palette));
                for(unsigned i=0;i<64;++i)expected[i]=composite(expected[i],color);
            }
        }
        for(unsigned i=0;i<64;++i) {
            uint16_t contribution=overlay.count?overlay.pixels[0][i]:0;
            assert(composite(base[i],contribution)==expected[i]);
        }
    }
}
static void check_accumulate_sampling_sizes(void) {
    uint8_t pattern[128];uint16_t palette[16];
    for(unsigned i=0;i<128;++i)pattern[i]=(uint8_t)(i*37u+11u);
    for(unsigned i=0;i<16;++i)palette[i]=(uint16_t)(0x8000u+i*67u);
    for(int w=2;w<=36;++w)for(int h=1;h<=32;++h) {
        t3_psx_particle_quad_t q={0};q.visible=1;
        q.xy[0][0]=q.xy[2][0]=92;q.xy[1][0]=q.xy[3][0]=(int16_t)(92+w);
        q.xy[0][1]=q.xy[1][1]=20;q.xy[2][1]=q.xy[3][1]=(int16_t)(20+2*h);
        t3_particle_tile_probe_t direct={0},accumulated={0};
        assert(t3_particle_tile_probe(&direct,&q,pattern,palette));
        assert(t3_particle_tile_accumulate(&accumulated,&q,pattern,palette));
        assert(direct.count==accumulated.count);
        for(unsigned i=0;i<direct.count;++i) {
            assert(direct.tile_x[i]==accumulated.tile_x[i] && direct.tile_y[i]==accumulated.tile_y[i]);
            assert(!memcmp(direct.pixels[i],accumulated.pixels[i],sizeof direct.pixels[i]));
        }
    }
}
/* Compare many overlapping sprites with the independent single-sprite
 * division-based renderer, including clipped and transparent texels. */
static void check_multi_sprite_reference(void) {
    static uint16_t reference[224][320];
    uint32_t rng=0xa58123u;
    for(unsigned trial=0;trial<12;++trial) {
        memset(reference,0,sizeof reference);
        t3_particle_tile_probe_t accumulated={0};
        const int origins[4][2]={{92,120},{-4,20},{345,440},{90,20}};
        const int ox=origins[trial%4][0],oy=origins[trial%4][1];
        for(unsigned item=0;item<16;++item) {
            uint8_t texture[128];uint16_t palette[16];
            for(unsigned i=0;i<128;++i){rng=rng*1664525u+1013904223u;texture[i]=(uint8_t)(rng>>24);}
            for(unsigned i=0;i<16;++i){rng=rng*1664525u+1013904223u;palette[i]=(rng&3u)?(uint16_t)(0x8000u|((rng>>8)&0x7fffu)):0;}
            rng=rng*1664525u+1013904223u;int left=ox+(int)((rng>>16)%12u);
            rng=rng*1664525u+1013904223u;int top=oy+2*(int)((rng>>16)%12u);
            rng=rng*1664525u+1013904223u;int width=2+(int)((rng>>16)%25u);
            rng=rng*1664525u+1013904223u;int height=1+(int)((rng>>16)%24u);
            t3_psx_particle_quad_t q={0};q.visible=1;
            q.xy[0][0]=q.xy[2][0]=(int16_t)left;q.xy[1][0]=q.xy[3][0]=(int16_t)(left+width);
            q.xy[0][1]=q.xy[1][1]=(int16_t)top;q.xy[2][1]=q.xy[3][1]=(int16_t)(top+height*2);
            t3_particle_tile_probe_t single={0};
            assert(t3_particle_tile_probe(&single,&q,texture,palette));
            assert(t3_particle_tile_accumulate(&accumulated,&q,texture,palette));
            for(unsigned t=0;t<single.count;++t)for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
                unsigned px=single.tile_x[t]*8u+x,py=single.tile_y[t]*8u+y;
                uint16_t color=single.pixels[t][y*8+x];
                if(color)reference[py][px]=(uint16_t)(0x8000u|composite(reference[py][px],color));
            }
        }
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x) {
            uint16_t actual=0;
            for(unsigned t=0;t<accumulated.count;++t)
                if(accumulated.tile_x[t]==x/8 && accumulated.tile_y[t]==y/8) {
                    actual=accumulated.pixels[t][(y%8)*8+x%8];break;
                }
            assert(actual==reference[y][x]);
        }
    }
}
static void check(int left,int top,int right,int bottom) {
    uint8_t pattern[128];uint16_t palette[16];t3_particle_tile_probe_t out;
    for(unsigned i=0;i<128;++i)pattern[i]=(uint8_t)(i*37u+11u);
    for(unsigned i=0;i<16;++i)palette[i]=(uint16_t)(i==0?0:i==1?0x8000:0x8000+i*67u);
    t3_psx_particle_quad_t q={0};q.visible=1;
    q.xy[0][0]=q.xy[2][0]=(int16_t)left;q.xy[1][0]=q.xy[3][0]=(int16_t)right;
    q.xy[0][1]=q.xy[1][1]=(int16_t)top;q.xy[2][1]=q.xy[3][1]=(int16_t)bottom;
    assert(t3_particle_tile_probe(&out,&q,pattern,palette));
    t3_particle_bounds_t bounds;int has_bounds=t3_particle_screen_bounds(&q,&bounds);
    int l=mx(left),r=mx(right),t=(top-20)/2,b=(bottom-20)/2;
    unsigned count=0;
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x) {
        uint16_t expected=0,actual=0;
        if((int)x>=l && (int)x<r && (int)y>=t && (int)y<b) {
            unsigned u=((int)x-l)*16/(r-l),v=((int)y-t)*16/(b-t),at=v*16+u;
            unsigned idx=(at&1)?pattern[at/2]&15:pattern[at/2]>>4;
            unsigned color=palette[idx];expected=color?(uint16_t)(0x8000|(color&0x7fff)):0;
        }
        for(unsigned i=0;i<out.count;++i)
            if(out.tile_x[i]==x/8 && out.tile_y[i]==y/8) {actual=out.pixels[i][(y%8)*8+x%8];++count;break;}
        assert(actual==expected);
        if(expected)assert(has_bounds && (int)x>=bounds.left && (int)x<=bounds.right &&
                           (int)y>=bounds.top && (int)y<=bounds.bottom);
    }
    assert(count==out.count*64u);
}
int main(void) {
    assert(t3_psx_viewport_x(0)==0 && t3_psx_viewport_x(192)==167 && t3_psx_viewport_x(368)==320);
    assert(t3_psx_viewport_y(20)==0 && t3_psx_viewport_y(240)==110 && t3_psx_viewport_y(468)==224);
    uint8_t patch[64]={0},coverage[8]={0};patch[0]=1;patch[7]=255;patch[12]=8;
    assert(t3_face_index8_coverage(patch,0,coverage));
    assert(coverage[0]==0x81 && coverage[1]==0x08);
    assert(t3_face_index8_coverage(patch,1,coverage));
    for(unsigned i=0;i<8;++i)assert(coverage[i]==0xff);
    assert(!t3_face_index8_coverage(patch,2,coverage));
    for(unsigned i=0;i<8;++i)assert(coverage[i]==0xff);
    assert(!t3_face_rect_tile_coverage(patch,0,4,4,3,3,0,0,coverage));
    for(unsigned i=0;i<8;++i)assert(coverage[i]==0xff);
    int16_t invalid_x[]={0,9,20,20},invalid_y[]={0,0,2,2};
    assert(!t3_face_triangle_tile_coverage(patch,0,invalid_x,invalid_y,0,0,coverage));
    for(unsigned i=0;i<8;++i)assert(coverage[i]==0xff);
    assert(!t3_face_parallelogram_tile_coverage(patch,0,invalid_x,invalid_y,0,0,coverage));
    for(unsigned i=0;i<8;++i)assert(coverage[i]==0xff);
    assert(!t3_face_quad_tile_coverage(patch,0,invalid_x,invalid_y,0,0,coverage));
    for(unsigned i=0;i<8;++i)assert(coverage[i]==0xff);
    t3_particle_bounds_t a={0,0,10,10},b={10,10,20,20},c={11,11,20,20};
    assert(t3_particle_bounds_intersect(&a,&b));
    assert(!t3_particle_bounds_intersect(&a,&c));
    assert(!t3_particle_bounds_intersect(0,&c));
    check_layered_mask();
    check_accumulate_sampling_sizes();
    check_multi_sprite_reference();
    check(92,20,110,52);check(-12,10,12,42);check(352,438,374,476);
    check(99,91,135,155);check(370,20,380,40);
    t3_particle_tile_probe_t out,before;memset(&out,0x5a,sizeof out);before=out;
    t3_psx_particle_quad_t q={0};q.visible=1;q.xy[1][0]=q.xy[3][0]=100;q.xy[2][1]=q.xy[3][1]=80;
    uint8_t tex[128]={0};uint16_t pal[16]={0};
    assert(!t3_particle_tile_probe(&out,&q,tex,pal));assert(!memcmp(&before,&out,sizeof out));
    /* Equal quads exercise saturating overlap, transparent and black texels. */
    t3_particle_tile_probe_t mixed={0};
    t3_psx_particle_quad_t sprite={0};sprite.visible=1;
    sprite.xy[0][0]=sprite.xy[2][0]=92;sprite.xy[1][0]=sprite.xy[3][0]=110;
    sprite.xy[0][1]=sprite.xy[1][1]=20;sprite.xy[2][1]=sprite.xy[3][1]=52;
    memset(tex,0x11,sizeof tex);pal[1]=0x8000u|(20u<<10)|(18u<<5)|12u;
    assert(t3_particle_tile_accumulate(&mixed,&sprite,tex,pal));
    assert(mixed.count==4 && mixed.pixels[0][0]==pal[1]);
    assert(t3_particle_tile_intersects(&mixed,0,80,0,80,0));
    assert(!t3_particle_tile_intersects(&mixed,0,88,0,95,7));
    assert(!t3_particle_tile_intersects(&mixed,mixed.count,80,0,87,7));
    { uint16_t saved=mixed.pixels[0][0];mixed.pixels[0][0]=0;
      assert(!t3_particle_tile_intersects(&mixed,0,80,0,80,0));
      assert(t3_particle_tile_intersects(&mixed,0,80,0,81,0));
      mixed.pixels[0][0]=saved; }
    { uint8_t all[8];memset(all,0xff,sizeof all);
      assert(t3_particle_tile_occlude(&mixed,mixed.tile_x[0],mixed.tile_y[0],all));
      assert(!t3_particle_tile_intersects(&mixed,0,80,0,87,7));
      assert(t3_particle_tile_accumulate(&mixed,&sprite,tex,pal)); }
    assert(t3_particle_tile_accumulate(&mixed,&sprite,tex,pal));
    assert(mixed.count==4 && mixed.pixels[0][0]==(0x8000u|(31u<<10)|(31u<<5)|24u));
    memset(tex,0,sizeof tex);assert(t3_particle_tile_accumulate(&mixed,&sprite,tex,pal));
    assert(mixed.pixels[0][0]==(0x8000u|(31u<<10)|(31u<<5)|24u));
    { t3_particle_tile_probe_t transparent={0};
      assert(t3_particle_tile_accumulate(&transparent,&sprite,tex,pal));
      assert(transparent.count==0);
      /* Only the upper-left texture quadrant contains an opaque color. */
      for(unsigned y=0;y<8;++y)for(unsigned x=0;x<4;++x)tex[y*8+x]=0x11;
      assert(t3_particle_tile_accumulate(&transparent,&sprite,tex,pal));
      assert(transparent.count==1 && transparent.tile_x[0]==10 && transparent.tile_y[0]==0);
      memset(tex,0,sizeof tex); }
    before=mixed;pal[1]=0x1234;assert(!t3_particle_tile_accumulate(&mixed,&sprite,tex,pal));
    assert(!memcmp(&before,&mixed,sizeof mixed));
    uint8_t mask[8]={0xff};before=mixed;
    assert(!t3_particle_tile_occlude(&mixed,40,0,mask));assert(!memcmp(&before,&mixed,sizeof mixed));
    assert(t3_particle_tile_occlude(&mixed,39,27,mask));assert(!memcmp(&before,&mixed,sizeof mixed));
    assert(!t3_particle_tile_occlude_index(&mixed,mixed.count,mask));assert(!memcmp(&before,&mixed,sizeof mixed));
    { t3_particle_tile_probe_t indexed=mixed;
      assert(t3_particle_tile_occlude(&mixed,mixed.tile_x[1],mixed.tile_y[1],mask));
      assert(t3_particle_tile_occlude_index(&indexed,1,mask));
      assert(!memcmp(&mixed,&indexed,sizeof mixed));mixed=before; }
    /* More than 52 simultaneously occupied cells must fit the 80-cell pool. */
    { t3_particle_tile_probe_t large={0};
      for(unsigned i=0;i<52;++i) {
          large.tile_x[i]=(uint8_t)(i%40u);large.tile_y[i]=(uint8_t)(10u+i/40u);
          large.pixels[i][0]=0x8001u;
      }
      large.count=52;
      t3_psx_particle_quad_t extra={0};extra.visible=1;
      extra.xy[0][0]=extra.xy[2][0]=92;extra.xy[1][0]=extra.xy[3][0]=129;
      extra.xy[0][1]=extra.xy[1][1]=20;extra.xy[2][1]=extra.xy[3][1]=84;
      uint8_t solid[128];memset(solid,0x11,sizeof solid);
      uint16_t colors[16]={0};colors[1]=0x8001u;
      assert(t3_particle_tile_accumulate(&large,&extra,solid,colors));
      assert(large.count>52 && large.count<=T3_PARTICLE_PROBE_MAX_TILES);
      for(unsigned i=0;i<52;++i)assert(large.pixels[i][0]==0x8001u);
    }
    pal[1]=0x8001;memset(tex,0x11,sizeof tex);
    mixed.count=T3_PARTICLE_PROBE_MAX_TILES;for(unsigned i=0;i<T3_PARTICLE_PROBE_MAX_TILES;++i){mixed.tile_x[i]=0;mixed.tile_y[i]=0;mixed.pixels[i][0]=0x8000;}
    before=mixed;assert(!t3_particle_tile_accumulate(&mixed,&sprite,tex,pal));
    assert(!memcmp(&before,&mixed,sizeof mixed));
    assert(t3_particle_tile_accumulate_status(&mixed,&sprite,tex,pal)==T3_PARTICLE_TILE_CAPACITY);
    assert(!memcmp(&before,&mixed,sizeof mixed));
    { t3_psx_particle_quad_t invalid=sprite;invalid.xy[3][0]++;
      assert(t3_particle_tile_accumulate_status(&mixed,&invalid,tex,pal)==T3_PARTICLE_TILE_UNSUPPORTED);
      assert(!memcmp(&before,&mixed,sizeof mixed)); }
    /* Reuse exactly four cleared cells, preserving all occupied pixels. */
    for(unsigned i=0;i<4;++i)memset(mixed.pixels[i],0,sizeof mixed.pixels[i]);
    assert(t3_particle_tile_accumulate(&mixed,&sprite,tex,pal));
    assert(mixed.count==T3_PARTICLE_PROBE_MAX_TILES);
    for(unsigned i=0;i<T3_PARTICLE_PROBE_MAX_TILES-4;++i)assert(mixed.pixels[i][0]==0x8000);
    puts("particle diagnostic tiles: ok");return 0;
}
