/*
 * Tekken 3 Saturn - experimental fighting prototype.
 * Uses only installed LibSaturn public APIs. By default the characters are
 * original procedural assets; T3_LOCAL_KMD explicitly opts into a private
 * user-supplied static KMD preview, never used for public CI.
 */
#include <stdint.h>

#include "fight.h"
#include "sim_profile.h"
#include "tick_clock.h"
#if defined(T3_CAPTURE_INPUT_TAPE) || defined(T3_REPLAY_INPUT_TAPE)
#include "input_tape.h"
#endif
#include "characters.h"
#if defined(T3_VDP1_COVERAGE_PROBE)
#include "coverage_probe.h"
#endif
#include "saturn/app.h"
#include "saturn/color.h"
#if defined(T3_DMA_COMMAND_LIST)
#include "saturn/dma.h"
#endif
#include "saturn/font.h"
#include "saturn/hud.h"
#include "saturn/input.h"
#include "saturn/loader.h"
#include "saturn/render3d.h"
#include "saturn/scene.h"
#include "saturn/scene3d.h"
#include "saturn/video.h"
#include "saturn/vdp1.h"
#include "saturn/time.h"
#if defined(T3_OVERLAP_VDP1)
#include "saturn/irq.h"
#endif
#if defined(T3_NATIVE_COMMANDS)
#include "saturn/vdp1_command_batch.h"
static sat_vdp1_command_batch_t g_native_commands;
volatile uint32_t g_t3_native_command_frames;
volatile uint32_t g_t3_cached_stage_faces;
#endif
#include "cold_data.h"
#include "diagnostics.h"
#include "placement.h"
#include "move_probe.h"
#include "perf.h"
#include "sfx.h"
#include "flipbook_burst.h"
#if defined(T3_ORIGINAL_HUD)
#include "original_hud.h"
#include "psx_hud_messages.h"
#endif
#include "combat_events.h"
#if defined(T3_PARTICLE_PREPARE)
#include "particle_prepare.h"
#if defined(T3_PARTICLE_TICK_AUDIT)
/* First visible prepared images: game tick, source frame, visible count. */
volatile uint32_t g_t3_particle_tick_audit[32][3];
volatile uint32_t g_t3_particle_tick_audit_count;
#endif
#endif
#if defined(T3_SLAVE_SIMULATION)
#include <saturn/parallel.h>
_Static_assert(T3_CLIP_COUNT==1,"Slave simulation currently requires the original single-clip bridge");
#endif
#if defined(T3_VDP2_FLOOR)
#include "stage_vdp2.h"
#endif
#if defined(T3_VDP1_FLOOR_PROBE)
#include "psx_viewport.h"
#include "stage_floor_visibility.h"
#include "floor_command_budget.h"
/* Grid placement follows Tekken3Remake stages.md / FloorDrawGrid.
 * Copyright (c) 2026 n0madic, PolyForm Noncommercial; see NOTICE.md. */
static int32_t g_floor_projected[121][3];
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
static uint16_t g_floor_vertex_shade[121];
#endif
static int32_t floor_grid_center(int32_t position) {
    int32_t rounded=position+900;
    int32_t tile=rounded/1800;
    if(rounded<0)--tile; /* Original truncates and then adjusts negatives. */
    return tile*1800;
}
#if defined(T3_VDP1_FLOOR_SUBDIVIDE_PROBE)
/* Experimental counters: subdivided quads, single quads, capacity fallbacks,
 * and maximum command occupancy observed at diagnostics publication. */
volatile uint32_t g_t3_floor_split_stats[4];
static uint16_t floor_half_colour(uint16_t a,uint16_t b) {
    unsigned r=((a&31u)+(b&31u))/2u;
    unsigned g=(((a>>5)&31u)+((b>>5)&31u))/2u;
    unsigned blue=(((a>>10)&31u)+((b>>10)&31u))/2u;
    return (uint16_t)(r|(g<<5)|(blue<<10));
}
#if T3_VDP1_FLOOR_SPLIT_MIN_AREA > 0
static unsigned floor_visible_bbox_area(const sat_distorted_sprite_cmd_t* cmd) {
    int x0=cmd->x[0],x1=x0,y0=cmd->y[0],y1=y0;
    for(unsigned i=1;i<4;++i) {
        if(cmd->x[i]<x0)x0=cmd->x[i];
        if(cmd->x[i]>x1)x1=cmd->x[i];
        if(cmd->y[i]<y0)y0=cmd->y[i];
        if(cmd->y[i]>y1)y1=cmd->y[i];
    }
    if(x0< -160)x0=-160;
    if(x1>159)x1=159;
    if(y0< -112)y0=-112;
    if(y1>111)y1=111;
    return x0>x1 || y0>y1?0u:(unsigned)(x1-x0+1)*(unsigned)(y1-y0+1);
}
#endif
static sat_result_t floor_draw_quarters(const sat_distorted_sprite_cmd_t* full,const uint16_t colour[4]) {
    sat_vdp1_command_checkpoint_t checkpoint;
    sat_result_t status=sat_vdp1_command_checkpoint(&checkpoint);
    if(status!=SAT_OK)return status;
    /* 3x3 grid: the centre lies on the PS1 B-D triangle diagonal. */
    int16_t x[9]={full->x[0],(full->x[0]+full->x[1])/2,full->x[1],
        (full->x[0]+full->x[3])/2,(full->x[1]+full->x[3])/2,(full->x[1]+full->x[2])/2,
        full->x[3],(full->x[3]+full->x[2])/2,full->x[2]};
    int16_t y[9]={full->y[0],(full->y[0]+full->y[1])/2,full->y[1],
        (full->y[0]+full->y[3])/2,(full->y[1]+full->y[3])/2,(full->y[1]+full->y[2])/2,
        full->y[3],(full->y[3]+full->y[2])/2,full->y[2]};
    uint16_t c[9]={colour[0],floor_half_colour(colour[0],colour[1]),colour[1],
        floor_half_colour(colour[0],colour[3]),floor_half_colour(colour[1],colour[3]),floor_half_colour(colour[1],colour[2]),
        colour[3],floor_half_colour(colour[3],colour[2]),colour[2]};
    for(unsigned q=0;q<4;++q) {
        unsigned start=(q>>1)*3+(q&1u),v[4]={start,start+1,start+4,start+3};
        sat_distorted_sprite_cmd_t cmd={0};uint16_t shades[4];
        cmd.texture=t3_stage_floor_quarter(q);cmd.flags=SAT_SPRITE_FLAG_OPAQUE;
        for(unsigned i=0;i<4;++i){cmd.x[i]=x[v[i]];cmd.y[i]=y[v[i]];shades[i]=c[v[i]];}
        sat_result_t st=sat_draw_sprite_distorted_gouraud(&cmd,shades);
        if(st!=SAT_OK) {
            sat_result_t rollback=sat_vdp1_command_rollback(&checkpoint);
            return rollback==SAT_OK?st:rollback;
        }
    }
    return SAT_OK;
}
#if defined(T3_VDP1_FLOOR_CAPACITY_SELFTEST)
/* Executed once before any real world commands. Every artificial command and
 * Gouraud table is rolled back; nothing is sent to the VDP1. */
volatile uint32_t g_t3_floor_capacity_selftest[4];
static sat_result_t floor_capacity_selftest(void) {
    if(g_t3_floor_capacity_selftest[0])return SAT_OK;
    sat_vdp1_command_checkpoint_t original,before,after;
    sat_vdp1_command_stats_t stats;
    sat_result_t st=sat_vdp1_command_checkpoint(&original);
    if(st!=SAT_OK)return st;
    if((st=sat_vdp1_command_stats(&stats))!=SAT_OK)return st;
    if(stats.capacity<=stats.overlay_reserved+original.used+4u)return SAT_ERR_CAPACITY;
    const uint16_t colours[4]={SAT_GOURAUD_NEUTRAL,SAT_GOURAUD_NEUTRAL,SAT_GOURAUD_NEUTRAL,SAT_GOURAUD_NEUTRAL};
    sat_distorted_sprite_cmd_t cmd={.x={-20,20,20,-20},.y={-20,-20,20,20},
        .texture=t3_stage_floor_texture(),.flags=SAT_SPRITE_FLAG_OPAQUE};
    for(unsigned available=1;available<=3;++available) {
        unsigned target=stats.capacity-stats.overlay_reserved-1u-available;
        for(unsigned i=original.used;i<target;++i)
            if((st=sat_draw_rect_screen(0,0,1,1,0x8000u))!=SAT_OK)return st;
        if((st=sat_vdp1_command_checkpoint(&before))!=SAT_OK)return st;
        st=floor_draw_quarters(&cmd,colours);
        if(st!=SAT_ERR_CAPACITY)return SAT_ERR_VERSION;
        if((st=sat_vdp1_command_checkpoint(&after))!=SAT_OK)return st;
        if(before.used!=after.used || before.gouraud_tables!=after.gouraud_tables)return SAT_ERR_VERSION;
        if((st=sat_draw_sprite_distorted_gouraud(&cmd,colours))!=SAT_OK)return st;
        if((st=sat_vdp1_command_checkpoint(&after))!=SAT_OK)return st;
        if(after.used!=before.used+1u || after.gouraud_tables!=before.gouraud_tables+1u)return SAT_ERR_VERSION;
        if((st=sat_vdp1_command_rollback(&original))!=SAT_OK)return st;
        ++g_t3_floor_capacity_selftest[available];
    }
    g_t3_floor_capacity_selftest[0]=1;
    return SAT_OK;
}
#endif
#endif
/* Last frame's protected face allowance and floor allowance, plus cumulative
 * single-tile and no-tile budget fallbacks and the largest requested reserve. */
volatile uint32_t g_t3_floor_budget_stats[5];
static struct {
    t3_psx_camera_matrix_t matrix;
    int32_t origin[2];
    uint32_t midpoint_distance;
    uint8_t drawable;
} g_floor_image;
/* Capture before fused actor preparation starts deferred simulation on the
 * Slave. Drawing later must not read the mutable owned match. */
