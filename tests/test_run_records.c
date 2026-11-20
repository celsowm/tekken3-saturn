#include "run_records.h"
#include <assert.h>
#include <string.h>
int main(void){
    t3_run_records_t r={0},back={1,1};
    uint8_t block[T3_RUN_RECORDS_BYTES];
    /* Survival: only more wins beat the record; none is no record. */
    assert(!t3_run_records_survival(&r,0));
    assert(t3_run_records_survival(&r,3) && r.survival_wins==3);
    assert(!t3_run_records_survival(&r,3) && !t3_run_records_survival(&r,2));
    assert(t3_run_records_survival(&r,9999) && r.survival_wins==255);
    /* Time Attack: only a completed run counts, and only a faster one beats the record. */
    assert(!t3_run_records_attack(&r,90,0) && !t3_run_records_attack(&r,0,1));
    assert(t3_run_records_attack(&r,300,1) && r.attack_seconds==300);
    assert(!t3_run_records_attack(&r,300,1) && !t3_run_records_attack(&r,301,1));
    assert(t3_run_records_attack(&r,299,1) && r.attack_seconds==299);
    assert(t3_run_records_attack(&r,99999,1)==0 && r.attack_seconds==299);
    /* The record survives a round trip and refuses damage, a wrong size and out-of-range times. */
    assert(t3_run_records_pack(&r,block) && t3_run_records_unpack(block,sizeof block,&back));
    assert(back.survival_wins==255 && back.attack_seconds==299);
    for(unsigned i=0;i<sizeof block;++i){
        uint8_t bad[T3_RUN_RECORDS_BYTES];memcpy(bad,block,sizeof bad);bad[i]^=1u;
        assert(!t3_run_records_unpack(bad,sizeof bad,&back));
    }
    assert(!t3_run_records_unpack(block,sizeof block-1,&back));
    t3_run_records_t high={0,T3_RUN_RECORDS_MAX_SECONDS+1u};
    assert(!t3_run_records_pack(&high,block));
    return 0;
}
