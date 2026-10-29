/* Isolated authored VDP1 test pattern; no commercial game data. */
#include "coverage_probe.h"
#include "saturn/render3d.h"
#include "saturn/vdp1.h"
#if !defined(T3_VDP1_COVERAGE_PLANES)
static sat_vdp1_texture_t texture;
#endif
#if defined(T3_VDP1_COVERAGE_PLANES)
static sat_vdp1_texture_t plane_textures[16] __attribute__((section(".wram_l")));
#endif
#if defined(T3_VDP1_COVERAGE_MAPPING) || defined(T3_VDP1_COVERAGE_TRIS) || (defined(T3_VDP1_COVERAGE_QUADS) && !defined(T3_VDP1_COVERAGE_BINARY))
#define T3_COVERAGE_UV 1
#endif
#if defined(T3_COVERAGE_UV)
static sat_vdp1_texture_t vertical_texture;
#endif
/* Asymmetric opaque/transparent pixels reveal orientation and sampling. */
#if !defined(T3_COVERAGE_UV) && !defined(T3_VDP1_COVERAGE_PLANES)
static const uint8_t pixels[32]={
    0x11,0x11,0x11,0x11, 0x10,0x00,0x00,0x01,
    0x10,0x11,0x10,0x01, 0x10,0x10,0x10,0x01,
    0x10,0x11,0x10,0x01, 0x10,0x00,0x00,0x01,
    0x10,0x10,0x10,0x01, 0x11,0x11,0x11,0x11
};
#endif
sat_result_t t3_coverage_probe_init(void) {
#if defined(T3_VDP1_COVERAGE_PLANES)
    uint16_t lut[16]={0},handle=0;lut[1]=0xfc1fu;
    SAT_TRY(sat_vdp1_upload_lut(lut,&handle));
    for(unsigned plane=0;plane<16;++plane) {
        uint8_t data[32];
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;x+=2) {
            unsigned a=plane<8?x:y,b=plane<8?x+1:y,k=plane&7u;
            data[y*4+x/2]=(uint8_t)(((a==k)<<4)|(b==k));
        }
        SAT_TRY(sat_tex_upload_lut4_pixels(&plane_textures[plane],data,8,8,handle));
    }
    return SAT_OK;
#elif defined(T3_COVERAGE_UV)
    uint16_t lut[16]={0},handle=0;uint8_t horizontal[32],vertical[32];
    for(unsigned i=1;i<=8;++i)lut[i]=(uint16_t)(0xfc00u|i);
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;x+=2) {
        horizontal[y*4+x/2]=(uint8_t)(((x+1)<<4)|(x+2));
        vertical[y*4+x/2]=(uint8_t)(((y+1)<<4)|(y+1));
    }
    SAT_TRY(sat_vdp1_upload_lut(lut,&handle));
    SAT_TRY(sat_tex_upload_lut4_pixels(&texture,horizontal,8,8,handle));
    return sat_tex_upload_lut4_pixels(&vertical_texture,vertical,8,8,handle);
#else
    uint16_t lut[16]={0},handle=0;lut[1]=0xfc1fu;
    SAT_TRY(sat_vdp1_upload_lut(lut,&handle));
    return sat_tex_upload_lut4_pixels(&texture,pixels,8,8,handle);
