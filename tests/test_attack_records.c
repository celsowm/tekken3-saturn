#include "attack_records.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { SIZE=132+256 };
static uint8_t bank_data[SIZE];

static void le16(uint8_t* p, unsigned v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static void le32(uint8_t* p, uint32_t v) {
    le16(p,v);le16(p+2u,v>>16);
}
static void point(uint8_t* p, int x, int y, int z) {
    uint64_t packed=(uint64_t)(x+4096) |
        ((uint64_t)(y+4096)<<13) | ((uint64_t)(z+8192)<<26);
    for (unsigned i=0;i<5u;++i) p[i]=(uint8_t)(packed>>(8u*i));
}
static void fixture(unsigned kind) {
    uint8_t* move=bank_data+76;
    uint8_t* desc=bank_data+132;
    memset(bank_data,0,sizeof(bank_data));
    bank_data[1]=99;le16(bank_data+2,1);
    le32(bank_data+4,64);
    le32(bank_data+8,76);
    le32(bank_data+12,132);
    for (unsigned i=3;i<=9;++i) le32(bank_data+4+i*4u,132);
    for (unsigned i=10;i<15;++i) le32(bank_data+4+i*4u,SIZE);
    le16(bank_data+64,0xC000);
    move[24]=8; move[45]=1; move[46]=3;
    le32(move+40,4);
    desc[0]=2;desc[1]=3;desc[2]=4;desc[3]=5;
    desc[4]=(uint8_t)(kind>>8);desc[5]=(uint8_t)kind;
    for (unsigned i=0;i<45u;++i)
        point(desc+6+5*i,100+(int)i*7,-(int)i,(int)i*2);
}
static void eq(t3_attack_point_t p, int x, int y, int z) {
    assert(p.x==x);assert(p.y==y);assert(p.z==z);
}
static void layouts(void) {
    const unsigned kinds[]={0,1,2,3,4,5,6,7,9,10,12,13,14};
    const int expected[13][4]={
        {0,1,-1,-1}, {0,1,-1,-1},{0,2,1,3},
        {1,2,0,3}, {0,1,2,3},{0,1,2,3},
        {0,1,2,1}, {0,1,1,2}, {1,2,2,1},
        {0,1,2,1},{0,1,2,0},{0,1,0,2},{0,1,2,2}
    };
    for(unsigned k=0;k<sizeof(kinds)/sizeof(kinds[0]);++k) {
        t3_divmot_t bank={0};
        t3_move_record_t move={0};
        t3_attack_sample_t sample={0};
        fixture(kinds[k]);
        assert(t3_divmot_open(&bank,bank_data,sizeof(bank_data)));
        assert(t3_divmot_move(&bank,0,&move));
        assert(move.attack_descriptor==4);
        assert(t3_attack_sample(&bank,&move,0,0,&sample)==0);
        assert(t3_attack_sample(&bank,&move,4,0,&sample)==0);
        assert(t3_attack_sample(&bank,&move,1,0,&sample)==1);
        assert(sample.layout_kind==kinds[k]);
        assert(sample.segment_count==(kinds[k]<=1?1:2));
        assert(sample.joint_a[0]==2 && sample.joint_b[0]==3);
        assert(sample.joint_a[1]==4 && sample.joint_b[1]==5);
        eq(sample.segments[0].start,100+expected[k][0]*7,-expected[k][0],expected[k][0]*2);
        eq(sample.segments[0].end,100+expected[k][1]*7,-expected[k][1],expected[k][1]*2);
        if(sample.segment_count==2) {
            eq(sample.segments[1].start,100+expected[k][2]*7,-expected[k][2],expected[k][2]*2);
            eq(sample.segments[1].end,100+expected[k][3]*7,-expected[k][3],expected[k][3]*2);
        }
        if(kinds[k]==1||kinds[k]==2||kinds[k]==4||kinds[k]==6||kinds[k]==7)
            assert(sample.segments[0].previous_start==1);
        else assert(sample.segments[0].previous_start==0);
    }
}
static void malformed(void) {
    t3_divmot_t bank={0};
    t3_move_record_t move={0};
    t3_attack_sample_t sample={0};
    fixture(5);
    assert(t3_divmot_open(&bank,bank_data,sizeof(bank_data)));
    assert(t3_divmot_move(&bank,0,&move));
    assert(t3_attack_sample(&bank,&move,3,0,&sample)==1);
    bank.sections[10]=bank.sections[9]+9;
    assert(t3_attack_sample(&bank,&move,2,0,&sample)==-1);
    bank.sections[10]=SIZE;
    move.attack_descriptor=0x7ff0;
    assert(t3_attack_sample(&bank,&move,1,0,&sample)==0);
    move.attack_descriptor=0;
    assert(t3_attack_sample(&bank,&move,1,0,&sample)==0);
    move.attack_descriptor=SIZE;
    assert(t3_attack_sample(&bank,&move,1,0,&sample)==-1);
    fixture(8);
    assert(t3_divmot_open(&bank,bank_data,sizeof(bank_data)));
    assert(t3_divmot_move(&bank,0,&move));
    assert(t3_attack_sample(&bank,&move,1,0,&sample)==-1);
    /* Alternative packed points start after the descriptor header. */
    fixture(0);
    bank_data[132+4]=0x80;
    bank_data[132+5]=0x50; /* alt1=5 bytes; alt2=10 bytes */
    assert(t3_divmot_open(&bank,bank_data,sizeof(bank_data)));
    assert(t3_divmot_move(&bank,0,&move));
    assert(t3_attack_sample(&bank,&move,1,0,&sample)==1);
    eq(sample.segments[0].start,100,0,0);
    assert(t3_attack_sample(&bank,&move,1,1,&sample)==1);
    assert(sample.alternative_selected==1);
    eq(sample.segments[0].start,107,-1,2);
    assert(t3_attack_sample(&bank,&move,1,2,&sample)==1);
    eq(sample.segments[0].start,114,-2,4);
    assert(t3_attack_sample(&bank,&move,1,3,&sample)==-1);
    fixture(2);
    assert(t3_divmot_open(&bank,bank_data,sizeof(bank_data)));
    assert(t3_divmot_move(&bank,0,&move));
    assert(t3_attack_sample(&bank,&move,2,0,&sample)==1);
    eq(sample.segments[0].start,114,-2,4);
    eq(sample.segments[0].end,128,-4,8);
    eq(sample.segments[1].start,121,-3,6);
    eq(sample.segments[1].end,135,-5,10);

    /* Positive and negative extremes survive the packed 40-bit format. */
    fixture(0);
    point(bank_data+132+6,-4096,4095,-8192);
    assert(t3_divmot_open(&bank,bank_data,sizeof(bank_data)));
    assert(t3_divmot_move(&bank,0,&move));
    assert(t3_attack_sample(&bank,&move,1,0,&sample)==1);
    eq(sample.segments[0].start,-4096,4095,-8192);
}
int main(void) {
    layouts();
    malformed();
    puts("PlayStation attack segments / bounds: passed");
    return 0;
}
