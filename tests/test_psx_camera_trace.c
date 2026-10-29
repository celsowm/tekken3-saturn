/* Replays a golden fight trace through the C camera director.
 *
 * Usage: test_psx_camera_trace <cases> <psx_trig.bin> <psx_atan.bin> <psx_body_sqrt.bin>
 * The cases file comes from scripts/camera_trace_cases.py (private, derived
 * from game RAM). Without it the test is skipped (exit 77).
 *
 * Two passes:
 *  - seeded: each frame starts from the trace's camera state of the frame
 *    before, so every frame is checked on its own;
 *  - free run: the state carries over from frame to frame, as in the game,
 *    and is reseeded only after frames that need unported cameras.
 * The trace does not record the loser camera (0x800A0890), the replay's
 * swing target (0x800A0880) or the overhead flag; the seeded pass carries
 * them over from the port's previous frame and neither pass compares them. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "psx_camera.h"

enum { FIGHTER_WORDS = 29 };
static const uint32_t FIGHTERS[2] = {0x800A96F0u, 0x800AAF7Cu};
#define HIT_SCRIPTS 0x80024418u
static size_t g_head;
static const struct { uint32_t addr, size; } RANGES[] = {
    {0x80095840u, 0xC0}, {0x80098750u, 0x10}, {0x80098920u, 0x10}, {0x800A0640u, 0x240},
    {0x800A8A80u, 0x28}, {0x800A9130u, 0x80}, {0x800A9650u, 0x30}, {0x800AE0D8u, 0xC},
    {0x800AE140u, 0x70}, {0x800AE438u, 0x20}, {0x800AFF00u, 0x80}, {0x80097340u, 0x170},
    {0x800A3E80u, 4}, {0x8009EA40u, 0x60}, {0x800A95F4u, 4}, {0x80097EC4u, 4}};
static uint32_t g_shake_base[T3_PSX_CAMERA_SHAKES];   /* each shake script's address */
enum { NRANGES = sizeof RANGES / sizeof RANGES[0] };

static uint8_t* g_data; static size_t g_frame_size; static uint32_t g_frames;