static void floor_capture_image(void) {
    g_floor_image.drawable=0;
    if(!t3_move_probe_camera_matrix(&g_floor_image.matrix))return;
    g_floor_image.origin[0]=t3_move_probe_world_center(0);
    g_floor_image.origin[1]=t3_move_probe_world_center(2);
    if(t3_move_probe_floor_center(g_floor_image.origin)==0)return;
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
    if(!t3_move_probe_floor_camera_distance(&g_floor_image.midpoint_distance))return;
#endif
    g_floor_image.drawable=1;
}
static void draw_floor_probe(uint32_t budget) {
    if(!g_floor_image.drawable)return;
    t3_psx_camera_matrix_t matrix=g_floor_image.matrix;
    const int32_t cx=floor_grid_center(g_floor_image.origin[0]);
    const int32_t cz=floor_grid_center(g_floor_image.origin[1]);
    const int32_t grid_origin[3]={cx,0,cz};
    t3_psx_camera_translate_view(&matrix,grid_origin,&matrix);
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
    const uint32_t midpoint_distance=g_floor_image.midpoint_distance;
#endif
    for(unsigned z=0;z<11;++z)for(unsigned x=0;x<11;++x) {
        const int16_t point[3]={(int16_t)(-9000+(int32_t)x*1800),0,(int16_t)(-9000+(int32_t)z*1800)};
        t3_psx_camera_project_s16(&matrix,point,g_floor_projected[z*11+x]);
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
        g_floor_vertex_shade[z*11+x]=t3_stage_floor_shade((uint32_t)g_floor_projected[z*11+x][2],midpoint_distance);
#endif
    }
#if defined(T3_VDP1_FLOOR_SUBDIVIDE_PROBE)
    /* First reserve one command for every remaining eligible tile. Larger
     * tiles can use their three extra commands only from the surplus. */
    uint32_t remaining_tiles=0;
    for(unsigned z=0;z<10;++z)for(unsigned x=0;x<10;++x) {
        const unsigned v[4]={(z+1)*11+x,(z+1)*11+x+1,z*11+x+1,z*11+x};
        int32_t corners[4][3];
        for(unsigned k=0;k<4;++k)for(unsigned axis=0;axis<3;++axis)
            corners[k][axis]=g_floor_projected[v[k]][axis];
        if(t3_stage_floor_visible((const int32_t (*)[3])corners))++remaining_tiles;
    }
#endif
    /* FloorDrawGrid prepends each Paul tile to the same PS1 OT bucket.
     * VDP1 consumes commands in submission order, so reverse the scan. */
    for(int z=9;z>=0;--z)for(int x=9;x>=0;--x) {
        const unsigned v[4]={(z+1)*11+x,(z+1)*11+x+1,z*11+x+1,z*11+x};
        sat_distorted_sprite_cmd_t cmd={0};
        cmd.texture=t3_stage_floor_texture();cmd.flags=SAT_SPRITE_FLAG_OPAQUE;
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
        uint16_t gouraud[4];
#endif
        int32_t corners[4][3];
        for(unsigned k=0;k<4;++k) {
            const int32_t* p=g_floor_projected[v[k]];
            for(unsigned axis=0;axis<3;++axis)corners[k][axis]=p[axis];
#if defined(T3_VDP1_FLOOR_SHADE_PROBE)
            gouraud[k]=g_floor_vertex_shade[v[k]];
#endif
            int sx=t3_psx_viewport_x(p[0]),sy=t3_psx_viewport_y(p[1]);
            cmd.x[k]=(int16_t)(sx-160);cmd.y[k]=(int16_t)(sy-112);
        }
        if(!t3_stage_floor_visible((const int32_t (*)[3])corners))continue;
        if(!budget){++g_t3_floor_budget_stats[3];return;}
#if defined(T3_VDP1_FLOOR_SUBDIVIDE_PROBE)
        --remaining_tiles;
#if T3_VDP1_FLOOR_SPLIT_MIN_AREA > 0
        int split=floor_visible_bbox_area(&cmd)>=T3_VDP1_FLOOR_SPLIT_MIN_AREA;
        if(split && budget<remaining_tiles+4u){split=0;++g_t3_floor_budget_stats[2];}
        sat_result_t st=split?floor_draw_quarters(&cmd,gouraud):sat_draw_sprite_distorted_gouraud(&cmd,gouraud);
#else
        int split=1;
        if(budget<remaining_tiles+4u){split=0;++g_t3_floor_budget_stats[2];}
        sat_result_t st=split?floor_draw_quarters(&cmd,gouraud):sat_draw_sprite_distorted_gouraud(&cmd,gouraud);
#endif
        if(st==SAT_ERR_CAPACITY && split) {
            ++g_t3_floor_split_stats[2];
            st=sat_draw_sprite_distorted_gouraud(&cmd,gouraud);
            split=0;
        }
        if(st==SAT_OK){++g_t3_floor_split_stats[split?0:1];budget-=split?4u:1u;}
#elif defined(T3_VDP1_FLOOR_SHADE_PROBE)
        sat_result_t st=sat_draw_sprite_distorted_gouraud(&cmd,gouraud);
        if(st==SAT_OK)--budget;
#else
        sat_result_t st=sat_draw_sprite_distorted(&cmd);
        if(st==SAT_OK)--budget;
#endif
        if(st==SAT_ERR_CAPACITY)break;
        SAT_PANIC_IF_ERROR(st);
    }
}
#endif
#if defined(T3_CPU_DIAGNOSTICS)
#include <saturn/sh2.h>
#include <saturn/parallel.h>
volatile uint32_t g_t3_boot_master_entry;
uint32_t g_t3_boot_cache[2] __attribute__((section(".wram_l")));
volatile uint32_t g_t3_boot_slave_verified;
static void capture_cpu_cache(void* context,uint32_t first,uint32_t last) {
    uint32_t* out=context;
    for(uint32_t i=first;i<last;++i)out[i]=sat_sh2_cache_control();
}
static void capture_boot_cache(void) {
    g_t3_boot_cache[0]=sat_sh2_cache_control();g_t3_boot_cache[1]=UINT32_MAX;
    g_t3_boot_slave_verified=0;
    sat_parallel_stats_t before={0},after={0};
    if(!sat_parallel_initialized() || !sat_parallel_slave_available() ||
       sat_parallel_stats(&before)!=SAT_OK || before.queued ||
       before.submitted!=before.completed || before.failed || before.cancelled)return;
    if(sat_parallel_for(0,2,1,capture_cpu_cache,g_t3_boot_cache,
            g_t3_boot_cache,sizeof(g_t3_boot_cache))==SAT_OK &&
       sat_parallel_stats(&after)==SAT_OK &&
       after.submitted==before.submitted+1 && after.completed==before.completed+1 &&
       after.slave_tasks==before.slave_tasks+1 && !after.failed && !after.cancelled)
        g_t3_boot_slave_verified=1;
    else g_t3_boot_cache[1]=UINT32_MAX;
}
#endif

#if defined(T3_VDP2_FLOOR)
#define T3_STAGE_SCENE_CAPACITY (T3_FACE_COUNT*2u+16u)
#else
#define T3_STAGE_SCENE_CAPACITY T3_SCENE_CAPACITY
#endif
enum { W = 320, H = 224, FACE_CAP = T3_STAGE_SCENE_CAPACITY, HUD_COMMANDS = 192,
       HUD_FRAME_Y=47,HUD_CPU_Y=3,HUD_MASTER_CACHE_X=88,HUD_SLAVE_CACHE_X=168 };
#define FX(x) ((sat_fx16_t)((x) * SAT_FX16_ONE))
#define Q(x)  ((sat_fx16_t)((x) * (SAT_FX16_ONE / 100)))

/* Fully initialized by t3_match_reset before any read. */
/* Reference traces occupy High WRAM. Keep the two auxiliary frontend
 * snapshots in the diagnostic's spare Low WRAM; the owned PS1 match stays
 * in its normal placement and the 16 KiB Master stack reserve is preserved. */
#if defined(T3_REFERENCE_REPLAY)
#define T3_FRONTEND_WRAM __attribute__((section(".wram_l")))
#else
#define T3_FRONTEND_WRAM T3_HOT_WRAM
#endif
static t3_match_t g_game T3_FRONTEND_WRAM;
static t3_tick_clock_t g_tick_clock;
#if defined(T3_SLAVE_SIMULATION)
/* Up to three ticks run after an image is captured, so an image is at most
 * three ticks behind the simulation. With two, a late image ran its third
 * tick before the capture while the Master waited, which made the next image
 * late too (THIRTY_IMAGE_GATE.md). */
enum { SIMULATION_TASK=0x101u,SIMULATION_TIMEOUT=60000u,DEFERRED_TICKS=3u };
typedef struct {
    sat_pad_state_t pads[2];
    uint32_t first,count,epoch;
} simulation_job_t;
static simulation_job_t g_simulation_job T3_HOT_WRAM;
/* The Slave's deepest measured use is 4,472 bytes (slave_sim_stack_used); 8 KiB keeps
 * about 1.8x headroom and frees room in the full High Work RAM. Keep simulation_stack.S in step. */
uint8_t g_t3_simulation_stack[8192] __attribute__((aligned(16)));
static t3_match_t g_image_match T3_FRONTEND_WRAM;
#if defined(T3_ORIGINAL_HUD) && defined(T3_ENABLE_PSX_OWNED_MATCH)
static t3_psx_hud_bar_t g_image_hud_bars[2]
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    __attribute__((section(".wram_l")))
#endif
    ;
#endif
#if defined(T3_ORIGINAL_HUD)
static int32_t g_image_round[12]
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
    __attribute__((section(".wram_l")))
#endif
    ;
#else
static int32_t g_image_round[6];
#endif
static uint32_t g_simulation_epoch=1u,g_slave_simulation_epoch;
volatile uint32_t g_t3_slave_sim_frames,g_t3_slave_sim_jobs,g_t3_slave_sim_ticks;
volatile uint32_t g_t3_slave_sim_pose_tick,g_t3_slave_sim_pose_lag;
volatile uint32_t g_t3_slave_sim_wait_ms,g_t3_slave_sim_max_wait_ms;
#endif
/* T3_SLAVE_FRT_PROBE: Slave FRT (/128) totals, read from a Mednafen save
 * state (scripts/mednafen_state.py): [0] ticks, [1] actor preparation,
 * [2] stage tables, [3] stage builds by the Slave, [4] by the Master. Off by
 * default: High WRAM has no room for the code. */
#if defined(T3_SLAVE_FRT_PROBE)
volatile uint32_t g_t3_slave_frt[5] __attribute__((section(".wram_l")));
#define SLAVE_FRT_START(name) const uint16_t name=sat_time_frc()
#define SLAVE_FRT_ADD(slot,name) (g_t3_slave_frt[slot]+=(uint16_t)(sat_time_frc()-(name)))
#define SLAVE_FRT_COUNT(slot) (++g_t3_slave_frt[slot])
#define SLAVE_FRT_RESET() do{for(unsigned i_=0;i_<5u;++i_)g_t3_slave_frt[i_]=0;}while(0)
#else
#define SLAVE_FRT_START(name) do{}while(0)
#define SLAVE_FRT_ADD(slot,name) do{}while(0)
#define SLAVE_FRT_COUNT(slot) do{}while(0)
#define SLAVE_FRT_RESET() do{}while(0)
#endif
static sat_scene_t g_scene;
/* Scene scratch uses the Saturn's existing second MiB. Keep authored banks
 * and hot actor data in High WRAM; the linker checks both memory budgets. */
#if defined(T3_PROJECTED_FACE_QUEUE)
static sat_scene3d_face_t g_faces[T3_COLD_FACES] __attribute__((section(".wram_l")));
#else
static sat_scene3d_face_t g_faces[FACE_CAP] __attribute__((section(".wram_l")));
#endif
#if defined(T3_PROJECTED_FACE_QUEUE)
#if defined(T3_NATIVE_COMMANDS)
static sat_scene3d_projected_face_t g_projected_faces[FACE_CAP] __attribute__((section(".wram_l")));
#else
static sat_scene3d_projected_face_t g_projected_faces[FACE_CAP];
#endif
#if defined(T3_DIRECT_FACE_SPANS)
static uint16_t g_projected_indices[FACE_CAP];
#endif
#endif
#if defined(T3_HIGH_SORT_KEYS)
static uint32_t g_keys[FACE_CAP];
#else
static uint32_t g_keys[FACE_CAP] T3_HOT_WRAM;
#endif
#if defined(T3_MIXED_OT_DIAGNOSTIC)
static uint16_t g_order[FACE_CAP] __attribute__((section(".wram_l")));
#else
static uint16_t g_order[FACE_CAP] T3_HOT_WRAM;
#endif
/* Explicitly cleared and uploaded by HUD initialization before its first use. */
static sat_ascii_font_t g_font __attribute__((section(".wram_l")));
static sat_hud_t g_hud;
#ifndef T3_VIEWPORT_SIGN
#define T3_VIEWPORT_SIGN 2
#endif
#ifndef T3_VIEWPORT_DX
#define T3_VIEWPORT_DX 6
#endif
static sat_camera3d_t g_camera;
#if defined(T3_NATIVE_COMMANDS)
enum { STAGE_FACES=60 };
static sat_view_cache_item_t g_stage_storage[STAGE_FACES] __attribute__((section(".wram_l")));
static sat_view_cache_camera_t g_stage_camera __attribute__((section(".wram_l")));
static sat_view_cache_t g_stage_cache;
static uint16_t g_stage_count,g_stage_face,g_stage_cursor,g_stage_ready_count;
static const sat_view_cache_item_t* g_stage_ready;
static uint8_t g_stage_baking;
#endif
#if defined(T3_STAGE_HUD) || \
    (defined(T3_NATIVE_PHASE_PROBE) && !defined(T3_NATIVE_PHASE_PROBE_SILENT)) || \
    (defined(T3_NATIVE_WAIT_PROBE) && !defined(T3_NATIVE_WAIT_PROBE_SILENT))
