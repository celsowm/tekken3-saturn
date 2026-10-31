#include "char_select.h"
#include "disc.h"
#include "fighter_pack.h"
#include "select_grid.h"
#include "saturn/app.h"
#include "saturn/color.h"
#include "saturn/input.h"
#include "saturn/vdp1.h"
#include "saturn/video.h"

/* Layout, timer and cursor rules follow Tekken3Remake docs/research/code/modes.md
 * (Character select) and formats/system-textures.md (HUD portraits);
 * Copyright (c) 2026 n0madic, PolyForm Noncommercial; see NOTICE.md. The first
 * ten positions of each original row are drawn, 35 original pixels apart. */
enum { W=320, FACES=20, FACE_W=32, FACE_H=58, FACE_BYTES=FACE_W*FACE_H/2,
       RECORD_BYTES=64+FACE_BYTES, HEADER_BYTES=16,
       CELL_W=28, CELL_H=30, PITCH=31, GRID_X=5, GRID_Y0=137, GRID_Y1=169,
       /* 20 * T frames, with T = 60 + 3 * (21 - 10) for the full roster. */
       TIMER_UNIT=93, SELECT_FRAMES=20*TIMER_UNIT };

static sat_vdp1_texture_t g_face[FACES];

static unsigned le16(const uint8_t* p) {return (unsigned)p[0]|((unsigned)p[1]<<8);}

static sat_result_t load_faces(void* scratch,uint32_t bytes) {
    sat_cdfs_volume_t* volume;
    sat_cdfs_file_t file;
    uint32_t read=0;
    const uint32_t total=HEADER_BYTES+FACES*RECORD_BYTES;
    if(!scratch || bytes<total)return SAT_ERR_CAPACITY;
    SAT_TRY(t3_disc_volume(&volume));
    SAT_TRY(sat_cdfs_lookup(volume,"SELECT.BIN",&file));
    if(file.size!=total)return SAT_ERR_VERSION;
    SAT_TRY(sat_cdfs_read_at(volume,&file,0,scratch,total,&read));
    if(read!=total)return SAT_ERR_IO;
    const uint8_t* data=scratch;
    static const uint8_t magic[8]={'T','3','S','E','L','0','1',0};
    for(unsigned i=0;i<8;++i)if(data[i]!=magic[i])return SAT_ERR_VERSION;
    if(le16(data+8)!=FACES || le16(data+10)!=FACE_W || le16(data+12)!=FACE_H)return SAT_ERR_VERSION;
    for(unsigned id=0;id<FACES;++id) {
        const uint8_t* record=data+HEADER_BYTES+id*RECORD_BYTES;
        uint16_t lut[16],handle;
        for(unsigned i=0;i<16;++i)lut[i]=(uint16_t)le16(record+2*i);
        SAT_TRY(sat_vdp1_upload_lut(lut,&handle));
        SAT_TRY(sat_tex_upload_lut4_pixels(&g_face[id],record+64,FACE_W,FACE_H,handle));
    }
    return SAT_OK;
}

static sat_result_t frame(const sat_hud_t* hud,int x,int y,int w,int h,uint16_t colour) {
    SAT_TRY(sat_hud_bar(hud,x,y,w,2,1,1,colour,colour));
    SAT_TRY(sat_hud_bar(hud,x,y+h-2,w,2,1,1,colour,colour));
    SAT_TRY(sat_hud_bar(hud,x,y,2,h,1,1,colour,colour));
    return sat_hud_bar(hud,x+w-2,y,2,h,1,1,colour,colour);
}

static sat_result_t draw_face(unsigned id,int cx,int cy,int w,int h) {
    return sat_draw_sprite_scaled_screen(&g_face[t3_select_face(id)],(int16_t)cx,(int16_t)cy,
                                         (uint16_t)w,(uint16_t)h,0);
}

static t3_select_cursor_t steer(t3_select_cursor_t cursor,const sat_pad_state_t* pad) {
    const int across=(pad->pressed&SAT_PAD_RIGHT)?1:(pad->pressed&SAT_PAD_LEFT)?-1:0;
    return t3_select_move(cursor,across,(pad->pressed&(SAT_PAD_UP|SAT_PAD_DOWN))!=0);
}

