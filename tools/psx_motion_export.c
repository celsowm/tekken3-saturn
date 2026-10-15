/* Host-only PRIVATE DIVMOT animation sampler for numerical oracle input.
 * Usage: t3_motion_export <character-bank> <common-bank|-> <splines|-> 
 *        <slot> <first-frame> <frame-count|0-all> <output.bin>
 *
 * Output is raw little-endian signed 16-bit[49] pose vectors per frame,
 * suitable for t3_pose_eval and diff_psx_pose_solver.py. No original
 * data is embedded into the public host binary or CI artifacts.
 */
#include "divmot.h"
#include "psx_motion.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_BANK=4*1024*1024, MAX_POSES=255, POSE_BYTES=98 };
static void w16(uint8_t* p,uint16_t n) {
    p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8u);
}
static int number(const char* text,unsigned* out) {
    unsigned long n;char* end;
    errno=0;
    if(!text || !*text || *text=='-')return 0;
    n=strtoul(text,&end,0);
    if(errno || *end!='\0' || n>UINT_MAX)return 0;
    *out=(unsigned)n;
    return 1;
}
static uint8_t* read_bank(const char* path,size_t* size,size_t max) {
    uint8_t* p;
    FILE* f=fopen(path,"rb");
    long len;
    if(!f){perror(path);return NULL;}
    if(fseek(f,0,SEEK_END)!=0 || (len=ftell(f))<0 ||
       (unsigned long)len>max || len<1 ||
       fseek(f,0,SEEK_SET)!=0) {
        fprintf(stderr,"invalid local data file size: %s\n",path);
        fclose(f);return NULL;
    }
    p=(uint8_t*)malloc((size_t)len);
    if(!p) {fclose(f);return NULL;}
    if(fread(p,1,(size_t)len,f)!=(size_t)len) {
        free(p);fclose(f);return NULL;
    }
    if(fclose(f)!=0){free(p);return NULL;}
    *size=(size_t)len;
    return p;
}
int main(int argc,char** argv) {
    uint8_t *character=NULL,*common=NULL,*spline=NULL;
    size_t char_size=0,common_size=0,spline_size=0;
    t3_divmot_t character_bank={0},common_bank={0};
    const t3_divmot_t *chosen_common=NULL,*owner=NULL;
    t3_motion_stream_t stream={0};
    t3_motion_tables_t tables;
    t3_move_record_t move;
    FILE* out=NULL;
    unsigned slot,start,count,frame,ch;
    int rc=1,opened;
    if(argc!=8) {
        fprintf(stderr,"usage: %s <character-bank> <common-bank|-> "
               "<splines|-> <slot> <start> <count|0-all> <output>\n",argv[0]);
        return 2;
    }
    if(!number(argv[4],&slot) || slot>=T3_DIVMOT_MOVE_SLOTS ||
       !number(argv[5],&start) ||
       !number(argv[6],&count) || count>MAX_POSES) {
        fprintf(stderr,"invalid slot, start or frame count\n");return 2;
    }
    character=read_bank(argv[1],&char_size,MAX_BANK);
    if(!character || !t3_divmot_open(&character_bank,character,char_size) ||
       character_bank.bank_type==99u) {
        fprintf(stderr,"invalid private character DIVMOT bank\n");goto done;
    }
    if(strcmp(argv[2],"-")!=0) {
        common=read_bank(argv[2],&common_size,MAX_BANK);
        if(!common || !t3_divmot_open(&common_bank,common,common_size) ||
           common_bank.bank_type!=99u) {
            fprintf(stderr,"invalid common DIVMOT bank\n");goto done;
        }
        chosen_common=&common_bank;
    }
    if(strcmp(argv[3],"-")!=0) {
        spline=read_bank(argv[3],&spline_size,T3_PSX_SPLINE_TABLE_BYTES);
        if(!spline || spline_size!=T3_PSX_SPLINE_TABLE_BYTES) {
            fprintf(stderr,"invalid 0x660-byte normalized private spline table\n");
            goto done;
        }
    }
    tables.blob=spline;
    tables.length=spline_size;
    opened=t3_divmot_slot_resolve(&character_bank,chosen_common,slot,&move,&owner);
    if(opened!=1) {
        fprintf(stderr,"missing or invalid PS1 move slot %u\n",slot);goto done;
    }
    /* A common owner may contain a bank-relative offset without a flag.
     * Preserve a flagged reference unchanged; never silently use a
     * character-owned section for a common-owned move. */
    if(owner==chosen_common && chosen_common &&
       move.animation_offset<0x40000000u)
        move.animation_offset|=0x40000000u;
    opened=t3_motion_open(&character_bank,chosen_common,
                          move.animation_offset,&stream);
    if(opened!=1) {
        fprintf(stderr,"move slot %u has no usable section-6 animation\n",slot);
        goto done;
    }
    if(start>=stream.frames || count>stream.frames-start) {
        fprintf(stderr,"requested frame window exceeds %u authored frames\n",
                (unsigned)stream.frames);goto done;
    }
    if(!count)count=stream.frames-start;
    out=fopen(argv[7],"wb");
    if(!out){perror(argv[7]);goto done;}
    for(frame=start;frame<start+count;++frame) {
        int16_t pose[T3_PSX_POSE_CHANNELS];
        uint8_t encoded[POSE_BYTES];
        int result=t3_motion_sample(&stream,frame,
                                    spline?&tables:NULL,pose);
        if(result!=1) {
            fprintf(stderr,"frame %u cannot be decoded (%s)\n",frame,
                    result==0?"private spline table missing":
                              "malformed data or unsupported spline");
            goto done;
        }
        for(ch=0;ch<T3_PSX_POSE_CHANNELS;++ch)
            w16(encoded+ch*2u,(uint16_t)pose[ch]);
        if(fwrite(encoded,1,sizeof(encoded),out)!=sizeof(encoded))goto done;
    }
    rc=0;
done:
    if(out && fclose(out)!=0)rc=1;
    if(rc && out)remove(argv[7]);
    free(character);free(common);free(spline);
    return rc;
}
