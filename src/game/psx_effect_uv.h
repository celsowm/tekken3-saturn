/* SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
 * Required Notice: Copyright (c) 2026 n0madic (PolyForm Noncommercial 1.0.0)
 * https://polyformproject.org/licenses/noncommercial/1.0.0/
 * The frames of the effect pool's fire, smoke, cloud and gas objects. The original gives each object a UV table and an index in
 * it (effects.md, "Drawing by type and phase"; the remake's UvTable); here a frame is one id over all the tables, in the order of
 * scripts/extract_psx_fire_flipbook.py FRAME_TABLES, so an object carries one byte. Id 0 is no frame: the glow sprite of its model. */
#ifndef T3_PSX_EFFECT_UV_H
#define T3_PSX_EFFECT_UV_H
enum {
    T3_UV_NONE=0,
    T3_UV_FIREBALL_GROW=1,T3_UV_FIREBALL_LOOP=9,T3_UV_FIREBALL_BURST=17,
    T3_UV_SMOKE_PUFF=25,T3_UV_SMOKE_FADE=33,
    T3_UV_FLAME_GROW=45,T3_UV_FLAME_LOOP=49,T3_UV_FLAME_BURST=53,
    T3_UV_FIRE_FADE=58,T3_UV_CLOUD=74,T3_UV_GAS=95,T3_UV_END=122
};
/* The half-size quads of those objects (FIRE.BIN's models, 1-based). */
enum {
    T3_FM_NONE=0,T3_FM_FIREBALL_A,T3_FM_FIREBALL_B,T3_FM_SMOKE,T3_FM_FLAME_A,T3_FM_FLAME_B,T3_FM_FLOOR_FIRE,
    T3_FM_CLOUD,T3_FM_BURN,T3_FM_BURN_GON,T3_FM_GAS_A,T3_FM_GAS_B,T3_FM_COUNT
};
#endif
