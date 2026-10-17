/*
 * Tekken 3 Saturn - experimental fighting prototype.
 * Uses only installed LibSaturn public APIs. By default the characters are
 * original procedural assets; T3_LOCAL_KMD explicitly opts into a private
 * user-supplied static KMD preview, never used for public CI.
 */
#include <stdint.h>

#include "fight.h"
#if defined(T3_CAPTURE_INPUT_TAPE) || defined(T3_REPLAY_INPUT_TAPE)
#include "input_tape.h"
#endif
#include "characters.h"
#include "saturn/app.h"
#include "saturn/color.h"
#include "saturn/font.h"
#include "saturn/hud.h"
#include "saturn/input.h"
#include "saturn/render3d.h"
#include "saturn/scene.h"
#include "saturn/scene3d.h"
#include "saturn/video.h"
#include "saturn/vdp1.h"
#include "saturn/time.h"
#include "diagnostics.h"
#include "move_probe.h"

enum { W = 320, H = 224, FACE_CAP = T3_SCENE_CAPACITY, HUD_COMMANDS = 192 };
#define FX(x) ((sat_fx16_t)((x) * SAT_FX16_ONE))
#define Q(x)  ((sat_fx16_t)((x) * (SAT_FX16_ONE / 100)))

/* Fully initialized by t3_match_reset before any read. */
static t3_match_t g_game __attribute__((section(".wram_l")));
static sat_scene_t g_scene;
/* Scene scratch uses the Saturn's existing second MiB. Keep authored banks
 * and hot actor data in High WRAM; the linker checks both memory budgets. */
static sat_scene3d_face_t g_faces[FACE_CAP] __attribute__((section(".wram_l")));
static uint32_t g_keys[FACE_CAP] __attribute__((section(".wram_l")));
static uint16_t g_order[FACE_CAP] __attribute__((section(".wram_l")));
/* Explicitly cleared and uploaded by HUD initialization before its first use. */
static sat_ascii_font_t g_font __attribute__((section(".wram_l")));
static sat_hud_t g_hud;
static sat_camera3d_t g_camera;
static uint8_t g_show_profiler;
static uint32_t g_render_work_ms, g_render_submissions;
static uint32_t g_rate_sample_vblank, g_rate_sample_frames, g_rate_x10;

/* Read by emulator/debugger using the ELF symbol g_t3_diagnostics.
 * frame_serial is written last: it marks a complete snapshot. */
volatile t3_diagnostics_t g_t3_diagnostics __attribute__((section(".wram_l")));

static sat_vec3_t vec(sat_fx16_t x, sat_fx16_t y, sat_fx16_t z) {
    sat_vec3_t v = {x, y, z};
    return v;
}
static sat_fx16_t from_q8(int32_t v) {
    return (sat_fx16_t)(v * (SAT_FX16_ONE / 256));
}

static void quad(sat_vec3_t a, sat_vec3_t b, sat_vec3_t c, sat_vec3_t d,
                 uint16_t color) {
    sat_quad3_t q = {{a, b, c, d}};
    sat_scene3d_material_t m = {0};
    m.kind = SAT_SCENE3D_RGB;
    m.rgb555 = color;
    m.color_calc_slot = SAT_INDEXED_SOLID_OPAQUE;
    SAT_PANIC_IF_ERROR(sat_scene_submit_quad(&g_scene, &q, &m, 0));
}

/* All six sides are submitted to the same global painter, rather than
 * submitting per-object and getting incorrectly ordered overlapping actors.
 * Front face is the -Z side (the camera looks from -Z). */
static void box3(sat_fx16_t x, sat_fx16_t y, sat_fx16_t z,
                 sat_fx16_t hx, sat_fx16_t hy, sat_fx16_t hz,
                 uint16_t front, uint16_t side, uint16_t top) {
    sat_fx16_t xl = x - hx, xr = x + hx;
    sat_fx16_t yb = y - hy, yt = y + hy;
    sat_fx16_t zn = z - hz, zf = z + hz;
    quad(vec(xr,yt,zn),vec(xl,yt,zn),vec(xl,yb,zn),vec(xr,yb,zn),front);
    quad(vec(xl,yt,zf),vec(xr,yt,zf),vec(xr,yb,zf),vec(xl,yb,zf),front);
    quad(vec(xl,yt,zn),vec(xl,yt,zf),vec(xl,yb,zf),vec(xl,yb,zn),side);
    quad(vec(xr,yt,zf),vec(xr,yt,zn),vec(xr,yb,zn),vec(xr,yb,zf),side);
    quad(vec(xl,yt,zn),vec(xr,yt,zn),vec(xr,yt,zf),vec(xl,yt,zf),top);
    quad(vec(xl,yb,zf),vec(xr,yb,zf),vec(xr,yb,zn),vec(xl,yb,zn),side);
}

