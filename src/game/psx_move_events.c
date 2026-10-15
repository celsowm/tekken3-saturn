#include "psx_move_events.h"

static uint16_t le16(const uint8_t* p) {
    return (uint16_t)(p[0]|((uint16_t)p[1]<<8u));
}
int t3_psx_move_events_sample(const t3_divmot_t* bank,
                              const t3_move_record_t* move,
                              unsigned previous_pose_frame,
                              unsigned current_pose_frame,
                              t3_psx_move_events_t* result) {
    t3_psx_move_events_t sample={0};
    const uint8_t* section;
    uint32_t len,ptr;
    unsigned i;
    if(!bank || !bank->data || !move || !result ||
       previous_pose_frame>65535u || current_pose_frame>65535u ||
       bank->sections[5]<bank->sections[4] ||
       bank->sections[5]>bank->size)
        return -1;
    if(move->event_index==0xFFFFFFFFu) {
        *result=sample;
        return 1;
    }
    len=bank->sections[5]-bank->sections[4];
    if(!len)return 0; /* optional original private data NOT imported */
    if(len%4u || move->event_index>len/4u ||
       (uint64_t)move->event_index*4u>=len)
        return -1;
    section=bank->data+bank->sections[4];
    ptr=move->event_index*4u;
    /* A corrupt list lacking a terminator must never read another
     * source section. Real event lists are short; no arbitrary limit. */
    for(i=ptr;i<len;i+=4u) {
        uint16_t frame=le16(section+i);
        uint16_t command;
        unsigned hi;
        if(!frame) {
            *result=sample;
            return 1;
        }
        command=le16(section+i+2u);
        if(frame<=previous_pose_frame || frame>current_pose_frame)
            continue;
        if(sample.fired==65535u)return -1;
        ++sample.fired;
        hi=command>>8u;
        if(hi>=11u && hi<=13u) {
            sample.extra_kind=(uint8_t)(hi-10u);
            sample.extra_damage=(uint8_t)command;
            sample.extra_frame=frame;
        }
    }
    return -1;
}
