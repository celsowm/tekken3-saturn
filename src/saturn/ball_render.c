/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * Tekken Ball's ball and court as scene faces; adapted from the pinned Tekken3Remake's tools/research/ball_sim.py
 * (ball_draw, court_draw) and remake/presentation/ball/ball_view.gd. Changes: the GTE's per-vertex lighting is src/game/ball_light.c
 * (checked against the GTE), carried by a VDP1 Gouraud table per face that clamps a corner 16 steps from the face's mean colour;
 * an opaque centre band and side lines. See ball_render.h. */
#include "ball_render.h"
#include "saturn/color.h"
#include "ball_pose.h"
#include "ball_light.h"
#include "fighter_light.h"

enum { UNIT=96, VERTICES=42, TRIANGLES=16, QUADS=32 };

static int32_t be16s(const uint8_t* p){return (int16_t)(uint16_t)(p[0]<<8|p[1]);}
static uint32_t be32u(const uint8_t* p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
/* 8-bit components to RGB555 (SAT_RGB555 takes five bits). */
static uint16_t rgb(unsigned r,unsigned g,unsigned b) {
    return (uint16_t)SAT_RGB555((r>255u?255u:r)>>3,(g>255u?255u:g)>>3,(b>255u?255u:b)>>3);
}

static void apply(const int32_t m[3][3],const int32_t v[3],int32_t out[3]) {
    for(unsigned i=0;i<3u;++i)out[i]=(m[i][0]*v[0]+m[i][1]*v[1]+m[i][2]*v[2])>>12;
}
/* The ball's light: the stage's rig (fighter_light.c) for the main light and the fill from below, the type's specular power and back
 * colour (its option row, bytes 0xC to 0xF), and kind 4's pulse in place of the back colour. */
static void light_setup(t3_ball_light_t* l,const t3_ball_mesh_t* mesh,int32_t kind,uint32_t pulse) {
    unsigned fill;
    const int32_t* rig=t3_fighter_light_rig(&fill);
    const unsigned w6=(unsigned)(mesh->style[12]<<8|mesh->style[13]),w7=(unsigned)(mesh->style[14]<<8|mesh->style[15]);
    for(unsigned i=0;i<3u;++i){l->light[i]=rig[3+i];l->colour[i]=(uint8_t)(rig[2]>>(8u*i));}
    l->power=(uint8_t)w6;l->fill=(uint8_t)fill;
    l->back[0]=(uint8_t)(w6>>8);l->back[1]=(uint8_t)w7;l->back[2]=(uint8_t)(w7>>8);
    if(kind==4) {   /* a set ball pulses through its back colour: FUN_8004E2C8's 0-255-0 wave */
        const unsigned p=pulse&511u,v=p<256u?p:511u-p;
        l->back[0]=l->back[1]=l->back[2]=(uint8_t)v;
    }
}

/* The flat kinds' colour: a serving ball fades in from black (kind 0, -8k) or out to white (kind 1, 8k - 1), a touched or scoring
 * ball is white. */
static unsigned flat_level(int32_t kind,int32_t k) {
    return kind==0?(unsigned)(-8*k)&255u:kind==1?(unsigned)(8*k-1)&255u:255u;
}

/* The face's Gouraud: the VDP1 adds each corner's entry (5-bit steps around 16) to the polygon colour, which is the corners'
 * mean. A corner further than 15 steps from it clamps. */
static uint16_t corner_entry(const unsigned c[3],const unsigned mean[3]) {
    return sat_gouraud_rgb(((int)c[0]-(int)mean[0])/8,((int)c[1]-(int)mean[1])/8,((int)c[2]-(int)mean[2])/8);
}

static sat_result_t submit(sat_scene_t* scene,const int32_t p[4][3],uint16_t colour,const uint16_t* gouraud,uint8_t calc) {
    sat_quad3_t q;
    sat_scene3d_material_t m={0};
    m.kind=SAT_SCENE3D_RGB;m.rgb555=colour;m.color_calc_slot=calc;m.vertex_gouraud=gouraud;
    for(unsigned i=0;i<4u;++i)q.v[i]=(sat_vec3_t){p[i][0],p[i][1],p[i][2]};
    return sat_scene_submit_quad(scene,&q,&m,0);
}

sat_result_t t3_ball_render(sat_scene_t* scene,const t3_ball_view_t* view,const t3_ball_mesh_t* mesh,
                            const uint8_t* trig,const int32_t eye[3],int32_t center_x,int32_t center_z,uint16_t shadow) {
    int32_t angle[3],rot[3][3],world[VERTICES][3],normal[VERTICES][3],ground[3];
    const t3_ball_t* b;
    sat_result_t st;
    unsigned i,k;
    if(!scene||!view||!mesh||!trig||!view->active)return SAT_ERR_INVALID_ARG;
    b=&view->ball;
    /* the court (FUN_800B3DA4): the centre band (x +-100) and the two side lines at the neutral zone's edges (the shadow line
     * of the original's gradient quads, one quad each here), running from z = -8000 to the far end of the court */
    {
        const int32_t zone=view->option==0?2304:view->option==1?1920:1536;
        const int32_t z0=-8000,z1=-8000+8*0x600;
        /* the original's band fades in and out (additive Gouraud quads); the scene takes no translucent RGB face, so it is one
         * opaque grey quad, the side lines white */
        for(k=0;k<3u;++k) {
            const int32_t x0=k==0?-100:k==1?-zone-30:zone-30,x1=k==0?100:k==1?-zone+30:zone+30;
            const int32_t p[4][3]={{(x0-center_x)*UNIT,1,-(z0-center_z)*UNIT},{(x1-center_x)*UNIT,1,-(z0-center_z)*UNIT},
                                   {(x1-center_x)*UNIT,1,-(z1-center_z)*UNIT},{(x0-center_x)*UNIT,1,-(z1-center_z)*UNIT}};
            if((st=submit(scene,p,k==0?rgb(0xC0,0xC0,0xC0):rgb(0xFF,0xFF,0xFF),0,SAT_INDEXED_SOLID_OPAQUE))!=SAT_OK)return st;
        }
    }
    angle[0]=b->spin[0];angle[1]=b->spin[1];angle[2]=b->spin[2];
    t3_ball_pose(trig,angle,b->squash_axis,b->squash,rot);   /* a hit squashes it along the axis */
    for(i=0;i<VERTICES;++i) {
        const int32_t v[3]={be16s(mesh->vertices+6u*i),be16s(mesh->vertices+6u*i+2),be16s(mesh->vertices+6u*i+4)};
        const int32_t n[3]={be16s(mesh->normals+6u*i),be16s(mesh->normals+6u*i+2),be16s(mesh->normals+6u*i+4)};
        int32_t r[3];
        apply(rot,v,r);apply(rot,n,normal[i]);
        /* original (x, y down, z) to the scene: y up, z toward the camera */
        world[i][0]=(b->pos[0]+r[0]-center_x)*UNIT;
        world[i][1]=-(b->pos[1]+r[1])*UNIT;
        world[i][2]=-(b->pos[2]+r[2]-center_z)*UNIT;
    }
    {
        /* its shadow (FUN_800B3198's _shadow): the equator ring (vertices 18-25) with its height dropped, turned by the
         * ball's yaw and squash and widened by (4096 + y / 2) / 4096, which narrows it as the ball rises (y is negative up) */
        int32_t sq[3][3],stretch;
        t3_ball_shadow_pose(trig,b->spin[1],b->squash_axis[1],b->squash,sq);
        stretch=(b->pos[1]>>1)+0x1000;
        ground[0]=(b->pos[0]-center_x)*UNIT;ground[1]=0;ground[2]=-(b->pos[2]-center_z)*UNIT;
        /* two quads over the ring (0 1 3 4 and 4 5 7 0), not the original's four: Work RAM low has no room for more faces */
        for(k=0;k<2u;++k) {
            static const unsigned halves[2][4]={{0,1,3,4},{4,5,7,0}};
            const unsigned* ring=halves[k];
            int32_t p[4][3];
            for(unsigned v=0;v<4u;++v) {
                const uint8_t* src=mesh->vertices+6u*(18u+ring[v]);
                const int32_t flat[3]={be16s(src),0,be16s(src+4)};
                int32_t r[3];
                apply(sq,flat,r);
                p[v][0]=ground[0]+(r[0]*stretch>>12)*UNIT;p[v][1]=0;p[v][2]=ground[2]-(r[2]*stretch>>12)*UNIT;
            }
            if((st=submit(scene,p,shadow,0,SAT_INDEXED_SOLID_OPAQUE))!=SAT_OK)return st;
        }
    }
    {
        /* the original lights every vertex (ColorMatCol: the stage's light and the type's back colour), then multiplies by the
         * face's colour; the light does not depend on the face, so it is lit once per vertex */
        t3_ball_light_t light;
        int32_t lit[VERTICES][3];
        const int flat=b->kind==0||b->kind==1||b->kind==2||b->kind==5||b->kind==8;
        light_setup(&light,mesh,b->kind,b->timer*16u);
        if(!flat)for(i=0;i<VERTICES;++i)t3_ball_light_vertex(&light,normal[i],lit[i]);
        for(i=0;i<TRIANGLES+QUADS;++i) {
            const uint8_t* idx=i<TRIANGLES?mesh->triangles+3u*i:mesh->quads+4u*(i-TRIANGLES);
            const unsigned corners=i<TRIANGLES?3u:4u,slot=i<TRIANGLES?(i&7u):8u+((i-TRIANGLES)&7u);
            const uint32_t c=be32u(mesh->colours+4u*slot);
            const unsigned face[3]={c&255u,(c>>8)&255u,(c>>16)&255u};
            unsigned corner[4][3],mean[3]={0,0,0};
            int32_t p[4][3];
            uint16_t table[4],colour;
            for(k=0;k<4u;++k) {
                /* a quad's corners are in strip order (the PS1's POLY_G4: 0 1 / 2 3), the scene takes them around; reversed, as the Z
                 * mirror of the scene turns the winding over */
                const unsigned vi=idx[corners==4u?(unsigned)"\2\3\1\0"[k]:k<3u?2u-k:0u];
                p[k][0]=world[vi][0];p[k][1]=world[vi][1];p[k][2]=world[vi][2];
                if(flat)continue;
                for(unsigned ch=0;ch<3u;++ch)corner[k][ch]=t3_ball_light_channel(face[ch],lit[vi][ch]);
                if(k<corners)for(unsigned ch=0;ch<3u;++ch)mean[ch]+=corner[k][ch];
            }
            if(flat) {
                const unsigned v=flat_level(b->kind,b->timer);
                colour=rgb(v,v,v);
            } else {
                for(unsigned ch=0;ch<3u;++ch)mean[ch]/=corners;
                for(k=0;k<4u;++k)table[k]=corner_entry(corner[k<corners?k:0u],mean);
                colour=rgb(mean[0],mean[1],mean[2]);
            }
            {   /* the original's NormalClip skips the faces turned away; the scene's painter order would show them through the front */
                const int64_t ex=(int64_t)(eye[0]-center_x)*UNIT,ey=-(int64_t)eye[1]*UNIT,ez=-(int64_t)(eye[2]-center_z)*UNIT;
                int32_t n[3]={0,0,0};
                for(k=0;k<corners;++k){const unsigned vi=idx[k];n[0]+=normal[vi][0];n[1]-=normal[vi][1];n[2]-=normal[vi][2];}
                if((int64_t)n[0]*(ex-p[0][0])+(int64_t)n[1]*(ey-p[0][1])+(int64_t)n[2]*(ez-p[0][2])<0)continue;
            }
            if((st=submit(scene,p,colour,flat?0:table,SAT_INDEXED_SOLID_OPAQUE))!=SAT_OK)return st;
        }
    }
    return SAT_OK;
}