static void floor_tiles(void) {
    int x, z;
    for (z = -2; z < 2; ++z) for (x = -6; x < 6; ++x) {
        sat_fx16_t l = FX(x), r = FX(x+1);
        sat_fx16_t n = FX(z), f = FX(z+1);
        uint16_t color = (x + z) & 1 ? SAT_RGB555(7, 10, 15)
                                      : SAT_RGB555(9, 12, 18);
        quad(vec(l,0,n),vec(r,0,n),vec(r,0,f),vec(l,0,f),color);
    }
    /* Stage perimeter at walk height, drawn only as thin rails. */
    box3(0, Q(4), -FX(2), FX(6), Q(4), Q(5),
         SAT_RGB555(10,16,20), SAT_RGB555(8,12,15), SAT_RGB555(14,21,23));
    box3(0, Q(4), FX(2), FX(6), Q(4), Q(5),
         SAT_RGB555(10,16,20), SAT_RGB555(8,12,15), SAT_RGB555(14,21,23));
}

static t3_input_t controller(const sat_pad_state_t* pad, int facing) {
    t3_input_t out = {0,0};
    uint16_t mask, pressed;
    if (!pad->connected) return out;
    mask = pad->held;
    pressed = pad->pressed;
    if (mask & (facing > 0 ? SAT_PAD_RIGHT : SAT_PAD_LEFT))
        out.held |= T3_IN_FORWARD;
    if (mask & (facing > 0 ? SAT_PAD_LEFT : SAT_PAD_RIGHT))
        out.held |= T3_IN_BACK;
    if (mask & SAT_PAD_UP) out.held |= T3_IN_LEFT;
    if (mask & SAT_PAD_DOWN) out.held |= T3_IN_RIGHT;
    if (mask & SAT_PAD_L) out.held |= T3_IN_GUARD;
    if (mask & SAT_PAD_R) out.held |= T3_IN_CROUCH;
    if (mask & SAT_PAD_C) out.held |= T3_IN_JUMP;
    if (mask & SAT_PAD_X) out.held |= T3_IN_LP;
    if (mask & SAT_PAD_Y) out.held |= T3_IN_RP;
    if (mask & SAT_PAD_A) out.held |= T3_IN_LK;
    if (mask & SAT_PAD_B) out.held |= T3_IN_RK;
    if (pressed & SAT_PAD_X) out.pressed |= T3_IN_LP;
    if (pressed & SAT_PAD_Y) out.pressed |= T3_IN_RP;
    if (pressed & SAT_PAD_A) out.pressed |= T3_IN_LK;
    if (pressed & SAT_PAD_B) out.pressed |= T3_IN_RK;
    return out;
}

static void draw_hud(void) {
#if defined(T3_RENDER_BENCH_TICK)
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud,"RENDER BENCHMARK",80,65));
#endif
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, "P1", 8, 5));
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, "P2", 288, 5));
    SAT_PANIC_IF_ERROR(sat_hud_bar(&g_hud, 8, 19, 140, 8,
        (uint32_t)g_game.fighters[0].health, T3_MAX_HEALTH,
        SAT_RGB555(13,4,4), SAT_RGB555(6,27,12)));
    SAT_PANIC_IF_ERROR(sat_hud_bar(&g_hud, 172, 19, 140, 8,
        (uint32_t)g_game.fighters[1].health, T3_MAX_HEALTH,
        SAT_RGB555(13,4,4), SAT_RGB555(6,27,12)));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "ROUND ", g_game.round, 8, 35));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "TIME ", (g_game.timer + 59u) / 60u, 125, 35));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "WINS ", g_game.wins[0], 8, 47));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "WINS ", g_game.wins[1], 248, 47));