sat_result_t t3_character_select(const sat_hud_t* hud,void* scratch,uint32_t bytes,uint8_t picks[2]) {
    if(!hud || !picks)return SAT_ERR_INVALID_ARG;
    SAT_TRY(load_faces(scratch,bytes));
    t3_select_cursor_t cursor[2]={t3_select_find(picks[0]),t3_select_find(picks[1])};
    unsigned chosen[2]={0,0};
    const uint16_t confirm=SAT_PAD_START|SAT_PAD_A|SAT_PAD_B|SAT_PAD_C|SAT_PAD_X|SAT_PAD_Y|SAT_PAD_Z;
    for(unsigned elapsed=0;elapsed<SELECT_FRAMES && !(chosen[0]&&chosen[1]);++elapsed) {
        sat_pad_state_t pad0={0},pad1={0};
        SAT_TRY(sat_wait_vblank());
        SAT_TRY(sat_set_clear_color(SAT_RGB555(2,3,8)));
        SAT_TRY(sat_vdp2_back_color_set(SAT_RGB555(2,3,8)));
        SAT_TRY(sat_input_poll());
        SAT_TRY(sat_pad_tap_state(0,0,&pad0));
        SAT_TRY(sat_pad_tap_state(1,0,&pad1));
        if(pad1.connected) {
            for(unsigned side=0;side<2u;++side) {
                const sat_pad_state_t* pad=side?&pad1:&pad0;
                if(chosen[side])continue;
                cursor[side]=steer(cursor[side],pad);
                if(pad->pressed&confirm)chosen[side]=1u;
            }
        } else if(!chosen[0]) {
            cursor[0]=steer(cursor[0],&pad0);
            if(pad0.pressed&confirm)chosen[0]=1u;
        } else if(pad0.pressed&SAT_PAD_B) {
            chosen[0]=0u; /* the first pick is taken back */
        } else {
            cursor[1]=steer(cursor[1],&pad0);
            if(pad0.pressed&confirm)chosen[1]=1u;
        }
        SAT_TRY(sat_begin_frame());
        SAT_TRY(sat_hud_text_centered(hud,"PLAYER SELECT",W/2,4));
        for(unsigned side=0;side<2u;++side) {
            const int x=side?268:52;
            const unsigned id=t3_select_id(cursor[side]);
            SAT_TRY(draw_face(id,x,70,56,100));
            SAT_TRY(sat_hud_text_centered(hud,side?(pad1.connected?"2P":"CPU"):"1P",x,18));
            SAT_TRY(sat_hud_text_centered(hud,t3_fighter_name(id),x,124));
        }
        SAT_TRY(sat_hud_text_centered(hud,"VS",W/2,64));
        for(unsigned row=0;row<T3_SELECT_ROWS;++row)
            for(unsigned column=0;column<T3_SELECT_DRAWN_COLUMNS;++column) {
                const t3_select_cursor_t cell={(uint8_t)row,(uint8_t)column};
                const int x=GRID_X+(int)column*PITCH,y=row?GRID_Y1:GRID_Y0;
                SAT_TRY(draw_face(t3_select_id(cell),x+CELL_W/2,y+CELL_H/2,CELL_W,CELL_H));
            }
        /* The eleventh cell is never drawn, so its cursor is invisible. */
        for(unsigned side=0;side<2u;++side) {
            if(cursor[side].column>=T3_SELECT_DRAWN_COLUMNS)continue;
            if(!chosen[side] && (elapsed&16u) && !(side==0u && !pad1.connected && chosen[0]))continue;
            const int x=GRID_X+(int)cursor[side].column*PITCH,y=cursor[side].row?GRID_Y1:GRID_Y0;
            const uint16_t colour=side?SAT_RGB555(31,12,3):SAT_RGB555(6,14,31);
            SAT_TRY(frame(hud,x-1,y-1,CELL_W+2,CELL_H+2,colour));
        }
        SAT_TRY(sat_hud_value(hud,"TIME ",(SELECT_FRAMES-elapsed+TIMER_UNIT-1u)/TIMER_UNIT,W/2-36,100));
        SAT_TRY(sat_hud_text_centered(hud,!pad1.connected&&chosen[0]?"CHOOSE THE OPPONENT   B: BACK":"START: CHOOSE",W/2,206));
        SAT_TRY(sat_end_frame());
    }
    picks[0]=(uint8_t)t3_select_id(cursor[0]);
    picks[1]=(uint8_t)t3_select_id(cursor[1]);
    return SAT_OK;
}
