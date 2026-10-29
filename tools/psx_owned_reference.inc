/* Shared owned-camera/state visitor. Requires game/config, has, read helpers,
 * check and REFERENCE_NAME. Expected expressions are host-only; the Saturn
 * macro hashes actual fields and does not evaluate/read expected RAM. */
static void compare_owned_camera(void) {
    t3_psx_camera_matrix_t view;
    t3_psx_match_fighter_view(&game.camera,&view);
    for(unsigned r=0;r<3;++r)for(unsigned k=0;k<3;++k) {
        char name[24];REFERENCE_NAME(name,sizeof(name),"matrix_%u_%u",r,k);
        check(0,"camera",name,view.m[r][k],i16(0x800ADE20u+6*r+2*k));
    }
    for(unsigned k=0;k<3;++k) {
        char name[24];REFERENCE_NAME(name,sizeof(name),"translation_%u",k);
        check(0,"camera",name,view.t[k],i32(0x800ADE34u+4*k));
    }
    check(0,"camera","projection",view.h,i16(0x800A8CD4u));
    if(has(0x800A02A8u,120))for(unsigned i=0;i<5;++i) {
        const t3_psx_camera_source_t* source=&game.camera.camera.source[i];
        const int32_t values[6]={source->x,source->y,source->z,source->pitch,source->yaw,source->h};
        for(unsigned k=0;k<6;++k) {
            char name[24];REFERENCE_NAME(name,sizeof(name),"source_%u_%u",i,k);
            check(0,"camera",name,values[k],i32(0x800A02A8u+24*i+4*k));
        }
    }
    if(has(0x800A8C68u,0x70u)) {
        check(0,"camera","state",game.camera.camera.state,i16(0x800A8C70u));
        for(unsigned i=0;i<5;++i) {
            char name[24];REFERENCE_NAME(name,sizeof(name),"weight_%u",i);
            check(0,"camera",name,game.camera.camera.weight[i],i16(0x800A8C78u+2*i));
        }
    }
    if(has(0x800A39B8u,4))check(0,"camera","rand",game.camera.rand,u32(0x800A39B8u));
    if(has(0x800A912Cu,4))check(0,"camera","camera_rand",game.camera.camera_rand,u32(0x800A912Cu));
    if(has(0x800ADCA0u,4))check(0,"camera","ai_rand",game.camera.ai_rand,u32(0x800ADCA0u));
    if(has(0x800ADCD4u,4))check(0,"camera","third_target_screen_x",game.camera.camera.third_target_screen_x,i32(0x800ADCD4u));
    if(has(0x800ADCA8u,0x50u))for(unsigned i=0;i<2;++i)for(unsigned k=0;k<4;++k) {
        char name[24];REFERENCE_NAME(name,sizeof(name),"target_%u_%u",i,k);
        check(i,"camera",name,game.camera.camera.target[i][k],i32(0x800ADCA8u+16*i+4*k));
        REFERENCE_NAME(name,sizeof(name),"prev_target_%u_%u",i,k);
        check(i,"camera",name,game.camera.camera.prev_target[i][k],i32(0x800ADCD8u+16*i+4*k));
    }
    check(0,"camera","phase",game.camera.phase,i16(0x8009546Cu));
    for(unsigned i=0;i<2;++i)
        check(i,"camera","screen_x",game.camera.screen_x[i],i16(0x800A9228u+i*0x188Cu+0x1278u));
    for(unsigned i=0;i<2;++i) {
        const uint32_t b=0x800A9228u+i*0x188Cu;
        (void)b;
        check(i,"input","history_head_valid",1,i32(b+0x40Cu)>=0 && i32(b+0x40Cu)<60);
        for(unsigned age=0;age<60;++age) {
            const t3_command_frame_t input=t3_history_at(&game.fighter[i].input,age);
            char name[24];REFERENCE_NAME(name,sizeof(name),"history_%u",age);
            check(i,"input",name,(input.pressed_buttons<<4)|input.direction,
                  u8(b+0x41Cu+(unsigned)(i32(b+0x40Cu)+60-(int)age)%60u));
        }
        const t3_command_frame_t input=t3_history_at(&game.fighter[i].input,0);
        check(i,"input","held",input.held_buttons,u16(b+0x406));
        check(i,"input","pressed",input.pressed_buttons,u16(b+0x404));
    }
}

static void compare_owned_auxiliary(void) {
    const int hud_checked=config.start_intro && has(0x80097D10u,0x20u);
    const int sound_checked=config.start_intro && config.sound_tables.size && has(0x800971DCu,0x1Au);
    if(config.start_intro) {
        check(0,"input","frame_seen",game.input_frame_seen,u32(0x80095464u));
        for(unsigned i=0;i<2;++i) {
            const uint32_t b=0x800A9228u+i*0x188Cu;(void)b;
            if(sound_checked) {
                const t3_psx_sound_state_t* s=&game.sound[i];
                check(i,"sound","script",s->script,i16(b+0x70));
                check(i,"sound","last_script",s->last_script,i16(b+0x72));
                check(i,"sound","position",s->position,i16(b+0x11E));
                check(i,"sound","frame",s->frame,i16(b+0x120));
                check(i,"sound","voice_cooldown",s->voice_cooldown,i16(0x800971E8u+2*i));
                check(i,"sound","damage_voice_timer",s->damage_voice_timer,i32(0x800971DCu+4*i));
                check(i,"sound","hold",s->hold,i16(0x800971F0u+2*i));
            }
            /* Internal RNG/eye parity requires a capture that includes
             * the shared generator, beyond the older combat schema. */
            if(config.camera && has(0x800A39B8u,4)) {
                check(i,"animation","blink_timer",game.fighter[i].blink.timer,i16(b+0x128E));
                check(i,"animation","blink_closed",game.fighter[i].blink.closed,u8(b+0x1290));
            }
            const t3_psx_tap_latch_t* tap=&game.fighter[i].taps;
            check(i,"input","tap_lp",tap->lp,u8(b+0xE0));
            check(i,"input","tap_rp",tap->rp,u8(b+0xE1));
            check(i,"input","tap_both",tap->both,u8(b+0xE2));
            check(i,"input","tap_condition_used",tap->condition_used,u8(b+0x8C));
            if(has(0x80097048u,4))
                check(i,"input","tap_previous",tap->previous,u16(0x80097048u+2*i));
        }
    }
    if(hud_checked)for(unsigned i=0;i<2;++i) {
        const uint32_t b=0x80097D14u+16u*i;(void)b;
        const t3_psx_hud_bar_t* bar=&game.hud_bars[i];
        check(i,"hud","last_health",bar->last_health,i32(b));
        check(i,"hud","target",bar->target,i16(b+4));
        check(i,"hud","drawn",bar->drawn,i16(b+6));
        check(i,"hud","recent",bar->recent,i16(b+8));
    }
}
