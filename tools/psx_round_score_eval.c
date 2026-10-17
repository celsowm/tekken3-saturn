/* Host adapter for the same normal-VS scorer linked into the Saturn game. */
#include "psx_round.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char** argv) {
    long x[13];
    const long lower[13]={0,0,0,0,1,1,1,0,0,0,0,-1,-1};
    const long upper[13]={4096,4096,5,5,9,5,9,3,1,1,3,1,1};
    t3_psx_round_score_t s={0};
    if(argc!=14)return 2;
    for(unsigned i=0;i<13;++i) {
        char* end;
        errno=0;x[i]=strtol(argv[i+1],&end,10);
        if(errno || end==argv[i+1] || *end || x[i]<lower[i] || x[i]>upper[i])return 2;
    }
    s.wins[0]=(uint16_t)x[2];s.wins[1]=(uint16_t)x[3];s.played=(uint16_t)x[4];
    s.to_win=(uint16_t)x[5];s.max_rounds=(int16_t)x[6];s.human_mask=(uint8_t)x[7];
    s.tie_choice=(uint8_t)x[8];s.draw_wins=(uint8_t)x[9];s.match_result=(uint8_t)x[10];
    s.match_winner=(int8_t)x[11];s.match_loser=(int8_t)x[12];
    if(!t3_psx_round_award(&s,(uint16_t)x[0],(uint16_t)x[1]))return 1;
    printf("{\"wins\":[%u,%u],\"round_won\":[%d,%d],\"max_rounds\":%d,"
           "\"match_result\":%u,\"match_winner\":%d,\"match_loser\":%d}\n",
           s.wins[0],s.wins[1],s.round_won[0],s.round_won[1],s.max_rounds,
           s.match_result,s.match_winner,s.match_loser);
    return 0;
}