static uint8_t g_show_profiler=1;
#else
static uint8_t g_show_profiler;
#endif
static uint32_t g_render_work_ms, g_render_submissions;
#if defined(T3_NATIVE_WAIT_PROBE)
static uint32_t g_t3_submit_ticks_last, g_t3_submit_ticks_sum, g_t3_submit_ticks_count;
static uint32_t g_t3_native_sim_ticks, g_t3_native_prepare_ticks;
static uint32_t g_t3_native_flush_ticks, g_t3_native_hud_ticks, g_t3_native_join_ticks;
static uint32_t g_t3_native_pre_ticks, g_t3_native_post_ticks;
/* Whole-run image statistics: duration buckets, and phase sums by the
 * image's tick count (2 or 3+). */
static uint32_t g_t3_hist[4], g_t3_by_ticks[2][6], g_t3_by_ticks_n[2];
static uint32_t g_t3_native_painter_ticks, g_t3_native_emit_ticks;
static uint32_t g_t3_painter_sum, g_t3_emit_sum, g_t3_flush_samples;
#endif
#if defined(T3_OVERLAP_VDP1)
volatile uint32_t g_t3_vdp1_overlap_frames;
#endif
static uint32_t g_rate_sample_vblank, g_rate_sample_frames, g_rate_x10;

static uint32_t display_fields(void) {
#if defined(T3_OVERLAP_VDP1)
    /* sat_frame_count advances when sat_wait_vblank observes an edge. The
     * interrupt counter keeps advancing during CPU preparation as well. */
    if(sat_irq_active())return sat_irq_count(SAT_IRQ_VBLANK_IN);
#endif
    return sat_frame_count();
}

/* Read by emulator/debugger using the ELF symbol g_t3_diagnostics.
 * frame_serial is written last: it marks a complete snapshot. */
volatile t3_diagnostics_t g_t3_diagnostics __attribute__((section(".wram_l")));

enum {
    BOOT_MODEL, BOOT_PLAYER1, BOOT_PLAYER2, BOOT_MOVES, BOOT_POSES, BOOT_READY,
    LOADING_TITLE_Y = 64, LOADING_LABEL_Y = 100, LOADING_STAGE_Y = 124,
    LOADING_BAR_Y = 160, LOADING_PERCENT_MAX = 100,
    FRAME_BACKGROUND = SAT_RGB555(2,3,8), LOADING_FILL = SAT_RGB555(28,5,4)
};

static void loading_frame(unsigned completed, const char* stage) {
    sat_loading_screen_t screen;
    sat_loading_screen_default(&screen);
    screen.background = FRAME_BACKGROUND;
    screen.fill = LOADING_FILL;
    screen.y = LOADING_BAR_Y;
    SAT_PANIC_IF_ERROR(sat_wait_vblank());
    SAT_PANIC_IF_ERROR(sat_set_clear_color(screen.background));
    SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(screen.background));
    SAT_PANIC_IF_ERROR(sat_begin_frame());
    SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud, "TEKKEN 3", W / 2, LOADING_TITLE_Y));
    SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud, "LOADING", W / 2, LOADING_LABEL_Y));
    SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud, stage, W / 2, LOADING_STAGE_Y));
    SAT_PANIC_IF_ERROR(sat_loading_screen_draw_bar(&screen,
        (uint16_t)(completed * LOADING_PERCENT_MAX / BOOT_READY)));
    SAT_PANIC_IF_ERROR(sat_end_frame());
}

static void character_loading_progress(void* context, t3_character_load_stage_t stage) {
    (void)context;
    switch (stage) {
    case T3_CHARACTER_LOAD_MODEL_READY: loading_frame(BOOT_PLAYER1, "PLAYER 1"); break;
    case T3_CHARACTER_LOAD_PLAYER1_READY: loading_frame(BOOT_PLAYER2, "PLAYER 2"); break;
    case T3_CHARACTER_LOAD_PLAYER2_READY: loading_frame(BOOT_MOVES, "MOVE DATA"); break;
    }
}

/* Inline: the original camera build uses neither helper. */
static inline sat_vec3_t vec(sat_fx16_t x, sat_fx16_t y, sat_fx16_t z) {
    sat_vec3_t v = {x, y, z};
    return v;
}
static inline sat_fx16_t from_q8(int32_t v) {
    return (sat_fx16_t)(v * (SAT_FX16_ONE / 256));
}

#if !defined(T3_VDP2_FLOOR)
static void quad(sat_vec3_t a, sat_vec3_t b, sat_vec3_t c, sat_vec3_t d,
                 uint16_t color) {
    sat_quad3_t q = {{a, b, c, d}};
    sat_scene3d_material_t m = {0};
    m.kind = SAT_SCENE3D_RGB;
    m.rgb555 = color;
    m.color_calc_slot = SAT_INDEXED_SOLID_OPAQUE;
#if defined(T3_NATIVE_COMMANDS)
    const uint16_t face=g_stage_face++;
    if(g_stage_ready && g_stage_cursor<g_stage_ready_count &&
       g_stage_ready[g_stage_cursor].tag==face && g_stage_ready[g_stage_cursor].color==color) {
        SAT_PANIC_IF_ERROR(sat_scene_queue_baked_view_item_material(
            &g_scene,&g_stage_ready[g_stage_cursor++],&m,0));
        return;
    }
    if(g_stage_baking) {
        const sat_result_t cached=sat_view_cache_append_world(&g_stage_cache,&q,color,face);
        if(cached!=SAT_ERR_UNSUPPORTED)SAT_PANIC_IF_ERROR(cached);
    }
#endif
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

#endif

#if defined(T3_VDP2_FLOOR) && defined(T3_PSX_CAMERA) && !defined(T3_NATIVE_COMMANDS)
static int32_t g_stage_input[3]; /* the world centre the image's camera was built around */
/* Hit and guard sparks: a four-point flash at the defender, 6 images long. The
 * original spawns effect sprites at the contact point (HitSpawnEffect,
 * FUN_80076F10); the flash sits on the hitting segment's end point. */
enum { SPARK_IMAGES = 6 };
static struct { uint8_t age; uint8_t block; int32_t at[3]; } g_spark[2];
static void spark_events(const t3_combat_event_t* events,unsigned count) {
    uint8_t fresh[2]={0,0};
    for(unsigned n=0;n<count;++n) {
        const t3_combat_event_t* e=&events[n];
        if(e->kind==T3_EVENT_SWING)continue;
        const unsigned i=e->fighter;
        /* The attacker's hitting segment end, in PS1 world units (as the camera reads it). */
        g_spark[i].age=1u;g_spark[i].block=e->kind==T3_EVENT_BLOCK;
        for(unsigned axis=0;axis<3u;++axis)g_spark[i].at[axis]=e->at[axis];
        fresh[i]=1u;
    }
    for(unsigned i=0;i<2u;++i)
        if(!fresh[i] && g_spark[i].age && ++g_spark[i].age>SPARK_IMAGES)g_spark[i].age=0u;
}
/* Drawn with the HUD, projected through the image's camera: scene faces in front
 * of the fighters cost about 2.5 images/s however small they were. */
static void __attribute__((optimize("Os"))) draw_sparks(void) {
    for(unsigned i=0;i<2u;++i) {
        if(!g_spark[i].age)continue;
        int16_t sx,sy;
        const sat_fx16_t x=(g_spark[i].at[0]-g_stage_input[0])*96;
        const sat_fx16_t z=-(g_spark[i].at[2]-g_stage_input[1])*96;
        const sat_fx16_t y=-g_spark[i].at[1]*96;
        if(sat_project_to_screen(&g_camera.view_proj,x,y,z,W,H,&sx,&sy)!=SAT_OK)continue;
        const int arm=6+g_spark[i].age*3,thick=2+g_spark[i].age/2;
        const uint16_t empty=0,color=g_spark[i].block?SAT_RGB555(14,22,31):SAT_RGB555(31,28,10);
        SAT_PANIC_IF_ERROR(sat_hud_bar(&g_hud,sx-arm,sy-thick/2,2*arm,thick,1u,1u,empty,color));
        SAT_PANIC_IF_ERROR(sat_hud_bar(&g_hud,sx-thick/2,sy-arm,thick,2*arm,1u,1u,empty,color));
    }
}
#else
static void spark_events(const t3_combat_event_t* events,unsigned count) { (void)events;(void)count; }
static void draw_sparks(void) {}
#endif

#if defined(T3_VDP2_FLOOR)
#if defined(T3_PSX_CAMERA) && !defined(T3_NATIVE_COMMANDS)
/* Drop shadows. The original flattens each fighter's PSSDW mesh onto the floor
 * with the stage light's shear (FighterBuildShadowPrims, opaque POLY_F3/F4, the
 * stage record's shadow colour); here an octagon of two quads sits under the
 * root at floor height instead, in the same colour (stage 0: 54, 24, 16).
 * World units match t3_move_probe_camera: PS1 units * 96. */
static void __attribute__((optimize("Os"))) drop_shadows(void) {
    const int32_t R = 420 * 96, S = R * 8 / 10;
    static const int8_t ring[2][4][2] = {{{-1,-1},{1,-1},{1,1},{-1,1}},{{0,-1},{1,0},{0,1},{-1,0}}};
    sat_scene3d_material_t m = {0};
    m.kind = SAT_SCENE3D_RGB;m.rgb555 = SAT_RGB555(54 >> 3, 24 >> 3, 16 >> 3);
    m.color_calc_slot = SAT_INDEXED_SOLID_OPAQUE;
    for(unsigned k = 0; k < 4u; ++k) {
        const unsigned i = k >> 1, shape = k & 1u, r = shape ? R : S;
        const int32_t x = (t3_move_probe_owned_value(i,1u) - t3_move_probe_world_center(0)) * 96;
        const int32_t z = -(t3_move_probe_owned_value(i,3u) - t3_move_probe_world_center(2)) * 96;
        sat_quad3_t q;
        for(unsigned v = 0; v < 4u; ++v)
            q.v[v] = (sat_vec3_t){x + ring[shape][v][0] * r, 0, z + ring[shape][v][1] * r};
        const sat_result_t st = sat_scene_submit_quad(&g_scene, &q, &m, 0);
        if(st != SAT_OK)SAT_PANIC_IF_ERROR(st);
    }
}
#endif
/* The inputs are read before the Slave task can start ticking; the tables
 * are built after the actors, while the Slave runs those ticks. */
static int32_t g_stage_input[3];
static void floor_tiles(void) {
#if defined(T3_VDP1_FLOOR_PROBE)
    floor_capture_image();
#endif
#if defined(T3_PSX_CAMERA)
    g_stage_input[2]=t3_move_probe_backdrop_turn();
#else
    g_stage_input[2]=0;
#endif
    g_stage_input[0]=t3_move_probe_world_center(0);
    g_stage_input[1]=t3_move_probe_world_center(2);
#if defined(T3_PSX_CAMERA) && !defined(T3_NATIVE_COMMANDS)
    drop_shadows();
#endif
}
/* Set when the Slave's task may build this image's tables after its ticks.
 * Then whichever CPU gets there first claims them: the Master once it has
 * flushed the scene and drawn the HUD, the Slave once its ticks are done. */
static uint8_t g_stage_on_worker,g_stage_by_master;
static volatile uint8_t g_stage_claim;
static sat_result_t stage_build(void) {
    return t3_stage_vdp2_prepare(&g_camera,g_stage_input[0],g_stage_input[1],g_stage_input[2]);
}
static void stage_claim_reset(void) {
    *(volatile uint8_t*)sat_parallel_uncached_address((const void*)&g_stage_claim)=0;
}
/* TAS.B is one locked read-modify-write on the bus: true for the first CPU. */
static int stage_claim(void) {
    volatile uint8_t* flag=sat_parallel_uncached_address((const void*)&g_stage_claim);
    int first;
    __asm__ volatile("tas.b @%1\n\tmovt %0":"=r"(first):"r"(flag):"t","memory");
    return first;
}
static void stage_prepare(void) {
    if(!g_stage_on_worker)SAT_PANIC_IF_ERROR(stage_build());
}
static sat_result_t stage_worker(void) {
    if(!stage_claim())return SAT_OK;
    SLAVE_FRT_START(started);
    const sat_result_t st=stage_build();
    SLAVE_FRT_ADD(2,started);
    SLAVE_FRT_COUNT(3);
    return st;
}
static void stage_before_join(void) {
#if defined(T3_STAGE_FORCE_SLAVE_PROBE)
    g_stage_by_master=0;
#else
    g_stage_by_master=stage_claim();
    if(g_stage_by_master){SAT_PANIC_IF_ERROR(stage_build());SLAVE_FRT_COUNT(4);}
#endif
}
static sat_result_t stage_sync(void) {return g_stage_by_master?SAT_OK:t3_stage_vdp2_sync();}
#else
static void stage_prepare(void) {}
static uint8_t g_stage_on_worker;
static void stage_claim_reset(void) {}
static void stage_before_join(void) {}
static sat_result_t stage_worker(void) {return SAT_OK;}
static sat_result_t stage_sync(void) {return SAT_OK;}
static void floor_tiles(void) {
    int x, z;
#if defined(T3_NATIVE_COMMANDS)
    g_stage_face=g_stage_cursor=0;g_stage_ready=NULL;g_stage_ready_count=0;
    const sat_result_t cached=sat_view_cache_view_camera(&g_stage_cache,0,&g_camera,Q(25),W,H,
        &g_stage_ready,&g_stage_ready_count);
    g_stage_baking=cached==SAT_ERR_NOT_FOUND;
    if(g_stage_baking)SAT_PANIC_IF_ERROR(sat_view_cache_begin_camera(&g_stage_cache,0,&g_camera,Q(25),W,H));
    else SAT_PANIC_IF_ERROR(cached);
#endif
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
#if defined(T3_NATIVE_COMMANDS)
    if(g_stage_baking)SAT_PANIC_IF_ERROR(sat_view_cache_finish(&g_stage_cache));
    g_stage_baking=0;
#endif
}

#endif

/* Arcade rule: with no pad in port 2 the original CPU opponent plays P2
 * (difficulty 1, level 4: the defaults the original's options start from).
 * Recorded and reference replays always drive both fighters themselves. */
static uint8_t g_arcade_stage;

/* Arcade sequencing: each match the player wins against the CPU raises the
 * CPU level (4..7); a loss or a draw repeats the stage. Only two fighters
 * exist, so the opponent is always Law. */
static void arcade_advance(int winner) {
    if(winner==0 && g_arcade_stage<3u)++g_arcade_stage;
}

static void choose_cpu(const sat_pad_state_t* pad1) {
#if defined(T3_REPLAY_INPUT_TAPE) || defined(T3_REFERENCE_REPLAY)
    (void)pad1;t3_move_probe_set_cpu(0u,1u,4u);
#else
    if(pad1->connected)g_arcade_stage=0u;
    t3_move_probe_set_cpu(pad1->connected?0u:2u,1u,(uint8_t)(4u+g_arcade_stage));
#endif
}

#if !defined(T3_REFERENCE_REPLAY)
static uint16_t original_start_buttons(const sat_pad_state_t* pad) {
    uint16_t out=0;
    if(pad->held&SAT_PAD_X)out|=0x80u;
    if(pad->held&SAT_PAD_Y)out|=0x10u;
    if(pad->held&SAT_PAD_A)out|=0x40u;
    if(pad->held&SAT_PAD_B)out|=0x20u;
    if(pad->held&SAT_PAD_L)out|=0x04u;
    return out;
}
#endif

#if defined(T3_TITLE_SCREEN)
/* Title: waits for START on pad 1, then re-reads port 2 (a pad means VS, an
 * empty port means ARCADE against the CPU). */
static void title_screen(void) {
    for(unsigned blink=0;;++blink) {
        sat_pad_state_t pad0={0},pad1={0};
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
        SAT_PANIC_IF_ERROR(sat_set_clear_color(FRAME_BACKGROUND));
        SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(FRAME_BACKGROUND));
        SAT_PANIC_IF_ERROR(sat_input_poll());
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(0,0,&pad0));
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(1,0,&pad1));
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"TEKKEN 3",W/2,70));
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"PAUL  VS  LAW",W/2,96));
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,
            pad1.connected?"2 PLAYER VS":"1 PLAYER ARCADE",W/2,128));
        if(blink&32u)SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"PRESS START",W/2,160));
        SAT_PANIC_IF_ERROR(sat_end_frame());
        if(pad0.pressed&SAT_PAD_START) {
            choose_cpu(&pad1);
#if !defined(T3_REFERENCE_REPLAY)
            t3_move_probe_set_start_buttons(original_start_buttons(&pad0),original_start_buttons(&pad1));
#endif
            t3_move_probe_reset(g_game.round);
            return;
        }
    }
}
#endif

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

