#include "psx_reversal.h"
#include <inttypes.h>
#include <stdio.h>

typedef struct {int result,list,called;} query_t;
static int query(void* user,int16_t list) {
    query_t* q=user;q->list=list;q->called=1;return q->result;
}
int main(void) {
    unsigned type,p,hit,j0,j1,j2,j3;int self,opp,attack,slot,anim;
    uint32_t flags;
    while(scanf("%u %u %d %d %u %"SCNu32" %d %d %u %u %u %u %d",
          &type,&p,&self,&opp,&hit,&flags,&attack,&slot,&j0,&j1,&j2,&j3,&anim)==13) {
        query_t q={anim,0,0};
        t3_psx_reversal_facts_t f={(int16_t)self,(int16_t)opp,(int16_t)attack,
            (int16_t)slot,(uint8_t)hit,{(uint8_t)j0,(uint8_t)j1,(uint8_t)j2,(uint8_t)j3},flags,query,&q};
        const int result=t3_psx_reversal_condition((uint8_t)type,(uint16_t)p,&f);
        printf("%d %d %d\n",result,q.called,q.list);
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