#if defined(T3_REPLAY_INPUT_TAPE)
    SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"RECORDED INPUT DEMO",W/2,61));
#endif
    const int replay=t3_move_probe_round_value(4);
    if(replay>=1 && replay<=4)
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"REPLAY",W/2,85));
    else if(t3_move_probe_round_value(0) && t3_move_probe_round_value(1)==0)
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"READY",W/2,85));
    else if (g_game.match_done)
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,
            g_game.winner < 0 ? "MATCH DRAW" :
            (g_game.winner == 0 ? "PLAYER 1 WINS!" : "PLAYER 2 WINS!"), W/2, 85));
    else if (g_game.round_done)
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,
            g_game.winner < 0 ? "DRAW" :
            (g_game.winner == 0 ? "PLAYER 1 ROUND" : "PLAYER 2 ROUND"), W/2, 85));
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, t3_move_probe_combat()?"X HIGH  DF+X MID  D+A LOW":t3_move_probe_owned()?
        "X/Y PUNCH  A/B KICK":"X/Y PUNCH  A/B KICK  L GUARD", 40, 196));
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, t3_move_probe_owned()?
        "D-PAD MOVE/GUARD START RESET":"D-PAD MOVE/STEP  START RESET", 40, 208));
}

static void draw_scene(void) {
    sat_fx16_t center = from_q8((g_game.fighters[0].x + g_game.fighters[1].x) / 2);
    g_camera.eye = vec(center, Q(390), -FX(9));
    g_camera.target = vec(center, FX(1), 0);
    SAT_PANIC_IF_ERROR(sat_camera3d_update(&g_camera));
    SAT_PANIC_IF_ERROR(sat_scene_begin(&g_scene, &g_camera, Q(25), W, H, HUD_COMMANDS));
    floor_tiles();
    SAT_PANIC_IF_ERROR(t3_characters_render(&g_scene, &g_game));
    SAT_PANIC_IF_ERROR(sat_scene_flush(&g_scene));
}


static void draw_profiler(void) {
    /* Last completed diagnostic snapshot, never partial current-frame data. */
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "LOOP x10 ", g_rate_x10, 4, 66));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "VDP1 CMD ", g_t3_diagnostics.commands_used, 4, 78));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "SCENE FACES ", g_t3_diagnostics.faces_flushed, 4, 90));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "FRAME MS ", g_t3_diagnostics.frame_work_ms, 4, 102));
    if (t3_move_probe_active()) {
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "PSX SLOT 1", t3_move_probe_slot(0), 4, 114));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "PSX SLOT 2", t3_move_probe_slot(1), 4, 126));
    }
}