#if defined(T3_SLAVE_SIMULATION)
extern sat_result_t t3_simulation_task_entry(const void*,uint32_t,void*,uint32_t,uint32_t*);
sat_result_t t3_simulation_task_body(const void* input,uint32_t bytes,void* output,
        uint32_t capacity,uint32_t* written) {
    if(!input || bytes!=sizeof(simulation_job_t) || !output ||
       capacity<sizeof(t3_match_t) || !written)return SAT_ERR_INVALID_ARG;
    const simulation_job_t* job=input;
    t3_match_t* view=output; /* Executor supplies an uncached Slave alias. */
    if(!job->count || job->first+job->count>T3_MAX_RECOVERY_FIELDS)return SAT_ERR_INVALID_ARG;
    if(g_slave_simulation_epoch!=job->epoch) {
        const sat_result_t st=t3_move_probe_simulation_sync(1);
        if(st!=SAT_OK)return st;
        g_slave_simulation_epoch=job->epoch;
    }
    SLAVE_FRT_START(ticks_started);
    for(uint32_t i=0;i<job->count;++i) {
        t3_input_t p1=controller(&job->pads[0],view->fighters[0].facing);
        t3_input_t p2=controller(&job->pads[1],view->fighters[1].facing);
        if(job->first+i) {p1.pressed=0;p2.pressed=0;}
#if defined(T3_REPLAY_INPUT_TAPE)
        t3_input_tape_step(view->total_ticks,&p1,&p2);
#endif
        const sat_result_t st=t3_move_probe_owned_step(p1,p2,view);
        if(st!=SAT_OK)return st;
        t3_move_probe_tick(view);
        t3_combat_events_tick(view->total_ticks);
    }
    SLAVE_FRT_ADD(0,ticks_started);
    g_t3_slave_sim_ticks=view->total_ticks;
    ++g_t3_slave_sim_jobs;
    *written=sizeof(*view);
    return SAT_OK;
}

static sat_result_t simulation_submit(const sat_pad_state_t pads[2],uint32_t first,
        uint32_t count,sat_parallel_handle_t* handle) {
    g_simulation_job=(simulation_job_t){{pads[0],pads[1]},first,count,g_simulation_epoch};
    return sat_parallel_submit(SIMULATION_TASK,&g_simulation_job,sizeof(g_simulation_job),
        &g_game,sizeof(g_game),handle);
}

#if defined(T3_SLAVE_SIMULATION_POLL_TICKS)
static sat_result_t simulation_wait(sat_parallel_handle_t handle) {
    const uint16_t begin=sat_time_frc();
    uint16_t checked=begin;
    uint32_t spins=0;
    for(;;) {
        /* A short wait services the same owned task and propagates executor
         * errors. A timeout retains ownership; it never reruns the task. */
        const sat_result_t st=sat_parallel_wait(handle,1u);
        if(st!=SAT_ERR_TIMEOUT)return st;
        uint16_t now=sat_time_frc();
        while((uint16_t)(now-checked)<T3_SLAVE_SIMULATION_POLL_TICKS) {
            if((uint16_t)(now-begin)>=SIMULATION_TIMEOUT || ++spins>=2000000u)
                return SAT_ERR_TIMEOUT;
            now=sat_time_frc();
        }
        checked=now;
        if((uint16_t)(now-begin)>=SIMULATION_TIMEOUT)return SAT_ERR_TIMEOUT;
    }
}
#endif

static sat_result_t simulation_join(sat_parallel_handle_t handle,uint32_t count) {
    const uint32_t started=sat_time_ms();
#if defined(T3_SLAVE_SIMULATION_POLL_TICKS)
    sat_result_t st=simulation_wait(handle);
#else
    sat_result_t st=sat_parallel_wait(handle,SIMULATION_TIMEOUT);
#endif
    g_t3_slave_sim_wait_ms=sat_time_ms()-started;
    if(g_t3_slave_sim_wait_ms>g_t3_slave_sim_max_wait_ms)
        g_t3_slave_sim_max_wait_ms=g_t3_slave_sim_wait_ms;
    if(st==SAT_ERR_TIMEOUT) {
        /* Authoritative work may be partial. Stop it; never redo its ticks. */
        (void)sat_parallel_abort(handle,SIMULATION_TIMEOUT);
        return st;
    }
    if(st!=SAT_OK)return st;
    uint32_t written=0;
    if((st=sat_parallel_result(handle,&written))!=SAT_OK)return st;
    if(written!=sizeof(g_game))return SAT_ERR_VERIFY_FAILED;
    if((st=sat_parallel_release(handle))!=SAT_OK)return st;
    if((st=t3_move_probe_simulation_sync(0))!=SAT_OK)return st;
    /* The single-clip bridge has no time-varying generic clip selection.
     * Advance its exact tick count locally; Slave never mutates actor storage. */
    for(uint32_t i=0;i<count;++i)
        if((st=t3_characters_tick(&g_game))!=SAT_OK)return st;
    return SAT_OK;
}

static void capture_image_match(void) {
    g_image_match=g_game;
#if defined(T3_PARTICLE_IMAGE_SNAPSHOT)
    SAT_PANIC_IF_ERROR(t3_move_probe_capture_particles());
#endif
#if defined(T3_ORIGINAL_HUD) && defined(T3_ENABLE_PSX_OWNED_MATCH)
    t3_move_probe_hud_bars(g_image_hud_bars);
#endif
    g_image_round[0]=t3_move_probe_round_value(0);
    g_image_round[1]=t3_move_probe_round_value(1);
    g_image_round[2]=t3_move_probe_round_value(4);
    g_image_round[3]=t3_move_probe_round_value(11);
    g_image_round[4]=t3_move_probe_round_value(10);
    g_image_round[5]=t3_move_probe_round_value(12);
#if defined(T3_ORIGINAL_HUD)
    g_image_round[6]=t3_move_probe_round_value(13);
    g_image_round[7]=t3_move_probe_round_value(14);
    g_image_round[8]=t3_move_probe_round_value(15);
    g_image_round[9]=t3_move_probe_round_value(16);
    g_image_round[10]=t3_move_probe_round_value(17);
    g_image_round[11]=t3_move_probe_round_value(18);
#endif
    g_t3_slave_sim_pose_tick=g_game.total_ticks;
}

