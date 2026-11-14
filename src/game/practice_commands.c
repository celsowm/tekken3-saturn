/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * See practice_commands.h and NOTICE.md. */
#include "practice_commands.h"

static uint32_t skip_string(const uint8_t* blob,uint32_t size,uint32_t at) {
    while(at<size && blob[at])++at;
    return at<size?at+1u:size+1u;
}

/* The offset of a character's line count, or 0. */
static uint32_t character_offset(const uint8_t* blob,uint32_t size,unsigned character) {
    if(!blob || size<7u || blob[0]!='T' || blob[1]!='3' || blob[2]!='C' || blob[3]!='L' || character>=blob[4])return 0;
    uint32_t at=5;
    for(unsigned c=0;c<character;++c) {
        if(at+2u>size)return 0;
        const unsigned lines=(unsigned)(blob[at]|(blob[at+1]<<8));
        at+=2u;
        for(unsigned l=0;l<lines;++l){at=skip_string(blob,size,at);at=skip_string(blob,size,at);}
        if(at>size)return 0;
    }
    return at+2u<=size?at:0;
}

unsigned t3_practice_command_count(const uint8_t* blob,uint32_t size,unsigned character) {
    const uint32_t at=character_offset(blob,size,character);
    return at?(unsigned)(blob[at]|(blob[at+1]<<8)):0u;
}

int t3_practice_command_line(const uint8_t* blob,uint32_t size,unsigned character,unsigned index,const char** name,const char** command) {
    uint32_t at=character_offset(blob,size,character);
    if(!at || index>=(unsigned)(blob[at]|(blob[at+1]<<8)))return 0;
    at+=2u;
    for(unsigned l=0;l<index;++l){at=skip_string(blob,size,at);at=skip_string(blob,size,at);}
    const uint32_t second=skip_string(blob,size,at);
    if(second>=size)return 0;
    *name=(const char*)(blob+at);*command=(const char*)(blob+second);
    return skip_string(blob,size,second)<=size;
}
