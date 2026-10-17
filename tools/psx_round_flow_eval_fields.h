/* USA SLUS_004.02 control fields: fixed fingerprint/layout, read-only oracle metadata. */
#ifndef T3_ROUND_FLOW_EVAL_FIELDS_H
#define T3_ROUND_FLOW_EVAL_FIELDS_H
#define T3_ROUND_FLOW_EVAL_FIELDS(X) \
    X(state,0x80096F2Cu,i32) \
    X(counter,0x80096F30u,i32) \
    X(hold,0x80096F38u,i32) \
    X(done,0x80096F3Cu,i32) \
    X(replay_request,0x80096F34u,i32) \
    X(frame,0x80095460u,i32) \
    X(played,0x80095484u,i32) \
    X(timer,0x800ADBCCu,i32) \
    X(result,0x800ADE78u,u16) \
    X(input[0],0x80095470u,i32) \
    X(input[1],0x80095474u,i32) \
    X(freeze,0x80095468u,i32) \
    X(no_damage,0x8009547Cu,i32) \
    X(camera,0x8009546Cu,i32) \
    X(timer_stopped,0x800954B0u,i32) \
    X(to_win,0x800ADDFCu,u16) \
    X(max_rounds,0x800ADF3Cu,u16) \
    X(blend_hold,0x800A8D6Cu,i32) \
    X(human_mask,0x800ADF10u,u16) \
    X(tie_choice,0x800ADF3Eu,u8) \
    X(draw_wins,0x800AFA90u,u8) \
    X(match_result,0x800AFAA9u,u8) \
    X(match_winner,0x800AFAAAu,u8) \
    X(match_loser,0x800AFAABu,u8) \
    X(win_done[0],0x8009E5F8u,i32) \
    X(win_done[1],0x8009E5FCu,i32)
#endif