#if defined(T3_FUSED_ACTOR_PREPARATION)
/* The deferred ticks follow the Slave's actor in one task, so they start as
 * soon as that actor is prepared instead of after the Master's actor, the
 * merge and another dispatch. The image state is captured just before. */
enum { PREPARE_SIMULATION_TASK=0x102u };
static simulation_job_t g_prepare_job;
static sat_parallel_handle_t g_prepare_handle;

static sat_result_t prepare_simulation_entry(const void* input,uint32_t bytes,void* output,
        uint32_t capacity,uint32_t* written) {
    sat_parallel_purge_cache(); /* the worker reads the Master's cached data */
    SLAVE_FRT_START(prep_started);
    t3_characters_prepare_worker();
    SLAVE_FRT_ADD(1,prep_started);
    const sat_result_t st=t3_simulation_task_entry(input,bytes,output,capacity,written);
    /* The Master is still drawing; its camera and stage inputs are final. */
    return st==SAT_OK && g_stage_on_worker?stage_worker():st;
}

static sat_result_t start_prepare_simulation(void) {
    capture_image_match();
    g_stage_on_worker=T3_PERF_MODE!=1;
    stage_claim_reset();
    g_simulation_job=g_prepare_job;
    return sat_parallel_submit(PREPARE_SIMULATION_TASK,&g_simulation_job,
        sizeof(g_simulation_job),&g_game,sizeof(g_game),&g_prepare_handle);
}
#endif
#endif

/* The Saturn HUD maps the original mode-0 2D rectangle (0,20,368,448)
 * into 320x224: x' = x*20/23, y' = (y-20)/2. The 3D projection's centre
 * at x=192 does not add a crop offset to these 2D coordinates. The
 * original-HUD build keeps FUN_8004E55C's lengths in 152-pixel units at 60 Hz
 * and only converts them here for the VDP1 sprite width. */
enum { BAR_X0 = 9, BAR_X1 = 176, BAR_WIDTH = 136, BAR_FILL = 132,
       BAR_Y = 10, BAR_HEIGHT = 12 };
#if !defined(T3_ORIGINAL_HUD_BARS) || !defined(T3_ENABLE_PSX_OWNED_MATCH)
static int g_recent_damage[2];
static void health_bar(int x, int inner_right, int health, int* recent) {
    int length = (health * BAR_WIDTH + T3_MAX_HEALTH - 1) / T3_MAX_HEALTH;
    if (length < 0) length = 0;
    if (length > BAR_WIDTH) length = BAR_WIDTH;
    if (*recent < length) *recent = length;
    else if (*recent > length) *recent -= (*recent - length + 7) / 8;
#if defined(T3_ORIGINAL_HUD_BARS)
    SAT_PANIC_IF_ERROR(t3_original_hud_bar(x,BAR_Y,BAR_WIDTH,length,*recent,inner_right));
#else
    const uint16_t empty = SAT_RGB555(13,4,4);
    SAT_PANIC_IF_ERROR(sat_hud_bar(&g_hud, x, BAR_Y, BAR_WIDTH, BAR_HEIGHT, 0, 1u, empty, empty));
    if (*recent > 0)
        SAT_PANIC_IF_ERROR(sat_hud_bar(&g_hud, inner_right ? x + BAR_WIDTH - *recent : x,
            BAR_Y, *recent, BAR_HEIGHT, 1u, 1u, empty, SAT_RGB555(28,22,4)));
    if (length > 0)
        SAT_PANIC_IF_ERROR(sat_hud_bar(&g_hud, inner_right ? x + BAR_WIDTH - length : x,
            BAR_Y, length, BAR_HEIGHT, 1u, 1u, empty, SAT_RGB555(6,27,12)));
#endif
}
#endif

static void draw_hud(void) {
#if defined(T3_SLAVE_SIMULATION)
    const t3_match_t* game=&g_image_match;
#else
    const t3_match_t* game=&g_game;
#endif
#if defined(T3_RENDER_BENCH_TICK) && !defined(T3_RENDER_BENCH_HIDE_STATUS)
#if defined(T3_STAGE_HUD)
    enum { BENCH_STATUS_Y=183 };
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud,"RENDER BENCHMARK",80,BENCH_STATUS_Y));
#else
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud,"RENDER BENCHMARK",80,65));
#endif
#endif
#if !defined(T3_ORIGINAL_HUD)
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, "P1", 8, 5));
    SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, t3_move_probe_cpu_mask()&2u?"CPU":"P2", 288, 5));
#endif
#if defined(T3_ORIGINAL_HUD_BARS) && defined(T3_ENABLE_PSX_OWNED_MATCH)
#if defined(T3_SLAVE_SIMULATION)
    const t3_psx_hud_bar_t* bars=g_image_hud_bars;
#else
    t3_psx_hud_bar_t current_bars[2];
    t3_move_probe_hud_bars(current_bars);
    const t3_psx_hud_bar_t* bars=current_bars;
#endif
    SAT_PANIC_IF_ERROR(t3_original_hud_bar(BAR_X0,BAR_Y,BAR_WIDTH,
        bars[0].drawn*BAR_FILL/152,bars[0].recent*BAR_FILL/152,1));
    SAT_PANIC_IF_ERROR(t3_original_hud_bar(BAR_X1,BAR_Y,BAR_WIDTH,
        bars[1].drawn*BAR_FILL/152,bars[1].recent*BAR_FILL/152,0));
#else
    health_bar(BAR_X0,1,game->fighters[0].health,&g_recent_damage[0]);
    health_bar(BAR_X1,0,game->fighters[1].health,&g_recent_damage[1]);
#endif
    draw_sparks();
#if !defined(T3_ORIGINAL_HUD)
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "ROUND ", game->round, 8, 35));
#endif
#if defined(T3_ORIGINAL_HUD)
    /* FUN_8004E410/FUN_8004E87C: digits at original x 184/169, y 22; marks
     * from x 151/202 at y 70, 14 apart. FUN_8004EB9C: names at 14 and
     * 354-width, y 66, on plates from four pixels outside the name, rows
     * y+9..y+19. */
    SAT_PANIC_IF_ERROR(t3_original_hud_timer((game->timer + 59u) / 60u,t3_original_hud_x(169),1));
    for(unsigned mark=0;mark<2;++mark) {
        SAT_PANIC_IF_ERROR(t3_original_hud_mark(0,game->wins[0]>mark,t3_original_hud_x(151-14*(int)mark),25));
        SAT_PANIC_IF_ERROR(t3_original_hud_mark(1,game->wins[1]>mark,t3_original_hud_x(202+14*(int)mark),25));
    }
    SAT_PANIC_IF_ERROR(t3_original_hud_plate(t3_original_hud_x(10),27,35));
    SAT_PANIC_IF_ERROR(t3_original_hud_name(0,t3_original_hud_x(14),23,28));
    SAT_PANIC_IF_ERROR(t3_original_hud_plate(t3_original_hud_x(324),27,30));
    SAT_PANIC_IF_ERROR(t3_original_hud_name(1,t3_original_hud_x(322),23,28));
#else
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "TIME ", (game->timer + 59u) / 60u, 125, 35));
#endif
#if !defined(T3_ORIGINAL_HUD)
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "WINS ", game->wins[0], 8, 47));
#endif
    /* One-based render submission serial, independent of logic ticks and
     * VBlanks. Keep it visible even when the optional profiler is hidden. */
#if !defined(T3_RENDER_BENCH_HIDE_STATUS)
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "FRAME ", g_render_submissions + 1u, 112, HUD_FRAME_Y));
#endif
#if defined(T3_CPU_DIAGNOSTICS)
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"MCCR ",g_t3_boot_cache[0],HUD_MASTER_CACHE_X,HUD_CPU_Y));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"SCCR ",g_t3_boot_slave_verified?(int32_t)g_t3_boot_cache[1]:-1,
        HUD_SLAVE_CACHE_X,HUD_CPU_Y));
#endif
#if !defined(T3_ORIGINAL_HUD)
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "WINS ", game->wins[1], 248, 47));
#endif
#if defined(T3_REPLAY_INPUT_TAPE)
    SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"RECORDED INPUT DEMO",W/2,61));
#endif
#if defined(T3_REFERENCE_REPLAY)
    SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"PS1 REFERENCE REPLAY",W/2,61));
#endif
#if defined(T3_SLAVE_SIMULATION)
    const int replay=g_image_round[2];
    const int round_active=g_image_round[0],round_state=g_image_round[1];
    const int intro_frame=g_image_round[3],round_number=g_image_round[4],final_round=g_image_round[5];
#if defined(T3_ORIGINAL_HUD)
    const int round_result=g_image_round[6],human_mask=g_image_round[7],replay_request=g_image_round[8];
    const int frame_counter=g_image_round[9],max_rounds=g_image_round[10],replay_entry=g_image_round[11];
#endif
#else
    const int replay=t3_move_probe_round_value(4);
    const int round_active=t3_move_probe_round_value(0),round_state=t3_move_probe_round_value(1);
    const int intro_frame=t3_move_probe_round_value(11),round_number=t3_move_probe_round_value(10);
    const int final_round=t3_move_probe_round_value(12);
#if defined(T3_ORIGINAL_HUD)
    const int round_result=t3_move_probe_round_value(13),human_mask=t3_move_probe_round_value(14);
    const int replay_request=t3_move_probe_round_value(15),frame_counter=t3_move_probe_round_value(16);
    const int max_rounds=t3_move_probe_round_value(17),replay_entry=t3_move_probe_round_value(18);
#endif
#endif
#if !defined(T3_ORIGINAL_HUD)
    if(replay>=1 && replay<=4)
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"REPLAY",W/2,85));
    else if(round_active && round_state==0) {
        /* hud.md: the round name from the intro's start, READY? from frame 106 (46 after round 1). */
        if(intro_frame<(round_number==1?106:46)) {
            if(final_round)SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"FINAL ROUND",W/2,70));
            else SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"ROUND ",(uint32_t)round_number,W/2-36,70));
        } else SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"READY?",W/2,85));
    } else if(round_active && round_state==1 && round_number==1 && intro_frame<45 && !game->round_done) {
        /* Round 1 starts in the fight itself (psx_match.c); announce it as it begins. */
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,"FIGHT!",W/2,70));
    } else if (game->match_done || game->round_done) {
        /* hud.md: K.O. / DOUBLE K.O. / TIME UP, then the winner. */
        const int down0=game->fighters[0].health<=0,down1=game->fighters[1].health<=0;
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,
            down0&&down1?"DOUBLE K.O.":down0||down1?"K.O.":"TIME UP",W/2,70));
        SAT_PANIC_IF_ERROR(sat_hud_text_centered(&g_hud,
            game->match_done?(game->winner<0?"MATCH DRAW":game->winner==0?"PLAYER 1 WINS!":"PLAYER 2 WINS!"):
            (game->winner<0?"DRAW":game->winner==0?"PLAYER 1 ROUND":"PLAYER 2 ROUND"),W/2,90));
    }
