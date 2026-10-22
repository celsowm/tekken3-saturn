/* Isolated transport/correctness probe for the experimental visibility DSP.
 * Build as a separate disc against the same installed LibSaturn as the game. */
#include <stdint.h>
#include "saturn/app.h"
#include "saturn/color.h"
#include "saturn/font.h"
#include "saturn/scu_dsp.h"
#include "saturn/time.h"
#include "dsp_visibility_words.h"

#ifndef T3_PROBE_VERTICES
#define T3_PROBE_VERTICES 48
#endif
enum { VERTICES = T3_PROBE_VERTICES, FACES = 16, ROUNDS = 16, TIMEOUT = 60000 };
typedef struct t3_dsp_visibility_probe {
    uint32_t magic;
    uint32_t status;
    uint32_t cpu_ticks;
    uint32_t pack_ticks;
    uint32_t transfer_ticks;
    uint32_t execute_ticks;
    uint32_t read_ticks;
    uint32_t dsp_total_ticks;
    uint32_t cpu_visible;
    uint32_t dsp_visible;
    uint32_t dsp_fallback;
    uint32_t mismatches;
    uint32_t rounds;
    uint32_t frames;
} t3_dsp_visibility_probe_t;

volatile t3_dsp_visibility_probe_t g_t3_dsp_visibility_probe;
static sat_ascii_font_t g_font;
static int32_t g_vertices[VERTICES][3];
static uint8_t g_faces[FACES][4];
static uint32_t g_axes[3][48];
static uint32_t g_face_words[16];
static uint32_t g_md0_control[7] = {0u, FACES, 0u, 1u, 32u, 0u, 0u};
static uint32_t g_md2_eye[3] = {0u, 5000u, 0u};
static uint32_t g_md2_bounds[2] = {0x02000000u, 0xFC000000u};

/* The SDK takes this fast branch for the bounded face edges in this fixture:
 * 32-bit cross components and an exact 64-bit dot. */
static __attribute__((noinline)) uint32_t cpu_visible(void) {
    uint32_t bits = 0;
    for (uint32_t f = 0; f < FACES; ++f) {
        const int32_t *a = g_vertices[g_faces[f][0]],
                      *b = g_vertices[g_faces[f][1]],
                      *c = g_vertices[g_faces[f][2]],
                      *d = g_vertices[g_faces[f][3]];
        const int32_t ux = d[0] - a[0], uy = d[1] - a[1],
                      uz = d[2] - a[2];
        const int32_t vx = b[0] - a[0], vy = b[1] - a[1],
                      vz = b[2] - a[2];
        const int32_t nx = uy * vz - uz * vy, ny = uz * vx - ux * vz,
                      nz = ux * vy - uy * vx;
        const int32_t cx = (int32_t)(((int64_t)a[0] + b[0] + c[0] + d[0]) / 4),
                      cy = (int32_t)(((int64_t)a[1] + b[1] + c[1] + d[1]) / 4),
                      cz = (int32_t)(((int64_t)a[2] + b[2] + c[2] + d[2]) / 4);
        const int64_t dot = nx * ((int64_t)(int32_t)g_md2_eye[0] - cx) +
                            ny * ((int64_t)(int32_t)g_md2_eye[1] - cy) +
                            nz * ((int64_t)(int32_t)g_md2_eye[2] - cz);
        if (dot > 0) bits |= (uint32_t)1 << f;
    }
    return bits;
}

static void pack(void) {
    for (uint32_t v = 0; v < VERTICES; ++v) {
        for (uint32_t k = 0; k < 3; ++k) g_axes[k][v] = (uint32_t)(g_vertices[v][k] / 6);
    }
    for (uint32_t f = 0; f < FACES; ++f) {
        g_face_words[f] = ((uint32_t)g_faces[f][0] << 24) |
                          ((uint32_t)g_faces[f][1] << 16) |
                          ((uint32_t)g_faces[f][2] << 8) | g_faces[f][3];
    }
}

static uint32_t transfer(void) {
    sat_result_t result;
    for (uint32_t k = 0; k < 3; ++k) {
        result = sat_dsp_write_data(k, 0u, g_axes[k], VERTICES);
        if (result != SAT_OK) return (uint32_t)(-result);
    }
    result = sat_dsp_write_data(3u, 0u, g_face_words, FACES);
    if (result != SAT_OK) return (uint32_t)(-result);
    result = sat_dsp_write_data(2u, 48u, g_md2_eye, 3u);
    if (result != SAT_OK) return (uint32_t)(-result);
    result = sat_dsp_write_data(2u, 61u, g_md2_bounds, 2u);
    if (result != SAT_OK) return (uint32_t)(-result);
    result = sat_dsp_write_data(0u, 51u, g_md0_control, 7u);
    return (uint32_t)(-result);
}

static void line(const char* label, uint32_t value, int y) {
    (void)sat_ascii_font_draw_label_u32(&g_font, label, value, 8, y, 8, 0u, 0u);
}

