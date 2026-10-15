/* Offline host differential adapter; uses only synthetic input in CI.
 * Every record is 21 little-endian signed 32-bit fields (84 bytes).
 * Output: 13 little-endian signed 32-bit fields (52 bytes), including
 * classification flags, severity and signed damage. No game data embedded.
 */
#include "psx_hit_damage.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

enum { INPUT_FIELDS=21, OUTPUT_FIELDS=13, RECORD_LIMIT=20000 };
static uint32_t le32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8u) |
           ((uint32_t)p[2]<<16u) | ((uint32_t)p[3]<<24u);
}
static void store32(unsigned char* p,int32_t n) {
    uint32_t w=(uint32_t)n;
    p[0]=(unsigned char)w;
    p[1]=(unsigned char)(w>>8u);
    p[2]=(unsigned char)(w>>16u);
    p[3]=(unsigned char)(w>>24u);
}
int main(int argc,char** argv) {
    FILE *source=NULL,*out=NULL;
    unsigned char input[INPUT_FIELDS*4],buffer[OUTPUT_FIELDS*4];
    long length;
    unsigned long count,frame;
    int ok=1;
    if(argc!=3) {
        fprintf(stderr,"usage: %s <private-int32-fixtures.bin> <out.bin>\n",argv[0]);
        return 2;
    }
    source=fopen(argv[1],"rb");
    if(!source){perror(argv[1]);return 2;}
    if(fseek(source,0,SEEK_END)!=0 || (length=ftell(source))<0 ||
       fseek(source,0,SEEK_SET)!=0 || length==0 ||
       length%(INPUT_FIELDS*4)!=0 ||
       length>(long)(RECORD_LIMIT*INPUT_FIELDS*4)) {
        fprintf(stderr,"Invalid input: expected 1..%u fixed records\n",RECORD_LIMIT);
        fclose(source);return 2;
    }
    count=(unsigned long)length/(INPUT_FIELDS*4u);
    out=fopen(argv[2],"wb");
    if(!out){perror(argv[2]);fclose(source);return 2;}
    for(frame=0;frame<count;++frame) {
        int32_t x[INPUT_FIELDS],result[OUTPUT_FIELDS];
        t3_move_record_t move={0};
        t3_psx_hit_facts_t facts={0};
        t3_psx_hit_candidate_t hit={0};
        unsigned j;
        if(fread(input,1,sizeof(input),source)!=sizeof(input)){ok=0;break;}
        for(j=0;j<INPUT_FIELDS;++j)x[j]=(int32_t)le32(input+j*4u);
        if(x[0]<0||x[0]>=14 ||
           x[5]<-32768||x[5]>32767 ||
           x[6]<-32768||x[6]>32767) {ok=0;break;}
        move.attack_word=(uint16_t)x[1];
        move.damage_class=(uint16_t)x[2];
        move.reaction=(uint16_t)x[3];
        move.close_reaction=(uint16_t)x[4];
        facts.attacker_damage=(int16_t)x[5];
        facts.defender_damage=(int16_t)x[6];
        facts.defender_guard=(uint16_t)x[7];
        facts.defender_rel_angle=(uint16_t)x[8];
        facts.defender_pose_frame=(uint16_t)x[9];
        facts.defender_active_first=(uint16_t)x[10];
        facts.defender_state=(uint16_t)x[11];
        facts.juggle_count=(uint16_t)x[12];
        facts.fighter_distance=(uint32_t)x[13];
        facts.close_threshold=(uint32_t)x[14];
        facts.close_reaction_word=(uint16_t)x[15];
        facts.close_table_available=(uint8_t)x[16];
        facts.attacker_power=(uint8_t)x[17];
        facts.defender_power=(uint8_t)x[18];
        facts.chip_enabled=(uint8_t)x[19];
        facts.defender_in_air=(uint8_t)x[20];
        if(!t3_psx_hit_preview(&move,(unsigned)x[0],&facts,&hit)) {
            fprintf(stderr,"Invalid or incomplete hit facts at record %lu\n",frame);
            ok=0;break;
        }
        result[0]=hit.damage;result[1]=hit.base_damage;
        result[2]=hit.no_damage;result[3]=hit.guarded;
        result[4]=hit.chip;result[5]=hit.close;
        result[6]=hit.hit;result[7]=hit.counter;
        result[8]=hit.airborne;result[9]=hit.severity;
        result[10]=hit.reaction_word;result[11]=hit.reaction_kind;
        result[12]=hit.attack_class;
        for(j=0;j<OUTPUT_FIELDS;++j)store32(buffer+j*4u,result[j]);
        if(fwrite(buffer,1,sizeof(buffer),out)!=sizeof(buffer)){ok=0;break;}
    }
    if(fclose(source)!=0)ok=0;
    if(fclose(out)!=0)ok=0;
    if(!ok)remove(argv[2]);
    return ok?0:1;
}