#else
    (void)replay;(void)final_round;
    const t3_psx_hud_messages_t facts={.state=round_state,.frame=intro_frame,
        .played=round_number,.max_rounds=max_rounds,.result=round_result,
        .replay_request=replay_request,.human_mask=human_mask,.winner=game->winner,
        .frame_counter=(uint32_t)frame_counter,.replay_mode=replay_entry,
        .winner_name=game->winner==0?"PAUL":"LAW"};
    t3_psx_hud_message_t messages[2];
    const unsigned count=t3_psx_hud_messages(&facts,messages);
    for(unsigned i=0;i<count;++i)
        SAT_PANIC_IF_ERROR(t3_original_hud_text(messages[i].text,messages[i].colour,
            messages[i].font,messages[i].x,messages[i].y));
#endif
    /* The original has no control hints; each glyph is a VDP1 command the
     * Master writes, so show them only while a round is introduced. */
    if(!round_active || round_state==0) {
#if !defined(T3_ORIGINAL_HUD)
        SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, t3_move_probe_combat()?"X HIGH  DF+X MID  D+A LOW":t3_move_probe_owned()?
            "X/Y PUNCH  A/B KICK":"X/Y PUNCH  A/B KICK  L GUARD", 40, 196));
        SAT_PANIC_IF_ERROR(sat_hud_text(&g_hud, t3_move_probe_owned()?
            "D-PAD MOVE/GUARD START RESET":"D-PAD MOVE/STEP  START RESET", 40, 208));
#endif
    }
}

static void prepare_scene(void) {
    T3_PERF_START(pose_publication_started);
    t3_move_probe_present();
    T3_PERF_ADD(placement,pose_publication_started);
    T3_PERF_START(camera_started);
#if defined(T3_PSX_CAMERA)
    /* The original fight camera, view_proj included. */
    SAT_PANIC_IF_ERROR(t3_move_probe_camera(&g_camera));
#else
    sat_fx16_t center = from_q8((g_game.fighters[0].x + g_game.fighters[1].x) / 2);
#if defined(T3_CLOSE_CAMERA)
    sat_fx16_t separation=from_q8(g_game.fighters[0].x-g_game.fighters[1].x);
    if(separation<0)separation=-separation;
    /* FOV 45 and a world-space margin leave room for limbs at both edges.
     * Change camera distance, never the authored model or contact scale. */
    sat_fx16_t distance=separation+Q(260);
    if(distance<Q(560))distance=Q(560);
    /* A fallen fighter extends across the floor towards the camera. Leave
     * room for that pose and the winner without putting the body under HUD. */
    if(g_game.round_done && distance<FX(7))distance=FX(7);
    g_camera.eye=vec(center,Q(200),-distance);
    g_camera.target=vec(center,g_game.round_done?Q(120):Q(150),0);
#else
    g_camera.eye = vec(center, Q(390), -FX(9));
    g_camera.target = vec(center, FX(1), 0);
#endif
    SAT_PANIC_IF_ERROR(sat_camera3d_update(&g_camera));
#endif
#if defined(T3_PARTICLE_GATED_PROJECTION) && defined(T3_STAGE_ORIGINAL_VERTICAL_PROBE)
    /* Images without an effect draw the fighters through the SDK, images with
     * one through the original projection, and the stage is mapped to the
     * original's vertical viewport (rows 20..468 of the picture onto the 224
     * rows, picture centre on row 110). The SDK fighters get the same mapping
     * here, in clip space: y' = 15/14 y + w/56. The stage keeps reading the
     * unadjusted g_camera and applies its own correction. */
    static sat_camera3d_t scene_camera;
    scene_camera=g_camera;
    for(unsigned j=0;j<4;++j) {
        scene_camera.view_proj.m[4+j]=(sat_fx16_t)((int64_t)g_camera.view_proj.m[4+j]*15/14+
            (int64_t)T3_VIEWPORT_SIGN*g_camera.view_proj.m[12+j]/112);
        /* The original's GTE centre is x=167, not 160. */
        scene_camera.view_proj.m[j]=(sat_fx16_t)((int64_t)g_camera.view_proj.m[j]+
            (int64_t)T3_VIEWPORT_DX*g_camera.view_proj.m[12+j]/160);
    }
    SAT_PANIC_IF_ERROR(sat_scene_begin(&g_scene, &scene_camera, Q(25), W, H, HUD_COMMANDS));
#else
    SAT_PANIC_IF_ERROR(sat_scene_begin(&g_scene, &g_camera, Q(25), W, H, HUD_COMMANDS));
#endif
#if defined(T3_VDP1_FLOOR_CAPACITY_SELFTEST)
    SAT_PANIC_IF_ERROR(floor_capacity_selftest());
#endif
#if defined(T3_NATIVE_COMMANDS)
    SAT_PANIC_IF_ERROR(sat_vdp1_command_batch_reserve(FACE_CAP,&g_native_commands));
    SAT_PANIC_IF_ERROR(sat_scene3d_faces_bind_native_commands(&g_scene.faces,&g_native_commands));
#endif
    T3_PERF_ADD(camera,camera_started);
    if(T3_PERF_MODE!=1) {
        T3_PERF_START(stage_started);
        floor_tiles();
        T3_PERF_ADD(stage,stage_started);
        SAT_PANIC_IF_ERROR(t3_characters_render(&g_scene, &g_game));
#if defined(T3_VDP1_FLOOR_PROBE)
        /* Actor/shadow geometry is queued but not emitted. Budget its SDK
         * clip/tiled worst case; HUD slots are already reserved. */
        sat_vdp1_command_stats_t command_stats;
        SAT_PANIC_IF_ERROR(sat_vdp1_command_stats(&command_stats));
        uint32_t protected_faces=t3_floor_protected_commands(&g_scene.faces);
        uint32_t occupied=(uint32_t)command_stats.used+command_stats.overlay_reserved+1u;
        uint32_t available=command_stats.capacity>occupied?command_stats.capacity-occupied:0u;
        uint32_t floor_budget=available>protected_faces?available-protected_faces:0u;
#if defined(T3_VDP1_FLOOR_FORCE_BUDGET)
        if(floor_budget>T3_VDP1_FLOOR_FORCE_BUDGET)floor_budget=T3_VDP1_FLOOR_FORCE_BUDGET;
#endif
        g_t3_floor_budget_stats[0]=protected_faces;g_t3_floor_budget_stats[1]=floor_budget;
        if(protected_faces>g_t3_floor_budget_stats[4])g_t3_floor_budget_stats[4]=protected_faces;
        draw_floor_probe(floor_budget);
#endif
        T3_PERF_START(stage_late_started);
        stage_prepare();
        T3_PERF_ADD(stage,stage_late_started);
    }
}

/* The most cold (clipping-fallback) scene records one frame has used:
 * clipped faces and emitted fallbacks, for sizing the cold store. */
volatile uint16_t g_t3_cold_peak[2] __attribute__((section(".wram_l")));

static void flush_scene(void) {
    T3_PERF_START(flush_started);
#if defined(T3_TRUSTED_SCENE_FLUSH)
    /* Both actors' textures and materials are uploaded once and the binding
     * is immutable after t3_characters_init, so the flush census revalidated
     * residency that cannot change: one 24-byte record per face every image.
     * The stage and shadows submit resident RGB faces, and the HUD is drawn
     * outside the scene. */
    SAT_PANIC_IF_ERROR(sat_scene_flush_trusted(&g_scene));
#else
    SAT_PANIC_IF_ERROR(sat_scene_flush(&g_scene));
#endif
    if(g_scene.faces.clipped_faces>g_t3_cold_peak[0])g_t3_cold_peak[0]=g_scene.faces.clipped_faces;
    if(g_scene.faces.fallback_faces>g_t3_cold_peak[1])g_t3_cold_peak[1]=g_scene.faces.fallback_faces;
#if defined(T3_NATIVE_WAIT_PROBE)
    sat_scene3d_profile_t scene_profile={0};
    if(sat_scene3d_profile_get(&scene_profile)==SAT_OK) {
        g_t3_native_painter_ticks=scene_profile.painter_ticks;
        g_t3_native_emit_ticks=scene_profile.emission_ticks;
        g_t3_painter_sum+=scene_profile.painter_ticks;
        g_t3_emit_sum+=scene_profile.emission_ticks;
        ++g_t3_flush_samples;
    }
#endif
#if defined(T3_NATIVE_COMMANDS)
    ++g_t3_native_command_frames;
    g_t3_cached_stage_faces=g_scene.replayed_items;
#endif
    T3_PERF_ADD(flush,flush_started);
}

#if defined(T3_OVERLAY_RESERVATION_SELFTEST)
/* One diagnostic frame deliberately fills the remaining world quota. These
 * single-dot commands are submitted once; following frames use normal HUD. */
volatile uint32_t g_t3_overlay_reservation_selftest[5];
static sat_result_t overlay_reservation_selftest(void) {
    sat_vdp1_command_stats_t before,after;
    sat_result_t st=sat_vdp1_command_stats(&before);
    if(st!=SAT_OK)return st;
    if(before.overlay_pass || !before.overlay_reserved)return SAT_ERR_VERSION;
    unsigned filled=0;
    while((st=sat_draw_rect_screen(0,0,1,1,0x8000u))==SAT_OK)++filled;
    if(st!=SAT_ERR_CAPACITY)return st;
    if((st=sat_vdp1_command_stats(&after))!=SAT_OK)return st;
    if(after.used!=before.used+filled || after.overlay_reserved!=before.overlay_reserved)return SAT_ERR_VERSION;
    if((st=sat_vdp1_overlay_begin())!=SAT_OK)return st;
    if((st=sat_draw_rect_screen(0,0,1,1,0x8000u))!=SAT_OK)return st;
    if((st=sat_vdp1_command_stats(&after))!=SAT_OK)return st;
    if(!after.overlay_pass || after.used!=before.used+filled+1u)return SAT_ERR_VERSION;
    g_t3_overlay_reservation_selftest[1]=filled;
    g_t3_overlay_reservation_selftest[2]=before.overlay_reserved;
    g_t3_overlay_reservation_selftest[3]=after.used;
    g_t3_overlay_reservation_selftest[4]=after.capacity;
    g_t3_overlay_reservation_selftest[0]=1;
    return SAT_OK;
}
#endif


static void draw_profiler(void) {
    /* Last completed diagnostic snapshot, never partial current-frame data. */
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "LOOP x10 ", g_rate_x10, 4, 66));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "VDP1 CMD ", g_t3_diagnostics.commands_used, 4, 78));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "SCENE FACES ", g_t3_diagnostics.faces_flushed, 4, 90));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "FRAME MS ", g_t3_diagnostics.frame_work_ms, 4, 102));
#if defined(T3_NATIVE_WAIT_PROBE)
    {
        static const char* const names[6]={"PRE ","PREP ","FLSH ","HUD ","JOIN ","POST "};
        for(unsigned k=0;k<2;++k) {
            const uint32_t n=g_t3_by_ticks_n[k]?g_t3_by_ticks_n[k]:1u;
            SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,k?"N3 ":"N2 ",g_t3_by_ticks_n[k],k?248:176,66));
            for(unsigned j=0;j<6;++j)
                SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,names[j],g_t3_by_ticks[k][j]/n,
                    k?248:176,78+12*j));
        }
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"H<7000 ",g_t3_hist[0],176,150));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"H<7465 ",g_t3_hist[1],176,162));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"H<8500 ",g_t3_hist[2],176,174));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"H>8500 ",g_t3_hist[3],176,186));
    }