int main(void) {
    uint16_t start, total;
    if (sat_app_init_default() != SAT_OK) return 1;
    if (sat_ascii_font_init_8x8_indexed8(&g_font, SAT_COLOR_WHITE, SAT_COLOR_BLACK, 7u) != SAT_OK) return 1;
    g_t3_dsp_visibility_probe.magic = SAT_FOURCC('D','V','P','1');
    for (uint32_t i = 0; i < VERTICES; ++i) {
        g_vertices[i][0] = (int32_t)(i % 8u) * 60;
        g_vertices[i][1] = ((int32_t)((i * 5u) % 7u) - 3) * 6;
        g_vertices[i][2] = (int32_t)(i / 8u) * 60;
    }
    for (uint32_t f = 0; f < FACES; ++f) {
        const uint8_t a = (uint8_t)((f * 2u) % (VERTICES - 8u));
        g_faces[f][0] = a;
        g_faces[f][1] = (uint8_t)(a + 1u);
        g_faces[f][2] = (uint8_t)(a + 9u);
        g_faces[f][3] = (uint8_t)(a + 8u);
    }
    g_t3_dsp_visibility_probe.status = (uint32_t)(-sat_dsp_load_program(
        kT3DspVisibility, kT3DspVisibilityCount, kT3DspVisibilityAddress));
    if (g_t3_dsp_visibility_probe.status == 0u) {
        for (uint32_t n = 0; n < ROUNDS; ++n) {
            start = sat_time_frc();
            const uint32_t expected = cpu_visible();
            g_t3_dsp_visibility_probe.cpu_ticks += (uint16_t)(sat_time_frc() - start);
            g_t3_dsp_visibility_probe.cpu_visible = expected;
            start = sat_time_frc();
            pack();
            g_t3_dsp_visibility_probe.pack_ticks += (uint16_t)(sat_time_frc() - start);
            total = sat_time_frc();
            start = total;
            g_t3_dsp_visibility_probe.status = transfer();
            g_t3_dsp_visibility_probe.transfer_ticks += (uint16_t)(sat_time_frc() - start);
            if (g_t3_dsp_visibility_probe.status != 0u) break;
            start = sat_time_frc();
            g_t3_dsp_visibility_probe.status = (uint32_t)(-sat_dsp_start(0u));
            if (g_t3_dsp_visibility_probe.status == 0u)
                g_t3_dsp_visibility_probe.status = (uint32_t)(-sat_dsp_wait(TIMEOUT));
            g_t3_dsp_visibility_probe.execute_ticks += (uint16_t)(sat_time_frc() - start);
            if (g_t3_dsp_visibility_probe.status != 0u) break;
            start = sat_time_frc();
            uint32_t visible = 0, fallback = 0;
            sat_result_t result = sat_dsp_read_data(0u, 53u, &visible, 1u);
            if (result == SAT_OK) result = sat_dsp_read_data(0u, 57u, &fallback, 1u);
            g_t3_dsp_visibility_probe.read_ticks += (uint16_t)(sat_time_frc() - start);
            if (result != SAT_OK) {
                g_t3_dsp_visibility_probe.status = (uint32_t)(-result);
                break;
            }
            g_t3_dsp_visibility_probe.dsp_total_ticks += (uint16_t)(sat_time_frc() - total);
            g_t3_dsp_visibility_probe.dsp_visible = visible;
            g_t3_dsp_visibility_probe.dsp_fallback = fallback;
            g_t3_dsp_visibility_probe.mismatches += (visible != expected || fallback != 0u);
            ++g_t3_dsp_visibility_probe.rounds;
        }
    }
    for (;;) {
        sat_pad_state_t pad = {0};
        if (sat_app_frame_begin(SAT_COLOR_BLACK, SAT_COLOR_GREEN, &pad) != SAT_OK) break;
        ++g_t3_dsp_visibility_probe.frames;
        line("STATUS ", g_t3_dsp_visibility_probe.status, 8);
        line("CPU ", g_t3_dsp_visibility_probe.cpu_ticks, 24);
        line("PACK ", g_t3_dsp_visibility_probe.pack_ticks, 40);
        line("XFER ", g_t3_dsp_visibility_probe.transfer_ticks, 56);
        line("DSP ", g_t3_dsp_visibility_probe.execute_ticks, 72);
        line("READ ", g_t3_dsp_visibility_probe.read_ticks, 88);
        line("TOTAL ", g_t3_dsp_visibility_probe.dsp_total_ticks, 104);
        line("MISMATCH ", g_t3_dsp_visibility_probe.mismatches, 120);
        line("ROUNDS ", g_t3_dsp_visibility_probe.rounds, 136);
        (void)sat_app_frame_end();
        if ((pad.pressed & SAT_PAD_START) != 0u) break;
    }
    (void)sat_shutdown();
    return 0;
}