static uint8_t* load(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb"); uint8_t* p; long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    p = malloc((size_t)n);
    if (p && fread(p, 1, (size_t)n, f) != (size_t)n) { free(p); p = NULL; }
    fclose(f); *size = (size_t)n; return p;
}
static int32_t le32(const uint8_t* p) { return (int32_t)((uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24); }
static int16_t le16(const uint8_t* p) { return (int16_t)(uint16_t)(p[0] | p[1] << 8); }
static const uint8_t* frame_at(uint32_t n) { return g_data + g_head + (size_t)n * g_frame_size; }
static t3_psx_camera_choice_t g_lists[3][256]; static size_t g_list_count[3];
static t3_psx_camera_stream_t g_stream_entries[2][128]; static t3_psx_camera_streams_t g_streams[2];
static uint32_t g_stream_base[2];   /* each bank's section 8 in RAM */
static const uint8_t* ram(uint32_t n, uint32_t addr, uint32_t len) {
    const uint8_t* p = frame_at(n) + 2 * FIGHTER_WORDS * 4 + 12;
    int i;
    for (i = 0; i < NRANGES; i++) {
        if (addr >= RANGES[i].addr && addr + len <= RANGES[i].addr + RANGES[i].size) return p + (addr - RANGES[i].addr);
        p += RANGES[i].size;
    }
    fprintf(stderr, "address %08x not recorded\n", addr); exit(2);
}
static int32_t r32(uint32_t n, uint32_t a) { return le32(ram(n, a, 4)); }
static int16_t r16(uint32_t n, uint32_t a) { return le16(ram(n, a, 2)); }
/* g_replayPlaying (0x800958C8) after frame n: 3 on the replay's first frame. */
static int16_t replay(uint32_t n) { return r16(n, 0x800958C8u); }
/* FightFrame runs for frame n: the round had not ended (state END 9) and
 * the fight is neither frozen nor paused. */
static int fight_frame_runs(uint32_t n) {
    return r16(n - 1, 0x80097350u) < 9 && !r16(n - 1, 0x8009588Cu) && !*ram(n - 1, 0x800958B8u, 1);
}

static void fighters_at(uint32_t m, uint32_t n, t3_psx_camera_fighter_t f[2], t3_psx_camera_frame_t* fr, int16_t* phase) {
    const uint8_t* p = frame_at(m); int k;
    for (k = 0; k < 2; k++, p += FIGHTER_WORDS * 4) {
        f[k].pos_x = le32(p); f[k].pos_z = le32(p + 4); f[k].root_x = le32(p + 8); f[k].root_z = le32(p + 12);
        f[k].head_y = le32(p + 16); f[k].screen_x = (int16_t)le32(p + 20); f[k].throw_state = (int16_t)le32(p + 24);
        f[k].pose_frame = (int16_t)le32(p + 28); f[k].cur_slot = (int16_t)le32(p + 32);
        f[k].bank_type = (int16_t)le32(p + 36); f[k].move_flags = (uint32_t)le32(p + 40);
        f[k].move_id = (uint32_t)le32(p + 44); f[k].move_state = (uint32_t)le32(p + 48);
        f[k].event_left_b = (int16_t)le32(p + 52); f[k].event_left_c = (int16_t)le32(p + 56);
        f[k].transition = (int16_t)le32(p + 60); f[k].heading = (int16_t)le32(p + 64); f[k].pos_y = le32(p + 68);
        f[k].hit_clean = (uint8_t)le32(p + 72); f[k].counter_hit = (uint8_t)le32(p + 76);
        f[k].close_hit = (uint8_t)le32(p + 80); f[k].ko = (uint8_t)le32(p + 84); f[k].last_damage = (int16_t)le32(p + 88);
        f[k].hit_point[0] = le32(p + 92); f[k].hit_point[1] = le32(p + 96); f[k].hit_point[2] = le32(p + 100);
        f[k].facing = (int16_t)le32(p + 104); f[k].root_y = le32(p + 108); f[k].health = le32(p + 112);
        f[k].bank_choices = g_lists[1 + k]; f[k].bank_choice_count = g_list_count[1 + k];
        f[k].bank_streams = &g_streams[k];
    }
    p = frame_at(n) + 2 * FIGHTER_WORDS * 4;
    fr->fighter_distance = le32(p); *phase = (int16_t)le32(p + 4); fr->rounds_played = le32(p + 8);
}

/* The port's values of the fields the trace does not record. */
static t3_psx_camera_t g_carry;

/* The director runs before the fighters move: it sees them as frame n-1 left them. */
static void fighters(uint32_t n, t3_psx_camera_fighter_t f[2], t3_psx_camera_frame_t* fr, int16_t* phase) {
    fighters_at(n - 1, n, f, fr, phase);
}

/* The camera's state as the trace holds it after frame n. */
static void seed(t3_psx_camera_t* c, uint32_t n, int faithful) {
    int i, k; uint32_t player;
    memset(c, 0, sizeof *c);
    for (i = 0; i < T3_PSX_CAMERA_SOURCES; i++) {
        uint32_t s = 0x800A0650u + 24u * (uint32_t)i, st = 0x800A06D0u + 0x30u * (uint32_t)i;
        t3_psx_camera_source_state_t* q = &c->source_state[i];
        c->source[i] = (t3_psx_camera_source_t){r32(n, s), r32(n, s + 4), r32(n, s + 8), r32(n, s + 12), r32(n, s + 16), r32(n, s + 20)};
        q->phase = r16(n, st); q->moved = r16(n, st + 2); q->cleared_a = r16(n, st + 4); q->cleared_b = r16(n, st + 6);
        q->frames = r16(n, st + 8); q->angle = r32(n, st + 0xC); q->speed = r32(n, st + 0x10); q->pitch = r32(n, st + 0x14);
        q->pitch_target = r32(n, st + 0x18); q->pitch_step = r32(n, st + 0x1C); q->error = r32(n, st + 0x20);
        q->depth_step = r32(n, st + 0x24); q->depth = r32(n, st + 0x28); q->lateral = r32(n, st + 0x2C);
        c->weight[i] = r16(n, 0x800A9140u + 2u * (uint32_t)i); c->prev_weight[i] = r16(n, 0x800A914Au + 2u * (uint32_t)i);
    }
    for (k = 0; k < 2; k++) for (i = 0; i < 4; i++) c->target[k][i] = r32(n, 0x800AE170u + 16u * (uint32_t)k + 4u * (uint32_t)i);
    c->third_target_screen_x = r32(n, 0x800AE19Cu);
    c->view.pitch = r32(n, 0x800A8A88u); c->view.yaw = r32(n, 0x800A8A8Cu); c->view.roll = r32(n, 0x800A8A90u);
    c->view.x = r32(n, 0x800A8A9Cu); c->view.y = r32(n, 0x800A8AA0u); c->view.z = r32(n, 0x800A8AA4u);
    c->h = r16(n, 0x800A919Cu); c->h_min = r16(n, 0x800A919Eu); c->h_max = r16(n, 0x800A91A0u); c->view.h = c->h;
    c->height_term = r32(n, 0x80098754u);
    c->state = r16(n, 0x800A9138u); c->enabled = r16(n, 0x800A913Cu); c->intro_timer = r16(n, 0x800A913Eu);
    c->cinematic = r16(n, 0x800A9154u); c->wait = r16(n, 0x800A915Au); c->count = r16(n, 0x800A915Cu);
    c->count_total = r16(n, 0x800A915Eu); c->throw_player = r16(n, 0x800A9160u);
    c->hit_count = r16(n, 0x800A9164u); c->hit_total = r16(n, 0x800A9166u); c->skip_side = r16(n, 0x800A91A8u);
    c->intro_base = (t3_psx_camera_source_t){r32(n, 0x800A0850u), r32(n, 0x800A0854u), r32(n, 0x800A0858u),
                                             r32(n, 0x800A085Cu), r32(n, 0x800A0860u), r32(n, 0x800A0864u)};
    c->intro_length = r16(n, 0x800A0868u); c->intro_index = r16(n, 0x800A086Au); c->intro_counter = r16(n, 0x8009892Cu);
    c->thrower = r16(n, 0x800A9156u); c->side = r16(n, 0x800A9158u); c->switch_mode = r16(n, 0x800A9162u);
    c->script = r32(n, 0x800A9168u); c->script_kind = r16(n, 0x800A916Cu); c->choice_index = r16(n, 0x800A916Eu);
    c->script_move = (uint32_t)r32(n, 0x800A9170u); c->preset_pitch = r32(n, 0x800A9174u);
    c->preset_yaw = r32(n, 0x800A9178u); c->mirror = r16(n, 0x800A9184u);
    /* The stream player is not in RAM; it is the thrower's. The stream is
     * held as a bank pointer; while one plays it is the running script. */
    c->script_player = c->stream_player = c->thrower;
    /* A stream's script in RAM is its address in the bank; the port keeps
     * the list value (bit 31 and the section 8 offset). */
    for (i = 0; i < 2; i++)
        if (c->script < -1 && (uint32_t)c->script - g_stream_base[i] < 0x10000u)
            c->script = (int32_t)(0x80000000u | ((uint32_t)c->script - g_stream_base[i]));
    c->stream = c->script_kind < 0 ? c->script : -1;
    c->stream_frames = r16(n, 0x800A9180u); c->stream_frame = r16(n, 0x800A9182u);
    for (i = 0; i < 3; i++) c->stream_origin[i] = r32(n, 0x800A918Cu + 4u * (uint32_t)i);
    c->stream_rescale = r16(n, 0x800A91A2u); c->stream_restart = r16(n, 0x800A91A6u);
    for (i = 0; i < 8; i++) c->stream_sample[i] = r16(n, 0x800A07D0u + 2u * (uint32_t)i);
    /* The hit script is held as its address. */
    c->hit_script = (uint32_t)r32(n, 0x800A07E0u) - HIT_SCRIPTS < 7u * 32u ? (int16_t)(((uint32_t)r32(n, 0x800A07E0u) - HIT_SCRIPTS) / 32u) : -1;
    for (i = 0; i < 3; i++) c->hit_point[i] = r32(n, 0x800A07F0u + 4u * (uint32_t)i);
    c->winner_style = *ram(n, 0x800A0844u, 1);
    player = (uint32_t)r32(n, 0x800A0840u);
    c->round_end_player = player == FIGHTERS[1] ? 1 : 0;
    for (i = 0; i < 9; i++) { c->winner_a[i] = r32(n, 0x8009EA48u + 4u * (uint32_t)i); c->winner_b[i] = r32(n, 0x8009EA70u + 4u * (uint32_t)i); }
    c->winner_drift[0] = r16(n, 0x8009EA98u); c->winner_drift[1] = r16(n, 0x8009EA9Cu);
    c->replay_style = *ram(n, 0x800A0874u, 1); c->replay_on = *ram(n, 0x800A0875u, 1); c->replay_reset = *ram(n, 0x800A0876u, 1);
    c->replay_distance = r16(n, 0x800A0870u); c->replay_yaw = r16(n, 0x800A0878u); c->replay_pitch = r16(n, 0x800A087Au);
    c->replay_swing = r16(n, 0x800A087Eu); c->replay_track = r16(n, 0x800A0838u);
    for (i = 0; i < 6; i++) c->replay_targets[i] = r32(n, 0x800A0820u + 4u * (uint32_t)i);
    c->replay_swing_to = g_carry.replay_swing_to; c->overhead = g_carry.overhead;
    memcpy(c->loser, g_carry.loser, sizeof c->loser); c->loser_height = g_carry.loser_height; c->loser_yaw = g_carry.loser_yaw;
    /* The shake is held as a pointer into its script, 0 for none. */
    c->shake_script = -1; c->shake_pos = 0;
    for (i = 0; i < T3_PSX_CAMERA_SHAKES; i++)
        if ((uint32_t)r32(n, 0x80097EC4u) - g_shake_base[i] < T3_PSX_CAMERA_SHAKE_BYTES) {
            c->shake_script = (int16_t)i; c->shake_pos = (int16_t)((uint32_t)r32(n, 0x80097EC4u) - g_shake_base[i]);
        }
    c->faithful = (uint8_t)faithful;
}

enum { C_SOURCES, C_STATE, C_WEIGHTS, C_TARGETS, C_VIEW_POS, C_VIEW_PITCH, C_VIEW_YAW, C_DIRECTOR, C_INTRO, C_PHASE, C_MATRIX, C_BACKDROP, C_THROW, C_STREAM, C_ROUND_END, C_REPLAY, NCHECKS };
static const char* NAMES[NCHECKS] = {"sources", "source state", "weights", "targets", "view position", "view pitch",
                                     "view yaw", "director", "intro", "phase", "view matrix", "backdrop turn", "throw camera", "stream", "round end", "replay"};

/* Compares c (after frame n) with the trace; sets bit i of the result for each failing check. */
static unsigned compare(const t3_psx_camera_t* c, int16_t phase, uint32_t n) {
    t3_psx_camera_t e; unsigned bad = 0; int i;
    seed(&e, n, c->faithful);
    for (i = 0; i < T3_PSX_CAMERA_SOURCES; i++) {
        /* Sources 1 and 4 are not written by the ported path. */
        if (memcmp(&c->source[i], &e.source[i], sizeof e.source[i])) bad |= 1u << C_SOURCES;
        if (memcmp(&c->source_state[i], &e.source_state[i], sizeof e.source_state[i])) bad |= 1u << C_STATE;
    }
    if (memcmp(c->weight, e.weight, sizeof e.weight) || memcmp(c->prev_weight, e.prev_weight, sizeof e.prev_weight)) bad |= 1u << C_WEIGHTS;
    if (memcmp(c->target, e.target, sizeof e.target)) bad |= 1u << C_TARGETS;
    if (c->view.x != e.view.x || c->view.y != e.view.y || c->view.z != e.view.z) bad |= 1u << C_VIEW_POS;
    if (c->view.pitch != e.view.pitch) bad |= 1u << C_VIEW_PITCH;
    if (c->view.yaw != e.view.yaw || c->h != e.h) bad |= 1u << C_VIEW_YAW;
    if (c->state != e.state || c->enabled != e.enabled || c->intro_timer != e.intro_timer || c->count != e.count ||
        c->count_total != e.count_total || c->hit_total != e.hit_total || c->height_term != e.height_term) bad |= 1u << C_DIRECTOR;
    if (memcmp(&c->intro_base, &e.intro_base, sizeof e.intro_base) || c->intro_length != e.intro_length ||
        c->intro_index != e.intro_index || c->intro_counter != e.intro_counter) bad |= 1u << C_INTRO;
    if (phase != r16(n, 0x80095890u)) bad |= 1u << C_PHASE;
    if (c->thrower != e.thrower || c->side != e.side || c->switch_mode != e.switch_mode || c->script != e.script ||
        c->script_kind != e.script_kind || c->choice_index != e.choice_index || c->preset_pitch != e.preset_pitch ||
        c->preset_yaw != e.preset_yaw || c->mirror != e.mirror || c->wait != e.wait || c->throw_player != e.throw_player ||
        (c->script_move != e.script_move && c->script_kind != 0)) bad |= 1u << C_THROW;
    if (c->stream_frames != e.stream_frames || c->stream_frame != e.stream_frame || c->stream_restart != e.stream_restart ||
        c->stream_rescale != e.stream_rescale || c->hit_count != e.hit_count ||
        (c->hit_total >= 0 && (c->hit_script != e.hit_script || memcmp(c->hit_point, e.hit_point, sizeof e.hit_point))) || memcmp(c->stream_origin, e.stream_origin, sizeof e.stream_origin) ||
        /* Past a stream's end the game decodes beyond its data; the port
         * repeats the last frame. That sample is never read again. */
        (e.stream_frame <= e.stream_frames && memcmp(c->stream_sample, e.stream_sample, sizeof e.stream_sample)))
        bad |= 1u << C_STREAM;
    if (c->winner_style != e.winner_style || memcmp(c->winner_a, e.winner_a, sizeof e.winner_a) ||
        memcmp(c->winner_b, e.winner_b, sizeof e.winner_b) || memcmp(c->winner_drift, e.winner_drift, sizeof e.winner_drift) ||
        (phase >= T3_PSX_CAMERA_WINNER_0 && phase <= T3_PSX_CAMERA_WINNER && c->round_end_player != e.round_end_player))
        bad |= 1u << C_ROUND_END;
    if (c->replay_style != e.replay_style || c->replay_on != e.replay_on || c->replay_reset != e.replay_reset ||
        c->replay_distance != e.replay_distance || c->replay_yaw != e.replay_yaw || c->replay_pitch != e.replay_pitch ||
        c->replay_swing != e.replay_swing || c->replay_track != e.replay_track ||
        memcmp(c->replay_targets, e.replay_targets, sizeof e.replay_targets)) bad |= 1u << C_REPLAY;
    return bad;
}

static void print_diff(const t3_psx_camera_t* c, uint32_t n) {
    t3_psx_camera_t e; int i;
    seed(&e, n, c->faithful);
    for (i = 0; i < T3_PSX_CAMERA_SOURCES; i++)
        if (memcmp(&c->source[i], &e.source[i], sizeof e.source[i]))
            printf("    source %d: got %d %d %d %d %d %d want %d %d %d %d %d %d\n", i,
                   c->source[i].x, c->source[i].y, c->source[i].z, c->source[i].pitch, c->source[i].yaw, c->source[i].h,
                   e.source[i].x, e.source[i].y, e.source[i].z, e.source[i].pitch, e.source[i].yaw, e.source[i].h);
    printf("    view got p%d y%d (%d %d %d) want p%d y%d (%d %d %d)\n", c->view.pitch, c->view.yaw, c->view.x, c->view.y,
           c->view.z, e.view.pitch, e.view.yaw, e.view.x, e.view.y, e.view.z);
    printf("    weights got %d %d %d %d %d want %d %d %d %d %d; state got %d want %d\n", c->weight[0], c->weight[1],
           c->weight[2], c->weight[3], c->weight[4], e.weight[0], e.weight[1], e.weight[2], e.weight[3], e.weight[4],
           c->state, e.state);
    if (c->replay_on != e.replay_on || c->replay_style != e.replay_style || c->replay_yaw != e.replay_yaw ||
        c->replay_swing != e.replay_swing || c->replay_distance != e.replay_distance || c->replay_track != e.replay_track ||
        c->replay_reset != e.replay_reset || memcmp(c->replay_targets, e.replay_targets, sizeof e.replay_targets))
        printf("    replay got on%d style%d reset%d yaw %d swing %d dist %d track %d tg %d %d %d %d %d %d"
               " want on%d style%d reset%d yaw %d swing %d dist %d track %d tg %d %d %d %d %d %d\n",
               c->replay_on, c->replay_style, c->replay_reset, c->replay_yaw, c->replay_swing, c->replay_distance,
               c->replay_track, c->replay_targets[0], c->replay_targets[1], c->replay_targets[2], c->replay_targets[3],
               c->replay_targets[4], c->replay_targets[5], e.replay_on, e.replay_style, e.replay_reset, e.replay_yaw,
               e.replay_swing, e.replay_distance, e.replay_track, e.replay_targets[0], e.replay_targets[1],
               e.replay_targets[2], e.replay_targets[3], e.replay_targets[4], e.replay_targets[5]);
    if (c->winner_style != e.winner_style || memcmp(c->winner_b, e.winner_b, sizeof e.winner_b) ||
        memcmp(c->winner_a, e.winner_a, sizeof e.winner_a))
        printf("    winner got style %d a %d %d (%d %d %d) b %d %d (%d %d %d) want style %d a %d %d (%d %d %d) b %d %d (%d %d %d)\n",
               c->winner_style, c->winner_a[0], c->winner_a[1], c->winner_a[5], c->winner_a[6], c->winner_a[7],
               c->winner_b[0], c->winner_b[1], c->winner_b[5], c->winner_b[6], c->winner_b[7], e.winner_style,
               e.winner_a[0], e.winner_a[1], e.winner_a[5], e.winner_a[6], e.winner_a[7], e.winner_b[0], e.winner_b[1],
               e.winner_b[5], e.winner_b[6], e.winner_b[7]);
    if (c->script != e.script || c->script_kind != e.script_kind || c->choice_index != e.choice_index || c->mirror != e.mirror ||
        c->wait != e.wait || c->preset_yaw != e.preset_yaw || c->side != e.side || c->thrower != e.thrower)
        printf("    throw got script %d kind %d choice %d mirror %d wait %d yaw %d side %d thrower %d move %08x"
               " want %d %d %d %d %d %d %d %d %08x\n", c->script, c->script_kind, c->choice_index, c->mirror, c->wait,
               c->preset_yaw, c->side, c->thrower, c->script_move, e.script, e.script_kind, e.choice_index, e.mirror,
               e.wait, e.preset_yaw, e.side, e.thrower, e.script_move);
    if (memcmp(c->stream_sample, e.stream_sample, sizeof e.stream_sample) || c->stream_frame != e.stream_frame ||
        c->stream_frames != e.stream_frames || c->stream_restart != e.stream_restart || c->stream_rescale != e.stream_rescale ||
        memcmp(c->stream_origin, e.stream_origin, sizeof e.stream_origin))
        printf("    stream got %d/%d r%d s%d (%d %d %d) [%d %d %d %d %d %d %d %d] want %d/%d r%d s%d (%d %d %d) [%d %d %d %d %d %d %d %d]\n",
               c->stream_frame, c->stream_frames, c->stream_restart, c->stream_rescale, c->stream_origin[0], c->stream_origin[1],
               c->stream_origin[2], c->stream_sample[0], c->stream_sample[1], c->stream_sample[2], c->stream_sample[3],
               c->stream_sample[4], c->stream_sample[5], c->stream_sample[6], c->stream_sample[7], e.stream_frame,
               e.stream_frames, e.stream_restart, e.stream_rescale, e.stream_origin[0], e.stream_origin[1], e.stream_origin[2],
               e.stream_sample[0], e.stream_sample[1], e.stream_sample[2], e.stream_sample[3], e.stream_sample[4],
               e.stream_sample[5], e.stream_sample[6], e.stream_sample[7]);
}

static unsigned matrix_check(const t3_psx_camera_tables_t* t, uint32_t n) {
    /* CameraBuildView from the trace's own g_camera, compared with its view matrix. */
    t3_psx_camera_t e; t3_psx_camera_matrix_t m; int i, k; unsigned bad = 0;
    seed(&e, n, 0);
    if (!t3_psx_camera_build_view(&e.view, t, &m)) return 1u << C_MATRIX;
    for (i = 0; i < 3; i++) for (k = 0; k < 3; k++) if (m.m[i][k] != r16(n, 0x800AE438u + 2u * (uint32_t)(3 * i + k))) bad = 1u << C_MATRIX;
    for (i = 0; i < 3; i++) if (m.t[i] != r32(n, 0x800AE44Cu + 4u * (uint32_t)i)) bad = 1u << C_MATRIX;
    return bad;
}

static unsigned backdrop_check(const t3_psx_camera_tables_t* t, uint32_t n) {
    t3_psx_camera_t e; t3_psx_backdrop_turn_t b; unsigned bad = 0;
    b.angle = r32(n - 1, 0x800A9650u); b.prev_yaw = r32(n - 1, 0x800A9658u); b.yaw = r32(n - 1, 0x800A9664u);
    b.point_x = r32(n - 1, 0x800AE0D8u); b.point_z = r32(n - 1, 0x800AE0E0u);
    seed(&e, n, 0);
    t3_psx_backdrop_step(&b, &e.view, t, r32(n, 0x80097350u) == 0);
    if ((b.angle & 0xFFF) != (r32(n, 0x800A9650u) & 0xFFF) || b.point_x != r32(n, 0x800AE0D8u) ||
        b.point_z != r32(n, 0x800AE0E0u)) bad = 1u << C_BACKDROP;
    return bad;
}

static t3_psx_camera_tables_t g_tables;

/* The roots as frame n leaves them, for frames whose replay step sets them before the camera. */
static void frame_roots(uint32_t n, t3_psx_camera_fighter_t f[2]) {
    t3_psx_camera_fighter_t g[2]; t3_psx_camera_frame_t unused; int16_t phase; int k;
    fighters_at(n, n, g, &unused, &phase);
    for (k = 0; k < 2; k++) { f[k].root_x = g[k].root_x; f[k].root_y = g[k].root_y; f[k].root_z = g[k].root_z; }
}

/* Runs frame n as the game does: the KO replay camera on replay frames, the
 * loser camera's set-up when the round flow enters phase LOSER_0/1, then the
 * director. Returns the director's result, 2 on a replay frame, 3 when
 * FightFrame does not run, 4 on a winner camera's first frame. */
static int drive(t3_psx_camera_t* c, uint32_t n, int16_t* phase, uint32_t rng[3]) {
    t3_psx_camera_fighter_t f[2]; t3_psx_camera_frame_t fr; int16_t after = r16(n, 0x80095890u); int r;
    fighters(n, f, &fr, phase);
    /* Other systems draw from the generators too; each frame starts from the trace's state. */
    /* The main loop steps the camera generator once before the fight frame. */
    rng[0] = (uint32_t)r32(n - 1, 0x800A3E80u); rng[1] = ((uint32_t)r32(n - 1, 0x800A95F4u) + 1u) * 0x10DCDu;
    rng[2] = (uint32_t)r32(n - 1, 0x800AE168u);
    fr.rand = &rng[0]; fr.camera_rand = &rng[1]; fr.ai_rand = &rng[2]; fr.practice = 0;
    if (!fight_frame_runs(n)) return 3;
    /* The frame after the replay ends (state 4) puts the fighters back and
     * places them for the win poses before the camera; frame n's state is
     * the closest the trace has (they move a little after the camera). */
    if (replay(n - 1) == 4) fighters_at(n, n, f, &fr, phase);
    if (replay(n)) {
        /* The replay's first frame puts the recorded roots in place, starts
         * the replay camera (one step) and then runs the replay frame's step. */
        if (replay(n) == 3) {
            frame_roots(n, f);
            if (!t3_psx_camera_replay_start(c, &g_tables, f, &fr)) return 0;
        }
        t3_psx_camera_replay_step(c, &g_tables, f);
        t3_psx_camera_shake_step(c, &g_tables);
        return 2;
    }
    /* The round flow enters WINNER_0/1 before the director, which moves on to WINNER. */
    if (after == T3_PSX_CAMERA_WINNER && *phase < T3_PSX_CAMERA_WINNER_0) {
        *phase = (int16_t)(T3_PSX_CAMERA_WINNER_0 + ((uint32_t)r32(n, 0x800A0840u) == FIGHTERS[1]));
        r = t3_psx_camera_step(c, &g_tables, f, &fr, phase);
        if (r > 0) t3_psx_camera_shake_step(c, &g_tables);
        return r > 0 ? 4 : r;
    }
    if ((after == T3_PSX_CAMERA_LOSER_0 || after == T3_PSX_CAMERA_LOSER_1) && *phase != after) {
        const t3_psx_camera_fighter_t* l = &f[after == T3_PSX_CAMERA_LOSER_1];
        if (!t3_psx_camera_loser_start(c, &g_tables, l, l->health != 0, &fr)) return 0;
        *phase = after;
    }
    r = t3_psx_camera_step(c, &g_tables, f, &fr, phase);
    if (r > 0) t3_psx_camera_shake_step(c, &g_tables);
    return r;
}

/* A shake the fighters started later in frame n (landings, move events):
 * the trace shows its script's start. */
static void shake_starts(t3_psx_camera_t* c, uint32_t n) {
    int i;
    for (i = 0; i < T3_PSX_CAMERA_SHAKES; i++)
        if ((uint32_t)r32(n, 0x80097EC4u) == g_shake_base[i]) t3_psx_camera_shake_start(c, i);
}

int main(int argc, char** argv) {
    size_t size, trig_size, atan_size, sqrt_size; uint8_t *trig, *atan, *sqrt_table;
    t3_psx_trig_t trig_table; t3_psx_camera_tables_t* const tp = &g_tables;
#define tables (*tp)
    uint32_t n, fails[NCHECKS] = {0}, first[NCHECKS], unported = 0, idle = 0, winner_starts = 0, winner_start_fails = 0, reseeds = 0, run_checked = 0, run_fails[NCHECKS] = {0};
    uint32_t checked = 0, streamed = 0, hits = 0, replayed = 0, round_end = 0, exact = 0, run_exact = 0, rng[3]; int i, shown = 0;
    memset(&tables, 0, sizeof tables);
    if (argc < 5) { fprintf(stderr, "usage: %s cases trig atan sqrt\n", argv[0]); return 2; }
    g_data = load(argv[1], &size); trig = load(argv[2], &trig_size); atan = load(argv[3], &atan_size);
    sqrt_table = load(argv[4], &sqrt_size);
    if (!g_data || !trig || !atan || !sqrt_table) { printf("skipped: inputs missing\n"); return 77; }
    if (memcmp(g_data, "T3CC", 4) || le32(g_data + 4) != 3) { fprintf(stderr, "bad cases file\n"); return 2; }
    g_frames = (uint32_t)le32(g_data + 8);
    {
        const uint8_t* p = g_data + 12 + 31 * 4; int k; size_t j;
        for (i = 0; i < 300; i++) tables.presets[i / 10][i % 10] = (int16_t)le32(p + 4 * i);
        p += 1200;
        for (i = 0; i < 30; i++) tables.preset_index[i] = (int16_t)le32(p + 4 * i);
        p += 120; tables.outside_blend_out = (int16_t)le32(p); p += 4;
        for (k = 0; k < 3; k++) {
            g_list_count[k] = (size_t)le32(p); p += 4;
            if (g_list_count[k] > 256) { fprintf(stderr, "choice list too long\n"); return 2; }
            for (j = 0; j < g_list_count[k]; j++, p += 12)
                g_lists[k][j] = (t3_psx_camera_choice_t){(int16_t)le32(p), (uint16_t)le32(p + 4), le32(p + 8)};
        }
        for (k = 0; k < 2; k++) {
            size_t count;
            g_stream_base[k] = (uint32_t)le32(p); p += 4;
            count = (size_t)le32(p); p += 4;
            if (count > 128) { fprintf(stderr, "too many streams\n"); return 2; }
            for (j = 0; j < count; j++, p += 16)
                g_stream_entries[k][j] = (t3_psx_camera_stream_t){
                    .offset=(uint16_t)le32(p),.frames=(uint16_t)le32(p + 4),
                    .yaw_mode=(uint16_t)le32(p + 8),.first=(uint32_t)le32(p + 12)};
            g_streams[k].streams = g_stream_entries[k]; g_streams[k].stream_count = count;
            g_streams[k].sample_count = (size_t)le32(p); p += 4;
            {
                int16_t (*smp)[7] = malloc(g_streams[k].sample_count * sizeof *smp + 1); size_t q; int w;
                if (!smp) return 2;
                for (q = 0; q < g_streams[k].sample_count; q++) for (w = 0; w < 7; w++, p += 4) smp[q][w] = (int16_t)le32(p);
                g_streams[k].samples = (const int16_t (*)[7])smp;
            }
        }
        for (i = 0; i < 12; i++) tables.hit_rows[i / 3][i % 3] = (int16_t)le32(p + 4 * i);
        p += 48;
        for (k = 0; k < T3_PSX_CAMERA_HIT_LISTS; k++) {
            size_t count = (size_t)le32(p); p += 4;
            if (count > T3_PSX_CAMERA_HIT_RECORDS) { fprintf(stderr, "hit list too long\n"); return 2; }
            tables.hit_record_count[k] = (uint8_t)count;
            for (j = 0; j < count; j++, p += 20)
                tables.hit_records[k][j] = (t3_psx_camera_hit_record_t){(int16_t)le32(p),
                    {(int16_t)le32(p + 4), (int16_t)le32(p + 8), (int16_t)le32(p + 12), (int16_t)le32(p + 16)}};
        }
        for (k = 0; k < T3_PSX_CAMERA_HIT_SCRIPTS; k++, p += 56) {
            t3_psx_camera_hit_script_t* h = &tables.hit_scripts[k]; int w;
            h->kind = (int16_t)le32(p); h->frames = (int16_t)le32(p + 4); h->blend_in = (int16_t)le32(p + 8);
            h->blend_out = (int16_t)le32(p + 12); h->start_distance = le32(p + 16); h->end_distance = le32(p + 20);
            for (w = 0; w < 4; w++) { h->pitches[w] = (int16_t)le32(p + 24 + 4 * w); h->yaws[w] = (int16_t)le32(p + 40 + 4 * w); }
        }
        for (i = 0; i < 3; i++) {
            tables.winner_lateral[i] = (int16_t)le32(p + 4 * i); tables.winner_forward[i] = (int16_t)le32(p + 12 + 4 * i);
            tables.winner_height[i] = (int16_t)le32(p + 24 + 4 * i);
        }
        p += 36;
        for (k = 0; k < T3_PSX_CAMERA_SHAKES; k++, p += 4 + T3_PSX_CAMERA_SHAKE_BYTES) {
            g_shake_base[k] = (uint32_t)le32(p);
            memcpy(tables.shakes[k], p + 4, T3_PSX_CAMERA_SHAKE_BYTES);
        }
        g_head = (size_t)(p - g_data);
        tables.choices = g_lists[0]; tables.choice_count = g_list_count[0];
    }
    g_frame_size = 2 * FIGHTER_WORDS * 4 + 12;
    for (i = 0; i < NRANGES; i++) g_frame_size += RANGES[i].size;
    if (g_head + g_frame_size * g_frames != size) { fprintf(stderr, "size mismatch\n"); return 2; }
    trig_table.data = trig; trig_table.size = trig_size;
    tables.trig = &trig_table; tables.atan = atan; tables.sqrt = sqrt_table; tables.sqrt_size = sqrt_size;
    for (i = 0; i < 30; i++) tables.intro_offsets[i / 3][i % 3] = (int16_t)le32(g_data + 12 + 4 * i);
    if (!t3_psx_camera_valid(&tables) || atan_size != 1025) { fprintf(stderr, "bad tables\n"); return 2; }
    for (i = 0; i < NCHECKS; i++) first[i] = UINT32_MAX;

    /* Seeded pass. The trace patches bug 1, so the fixed look point is compared. */
    for (n = 1; n < g_frames; n++) {
        t3_psx_camera_t c; int16_t phase; int r; unsigned bad;
        seed(&c, n - 1, 0);
        r = drive(&c, n, &phase, rng);
        g_carry = c;
        if (r == 0) { fprintf(stderr, "frame %u: invalid input\n", n); return 1; }
        if (r < 0) { unported++; continue; }
        if (r == 3) { idle++; continue; }
        if (r == 4) {
            /* The winner camera starts from the fighters as the round flow placed
             * them for the win poses, a state the trace does not record (it holds
             * the frame's end). Only the style and the phase are checked. */
            t3_psx_camera_t e; seed(&e, n, 0); winner_starts++;
            if (c.winner_style != e.winner_style || phase != r16(n, 0x80095890u)) {
                printf("seeded frame %u: winner camera start differs (style %d want %d)\n", n, c.winner_style, e.winner_style);
                winner_start_fails++;
            }
            continue;
        }
        checked++;
        if (r == 2) replayed++;
        if (phase >= T3_PSX_CAMERA_WINNER_0) round_end++;
        if (c.state == T3_PSX_CINEMATIC_STREAM) streamed++;
        if (c.state == T3_PSX_CINEMATIC_HIT) hits++;
        bad = compare(&c, phase, n) | matrix_check(&tables, n) | backdrop_check(&tables, n);
        if (r == 2 || phase >= T3_PSX_CAMERA_WINNER_0) bad &= ~(1u << C_PHASE);   /* set by the round flow */
        if (!(bad & ~(1u << C_MATRIX))) exact++;
        for (i = 0; i < NCHECKS; i++) if (bad & 1u << i) { fails[i]++; if (first[i] == UINT32_MAX) first[i] = n; }
        if (bad & ~(1u << C_MATRIX | 1u << C_BACKDROP) && shown < 4) {
            printf("seeded frame %u (phase %d) differs:", n, phase);
            for (i = 0; i < NCHECKS; i++) if (bad & 1u << i) printf(" [%s]", NAMES[i]);
            printf("\n"); print_diff(&c, n); shown++;
        }
    }
    printf("seeded: %u frames, %u checked (%u in camera streams, %u in hit cameras, %u KO replay, %u round end), "
           "%u unported, %u without FightFrame, %u exact (camera state)\n", g_frames - 1, checked, streamed, hits, replayed,
           round_end, unported, idle, exact);
    printf("  %u winner camera starts: style and phase only (inputs not in the trace)\n", winner_starts);
    for (i = 0; i < NCHECKS; i++) if (fails[i]) printf("  %-14s %5u frames differ, first %u\n", NAMES[i], fails[i], first[i]);

    /* Free run from frame 0. */
    {
        t3_psx_camera_t c; int carried = 0; shown = 0;
        for (n = 1; n < g_frames; n++) {
            int16_t phase; int r; unsigned bad;
            if (!carried) { memset(&g_carry, 0, sizeof g_carry); seed(&c, n - 1, 0); reseeds++; carried = 1; }
            r = drive(&c, n, &phase, rng);
            g_carry = c;
            if (r <= 0) { carried = 0; continue; }
            shake_starts(&c, n);
            if (r == 3) continue;
            if (r == 4) { carried = 0; continue; }   /* reseeded after the winner camera's start */
            run_checked++;
            bad = compare(&c, phase, n);
            if (r == 2 || phase >= T3_PSX_CAMERA_WINNER_0) bad &= ~(1u << C_PHASE);
            if (!bad) run_exact++;
            for (i = 0; i < NCHECKS; i++) if (bad & 1u << i) run_fails[i]++;
            if (bad && shown < 3) {
                printf("free-run frame %u (phase %d) differs:", n, phase);
                for (i = 0; i < NCHECKS; i++) if (bad & 1u << i) printf(" [%s]", NAMES[i]);
                printf("\n"); print_diff(&c, n); shown++;
            }
        }
        printf("free run: %u of %u frames exact, %u reseeds\n", run_exact, run_checked, reseeds);
        for (i = 0; i < NCHECKS; i++) if (run_fails[i]) printf("  %-14s %5u frames differ\n", NAMES[i], run_fails[i]);
    }
    return exact == checked && run_exact == run_checked && !unported && !winner_start_fails ? 0 : 1;
}