#endif
#if defined(T3_NATIVE_PHASE_PROBE)
    const t3_character_phase_profile_t* phase=t3_characters_phase_profile();
    if(phase) {
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P1 SKIN ",phase->skin[0],176,66));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P1 PLACE ",phase->placement[0],176,78));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P1 GEOM ",phase->geometry[0],176,90));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P2 SKIN ",phase->skin[1],176,102));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P2 PLACE ",phase->placement[1],176,114));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P2 GEOM ",phase->geometry[1],176,126));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"SETUP ",phase->setup,176,138));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"DISPATCH ",phase->dispatch,176,150));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"MERGE ",phase->merge,176,162));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"FALLBACK ",g_scene.faces.fallback_faces,176,174));
#if defined(SAT_SCENE3D_INSTANCE_PROFILE_AVAILABLE)
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P1 PROJ ",phase->projection[0],4,138));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P1 FACE ",phase->face_prep[0],4,150));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P2 PROJ ",phase->projection[1],4,162));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"P2 FACE ",phase->face_prep[1],4,174));
#endif
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"CLIPPED ",g_scene.faces.clipped_faces,4,186));
    }
#endif
    if (t3_move_probe_active()) {
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "PSX SLOT 1", t3_move_probe_slot(0), 4, 114));
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud, "PSX SLOT 2", t3_move_probe_slot(1), 4, 126));
    }
#if defined(T3_STAGE_HUD)
    enum { COLUMN_X=176,FIRST_Y=66,ROW_HEIGHT=12 };
    t3_perf_sample_t completed={0};
    if(g_t3_perf.count) {
        const uint32_t at=(g_t3_perf.cursor+T3_PERF_CAPACITY-1u)%T3_PERF_CAPACITY;
        completed=g_t3_perf.samples[at];
    }
    const char* const labels[]={"SIM ","SKEL ","SKIN ","PLACE ","GEOM ","FLUSH ","PRES ","WORK ","TICKS "};
    const uint32_t values[]={completed.simulation,completed.skeleton,completed.skin,
        completed.placement,completed.geometry,completed.flush,completed.present,completed.work,completed.ticks};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)
        SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,labels[i],values[i],COLUMN_X,FIRST_Y+ROW_HEIGHT*i));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"CAM ",completed.camera,4,FIRST_Y+ROW_HEIGHT*6));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"FLOOR ",completed.stage,4,FIRST_Y+ROW_HEIGHT*7));
    SAT_PANIC_IF_ERROR(sat_hud_value(&g_hud,"DIAG ",completed.diagnostics,4,FIRST_Y+ROW_HEIGHT*8));
#endif
}

static void publish_diagnostics(uint32_t work_started_ms, uint32_t tick_count,
                                const sat_pad_state_t pads[2]) {
    sat_scene_stats_t scene = {0};
    sat_vdp1_command_stats_t commands = {0};
    sat_vdp1_frame_stats_t pacing = {0};
    uint32_t now = display_fields();
    uint32_t span = now - g_rate_sample_vblank;
    ++g_t3_diagnostics.snapshot_epoch;
    g_t3_diagnostics.elapsed_fields_last=g_tick_clock.elapsed_last;
    g_t3_diagnostics.observed_fields=g_tick_clock.observed_fields;
    g_t3_diagnostics.discarded_fields=g_tick_clock.discarded_fields;
    g_t3_diagnostics.scheduled_ticks=g_tick_clock.scheduled_ticks;
    uint32_t pose_cache[4];
    t3_move_probe_pose_cache_stats(pose_cache);
    for(unsigned i=0;i<4;++i)g_t3_diagnostics.local_pose_cache[i]=pose_cache[i];
    SAT_PANIC_IF_ERROR(sat_scene_stats(&g_scene, &scene));
    SAT_PANIC_IF_ERROR(sat_vdp1_command_stats(&commands));
#if defined(T3_VDP1_FLOOR_SUBDIVIDE_PROBE)
    if(commands.used>g_t3_floor_split_stats[3])g_t3_floor_split_stats[3]=commands.used;
#endif
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
        g_t3_diagnostics.psx_throw_state[fighter]=(int16_t)t3_move_probe_owned_value(fighter,31);
        g_t3_diagnostics.psx_in_throw[fighter]=(uint8_t)t3_move_probe_owned_value(fighter,32);
        g_t3_diagnostics.psx_attack_pending[fighter]=(uint8_t)t3_move_probe_owned_value(fighter,33);
    }
    g_t3_diagnostics.psx_camera_throw_player=(int16_t)t3_move_probe_camera_value(0);
    g_t3_diagnostics.psx_camera_thrower=(int16_t)t3_move_probe_camera_value(1);
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

#if defined(T3_COLD_LOW_WRAM)
extern uint8_t __t3_sdk_hot_start[],__t3_sdk_hot_end[];
#endif

int main(void) {
#if defined(T3_COLD_LOW_WRAM)
    /* hot_wram.ld moves SDK runtime state out of crt0's cleared ranges. */
    for(uint8_t* byte=__t3_sdk_hot_start;byte<__t3_sdk_hot_end;++byte)*byte=0;
#endif
#if defined(T3_CPU_DIAGNOSTICS)
    g_t3_boot_master_entry=sat_sh2_cache_control();
#endif
    sat_video_config_t video = {W, H, SAT_VIDEO_AUTO, 0};
    sat_vec3_t eye = {0, Q(390), -FX(9)};
    sat_vec3_t target = {0, FX(1), 0};
    sat_vec3_t up = {0, SAT_FX16_ONE, 0};
    uint32_t prev_frame;
    g_t3_diagnostics=(t3_diagnostics_t){
        .magic=T3_DIAGNOSTICS_MAGIC,.version=T3_DIAGNOSTICS_VERSION};
    SAT_PANIC_IF_ERROR(sat_init(&video));
#if defined(T3_DMA_COMMAND_LIST)
    sat_dma_set_command_list(1);
#endif
#if defined(T3_FRAME_ON_SUBMIT)
    SAT_PANIC_IF_ERROR(sat_vdp1_set_frame_mode(SAT_VDP1_FRAME_ON_SUBMIT));
#endif
    t3_perf_init();
    t3_sim_init();
    g_font=(sat_ascii_font_t){0};
    SAT_PANIC_IF_ERROR(sat_hud_init_default(&g_hud, &g_font));
#if defined(T3_MEMORY_PROBE)
    {
        void t3_memory_probe(void);
        t3_memory_probe();
    }
#endif
    loading_frame(BOOT_MODEL, "MODEL DATA");
    SAT_PANIC_IF_ERROR(sat_scene_init(&g_scene,g_faces,g_keys,g_order,FACE_CAP));
#if defined(T3_NATIVE_COMMANDS)
    SAT_PANIC_IF_ERROR(sat_view_cache_init(&g_stage_cache,g_stage_storage,&g_stage_count,1,STAGE_FACES));
    SAT_PANIC_IF_ERROR(sat_view_cache_bind_cameras(&g_stage_cache,&g_stage_camera,1));
#endif
#if defined(T3_PROJECTED_FACE_QUEUE)
    SAT_PANIC_IF_ERROR(sat_scene3d_faces_use_projected_storage(
        &g_scene.faces,g_projected_faces,FACE_CAP));
    g_scene.faces.cold_capacity=T3_COLD_FACES;
#if defined(T3_DIRECT_FACE_SPANS)
    SAT_PANIC_IF_ERROR(sat_scene3d_faces_use_projected_indices(
        &g_scene.faces,g_projected_indices,FACE_CAP));
#endif
#endif
    SAT_PANIC_IF_ERROR(sat_camera3d_init(&g_camera,&eye,&target,&up,
#if defined(T3_CLOSE_CAMERA)
        FX(45),
#else
        FX(60),
#endif
        (sat_fx16_t)(SAT_FX16_ONE * W / H), Q(25), FX(40)));
    SAT_PANIC_IF_ERROR(t3_characters_init(character_loading_progress, NULL));
    t3_match_reset(&g_game);
    SAT_PANIC_IF_ERROR(t3_cold_data_load());
#if defined(T3_REFERENCE_REPLAY)
    /* The reference fixture owns both pads from the very first match init. */
    choose_cpu(NULL);
#endif
    SAT_PANIC_IF_ERROR(t3_move_probe_init());
#if !defined(T3_REFERENCE_REPLAY)
    {
        sat_pad_state_t boot_pad0={0},boot_pad1={0};
        SAT_PANIC_IF_ERROR(sat_input_poll());
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(0,0,&boot_pad0));
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(1,0,&boot_pad1));
        choose_cpu(&boot_pad1);
        t3_move_probe_set_start_buttons(original_start_buttons(&boot_pad0),original_start_buttons(&boot_pad1));
    }
    /* The reference initializer already applied its single original seed. */
    t3_move_probe_reset(g_game.round);
#endif
#if defined(T3_CAPTURE_INPUT_TAPE) || defined(T3_REPLAY_INPUT_TAPE)
    t3_input_tape_reset();
#endif
    loading_frame(BOOT_POSES, "FIGHTER POSES");
    SAT_PANIC_IF_ERROR(t3_characters_reset());
    SAT_PANIC_IF_ERROR(t3_characters_tick(&g_game));
#if defined(T3_CPU_DIAGNOSTICS)
    capture_boot_cache();
#endif
#if defined(T3_TITLE_SCREEN)
    title_screen();
#endif
#if defined(T3_VDP2_FLOOR)
    loading_frame(BOOT_POSES, "PAUL STAGE");
    SAT_PANIC_IF_ERROR(t3_stage_vdp2_init());
#endif
#if defined(T3_ORIGINAL_HUD)
    SAT_PANIC_IF_ERROR(t3_original_hud_init(g_faces,sizeof g_faces));
#endif
#if defined(T3_PARTICLE_PREPARE)
    SAT_PANIC_IF_ERROR(t3_particle_prepare_init());
#endif
#if defined(T3_FLIPBOOK_BURST)
    SAT_PANIC_IF_ERROR(t3_flipbook_burst_init(g_faces,sizeof g_faces));
#endif
    /* The face store is idle until the first scene: it holds the sound file. */
    SAT_PANIC_IF_ERROR(t3_sfx_init(g_faces,sizeof g_faces));
#if defined(T3_VDP1_COVERAGE_PROBE)
    SAT_PANIC_IF_ERROR(t3_coverage_probe_init());
#endif
    loading_frame(BOOT_READY, "READY");
#if defined(T3_SLAVE_SIMULATION)
    for(unsigned stack_byte=0;stack_byte<sizeof(g_t3_simulation_stack);++stack_byte)
        g_t3_simulation_stack[stack_byte]=0xA5u;
    SAT_PANIC_IF_ERROR(sat_parallel_register_task(SIMULATION_TASK,t3_simulation_task_entry));
    SLAVE_FRT_RESET(); /* NOLOAD Low WRAM */
#if defined(T3_FUSED_ACTOR_PREPARATION)
    SAT_PANIC_IF_ERROR(sat_parallel_register_task(PREPARE_SIMULATION_TASK,prepare_simulation_entry));
#endif
#endif
    prev_frame = display_fields();
    g_rate_sample_vblank = prev_frame;
#if defined(T3_OVERLAP_VDP1)
    /* Initialize the original pose before the first gameplay image. Later
     * iterations overlap preparation; this one startup wait is not repeated. */
    SAT_PANIC_IF_ERROR(sat_wait_vblank());
#endif
#if defined(T3_NATIVE_WAIT_PROBE)
    uint16_t native_loop_started=sat_time_frc();
