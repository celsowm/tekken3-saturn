/* Practice's attack data against the original code: each line of build/practice-golden/cases.txt (made by
 * scripts/export_practice_trace.py from Tekken3Remake's flow trace of the original practice overlay) holds the
 * per-player state before a frame, the fighter records' facts after it, and the state the original stored.
 * Skipped (77) without the file: it is derived from the game and is not in git. */
#include "practice_data.h"
#include "practice_keys.h"
#include <stdio.h>
#include <stdlib.h>

static void load_player(t3_practice_player_t* p,const long v[16]) {
    p->total=(int32_t)v[0];p->combo_damage=(int32_t)v[1];p->shown_damage=(int32_t)v[2];
    p->hit_damage=(int16_t)v[3];p->hit_share=(int16_t)v[4];p->marker=(int16_t)v[5];p->hits=(int16_t)v[6];
    p->combo=(uint8_t)v[7];p->new_best=(uint8_t)v[8];p->ended=(uint8_t)v[9];p->previous=(uint8_t)v[10];
    p->counter=(uint8_t)v[11];p->clean=(uint8_t)v[12];p->hit_id=(int8_t)v[13];
    p->throw_now=(uint8_t)v[14];p->throw_before=(uint8_t)v[15];
}
static const char* const NAMES[16]={"total","combo_damage","shown_damage","hit_damage","hit_share","marker","hits",
    "combo","new_best","ended","previous","counter","clean","hit_id","throw","throw_before"};
static void store(const t3_practice_player_t* p,long v[16]) {
    v[0]=p->total;v[1]=p->combo_damage;v[2]=p->shown_damage;v[3]=p->hit_damage;v[4]=p->hit_share;v[5]=p->marker;
    v[6]=p->hits;v[7]=p->combo;v[8]=p->new_best;v[9]=p->ended;v[10]=p->previous;v[11]=p->counter;v[12]=p->clean;
    v[13]=p->hit_id;v[14]=p->throw_now;v[15]=p->throw_before;
}
int main(int argc,char** argv) {
    FILE* f=fopen(argc>1?argv[1]:"build/practice-golden/cases.txt","r");
    if(!f){puts("no practice golden cases: skipped");return 77;}
    unsigned marker_frames=0,marker_bad=0;
    {   /* The hit markers: S+0x54's four (attack word, frames left) and the index at S+0x74. */
        FILE* m=fopen(argc>3?argv[3]:"build/practice-golden/markers.txt","r");
        long w[20];
        while(m) {
            unsigned n=0;
            while(n<20 && fscanf(m,"%ld",&w[n])==1)++n;
            if(n<20)break;
            t3_practice_markers_t mk={{0},{0},(uint8_t)w[0]};
            for(unsigned i=0;i<4;++i){mk.attack[i]=(int16_t)w[1+2*i];mk.frames[i]=(int16_t)w[2+2*i];}
            t3_practice_markers_step(&mk,(uint16_t)w[10],(int)w[9]);
            ++marker_frames;
            int ok=mk.index==(uint8_t)w[11];
            for(unsigned i=0;i<4;++i)ok=ok && mk.attack[i]==(int16_t)w[12+2*i] && mk.frames[i]==(int16_t)w[13+2*i];
            if(!ok){if(marker_bad<10)fprintf(stderr,"marker frame %u differs\n",marker_frames);++marker_bad;}
        }
        if(m)fclose(m);
        printf("hit markers: %u frames, %u mismatches\n",marker_frames,marker_bad);
    }
    unsigned key_frames=0,key_bad=0;
    {   /* The key ring (FUN_800B7B08): the ring entry and head before, the pad word, the entry and head after. */
        FILE* k=fopen(argc>2?argv[2]:"build/practice-golden/keys.txt","r");
        long w[6];
        while(k && fscanf(k,"%ld %ld %ld %ld %ld %ld",&w[0],&w[1],&w[2],&w[3],&w[4],&w[5])==6) {
            t3_practice_keys_t ring;t3_practice_keys_reset(&ring);
            ring.head=(int32_t)w[0];ring.word[ring.head]=(uint32_t)w[1];
            /* The ring was emptied before logging when the original's head came back to its start. */
            if(w[0]>1 && w[4]==1)t3_practice_keys_reset(&ring);
            t3_practice_keys_log(&ring,(uint16_t)w[2],(int)w[3]);
            ++key_frames;
            if(ring.head!=(int32_t)w[4] || ring.word[ring.head]!=(uint32_t)w[5]) {
                if(key_bad<10)fprintf(stderr,"key frame %u: head %d word %08x, original head %ld word %08lx\n",key_frames,ring.head,
                    (unsigned)ring.word[ring.head],w[4],(unsigned long)w[5]&0xFFFFFFFFul);
                ++key_bad;
            }
        }
        if(k)fclose(k);
        printf("key ring: %u frames, %u mismatches\n",key_frames,key_bad);
    }
    unsigned lines=0,bad=0,seen_hit=0,seen_combo=0;
    long v[96];
    for(;;) {
        unsigned n=0;
        while(n<96 && fscanf(f,"%ld",&v[n])==1)++n;
        if(n<96)break;
        t3_practice_data_t d;t3_practice_facts_t facts[2];
        t3_practice_data_reset(&d);
        load_player(&d.p[0],v);load_player(&d.p[1],v+16);
        for(unsigned i=0;i<2;++i) {
            const long* x=v+32+16*i;
            facts[i]=(t3_practice_facts_t){.hit_clean=(uint8_t)x[0],.counter_hit=(uint8_t)x[1],.close_hit=(uint8_t)x[2],
                .got_hit=(uint8_t)x[3],.was_hit_this_move=(uint8_t)x[4],.in_reaction=(uint8_t)x[5],
                .move_changed=(uint8_t)x[6],.whiffed=(uint8_t)x[7],.last_extra_damage=(int16_t)x[8],
                .throw_state=(int16_t)x[9],.damage=(int16_t)x[10],.base_damage=(int16_t)x[11],
                .move_flags=(uint32_t)x[12],.cur_slot=(uint16_t)x[13],.bank_type=(uint8_t)x[14],
                .react_chain=(int16_t)x[15]};
            if(x[0])++seen_hit;
        }
        t3_practice_data_step(&d,facts);
        for(unsigned p=0;p<2;++p) {
            long got[16];store(&d.p[p],got);
            const long* want=v+64+16*p;
            if(want[7]>1)++seen_combo;
            for(unsigned k=0;k<16;++k)
                if(got[k]!=want[k]) {
                    if(bad<20)fprintf(stderr,"line %u player %u %s: got %ld, original %ld\n",lines+1,p,NAMES[k],got[k],want[k]);
                    ++bad;
                }
        }
        ++lines;
    }
    fclose(f);
    printf("%u frames, %u clean hits, %u frames inside a combo, %u mismatches\n",lines,seen_hit,seen_combo,bad);
    return lines && !bad && !key_bad && !marker_bad?0:1;
}