static void publish_diagnostics(uint32_t work_started_ms, uint32_t tick_count,
                                const sat_pad_state_t pads[2]) {
    sat_scene_stats_t scene = {0};
    sat_vdp1_command_stats_t commands = {0};
    sat_vdp1_frame_stats_t pacing = {0};
    uint32_t now = sat_frame_count();
    uint32_t span = now - g_rate_sample_vblank;
    ++g_t3_diagnostics.snapshot_epoch;
    SAT_PANIC_IF_ERROR(sat_scene_stats(&g_scene, &scene));
    SAT_PANIC_IF_ERROR(sat_vdp1_command_stats(&commands));
    SAT_PANIC_IF_ERROR(sat_vdp1_frame_stats(&pacing));

    ++g_rate_sample_frames;
    if (span >= sat_video_refresh_hz()) {
        /* Work-loop throughput, NOT a count of actual presented frames. */
        g_rate_x10 = (g_rate_sample_frames * sat_video_refresh_hz() * 10u + span/2u) / span;
        g_rate_sample_vblank = now;
        g_rate_sample_frames = 0;
    }

    g_t3_diagnostics.vblanks = now;
    g_t3_diagnostics.sim_ticks = g_game.total_ticks;
    g_t3_diagnostics.sim_steps_last_frame = tick_count;
    g_t3_diagnostics.loop_rate_x10 = g_rate_x10;
    g_t3_diagnostics.frame_work_ms = sat_time_ms() - work_started_ms;
    g_t3_diagnostics.faces_submitted = scene.submitted_faces;
    g_t3_diagnostics.faces_flushed = scene.flushed_faces;
    g_t3_diagnostics.faces_culled = scene.culled_faces;
    g_t3_diagnostics.faces_rejected = scene.rejected_faces;
    g_t3_diagnostics.commands_used = commands.used;
    g_t3_diagnostics.commands_capacity = commands.capacity;
    g_t3_diagnostics.late_swaps = pacing.late_swaps;
    g_t3_diagnostics.draw_waits = pacing.draw_waits;
    g_t3_diagnostics.draw_timeouts = pacing.draw_timeouts;
    g_t3_diagnostics.first_error = (int32_t)scene.result;
    for (unsigned i = 0; i < 2u; ++i) {
        const t3_fighter_t* f = &g_game.fighters[i];
        g_t3_diagnostics.pad_connected[i] = pads[i].connected;
        g_t3_diagnostics.pad_held[i] = pads[i].held;
        g_t3_diagnostics.pad_pressed[i] = pads[i].pressed;
        g_t3_diagnostics.fighter_x[i] = f->x;
        g_t3_diagnostics.fighter_z[i] = f->z;
        g_t3_diagnostics.health[i] = f->health;
        g_t3_diagnostics.attack[i] = f->attack;
        g_t3_diagnostics.attack_age[i] = f->attack_age;
        g_t3_diagnostics.guarding[i] = f->guarding;
        g_t3_diagnostics.crouching[i] = f->crouching;
        g_t3_diagnostics.stun[i] = f->stun;
        g_t3_diagnostics.wins[i] = g_game.wins[i];
    }
    g_t3_diagnostics.round = g_game.round;
    g_t3_diagnostics.round_done = g_game.round_done;
    g_t3_diagnostics.match_done = g_game.match_done;
    g_t3_diagnostics.winner = g_game.winner;
    g_t3_diagnostics.timer = g_game.timer;
    g_t3_diagnostics.hits = g_game.hits;
    g_t3_diagnostics.blocks = g_game.blocks;
    g_t3_diagnostics.psx_logic_active = t3_move_probe_active();
    g_t3_diagnostics.psx_move_slot[0] = t3_move_probe_slot(0);
    g_t3_diagnostics.psx_move_slot[1] = t3_move_probe_slot(1);
    g_t3_diagnostics.psx_move_frame[0] = t3_move_probe_frame(0);
    g_t3_diagnostics.psx_move_frame[1] = t3_move_probe_frame(1);
    g_t3_diagnostics.psx_attack_word[0] = t3_move_probe_attack_word(0);
    g_t3_diagnostics.psx_attack_word[1] = t3_move_probe_attack_word(1);
    g_t3_diagnostics.psx_transitions[0] = t3_move_probe_transitions(0);
    g_t3_diagnostics.psx_transitions[1] = t3_move_probe_transitions(1);
    g_t3_diagnostics.psx_attack_segments[0] = t3_move_probe_segments(0);
    g_t3_diagnostics.psx_attack_segments[1] = t3_move_probe_segments(1);
    g_t3_diagnostics.psx_event_status[0] = t3_move_probe_event_status(0);
    g_t3_diagnostics.psx_event_status[1] = t3_move_probe_event_status(1);
    g_t3_diagnostics.psx_extra_kind[0] = t3_move_probe_extra_kind(0);
    g_t3_diagnostics.psx_extra_kind[1] = t3_move_probe_extra_kind(1);
    g_t3_diagnostics.psx_extra_damage[0] = t3_move_probe_extra_damage(0);
    g_t3_diagnostics.psx_extra_damage[1] = t3_move_probe_extra_damage(1);
    g_t3_diagnostics.psx_events_fired[0] = t3_move_probe_event_fired(0);
    g_t3_diagnostics.psx_events_fired[1] = t3_move_probe_event_fired(1);
    g_t3_diagnostics.psx_attack_decode_errors[0] = t3_move_probe_decode_errors(0);
    g_t3_diagnostics.psx_attack_decode_errors[1] = t3_move_probe_decode_errors(1);
    g_t3_diagnostics.psx_pose_status[0] = t3_move_probe_pose_status(0);
    g_t3_diagnostics.psx_pose_status[1] = t3_move_probe_pose_status(1);
    g_t3_diagnostics.psx_pose_frames[0] = t3_move_probe_pose_frames(0);
    g_t3_diagnostics.psx_pose_frames[1] = t3_move_probe_pose_frames(1);
    g_t3_diagnostics.psx_motion_root[0][0] = t3_move_probe_root_pose(0,0);
    g_t3_diagnostics.psx_motion_root[0][1] = t3_move_probe_root_pose(0,1);
    g_t3_diagnostics.psx_motion_root[0][2] = t3_move_probe_root_pose(0,2);
    g_t3_diagnostics.psx_motion_root[1][0] = t3_move_probe_root_pose(1,0);
    g_t3_diagnostics.psx_motion_root[1][1] = t3_move_probe_root_pose(1,1);
    g_t3_diagnostics.psx_motion_root[1][2] = t3_move_probe_root_pose(1,2);
    g_t3_diagnostics.psx_fk_valid_slots[0] = t3_move_probe_fk_mask(0);
    g_t3_diagnostics.psx_fk_valid_slots[1] = t3_move_probe_fk_mask(1);
    g_t3_diagnostics.psx_fk_valid_model_parts[0] = t3_move_probe_fk_parts(0);
    g_t3_diagnostics.psx_fk_valid_model_parts[1] = t3_move_probe_fk_parts(1);
    g_t3_diagnostics.psx_ik_valid_slots[0] = t3_move_probe_ik_mask(0);
    g_t3_diagnostics.psx_ik_valid_slots[1] = t3_move_probe_ik_mask(1);
    g_t3_diagnostics.psx_skin_preview_applied[0] = t3_characters_skin_preview_applied(0);
    g_t3_diagnostics.psx_skin_preview_applied[1] = t3_characters_skin_preview_applied(1);
    g_t3_diagnostics.psx_hurt_zones_ready[0] = t3_move_probe_hurt_ready(0);
    g_t3_diagnostics.psx_hurt_zones_ready[1] = t3_move_probe_hurt_ready(1);
    g_t3_diagnostics.psx_hurt_head_local_y[0] = t3_move_probe_hurt_head_y(0);
    g_t3_diagnostics.psx_hurt_head_local_y[1] = t3_move_probe_hurt_head_y(1);
    g_t3_diagnostics.psx_fk_root_xx[0] = t3_move_probe_fk_root_xx(0);
    g_t3_diagnostics.psx_fk_root_xx[1] = t3_move_probe_fk_root_xx(1);
    g_t3_diagnostics.model_archive_bytes=T3_ARCHIVE_BYTES;
    g_t3_diagnostics.model_texture_count=T3_TEXTURE_COUNT;
    g_t3_diagnostics.model_texture_vram_bytes=t3_characters_texture_vram_bytes();
    g_t3_diagnostics.skin_work_ms=t3_characters_skin_work_ms();
    g_t3_diagnostics.render_work_ms=g_render_work_ms;
    g_t3_diagnostics.submitted_frames=g_render_submissions;
    g_t3_diagnostics.psx_lifecycle_active=t3_move_probe_phased();
    g_t3_diagnostics.psx_owned_active=t3_move_probe_owned();
    g_t3_diagnostics.psx_body_active=t3_move_probe_owned();
    g_t3_diagnostics.psx_contact_active=t3_move_probe_owned();
    g_t3_diagnostics.psx_combat_active=t3_move_probe_combat();
    g_t3_diagnostics.psx_round_flow_active=(uint32_t)t3_move_probe_round_value(0);
    g_t3_diagnostics.psx_round_state=t3_move_probe_round_value(1);
    g_t3_diagnostics.psx_round_counter=t3_move_probe_round_value(2);
    g_t3_diagnostics.psx_round_hold=t3_move_probe_round_value(3);
    g_t3_diagnostics.psx_replay_mode=t3_move_probe_round_value(4);
    g_t3_diagnostics.psx_replay_record=t3_move_probe_round_value(5);
    g_t3_diagnostics.psx_replay_play=t3_move_probe_round_value(6);
    g_t3_diagnostics.psx_replay_mark=t3_move_probe_round_value(7);
    g_t3_diagnostics.psx_replay_recorded=(uint32_t)t3_move_probe_round_value(8);
    g_t3_diagnostics.psx_replay_epoch=(uint32_t)t3_move_probe_round_value(9);
    for(unsigned fighter=0;fighter<2u;++fighter) {
        g_t3_diagnostics.psx_registered_hits[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,16);
        g_t3_diagnostics.psx_registered_blocks[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,17);
        g_t3_diagnostics.psx_unsupported_moves[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,18);
        g_t3_diagnostics.psx_damage_last_tick[fighter]=t3_move_probe_owned_value(fighter,19);
        g_t3_diagnostics.psx_reaction_slot[fighter]=t3_move_probe_owned_value(fighter,20);
        g_t3_diagnostics.psx_reaction_transition[fighter]=t3_move_probe_owned_value(fighter,21);
        g_t3_diagnostics.psx_hit_done[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,22);
        g_t3_diagnostics.psx_guarded_last_tick[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,23);
        g_t3_diagnostics.psx_ko_collapse[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,25);
        g_t3_diagnostics.psx_geometry_status[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,11);
        g_t3_diagnostics.psx_contact_zone[fighter]=t3_move_probe_owned_value(fighter,12);
        g_t3_diagnostics.psx_hit_gate[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,13);
        g_t3_diagnostics.psx_gate_frames[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,15);
        g_t3_diagnostics.psx_contact_frames[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,14);
        g_t3_diagnostics.psx_body_contacts[fighter]=(uint32_t)t3_move_probe_owned_value(fighter,7);
        for(unsigned axis=0;axis<3u;++axis)
            g_t3_diagnostics.psx_body_push[fighter][axis]=t3_move_probe_owned_value(fighter,axis+8u);
        g_t3_diagnostics.psx_health_q16[fighter]=t3_move_probe_owned_value(fighter,0);
        for(unsigned axis=0;axis<3u;++axis) {
            g_t3_diagnostics.psx_world_root[fighter][axis]=t3_move_probe_owned_value(fighter,axis+1u);
            g_t3_diagnostics.psx_anchor[fighter][axis]=t3_move_probe_owned_value(fighter,axis+4u);
        }
        g_t3_diagnostics.psx_pending_slot[fighter]=t3_move_probe_pending_slot(fighter);
        g_t3_diagnostics.psx_root_frame[fighter]=t3_move_probe_root_frame(fighter);
        g_t3_diagnostics.psx_move_age[fighter]=t3_move_probe_move_age(fighter);
        g_t3_diagnostics.psx_freeze[fighter]=t3_move_probe_freeze(fighter);
        g_t3_diagnostics.psx_hold_frames[fighter]=t3_move_probe_hold_frames(fighter);
    }
    g_t3_diagnostics.psx_shadow_contact_status[0] = t3_move_probe_shadow_status(0);
    g_t3_diagnostics.psx_shadow_contact_status[1] = t3_move_probe_shadow_status(1);
    g_t3_diagnostics.psx_shadow_contact_zone[0] = t3_move_probe_shadow_zone(0);
    g_t3_diagnostics.psx_shadow_contact_zone[1] = t3_move_probe_shadow_zone(1);
    g_t3_diagnostics.psx_shadow_contact_edges[0] = t3_move_probe_shadow_edges(0);
    g_t3_diagnostics.psx_shadow_contact_edges[1] = t3_move_probe_shadow_edges(1);
    g_t3_diagnostics.psx_shadow_overlap_frames[0] = t3_move_probe_shadow_frames(0);
    g_t3_diagnostics.psx_shadow_overlap_frames[1] = t3_move_probe_shadow_frames(1);
    ++g_t3_diagnostics.frame_serial;
    ++g_t3_diagnostics.snapshot_epoch;
}

