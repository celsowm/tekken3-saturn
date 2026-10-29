/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic
 * Normal round messages adapted from pinned Tekken3Remake round_hud.gd,
 * hud.md and fight-frame.md. Explicit facts and bounded output replace
 * Godot state and presentation objects. See NOTICE.md. */
#include "psx_hud_messages.h"
#include "psx_round_flow.h"
#include "psx_replay.h"

static void message(t3_psx_hud_message_t* out,const char* text,
                    unsigned colour,unsigned font,int x,int y) {
    unsigned i=0;
    *out=(t3_psx_hud_message_t){.x=(int16_t)x,.y=(int16_t)y,
        .colour=(uint8_t)colour,.font=(uint8_t)font};
    while(text[i] && i<sizeof out->text-1u) {out->text[i]=text[i];++i;}
}
unsigned t3_psx_hud_messages(const t3_psx_hud_messages_t* f,t3_psx_hud_message_t out[2]) {
    unsigned count=0;
    if(f->replay_mode>=T3_REPLAY_PLAYING && f->replay_mode<=T3_REPLAY_FIRST) {
        /* ReplayUpdate draws this only in its playing branch, before the
         * half-frame cursor advances. The caller supplies that entry mode. */
        if(f->replay_mode==T3_REPLAY_PLAYING && (f->frame_counter&16u)) {
            message(out,"REPLAY",7,1,244,30);return 1;
        }
        return 0;
    }
    if(f->state==T3_ROUND_INTRO && f->frame!=0) {
        if(f->max_rounds<=f->played)message(&out[count++],"FINAL ROUND",8,2,63,112);
        else if(f->frame>=(f->played==1?61:1)) {
            message(&out[count],"ROUND 0",8,2,107,112);
            out[count++].text[6]="0123456789ABCDEF"[(unsigned)f->played&15u];
        }
        if(f->frame>=(f->played==1?106:46))message(&out[count++],"READY?",9,2,118,228);
    } else if(f->state==T3_ROUND_RESULT && !f->replay_request) {
        if(f->result&1)message(out,"TIME UP",3,2,107,228);
        else if(f->result&8)message(out,"PERFECT!",2,2,96,228);
        else if(f->result&4)message(out,"DOUBLE K.O.",1,2,63,228);
        else message(out,"K.O.",1,2,140,228);
        count=1;
    } else if(f->state==T3_ROUND_WIN_WAIT || f->state==T3_ROUND_END) {
        if(f->winner<0)message(out,"DRAW",6,2,140,368);
        else if(f->human_mask==3) {
            unsigned length=0;
            /* Leave space for the suffix and terminator. */
            while(f->winner_name[length] && length<sizeof out->text-7u)++length;
            message(out,f->winner_name,4,2,184-(int)(11*length+66),368);
            const char suffix[]=" WINS!";
            for(unsigned i=0;i<sizeof suffix;++i)out->text[length+i]=suffix[i];
        } else if(((unsigned)f->human_mask>>(unsigned)f->winner)&1u)
            message(out,"YOU WIN!",4,2,96,368);
        else message(out,"YOU LOSE",5,2,96,368);
        count=1;
    }
    return count;
}
