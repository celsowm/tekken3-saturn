#ifndef T3_PSX_FIGHT_CAMERA_H
#define T3_PSX_FIGHT_CAMERA_H
#include "psx_camera.h"

/* The original fight camera (psx_camera) run by the owned match in FightFrame
 * order: the director after RoundFlow, the shake, the view matrix, then the
 * fighters' screen x from that view. Optional: a match without a camera
 * config keeps no camera. */
typedef struct {
    const t3_psx_camera_tables_t* tables;
    /* The fighters' bank section 7 (camera choices) and section 8 streams. */
    const t3_psx_camera_choice_t* bank_choices;size_t bank_choice_count;
    const t3_psx_camera_streams_t* bank_streams;
} t3_psx_fight_camera_config_t;

typedef struct {
    t3_psx_camera_t camera;
    t3_psx_camera_matrix_t matrix;   /* CameraFrame's view of the last frame */
    /* rand() (0x800A3E80), the camera generator (0x800A95F4), the AI generator
     * (0x800AE168). Only the camera draws from them here. */
    uint32_t rand,camera_rand,ai_rand;
    int16_t phase;                   /* the director's g_cameraPhase */
    int16_t flow;                    /* the round flow's phase last seen */
    int16_t screen_x[2];             /* fighter +0x1278 */
    uint8_t started;
} t3_psx_fight_camera_t;
#endif
