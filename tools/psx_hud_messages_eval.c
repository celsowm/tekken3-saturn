/* Stream adapter for original-code round-text comparisons. */
#include "psx_hud_messages.h"
#include <stdio.h>

int main(void) {
    t3_psx_hud_messages_t f;
    char name[25];
    unsigned counter;
    while(scanf("%d %d %d %d %d %d %d %d %d %u %24s",&f.state,&f.frame,
            &f.played,&f.max_rounds,&f.result,&f.replay_request,&f.human_mask,
            &f.winner,&f.replay_mode,&counter,name)==11) {
        f.frame_counter=counter;f.winner_name=name;
        t3_psx_hud_message_t out[2];
        const unsigned count=t3_psx_hud_messages(&f,out);
        putchar('[');
        for(unsigned i=0;i<count;++i)
            printf("%s[\"%s\",%u,%u,%d,%d]",i?",":"",out[i].text,
                out[i].colour,out[i].font,out[i].x,out[i].y);
        puts("]");
    }
    return ferror(stdin) || !feof(stdin)?2:0;
}
