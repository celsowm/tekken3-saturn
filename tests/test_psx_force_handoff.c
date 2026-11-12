/* The Tekken Force hand-off block round-trips, fits sat_exec's argument and rejects anything else. */
#include "psx_force_handoff.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <string.h>

int main(void){
    _Static_assert(T3_FORCE_HANDOFF_BYTES<=64u,"the block must fit SAT_EXEC_ARG_MAX");
    t3_force_handoff_t a={2,T3_FORCE_SECTION_BOSS,9,1,2,1|T3_FORCE_FLAG_SAVED_DOCTOR,
                          99999990u,12345u,-60,0x7FFFFFFF,4000,0xDEADBEEFu,1,{1,45,36,255},1},b;
    uint8_t block[T3_FORCE_HANDOFF_BYTES];
    t3_force_handoff_pack(&a,block);
    assert(t3_force_handoff_unpack(block,sizeof block,&b));
    assert(a.new_record==b.new_record);
    assert(a.level==b.level&&a.section==b.section&&a.character==b.character&&a.costume==b.costume&&a.keys==b.keys);
    assert(a.flags==b.flags&&a.score==b.score&&a.hiscore==b.hiscore&&a.timer==b.timer&&a.health==b.health);
    assert(a.kills==b.kills&&a.seed==b.seed&&a.pad_side==b.pad_side);
    assert(!t3_force_handoff_unpack(block,sizeof block-1u,&b));
    assert(!t3_force_handoff_unpack(block,16,&b));      /* the 16-byte boot argument is not ours */
    for(unsigned i=0;i<sizeof block;++i){
        uint8_t bad[sizeof block];
        memcpy(bad,block,sizeof block);
        bad[i]^=0x10;
        assert(!t3_force_handoff_unpack(bad,sizeof bad,&b));   /* every byte is covered */
    }
    assert(memcmp(a.bosses,b.bosses,sizeof a.bosses)==0);
    a.level=4;
    a.flags=T3_FORCE_FLAG_DOCTOR_UNLOCKED;
    t3_force_handoff_pack(&a,block);
    assert(t3_force_handoff_unpack(block,sizeof block,&b));
    a.level=5;
    t3_force_handoff_pack(&a,block);
    assert(!t3_force_handoff_unpack(block,sizeof block,&b));    /* out of range, even with a good checksum */
    return 0;
}