#endif
}
sat_result_t t3_coverage_probe_draw(void) {
    /* Coordinates are relative to the 320x224 screen centre. */
#if defined(T3_VDP1_COVERAGE_PLANES)
    for(unsigned plane=0;plane<16;++plane) {
        int x=10+(int)(plane%4)*75-160,y=10+(int)(plane/4)*50-112;
#if defined(T3_VDP1_COVERAGE_PLANES_TRIANGLE)
        sat_quad2_t q={{x,x+6,x+3,x+3},{y+4,y,y+11,y+11}};
#else
        sat_quad2_t q={{x,x+32,x+36,x+4},{y,y+5,y+30,y+25}};
#endif
        SAT_TRY(sat_draw_quad2_sprite(&q,&plane_textures[plane],0,0));
    }
    return SAT_OK;
#elif defined(T3_VDP1_COVERAGE_QUADS) || defined(T3_VDP1_COVERAGE_TRIS)
    static const sat_quad2_t quads[]={
#if defined(T3_VDP1_COVERAGE_SMALL_NEW)
        {{-132,-123,-125,-125},{-89,-91,-80,-80}},
        {{-99,-91,-103,-103},{-95,-91,-80,-80}},
        {{-54,-63,-48,-48},{-96,-87,-84,-84}},
        {{-21,-13,-24,-24},{-85,-95,-90,-90}},
        {{19,27,17,17},{-96,-91,-81,-81}},
        {{55,48,62,62},{-86,-95,-89,-89}},
        {{90,99,102,102},{-84,-88,-96,-96}},
        {{132,120,127,127},{-95,-88,-80,-80}}
#elif defined(T3_VDP1_COVERAGE_SMALL)
        /* Authored test triangles, not captured fighter coordinates. */
        {{-130,-124,-127,-127},{-90,-94,-83,-83}},
        {{-96,-94,-101,-101},{-94,-88,-84,-84}},
        {{-54,-61,-51,-51},{-90,-85,-83,-83}},
        {{-20,-16,-21,-21},{-85,-94,-89,-89}},
        {{20,24,18,18},{-94,-88,-83,-83}},
        {{54,50,60,60},{-87,-94,-90,-90}},
        {{92,98,99,99},{-85,-89,-95,-95}},
        {{130,121,128,128},{-94,-89,-84,-84}}
#elif defined(T3_VDP1_COVERAGE_TRIS)
        {{-140,-108,-124,-124},{-92,-92,-68,-68}},
        {{-70,-39,-64,-64},{-92,-92,-66,-66}},
        {{4,32,32,32},{-92,-92,-60,-60}},
        {{80,114,80,80},{-92,-92,-60,-60}},
        {{-140,-108,-100,-100},{-42,-42,-12,-12}},
        {{-70,-38,-80,-80},{-42,-42,-12,-12}},
        {{4,10,7,7},{-42,-42,-36,-36}},
        {{80,111,95,95},{-42,-42,-39,-39}}
#else
        {{-140,-108,-108,-140},{-92,-92,-68,-68}},
        {{-70,-39,-32,-76},{-91,-96,-65,-62}},
        {{4,32,24,-4},{-97,-80,-53,-70}},
        {{80,114,96,96},{-92,-92,-60,-60}},
        {{-140,-108,-104,-136},{-42,-37,-12,-17}},
        {{-70,-38,-45,-77},{-47,-38,-12,-21}},
        {{4,10,12,6},{-42,-42,-12,-12}},
        {{80,111,112,81},{-42,-40,-37,-39}}
#endif
    };
    for(unsigned k=0;k<sizeof(quads)/sizeof(quads[0]);++k) {
        sat_quad2_t q=quads[k];SAT_TRY(sat_draw_quad2_sprite(&q,&texture,0,0));
#if defined(T3_COVERAGE_UV)
        for(unsigned i=0;i<4;++i)q.y[i]+=100;
        SAT_TRY(sat_draw_quad2_sprite(&q,&vertical_texture,0,0));
#endif
    }
    return SAT_OK;
#elif defined(T3_VDP1_COVERAGE_MAPPING)
    for(unsigned h=0;h<8;++h)for(unsigned w=0;w<8;++w) {
        int x=20+(int)w*35-160,y=20+(int)h*12-112;
        sat_quad2_t q={{x,x+(int)w,x+(int)w,x},{y,y,y+(int)h,y+(int)h}};
        SAT_TRY(sat_draw_quad2_sprite(&q,&texture,0,0));
        for(unsigned i=0;i<4;++i)q.y[i]+=100;
        SAT_TRY(sat_draw_quad2_sprite(&q,&vertical_texture,0,0));
    }
    return SAT_OK;
#elif defined(T3_VDP1_COVERAGE_RECTS)
    static const sat_quad2_t quads[]={
        {{-140,-133,-133,-140},{-48,-48,-41,-41}},
        {{-100,-97,-97,-100},{-48,-48,-45,-45}},
        {{-60,-48,-48,-60},{-48,-48,-31,-31}},
        {{-20,12,12,-20},{-48,-48,-24,-24}},
        {{35,54,54,35},{-48,-48,-39,-39}},
        {{85,86,86,85},{-48,-48,-47,-47}},
        {{120,120,120,120},{-48,-48,-48,-48}}
    };
#else
    static const sat_quad2_t quads[4]={
        {{-140,-108,-108,-140},{-48,-48,-24,-24}},
        {{-70,-37,-30,-76},{-48,-53,-23,-20}},
        {{0,28,20,-8},{-54,-37,-10,-27}},
        {{76,110,92,92},{-49,-49,-17,-17}}
    };
#endif
#if !defined(T3_VDP1_COVERAGE_MAPPING) && !defined(T3_VDP1_COVERAGE_QUADS) && !defined(T3_VDP1_COVERAGE_TRIS) && !defined(T3_VDP1_COVERAGE_PLANES)
    for(unsigned i=0;i<sizeof(quads)/sizeof(quads[0]);++i)SAT_TRY(sat_draw_quad2_sprite(&quads[i],&texture,0,0));
    return SAT_OK;
#endif
}
