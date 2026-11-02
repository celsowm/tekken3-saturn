#undef NDEBUG
#include "stage_rules.h"
#include <assert.h>

int main(void) {
    assert(t3_stage_side(1)==0u);
    assert(t3_stage_side(0)==1u);
    const uint8_t plain[2]={0,1},first[2]={20,1},second[2]={0,20},near[2]={19,21};
    assert(!t3_stage_true_ogre(plain));
    assert(t3_stage_true_ogre(first));
    assert(t3_stage_true_ogre(second));
    assert(!t3_stage_true_ogre(near));
    return 0;
}
