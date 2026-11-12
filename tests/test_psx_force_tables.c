/* A correctly sized zero-filled private asset used to boot, then crash on
 * the first active enemy. Reject it at the asset boundary. */
#include "psx_force_level.h"
#include <assert.h>
#include <string.h>
static uint8_t tables[T3_FORCE_TABLE_BYTES];
static void put32(unsigned at,unsigned value){
    for(unsigned i=0;i<4;++i)tables[at+i]=(uint8_t)(value>>(i*8u));
}
int main(void){
    assert(!t3_force_tables_valid(NULL,sizeof tables));
    assert(!t3_force_tables_valid(tables,sizeof tables));
    for(unsigned i=0;i<25;++i)put32(0x13Cu+4u*i,20u*65536u);
    for(unsigned i=0;i<4;++i)put32(0x1C0u+4u*i,5000u);
    assert(t3_force_tables_valid(tables,sizeof tables));
    assert(!t3_force_tables_valid(tables,sizeof tables-1u));
    for(unsigned i=0;i<25;++i){
        put32(0x13Cu+4u*i,0);
        assert(!t3_force_tables_valid(tables,sizeof tables));
        put32(0x13Cu+4u*i,20u*65536u);
    }
    tables[0x1A0]=4;
    assert(!t3_force_tables_valid(tables,sizeof tables));
    tables[0x1A0]=0;
    put32(0x1C0,0);
    assert(!t3_force_tables_valid(tables,sizeof tables));
    return 0;
}
