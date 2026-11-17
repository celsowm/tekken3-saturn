/* Golden-case test of the Tekken Ball ball (src/game/ball_rules.c): the cases come from
 * scripts/export_ball_cases.py (set T3_BALL_CASES to build/ball-cases/rules.bin). Skips (77) when the file is
 * missing. */
#include "ball_rules.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { M_UPDATE, M_AFTER, M_TUNING, M_START };

static int32_t be32(const uint8_t* p){return (int32_t)((uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]);}

typedef struct { t3_ball_t ball; t3_ball_world_t world; t3_ball_fighter_t f[2]; } state_t;

static void unpack(state_t* s,const uint8_t* at,int n) {
    int32_t* v=(int32_t*)s;
    for(int i=0;i<n;++i)v[i]=be32(at+4*i);
}

int main(void) {
    const char* path=getenv("T3_BALL_CASES");
    FILE* fp=path?fopen(path,"rb"):NULL;
    if(!fp){puts("skip: no golden cases");return 77;}
    fseek(fp,0,SEEK_END);long size=ftell(fp);fseek(fp,0,SEEK_SET);
    uint8_t* d=malloc((size_t)size);
    if(!d||fread(d,1,(size_t)size,fp)!=(size_t)size){fclose(fp);return 1;}
    fclose(fp);
    if((uint32_t)be32(d)!=0x54334243u||be32(d+4)!=1){fputs("bad case file\n",stderr);return 1;}
    const int n=be32(d+8),cases=be32(d+12),trig_bytes=be32(d+16),atan_bytes=be32(d+20);
    if((size_t)n*4!=sizeof(state_t)){fprintf(stderr,"vector %d words, state %zu\n",n,sizeof(state_t)/4);return 1;}
    const uint8_t* trig=d+24;
    const uint8_t* atan=trig+trig_bytes;
    const uint8_t* skip=atan+atan_bytes;
    const uint8_t* at=skip+n;
    t3_ball_tables_t t;
    memset(&t,0,sizeof t);
    t.tables.ground.trig.data=trig;t.tables.ground.trig.size=(size_t)trig_bytes;
    t.tables.ground.atan=atan;t.tables.ground.atan_size=(size_t)atan_bytes;
    unsigned bad=0,fields=0,bad_events=0;
    unsigned by_mode[4]={0,0,0,0},total_mode[4]={0,0,0,0};
    for(int c=0;c<cases;++c) {
        const int mode=be32(at),s7=be32(at+4);
        uint16_t row[12];
        state_t s,want;
        t3_ball_events_t ev={{{0}},0};
        for(int i=0;i<12;++i)row[i]=(uint16_t)be32(at+8+4*i);
        at+=8+48;
        unpack(&s,at,n);at+=4*n;
        unpack(&want,at,n);at+=4*n;
        const int want_events=be32(at);at+=4;
        int32_t wev[16][6];
        for(int i=0;i<16;++i){for(int k=0;k<6;++k)wev[i][k]=be32(at+4*(6*i+k));}
        at+=4*6*16;
        switch(mode) {
        case M_UPDATE:t3_ball_update(&s.ball,&s.world,s.f,&t,s7,&ev);break;
        case M_AFTER:t3_ball_after_update(&s.ball,&s.world,&t,&ev);break;
        case M_TUNING:t3_ball_move_tuning(&s.f[s7&1]);break;
        case M_START:t3_ball_fight_start(&s.ball,&s.world,row);break;
        }
        ++total_mode[mode&3];
        const int32_t* got=(const int32_t*)&s;
        const int32_t* exp=(const int32_t*)&want;
        int bad_here=0;
        for(int i=0;i<n;++i) {
            if(skip[i]||got[i]==exp[i])continue;
            if(bad<12)printf("case %d mode %d field %d: %d, want %d\n",c,mode,i,got[i],exp[i]);
            ++fields;bad_here=1;
        }
        /* events: the stand-in copy is the caller's, the original has no call for it */
        int g=0,w=0,ev_bad=0;
        for(;;) {
            while(g<ev.count&&ev.e[g].kind==BALL_EV_STAND_IN)++g;
            if(g>=ev.count||w>=want_events)break;
            const t3_ball_event_t* e=&ev.e[g];
            if(e->kind!=wev[w][0]||e->a!=wev[w][1]||e->b!=wev[w][2]||
               (e->kind==BALL_EV_POPUP&&(e->c!=wev[w][3]||e->d!=wev[w][4]||e->e!=wev[w][5])))ev_bad=1;
            ++g;++w;
        }
        while(g<ev.count&&ev.e[g].kind==BALL_EV_STAND_IN)++g;
        if(g!=ev.count||w!=want_events)ev_bad=1;
        if(ev_bad) {
            if(bad_events<8) {
                printf("case %d mode %d events:",c,mode);
                for(int i=0;i<ev.count;++i)printf(" [%d %d %d]",ev.e[i].kind,ev.e[i].a,ev.e[i].b);
                printf(" want");
                for(int i=0;i<want_events;++i)printf(" [%d %d %d]",wev[i][0],wev[i][1],wev[i][2]);
                printf("\n");
            }
            ++bad_events;
        }
        if(bad_here||ev_bad){++bad;++by_mode[mode&3];}
    }
    printf("%d cases, %u mismatching (%u field values, %u event lists); update %u/%u after %u/%u tuning %u/%u start %u/%u\n",
           cases,bad,fields,bad_events,by_mode[0],total_mode[0],by_mode[1],total_mode[1],by_mode[2],total_mode[2],
           by_mode[3],total_mode[3]);
    free(d);
    return bad?1:0;
}
