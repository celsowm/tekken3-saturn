#ifndef T3_SATURN_PARTICLE_PREPARE_H
#define T3_SATURN_PARTICLE_PREPARE_H
#include "saturn/core.h"
#include "psx_particle_prepare.h"
/* Experimental diagnostic storage; no native drawing or viewport mapping. */
typedef struct {
    t3_psx_particle_draw_t draw[T3_PSX_EFFECT_CAPACITY];
    uint32_t count,visible,frame_counter;
} t3_prepared_particle_image_t;
extern t3_prepared_particle_image_t g_t3_prepared_particles;
extern volatile uint32_t g_t3_particle_prepare_images,g_t3_particle_prepare_peak;
sat_result_t t3_particle_prepare_init(void);
sat_result_t t3_particle_prepare_image(void);
#if defined(T3_PARTICLE_ORDERED_MASK_PROBE)
sat_result_t t3_particle_prepare_add(const t3_psx_particle_draw_t* draw);
#endif
#endif