#endif
    for (;;) {
        sat_pad_state_t pad0 = {0}, pad1 = {0};
        uint32_t now, elapsed, hz, ticks, work_started_ms;
#if !defined(T3_SLAVE_SIMULATION)
        uint32_t i;
#endif
        /* Both pads are sampled by ONE SMPC transaction. Using the generic
         * app helper followed by a second port poll wasted one transaction. */
#if !defined(T3_OVERLAP_VDP1)
        SAT_PANIC_IF_ERROR(sat_wait_vblank());
#else
        /* Geometry/commands stay in WRAM. sat_end_frame waits for both END
         * and the preceding swap before it writes VDP1 VRAM. */
        const int overlapping_draw=sat_irq_active();
        if(!overlapping_draw)SAT_PANIC_IF_ERROR(sat_wait_vblank());
#endif
        SAT_PANIC_IF_ERROR(sat_vdp2_back_color_set(FRAME_BACKGROUND));
#if defined(T3_VDP2_FLOOR)
        SAT_PANIC_IF_ERROR(sat_vdp1_set_erase_transparent());
#else
        SAT_PANIC_IF_ERROR(sat_set_clear_color(FRAME_BACKGROUND));
#endif
        SAT_PANIC_IF_ERROR(sat_begin_frame());
        work_started_ms = sat_time_ms();
        t3_perf_begin();
        T3_PERF_START(input_started);
        SAT_PANIC_IF_ERROR(sat_input_poll());
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(0, 0, &pad0));
        SAT_PANIC_IF_ERROR(sat_pad_tap_state(1, 0, &pad1));
        T3_PERF_ADD(input,input_started);
        if (pad0.pressed & SAT_PAD_Z) g_show_profiler ^= 1u;
        if (pad0.pressed & SAT_PAD_START) {
#if defined(T3_SLAVE_SIMULATION)
            SAT_PANIC_IF_ERROR(t3_move_probe_simulation_sync(1));
            ++g_simulation_epoch;
#endif
            if(g_game.match_done)arcade_advance(g_game.winner);
            t3_combat_events_reset();
            t3_match_reset(&g_game);
            choose_cpu(&pad1);
#if !defined(T3_REFERENCE_REPLAY)
            t3_move_probe_set_start_buttons(original_start_buttons(&pad0),original_start_buttons(&pad1));
#endif
            t3_move_probe_reset(g_game.round);
#if defined(T3_CAPTURE_INPUT_TAPE) || defined(T3_REPLAY_INPUT_TAPE)
            t3_input_tape_reset();
#endif
            SAT_PANIC_IF_ERROR(t3_characters_reset());
        }
        {
            t3_combat_event_t events[32];
            unsigned count=0;
            while(count<32u && t3_combat_events_pop(&events[count]))++count;
            t3_sfx_play_events(events,count);
            t3_sfx_round_update();
            spark_events(events,count);
        }
        now = display_fields();
        elapsed = now - prev_frame;
#if defined(T3_PERF_SAMPLES)
        g_t3_perf_current.field=now;
        g_t3_perf_current.elapsed=elapsed;
#endif
        prev_frame = now;
        hz = sat_video_refresh_hz();
        ticks = t3_tick_clock_advance(&g_tick_clock,elapsed,hz,T3_HZ);
#if defined(T3_RENDER_BENCH_TICK)
        if(g_game.total_ticks>=T3_RENDER_BENCH_TICK)ticks=0;
        else if(ticks>T3_RENDER_BENCH_TICK-g_game.total_ticks)
            ticks=T3_RENDER_BENCH_TICK-g_game.total_ticks;
#endif
#if defined(T3_NATIVE_WAIT_PROBE)
        const uint16_t native_sim_started=sat_time_frc();
#endif
        T3_PERF_START(simulation_started);
#if defined(T3_SLAVE_SIMULATION)
        const sat_pad_state_t simulation_pads[2]={pad0,pad1};
        const uint32_t deferred=g_game.total_ticks==0?0u:(ticks<DEFERRED_TICKS?ticks:DEFERRED_TICKS);
        const uint32_t leading=ticks-deferred;
        sat_parallel_handle_t simulation_handle=0;
        if(leading) {
            SAT_PANIC_IF_ERROR(simulation_submit(simulation_pads,0,leading,&simulation_handle));
            SAT_PANIC_IF_ERROR(simulation_join(simulation_handle,leading));
        }
#else
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
            t3_sim_tick_begin(now,g_game.total_ticks);
            if(t3_move_probe_owned()) {
                SAT_PANIC_IF_ERROR(t3_move_probe_owned_step(p1,p2,&g_game));
            } else t3_match_step(&g_game,p1,p2);
            T3_SIM_START(bridge_started,T3_SIM_BRIDGE);
            t3_move_probe_tick(&g_game);
            t3_combat_events_tick(g_game.total_ticks);
            T3_SIM_END(bridge_started);
            SAT_PANIC_IF_ERROR(t3_characters_tick(&g_game));
            t3_sim_tick_end();
        }
#endif
        SAT_PANIC_IF_ERROR(t3_cold_data_verify_slice());
        T3_PERF_ADD(simulation,simulation_started);
#if defined(T3_NATIVE_WAIT_PROBE)
        g_t3_native_sim_ticks=(uint16_t)(sat_time_frc()-native_sim_started);
#endif
#if defined(T3_PERF_SAMPLES)
        g_t3_perf_current.ticks=ticks;
#endif
        const uint32_t render_started_ms=sat_time_ms();
#if defined(T3_NATIVE_WAIT_PROBE)
        const uint16_t native_prepare_started=sat_time_frc();
        g_t3_native_pre_ticks=(uint16_t)(native_prepare_started-native_loop_started);
#endif
#if defined(T3_SLAVE_SIMULATION) && defined(T3_FUSED_ACTOR_PREPARATION)
        g_prepare_handle=0;g_stage_on_worker=0;
        if(deferred) {
            g_prepare_job=(simulation_job_t){{pad0,pad1},leading,deferred,g_simulation_epoch};
            t3_characters_set_worker_start(start_prepare_simulation);
        }
#endif
        prepare_scene();
#if defined(T3_NATIVE_WAIT_PROBE)
        g_t3_native_prepare_ticks=(uint16_t)(sat_time_frc()-native_prepare_started);
#endif
#if defined(T3_SLAVE_SIMULATION)
#if defined(T3_FUSED_ACTOR_PREPARATION)
        t3_characters_set_worker_start(NULL);
        if(g_prepare_handle)simulation_handle=g_prepare_handle;
        else
#endif
        {
            capture_image_match();
            if(deferred)
                SAT_PANIC_IF_ERROR(simulation_submit(simulation_pads,leading,deferred,&simulation_handle));
        }
#endif
#if defined(T3_PARTICLE_PREPARE)
        SAT_PANIC_IF_ERROR(t3_particle_prepare_image());
#if defined(T3_PARTICLE_TICK_AUDIT)
        if(g_t3_prepared_particles.visible && g_t3_particle_tick_audit_count<32u) {
            const unsigned i=g_t3_particle_tick_audit_count++;
            g_t3_particle_tick_audit[i][0]=g_game.total_ticks;
            g_t3_particle_tick_audit[i][1]=g_t3_prepared_particles.frame_counter;
            g_t3_particle_tick_audit[i][2]=g_t3_prepared_particles.visible;
        }
#endif
#if defined(T3_MIXED_OT_DIAGNOSTIC)
        SAT_PANIC_IF_ERROR(t3_characters_mixed_ot(&g_scene,g_t3_prepared_particles.draw,
                                                   g_t3_prepared_particles.count));
#endif
#endif
#if defined(T3_NATIVE_WAIT_PROBE)
        const uint16_t native_flush_started=sat_time_frc();
#endif
        flush_scene();
#if defined(T3_OVERLAY_RESERVATION_SELFTEST)
        if(!g_t3_overlay_reservation_selftest[0] && g_game.total_ticks>=45u)
            SAT_PANIC_IF_ERROR(overlay_reservation_selftest());
        else
#endif
            SAT_PANIC_IF_ERROR(sat_vdp1_overlay_begin());
#if defined(T3_NATIVE_WAIT_PROBE)
        g_t3_native_flush_ticks=(uint16_t)(sat_time_frc()-native_flush_started);
        const uint16_t native_hud_started=sat_time_frc();
#endif
        T3_PERF_START(hud_started);
#if defined(T3_FLIPBOOK_BURST)
        SAT_PANIC_IF_ERROR(t3_flipbook_burst_draw());
#endif
        if(T3_PERF_MODE!=1)draw_hud();
#if defined(T3_VDP1_COVERAGE_PROBE)
        SAT_PANIC_IF_ERROR(t3_coverage_probe_draw());
#endif
#if defined(T3_SLAVE_SIMULATION)
        if(deferred) {
#if defined(T3_NATIVE_WAIT_PROBE)
            const uint16_t native_join_started=sat_time_frc();
#endif
            if(g_stage_on_worker)stage_before_join();
            SAT_PANIC_IF_ERROR(simulation_join(simulation_handle,deferred));
            if(g_stage_on_worker) {
                SAT_PANIC_IF_ERROR(stage_sync());
                g_stage_on_worker=0;
            }
#if defined(T3_NATIVE_WAIT_PROBE)
            g_t3_native_join_ticks=(uint16_t)(sat_time_frc()-native_join_started);
#endif
            ++g_t3_slave_sim_frames;
            /* Diagnostics describe the latest committed core. The separate
             * pose tick identifies the older image and bounds its tick lag. */
            t3_move_probe_present();
        }
        g_t3_slave_sim_pose_lag=g_game.total_ticks-g_t3_slave_sim_pose_tick;
#endif
        if (T3_PERF_MODE!=1 && g_show_profiler) draw_profiler();
        T3_PERF_ADD(hud,hud_started);
#if defined(T3_NATIVE_WAIT_PROBE)
        const uint16_t native_post_started=sat_time_frc();
        g_t3_native_hud_ticks=(uint16_t)(native_post_started-native_hud_started);
#endif
        g_render_work_ms=sat_time_ms()-render_started_ms;
        T3_PERF_START(diagnostics_started);
        {
            const sat_pad_state_t pads[2] = {pad0, pad1};
            publish_diagnostics(work_started_ms, ticks, pads);
        }
        T3_PERF_ADD(diagnostics,diagnostics_started);
        T3_PERF_START(present_started);
#if defined(T3_VDP2_FLOOR)
        SAT_PANIC_IF_ERROR(t3_stage_vdp2_present());
#if defined(T3_NATIVE_WAIT_PROBE)
        const uint16_t submit_started=sat_time_frc();
#endif
        SAT_PANIC_IF_ERROR(sat_end_frame());
#if defined(T3_NATIVE_WAIT_PROBE)
        g_t3_submit_ticks_last=(uint16_t)(sat_time_frc()-submit_started);
        g_t3_submit_ticks_sum+=g_t3_submit_ticks_last;
        ++g_t3_submit_ticks_count;
#endif
#else
        SAT_PANIC_IF_ERROR(sat_end_frame());
#endif
#if defined(T3_OVERLAP_VDP1)
        if(overlapping_draw)++g_t3_vdp1_overlap_frames;
#endif
        T3_PERF_ADD(present,present_started);
        T3_PERF_ADD(work,work_started_ms);
        t3_perf_end();
        ++g_render_submissions;
#if defined(T3_NATIVE_WAIT_PROBE)
        {
            const uint16_t ended=sat_time_frc();
            g_t3_native_post_ticks=(uint16_t)(ended-native_post_started);
            const uint16_t duration=(uint16_t)(ended-native_loop_started);
            native_loop_started=ended;
            if(g_game.total_ticks>120u) {
                ++g_t3_hist[duration<7000u?0:duration<7465u?1:duration<8500u?2:3];
                const unsigned k=ticks>=3u?1u:0u;
                if(ticks>=2u) {
                    const uint32_t phases[6]={g_t3_native_pre_ticks,g_t3_native_prepare_ticks,
                        g_t3_native_flush_ticks,g_t3_native_hud_ticks,g_t3_native_join_ticks,
                        g_t3_native_post_ticks};
                    for(unsigned j=0;j<6;++j)g_t3_by_ticks[k][j]+=phases[j];
                    ++g_t3_by_ticks_n[k];
                }
            }
        }
#endif
    }
}
