#include "psx_reversal.h"
#include <stdio.h>
static int query(void* user,const t3_branch_record_t* row) {
    return t3_psx_reversal_condition(row->condition,row->parameter,user);
}
int main(int argc,char** argv) {
    uint8_t own[1024],shared[1024];size_t sizes[2];
    if(argc!=3)return 2;
    for(unsigned i=0;i<2;++i) {
        FILE* f=fopen(argv[i+1],"rb");if(!f)return 2;
        sizes[i]=fread(i?shared:own,1,1024,f);
        const int bad=ferror(f) || fgetc(f)!=EOF;fclose(f);
        if(bad || sizes[i]%12)return 2;
    }
    t3_divmot_t bank={0};bank.data=own;bank.branch_count=(uint16_t)(sizes[0]/12);
    bank.common_branches=shared;bank.common_branch_count=(uint16_t)(sizes[1]/12);
    t3_input_history_t input;t3_history_clear(&input);
    t3_branch_context_t restrictions={0};restrictions.known=T3_FACT_IDENTITY;restrictions.opponent_bank=1;
    unsigned frame,hit;int angle,other_angle,slot,attack;
    while(scanf("%u %u %d %d %d %d",&frame,&hit,&angle,&other_angle,&slot,&attack)==6) {
        t3_psx_reversal_facts_t facts={(int16_t)angle,(int16_t)other_angle,(int16_t)attack,
            (int16_t)slot,(uint8_t)hit,{6,10,14,17},0,NULL,NULL};
        t3_branch_record_t picked;
        const int found=t3_psx_reversal_find(&bank,(t3_move_record_t){0},&input,frame,0,
            &restrictions,NULL,0,query,&facts,&picked);
        printf("%d %d\n",found,found>0?(int)picked.target_slot:-1);
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