int main(void) {
    sat_video_config_t video = {W, H, SAT_VIDEO_AUTO, 0};
    sat_vec3_t eye = {0, Q(390), -FX(9)};
    sat_vec3_t target = {0, FX(1), 0};
    sat_vec3_t up = {0, SAT_FX16_ONE, 0};
    uint32_t prev_frame, accumulated = 0;
    g_t3_diagnostics=(t3_diagnostics_t){
        .magic=T3_DIAGNOSTICS_MAGIC,.version=T3_DIAGNOSTICS_VERSION};
    SAT_PANIC_IF_ERROR(sat_init(&video));
    g_font=(sat_ascii_font_t){0};
    SAT_PANIC_IF_ERROR(sat_hud_init_default(&g_hud, &g_font));
    SAT_PANIC_IF_ERROR(sat_scene_init(&g_scene,g_faces,g_keys,g_order,FACE_CAP));
    SAT_PANIC_IF_ERROR(sat_camera3d_init(&g_camera,&eye,&target,&up,
        FX(60), (sat_fx16_t)(SAT_FX16_ONE * W / H), Q(25), FX(40)));
    SAT_PANIC_IF_ERROR(t3_characters_init());
    t3_match_reset(&g_game);
    SAT_PANIC_IF_ERROR(t3_move_probe_init());
    t3_move_probe_reset(g_game.round);
#if defined(T3_CAPTURE_INPUT_TAPE) || defined(T3_REPLAY_INPUT_TAPE)
    t3_input_tape_reset();
#endif
    SAT_PANIC_IF_ERROR(t3_characters_reset());
    SAT_PANIC_IF_ERROR(t3_characters_tick(&g_game));
    prev_frame = sat_frame_count();
    g_rate_sample_vblank = prev_frame;
    for (;;) {
        sat_pad_state_t pad0 = {0}, pad1 = {0};
        uint32_t now, elapsed, hz, ticks, i, work_started_ms;
        /* Both pads are sampled by ONE SMPC transaction. Using the generic
         * app helper followed by a second port poll wasted one transaction. */
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(SAT_RGB555(2,3,8)));
        SAT_PANIC_IF_ERROR(sat_set_clear_color(SAT_RGB555(2,3,8)));
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        work_started_ms = sat_time_ms();
        SAT_PANIC_IF_ERROR(sat_input_poll());
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(0, 0, &pad0));
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(1, 0, &pad1));
        if (pad0.pressed & SAT_PAD_Z) g_show_profiler ^= 1u;
        if (pad0.pressed & SAT_PAD_START) {
            t3_match_reset(&g_game);
            t3_move_probe_reset(g_game.round);
#if defined(T3_CAPTURE_INPUT_TAPE) || defined(T3_REPLAY_INPUT_TAPE)
            t3_input_tape_reset();
#endif
            SAT_PANIC_IF_ERROR(t3_characters_reset());
        }
        now = sat_frame_count();
        elapsed = now - prev_frame;
        prev_frame = now;
        if (elapsed > 4u) elapsed = 4u; /* bounded recovery after a stall */
        hz = sat_video_refresh_hz();
        accumulated += elapsed * T3_HZ;
        ticks = accumulated / hz;
        accumulated %= hz;
