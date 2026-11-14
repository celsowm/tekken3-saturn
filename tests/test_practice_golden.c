/* Practice's attack data against the original code: each line of build/practice-golden/cases.txt (made by
 * scripts/export_practice_trace.py from Tekken3Remake's flow trace of the original practice overlay) holds the
 * per-player state before a frame, the fighter records' facts after it, and the state the original stored.
 * Skipped (77) without the file: it is derived from the game and is not in git. */
#include "practice_data.h"
#include "practice_keys.h"
#include "practice_guide.h"
#include "practice_replay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    unsigned replay_frames_n=0,replay_bad=0;
    {   /* REPLAY SETTINGS' counters: REPLAY_FRAMES and COMBO_FRAMES after a frame, and the dummy's dropped combo. */
        FILE* rf=fopen(argc>6?argv[6]:"build/practice-golden/replay.txt","r");
        long w[11];
        while(rf && fscanf(rf,"%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld",&w[0],&w[1],&w[2],&w[3],&w[4],&w[5],&w[6],&w[7],&w[8],&w[9],&w[10])==11) {
            t3_practice_replay_t r={(uint16_t)w[0],(uint16_t)w[1]};
            t3_practice_replay_in_t in={.setting=(uint8_t)w[2],.combo=(uint8_t)w[3],.last=(uint8_t)w[4],.ended=(uint8_t)w[5],
                .acting=(uint8_t)w[6],.select_pressed=(uint8_t)w[7],.move_length=(uint16_t)w[9]};   /* the length is not in the records */
            uint8_t dropped=0;
            const unsigned started=t3_practice_replay_step(&r,&in,&dropped);
            ++replay_frames_n;
            if(started==0 && (r.replay_frames!=(uint16_t)w[8] || r.combo_frames!=(uint16_t)w[9] || dropped!=(uint8_t)w[10])) {
                if(replay_bad<10)fprintf(stderr,"replay frame %u: counters %u/%u dropped %u, original %ld/%ld %ld\n",replay_frames_n,r.replay_frames,r.combo_frames,dropped,w[8],w[9],w[10]);
                ++replay_bad;
            }
        }
        if(rf)fclose(rf);
        printf("replay counters: %u frames, %u mismatches\n",replay_frames_n,replay_bad);
    }
    unsigned guide_frames=0,guide_bad=0;
    {   /* The guide and recorder: S's guide block before a frame, the inputs and facts, and the block after. */
        FILE* gf=fopen(argc>4?argv[4]:"build/practice-golden/guide.txt","r");
        static uint8_t blob[16384];uint32_t blob_size=0;
        FILE* bf=fopen(argc>5?argv[5]:"build/practice-golden/practice-combos.bin","rb");
        if(bf){blob_size=(uint32_t)fread(blob,1,sizeof blob,bf);fclose(bf);}
        long w[140];
        static const char* const NAMES[8]={"state","prompt","prompt_timer","guide_prompt","guide_length","resumed_input","replay_pos","replay_word"};
        while(gf && blob_size) {
            unsigned n=0;
            while(n<140 && fscanf(gf,"%ld",&w[n])==1)++n;
            if(n<140)break;
            const long* in=w;const long* prev=w+16;const long script_prev=w[16+61];const long* next=w+16+62;const long script_next=w[16+62+61];
            t3_practice_guide_t g;memset(&g,0,sizeof g);
            g.state=(uint8_t)prev[0];g.prompt=(uint8_t)prev[1];g.prompt_timer=(uint8_t)prev[2];g.guide_prompt=(uint8_t)prev[3];
            g.guide_length=(uint8_t)prev[4];g.resumed_input=(uint8_t)prev[5];g.replay_pos=(int32_t)prev[6];
            g.replay_word=(uint32_t)prev[7];g.move_count=(int32_t)prev[8];g.script=(uint16_t)script_prev;
            const unsigned cp=(unsigned)in[3];
            g.ring.head=(int32_t)prev[9+cp];
            for(unsigned i=0;i<50;++i)g.ring.word[i]=(uint32_t)prev[11+i];
            const unsigned bank=(unsigned)in[14];
            t3_practice_combo_t combo;
            const int have=in[2]>=0 && t3_practice_combo_get(blob,blob_size,bank,(unsigned)in[2],&combo);
            t3_practice_guide_in_t gi={.sub=(uint8_t)in[0],.guide_on=(uint8_t)in[1],.combo_type=(int8_t)in[2],
                .combo_known=t3_practice_combo_count(blob,blob_size,bank)!=0,.combo=have?&combo:NULL,
                .select_pressed=(in[5]&0x100)!=0,.select_held=(in[4]&0x100)!=0,.held_down=(in[4]&0x4000)!=0,
                .held_horizontal=(in[4]&0xA000)!=0,.mapped_record_key=(in[6]&0xF0FF)!=0,.pad_word=(uint16_t)in[7],
                .side_flag=(uint8_t)in[8],.cur_slot=(uint16_t)in[9],.hit_clean=(uint8_t)in[10],.move_pending=(uint8_t)in[11],
                .hold_turn=(uint8_t)in[12],.turned=(uint8_t)in[13],.me_cur_slot=(uint16_t)in[15],.stance_slot=0xFFFFu,.me_stance_slot=0xFFFFu};
            /* The records hold neither the stance slot nor the move row's state: take them from what the original did. */
            if(prev[0]==T3_GUIDE_STAND) {
                if(next[0]!=T3_GUIDE_STAND)gi.stance_slot=gi.cur_slot;
                else if(gi.cur_slot==3||gi.cur_slot==4||gi.cur_slot==5)gi.lying=1;
            }
            if(prev[0]==T3_GUIDE_REPLAY_END && next[0]!=T3_GUIDE_REPLAY_END)gi.me_stance_slot=gi.me_cur_slot;
            t3_practice_guide_step(&g,&gi);
            long got[8]={g.state,g.prompt,g.prompt_timer,g.guide_prompt,g.guide_length,g.resumed_input,g.replay_pos,(long)g.replay_word};
            int bad=0;
            for(unsigned k=0;k<8;++k) {
                const long want=k==7?(long)(uint32_t)next[7]:next[k];
                if(got[k]!=want){if(guide_bad<12)fprintf(stderr,"guide frame %u: %s got %ld, original %ld (state %ld -> %ld)\n",guide_frames+1,NAMES[k],got[k],want,prev[0],next[0]);bad=1;}
            }
            if(g.ring.head!=(int32_t)next[9+cp]){if(guide_bad<12)fprintf(stderr,"guide frame %u: head %d, original %ld\n",guide_frames+1,g.ring.head,next[9+cp]);bad=1;}
            for(unsigned i=0;i<50;++i)if(g.ring.word[i]!=(uint32_t)next[11+i]){if(guide_bad<12)fprintf(stderr,"guide frame %u: ring[%u] %08x, original %08lx\n",guide_frames+1,i,(unsigned)g.ring.word[i],(unsigned long)next[11+i]&0xFFFFFFFFul);bad=1;break;}
            if((prev[0]==T3_GUIDE_STAND||prev[0]==T3_GUIDE_PLAY||prev[0]==T3_GUIDE_REPLAY) && g.script!=(uint16_t)script_next){
                if(guide_bad<12)fprintf(stderr,"guide frame %u: script %04x, original %04lx (state %ld)\n",guide_frames+1,g.script,(unsigned long)script_next&0xFFFF,prev[0]);bad=1;}
            guide_bad+=(unsigned)bad;++guide_frames;
        }
        if(gf)fclose(gf);
        printf("guide and recorder: %u frames, %u mismatching\n",guide_frames,guide_bad);
    }
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
    return lines && !bad && !key_bad && !marker_bad && !guide_bad && !replay_bad?0:1;
}
