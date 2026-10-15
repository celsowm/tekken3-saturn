/*
 * Tekken 3 Saturn - independent visual blockout / first playable slice.
 * Uses only installed LibSaturn public APIs. Does not load Tekken 3 assets.
 */
#include <stdint.h>

#include "fight.h"
#include "saturn/app.h"
#include "saturn/color.h"
#include "saturn/font.h"
#include "saturn/hud.h"
#include "saturn/input.h"
#include "saturn/render3d.h"
#include "saturn/scene.h"
#include "saturn/scene3d.h"
#include "saturn/video.h"

enum { W = 320, H = 224, FACE_CAP = 256, HUD_COMMANDS = 192 };
#define FX(x) ((sat_fx16_t)((x) * SAT_FX16_ONE))
#define Q(x)  ((sat_fx16_t)((x) * (SAT_FX16_ONE / 100)))

static t3_match_t g_game;
static sat_scene_t g_scene;
static sat_scene3d_face_t g_faces[FACE_CAP];
static uint32_t g_keys[FACE_CAP];
static uint16_t g_order[FACE_CAP];
static sat_ascii_font_t g_font;
static sat_hud_t g_hud;
static sat_camera3d_t g_camera;

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

/* Placeholder humanoid: torso, pelvis, head, two legs and two arms.
 * Rendering responds to guard, stun and the current attack. Character art
 * and skeletal animation deliberately belong to a subsequent milestone. */
static void draw_fighter(const t3_fighter_t* f, unsigned player) {
    sat_fx16_t x = from_q8(f->x), z = from_q8(f->z);
    sat_fx16_t arm = (f->attack && f->attack_age < 18)
        ? Q(28) * f->facing : 0;
    sat_fx16_t guard = f->guarding ? Q(20) : 0;
    sat_fx16_t lean = f->stun ? Q(10) * (-f->facing) : 0;
    uint16_t primary = player ? SAT_RGB555(29,9,7) : SAT_RGB555(5,17,29);
    uint16_t shadow = player ? SAT_RGB555(17,5,5) : SAT_RGB555(3,9,17);
    uint16_t light = player ? SAT_RGB555(31,18,12) : SAT_RGB555(14,27,31);
    box3(x + lean, Q(129), z, Q(30), Q(39), Q(19), primary, shadow, light);
    box3(x + lean, Q(89), z, Q(28), Q(13), Q(18), shadow, shadow, primary);
    box3(x + lean, Q(185), z, Q(21), Q(23), Q(21),
         SAT_RGB555(24,20,16), SAT_RGB555(16,12,11),
         SAT_RGB555(30,25,20));
    box3(x-Q(16), Q(46), z, Q(12), Q(42), Q(13), shadow, shadow, primary);
    box3(x+Q(16), Q(46), z, Q(12), Q(42), Q(13), shadow, shadow, primary);
    box3(x-Q(39)+arm, Q(124)+guard, z, Q(11), Q(30), Q(12),
         primary, shadow, light);
    box3(x+Q(39)+arm, Q(124)+guard, z, Q(11), Q(30), Q(12),
         primary, shadow, light);
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
    if (pressed & SAT_PAD_X) out.pressed |= T3_IN_LP;
    if (pressed & SAT_PAD_Y) out.pressed |= T3_IN_RP;
    if (pressed & SAT_PAD_A) out.pressed |= T3_IN_LK;
    if (pressed & SAT_PAD_B) out.pressed |= T3_IN_RK;
    return out;
}

static void draw_hud(void) {
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
    if (g_game.match_done)
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,
            g_game.winner == 0 ? "PLAYER 1 WINS!" : "PLAYER 2 WINS!", W/2, 85));
    else if (g_game.round_done)
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,
            g_game.winner < 0 ? "DRAW" :
            (g_game.winner == 0 ? "PLAYER 1 ROUND" : "PLAYER 2 ROUND"), W/2, 85));
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, "X/Y PUNCH  A/B KICK  L GUARD", 40, 196));
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, "D-PAD MOVE/STEP  START RESET", 40, 208));
}

static void draw_scene(void) {
    sat_fx16_t center = from_q8((g_game.fighters[0].x + g_game.fighters[1].x) / 2);
    g_camera.eye = vec(center, Q(390), -FX(9));
    g_camera.target = vec(center, FX(1), 0);
    SAT_PANIC_IF_ERROR(sat_camera3d_update(&g_camera));
    SAT_PANIC_IF_ERROR(sat_scene_begin(&g_scene, &g_camera, Q(25), W, H, HUD_COMMANDS));
    floor_tiles();
    draw_fighter(&g_game.fighters[0], 0);
    draw_fighter(&g_game.fighters[1], 1);
    SAT_PANIC_IF_ERROR(sat_scene_flush(&g_scene));
}

int main(void) {
    sat_video_config_t video = {W, H, SAT_VIDEO_AUTO, 0};
    sat_vec3_t eye = {0, Q(390), -FX(9)};
    sat_vec3_t target = {0, FX(1), 0};
    sat_vec3_t up = {0, SAT_FX16_ONE, 0};
    uint32_t prev_frame, accumulated = 0;
    SAT_PANIC_IF_ERROR(sat_init(&video));
    SAT_PANIC_IF_ERROR(sat_hud_init_default(&g_hud, &g_font));
    SAT_PANIC_IF_ERROR(sat_scene_init(&g_scene,g_faces,g_keys,g_order,FACE_CAP));
    SAT_PANIC_IF_ERROR(sat_camera3d_init(&g_camera,&eye,&target,&up,
        FX(60), (sat_fx16_t)(SAT_FX16_ONE * W / H), Q(25), FX(40)));
    t3_match_reset(&g_game);
    prev_frame = sat_frame_count();
    for (;;) {
        sat_pad_state_t pad0 = {0}, pad1 = {0};
        uint32_t now, elapsed, hz, ticks, i;
        SAT_PANIC_IF_ERROR(sat_app_frame_begin(
            SAT_RGB555(2,3,8), SAT_RGB555(2,3,8), &pad0));
        SAT_PANIC_IF_ERROR(sat_pad_poll_port(1, &pad1));
        if (pad0.pressed & SAT_PAD_START) t3_match_reset(&g_game);
        now = sat_frame_count();
        elapsed = now - prev_frame;
        prev_frame = now;
        if (elapsed > 4u) elapsed = 4u; /* bounded recovery after a stall */
        hz = sat_video_refresh_hz();
        accumulated += elapsed * T3_HZ;
        ticks = accumulated / hz;
        accumulated %= hz;
        for (i = 0; i < ticks; ++i) {
            t3_input_t p1 = controller(&pad0, g_game.fighters[0].facing);
            t3_input_t p2 = pad1.connected ?
                controller(&pad1, g_game.fighters[1].facing) :
                t3_cpu_input(&g_game, 1);
            if (i > 0u) {
                p1.pressed = 0; /* a press belongs to only one sim frame */
                if (pad1.connected) p2.pressed = 0;
            }
            t3_match_step(&g_game,p1,p2);
        }
        draw_scene();
        draw_hud();
        SAT_PANIC_IF_ERROR(sat_app_frame_end());
    }
}