#if defined(T3_RENDER_BENCH_TICK)
        if(g_game.total_ticks>=T3_RENDER_BENCH_TICK)ticks=0;
        else if(ticks>T3_RENDER_BENCH_TICK-g_game.total_ticks)
            ticks=T3_RENDER_BENCH_TICK-g_game.total_ticks;
#endif
        for (i = 0; i < ticks; ++i) {
            t3_input_t p1 = controller(&pad0, g_game.fighters[0].facing);
            t3_input_t p2 = pad1.connected ?
                controller(&pad1, g_game.fighters[1].facing) :
                (t3_move_probe_owned() ? (t3_input_t){0} : t3_cpu_input(&g_game, 1));
            if (i > 0u) {
                p1.pressed = 0; /* a press belongs to only one sim frame */
                if (pad1.connected) p2.pressed = 0;
            }
#if defined(T3_CAPTURE_INPUT_TAPE) || defined(T3_REPLAY_INPUT_TAPE)
            t3_input_tape_step(g_game.total_ticks,&p1,&p2);
#endif
            if(t3_move_probe_owned()) {
                SAT_PANIC_IF_ERROR(t3_move_probe_owned_step(p1,p2,&g_game));
            } else t3_match_step(&g_game,p1,p2);
            t3_move_probe_tick(&g_game);
            SAT_PANIC_IF_ERROR(t3_characters_tick(&g_game));
        }
        const uint32_t render_started_ms=sat_time_ms();
        draw_scene();
        draw_hud();
        if (g_show_profiler) draw_profiler();
        g_render_work_ms=sat_time_ms()-render_started_ms;
        {
            const sat_pad_state_t pads[2] = {pad0, pad1};
            publish_diagnostics(work_started_ms, ticks, pads);
        }
        SAT_PANIC_IF_ERROR(sat_end_frame());
        ++g_render_submissions;
    }
}
