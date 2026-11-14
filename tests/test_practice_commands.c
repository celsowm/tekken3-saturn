#include "practice_commands.h"
#include <assert.h>
#include <string.h>
int main(void) {
    /* two characters: the first with two lines, the second with none */
    uint8_t blob[64];unsigned n=0;
    memcpy(blob,"T3CL",4);n=4;blob[n++]=2;
    blob[n++]=2;blob[n++]=0;
    memcpy(blob+n,"Jab\0f,1\0",8);n+=8;
    memcpy(blob+n,"Kick\0d,3\0",9);n+=9;
    blob[n++]=0;blob[n++]=0;
    assert(t3_practice_command_count(blob,n,0)==2 && t3_practice_command_count(blob,n,1)==0 && t3_practice_command_count(blob,n,2)==0);
    const char *name,*command;
    assert(t3_practice_command_line(blob,n,0,0,&name,&command) && !strcmp(name,"Jab") && !strcmp(command,"f,1"));
    assert(t3_practice_command_line(blob,n,0,1,&name,&command) && !strcmp(name,"Kick") && !strcmp(command,"d,3"));
    assert(!t3_practice_command_line(blob,n,0,2,&name,&command) && !t3_practice_command_line(blob,n,1,0,&name,&command));
    assert(t3_practice_command_count(NULL,0,0)==0 && t3_practice_command_count(blob,n-1,1)==0);
    return 0;
}
