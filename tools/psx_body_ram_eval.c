/* Native original normal-mode separation/bounds vs PRIVATE CPU snapshots. */
#include "psx_body.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx_ram_ranges.h"
static uint8_t sqrt_bytes[432],trig_bytes[0x2800],atan_bytes[1025];
static void state(t3_psx_body_actor_t* a,unsigned index,uint32_t prev,uint32_t ramp) {
    uint32_t b=0x800A9228u+index*0x188Cu;
    *a=(t3_psx_body_actor_t){0};
    a->anchor=(t3_vec3i_t){i32(b),i32(b+4),i32(b+8)};
    a->root=(t3_vec3i_t){i32(b+0xF68),i32(b+0xF6C),i32(b+0xF70)};
    a->placed=(t3_vec3i_t){i32(b+0x12C),0,i32(b+0x130)};
    a->previous_root=(t3_vec3i_t){u16(prev+16*index+4),u16(prev+16*index+8),u16(prev+16*index+12)};
    a->push=(t3_vec3i_t){i32(b+0x3E8),i32(b+0x3EC),i32(b+0x3F0)};
    a->ramp=i32(ramp+4*index);a->distance=u32(b+0xF8);a->target_dir=u16(b+0x2A);
    a->facing=(uint16_t)i16(b+0xE);a->active=u8(b+0xC3);a->invulnerable=u8(b+0xC2);
    a->no_push=u8(b+0xD9);a->contact=u8(b+0xD5);
    a->contact_with=(uint8_t)((u8(b+0xD6)?1:0)|(u8(b+0xD7)?2:0));
    a->active_first=u8(u32(b+0x54)+0x2D);
    for(unsigned k=0;k<8;++k) {
        uint32_t p=b+0x324+16*k;
        a->spheres[k]=(t3_psx_body_sphere_t){{i32(p),i32(p+4),i32(p+8)},u16(p+12)};
    }
}
static unsigned diffs;
static void check(unsigned i,const char* name,int32_t actual,int32_t expected) {
    if(actual!=expected) {
        if(diffs++)putchar(',');
        printf("{\"fighter\":%u,\"field\":\"%s\",\"actual\":%d,\"expected\":%d}",i,name,actual,expected);
    }
}
int main(int argc,char** argv) {
    char before[2048],after[2048],kind[16];unsigned index,call;
    t3_psx_body_tables_t tables={0};uint32_t prev,pair,ramp;
    FILE* list;
    if(argc!=8)return 2;
    if(!read_blob(argv[1],sqrt_bytes,sizeof(sqrt_bytes)) || !read_blob(argv[2],trig_bytes,sizeof(trig_bytes)) ||
       !read_blob(argv[3],atan_bytes,sizeof(atan_bytes)))return 2;
    prev=(uint32_t)strtoul(argv[4],NULL,0);pair=(uint32_t)strtoul(argv[5],NULL,0);ramp=(uint32_t)strtoul(argv[6],NULL,0);
    tables.sqrt=sqrt_bytes;tables.sqrt_size=384u;
    tables.ground.trig=(t3_psx_trig_t){trig_bytes,sizeof(trig_bytes)};
    tables.ground.atan=atan_bytes;tables.ground.atan_size=sizeof(atan_bytes);
    list=fopen(argv[7],"r");if(!list)return 2;
    while(fscanf(list,"%2047s\t%2047s\t%15s\t%u\t%u",before,after,kind,&index,&call)==5) {
        t3_psx_body_actor_t actors[2],expected[2];t3_vec3i_t original[2];int ok;unsigned shape_diffs=0;uint32_t distance;
        if(index>1 || !snapshot(before)){fclose(list);return 2;}
        if(i32(0x800AFA88u)==7 || i32(0x800AFA88u)==8){fclose(list);return 2;}
        for(unsigned i=0;i<2;++i)state(&actors[i],i,prev,ramp);
        original[0]=actors[0].root;original[1]=actors[1].root;
        distance=u32(pair+0x10u);
        if(bad){fclose(list);return 2;}
        if(!strcmp(kind,"body")) {
            for(unsigned i=0;i<2;++i) {
                uint32_t b=0x800A9228u+i*0x188Cu,row=u32(b+0x54);
                t3_psx_joint_t parts[T3_PSX_DRAW_PARTS]={0};
                t3_move_record_t move={0};t3_psx_body_actor_t shaped=actors[i];
                move.body_profile=u16(row+48);move.flags=u32(row+36);
                move.active_first=u8(row+45);move.active_last=u8(row+46);
                for(unsigned k=0;k<T3_PSX_DRAW_PARTS;++k) {
                    uint32_t p=b+0x8F4u+0x44u*k+0x14u;
                    parts[k].origin=(t3_vec3i_t){i32(p),i32(p+4),i32(p+8)};
                }
                if(bad || !t3_psx_body_shapes(&shaped,parts,0x3FFFFu,sqrt_bytes+384,
                        &move,i16(b+0x58),u8(b+0xDB),u8(b+0x87))){fclose(list);return 2;}
                for(unsigned k=0;k<8;++k) {
                    const t3_psx_body_sphere_t *a=&actors[i].spheres[k],*s=&shaped.spheres[k];
                    if(a->center.x!=s->center.x || a->center.y!=s->center.y ||
                       a->center.z!=s->center.z || a->radius!=s->radius)++shape_diffs;
                }
            }
            ok=t3_psx_body_separate(actors,distance,&tables);
        }
        else if(!strcmp(kind,"arena"))ok=t3_psx_arena_bounds(&actors[index],&actors[1u-index],&tables);
        else {fclose(list);return 2;}
        if(!ok || !snapshot(after)){fclose(list);return 2;}
        for(unsigned i=0;i<2;++i)state(&expected[i],i,prev,ramp);
        if(bad){fclose(list);return 2;}
        printf("{\"call\":%u,\"diffs\":[",call);diffs=0;
#define CHECK(field) check(i,#field,actors[i].field,expected[i].field)
        for(unsigned i=0;i<2;++i) {
            CHECK(anchor.x);CHECK(anchor.y);CHECK(anchor.z);CHECK(root.x);CHECK(root.y);CHECK(root.z);
            CHECK(push.x);CHECK(push.y);CHECK(push.z);CHECK(contact);CHECK(contact_with);CHECK(no_push);
        }
#undef CHECK
        printf("],\"shape_diffs\":%u,\"corrected\":%s,\"passed\":%s}\n",shape_diffs,
            (actors[0].root.x!=original[0].x || actors[0].root.z!=original[0].z ||
             actors[1].root.x!=original[1].x || actors[1].root.z!=original[1].z)?"true":"false",(diffs || shape_diffs)?"false":"true");
    }
    fclose(list);return 0;
}
