# Fighter fidelity toward the PS1 original

The rule for this work is fixed: a fidelity change ships only if an A/B
shows that the marked img/s and the worst segment are no worse than the
control's. Otherwise it stays behind a CMake option that is off by default.
Run-to-run noise on one configuration is about ±0.04 img/s.

Every measurement here is `scripts/mednafen_fight.py --seconds 280` on the
same neutral-input fight (Kuma against Jin, the AI pad hash `0xb99cc280`
in every run).

## Results

| Configuration | Marked img/s | Worst segment | VDP1 VRAM free |
|---|---:|---:|---:|
| Control: 8x8 INDEX8 face patches (`T3_FIGHTER_TEXTURE_CAP=0`) | 24.79–24.83 | 21.43 | 145,824 B |
| Native LUT4 patches, cap 32 | 24.80 | 21.43 | 64,864 B |
| Native LUT4, cap 32, high-speed shrink | 24.73 | 21.40 | 64,864 B |
| Native LUT4 + lighting, Gouraud tables never relit | 23.97 | 20.80 | 52,064 B |
| Native LUT4 + lighting | 21.96 | 18.69 | 52,064 B |

None of the candidates met the rule when this table was measured. Lighting
with the native textures has since become the default (2026-11-16, see
"Lighting as the default" below): it still costs 0.93 img/s, and the owner
accepted a cost under 1 img/s for it. That is the one exception to the rule.

- `T3_FIGHTER_TEXTURE_CAP` (default 32; 0 gives the 8x8 patches)
- `SAT_SPRITE_FLAG_HIGH_SPEED_SHRINK` on the LUT4 materials
- `T3_FIGHTER_LIGHTING` (default ON)
- `T3_FIGHTER_HAND_VARIANTS` (default OFF, Phase 3)

## Phase 1: native LUT4 textures

`scripts/psx_character_textures.py` bakes each face's patch at its native
texel density, up to the cap, with the face's original 16-colour table.
This replaces the 8x8 INDEX8 resample. The patches look visibly sharper,
but they cost about 81 KB of VDP1 VRAM.

The result is within noise of the control but never above it. High-speed
shrink (CMDPMOD bit 12, even texels only when a line shrinks) was meant to
win back VDP1 read time. It did not: the result is still within noise and
still not above the control. So VDP1 texel fetch is not what limits this
scene.

## Phase 2: per-normal lighting as VDP1 Gouraud

`src/saturn/fighter_light.c` adapts the original's per-normal light rule:
base·(back + light·max(N·L,0) + fill·max(N·down,0)), with the stage's light
rig. Normals come from the KMD exchange, exported by
`scripts/kmd_to_ska.py lit_colours`.

On the Saturn, each corner's colour is a lookup in a per-actor shade table
indexed by tone class (16), light level (16) and fill level (8). The
result is written as one 8-byte VDP1 Gouraud table per face. One actor is
relit per image, in turn, from its PS1 joint rotations.

Screenshots show correct, PS1-like shading on both fighters. The cost
splits as follows:

- **VDP1 Gouraud drawing:** about 0.8 img/s. That is the run with the
  tables never relit.
- **The relight on the SH-2:** a further 2.0 img/s. It runs serially after
  skinning and writes 2 words per face over the B-bus into VRAM.

Gouraud drawing alone already breaks the rule, so a cheaper relight
(building the tables in WRAM and copying them by DMA, or relighting less
often) cannot make lighting ship by itself. It would need the Gouraud cost
to be paid back somewhere else in the VDP1 frame first.

**Probe with no CPU cost per face (2026-11-15).** `-DT3_FIGHTER_GOURAUD_PROBE=ON`
shades every fighter face from contiguous neutral tables (LibSaturn's
`face_gouraud_first`, the shaded draw mode precompiled in the program), so
the SH-2 does nothing extra per face and the picture is unchanged. The fight
still drops from 29.73 / 28.25 to 29.46 / 27.85: about 0.27 img/s is the
VDP1 drawing shaded textured faces. The probe and `T3_FIGHTER_LIGHTING` stay
OFF; fighter packs carry light normals only with `T3_FIGHTER_LIGHTING`.

**Lighting on today's base (2026-11-16).** Same fight, Mednafen 170 s,
measurement build without Tekken Force and the practice data (see memory
below). Control 34.27 / 32.55.

| Configuration | Marked | Worst |
|---|---:|---:|
| Native LUT4 textures (cap 32), no lighting | 34.14 | 32.45 |
| + lighting, tables never relit (shaded drawing only) | 33.96 | 32.29 |
| + relight in each actor's preparation, compiled screen cull | 28.69 | 26.89 |
| + LibSaturn's assembly screen cull for shaded faces (18166793) | 29.25 | 27.20 |
| + relight on the Master before the join, images of two or more ticks | 30.21 | 28.05 |

What changed in the code at that step (lighting was still OFF by default):

- Tables are consecutive (`face_gouraud_first`), so lit faces take the
  assembly screen cull and no per-face table index is kept.
- `g_light` shrank from 21,760 bytes to 32: one shade table per actor, in
  VDP1 VRAM after its Gouraud tables; the per-normal levels are a local of
  the relight.
- The pack's light data ends its hot block. A program built without
  lighting (Tekken Force) reads the block only up to it
  (`T3_ROSTER_STORE_BYTES` is the unlit size there).
- `t3_characters_relight` runs on the Master where it would otherwise wait
  for the Slave (`main.c`, before `simulation_join`), one fighter per call in
  turn, on images of two or more ticks, and on every fourth image otherwise.

### Lighting as the default (2026-11-16)

Same fight, Mednafen 170 s, pad hash `0xb99cc280` in every run.

| Configuration | Marked | Worst | VDP1 VRAM free |
|---|---:|---:|---:|
| Unlit, 8x8 patches (`-DT3_FIGHTER_LIGHTING=OFF -DT3_FIGHTER_TEXTURE_CAP=0`) | 34.30 | 32.55 | 185,984 B |
| Lit, relight never run (shaded drawing and the Master's polling only) | 33.72 | 32.09 | |
| **Lit, native textures (default)** | 33.37 | 31.75 | 84,032 B |

The cost fell from 4.1 img/s to 0.93 (worst segment 0.80). About 0.6 is
drawing shaded native textures; the relight is the other 0.35.

The relight:

- **Only what changed.** Each part's light and fill directions are two rows
  of signed bytes; a normal's level is its dot product with them (16 light
  levels, 8 fill levels). A normal whose level is the same as last time
  costs its dot products and nothing else. A changed normal writes the
  colour of its corners, and the tables of the faces it alone lights. Faces
  shaded across several normals are written only when a corner changed.
  A part whose rows did not move is not walked at all.
- **Assembly.** `fighter_light_sh2.s`: the normals of a part
  (`t3_fighter_light_part_sh2`) and the shaded faces of four and of three
  corners. `fighter_light.c` only states each job.
- **In slices, while the Master would wait.** `main.c` runs slices of one
  fighter's relight for as long as the Slave is still simulating
  (`sat_parallel_done`), and one small slice when it is not. Fighters take
  turns; a fighter whose light did not move is skipped.

Memory, which is what blocked it before:

- **Work RAM.** The compact mesh drops what the fight never reads after
  upload: the LUT4 colour tables always, the bind vertices in the fight
  program (`sat_character_asset_compact`, LibSaturn 275f617e). The light
  data is bytes, normals with the same direction are merged, and the
  per-normal state lives in the pack's hot block. The Work RAM copy of the
  palette for the freeze signal is gone: a lit fighter shows the signal
  through its back light (`t3_fighter_light_back`).
- **VDP1 VRAM.** Practice mode did not start: its icons and font need
  54 KB and 42 KB was left, so `t3_flipbook_burst_init` failed behind the VS
  screen. The hit-spark flipbooks took 89,088 bytes as INDEXED8. They are
  now LUT4 through tables of colour-bank codes
  (`sat_vdp1_upload_color_calc_lut`, LibSaturn bd4d3c07), which keep the VDP2
  addition: 44,544 bytes, same picture, in every build. The unlit build
  measures the same with them (34.30 / 32.55, frozen pixels identical).

Checks on the default build: practice mode in Ymir (icons, font, command
list, green freeze signal), Tekken Force (unlit program, same rate as its
control), the frozen-pixel gate on the unlit build
(`5d69df645f1fb035c9fb22bf2be70408357db222`).

### Before that: why it was not the default

- **The relight is too long.** Shaded drawing costs 0.2 img/s; the rest is
  the relight. The wait probe puts it near 1,900 FRT/128 (about 240,000
  cycles) against the 1,200 the Master has idle on a two-tick image. Mednafen
  charges each VDP1 VRAM access heavily, and one relight makes about 1,560
  long writes (the tables) and 2,300 word reads (the shade table, now in
  VRAM). To fit, it needs the shade table in Work RAM, an assembly routine,
  and probably writes only for faces whose levels changed.
- **Work RAM.** With cap 32 and lighting the fight program is 15.6 KB short
  of its 16 KB Master stack reserve in High Work RAM and Low Work RAM is
  340 bytes over; Tekken Force is 10.4 KB short. Cap 32 alone grows each
  actor's store by 9,072 bytes (47,632 to 56,704) and the light data by
  another 6,016. The default build has 17.0 KB (fight) and 13.0 KB (Force)
  above the reserve, and 160 bytes in Low Work RAM.

## Phase 3: hand and jaw vertex variants

The original swaps whole vertex blocks for the hands (rows 14 and 18 for
most fighters), Kuma's and Gon's jaws (rows 19 and 20) and True Ogre's
wings. Two s16 channels per fighter drive the swap (`+0x127C` current,
`+0x1280` target, `+0x1284` rate):

- `HandFaceCommand` (`0x800346F8`) sets them from move events (hi bytes
  14..61) and from the costume's shape tables (`0x80095D74` attacking,
  `0x80095DA8` on a clean hit).
- `FighterHandPoses` (`0x80034BC4`) steps them every frame and picks the
  variant.

The port:

- `src/game/psx_hands.c`: the channels. 4000 random cases match the
  original MIPS code (`ps1_original_hand_channels`).
- `scripts/kmd_to_ska.py hand_variants`: re-exports each variant and
  stores only the positions it moves. That is 35 vertices per hand for
  most fighters.
- `src/saturn/fighter_hands.c`: rewrites the changed rows in the projected
  blocks and in the skin records before skinning, then syncs the Slave's
  cache.

| Configuration | Marked img/s | Worst segment |
|---|---:|---:|
| Control, hand channels always simulated, rows in every pack | 24.67 | 21.36 |
| Hand variants | 24.58 | 21.24 |
| Control, channels compiled out, rows still in the packs | 24.74 | 21.46 |
| Default build: neither channels nor rows (shipped) | 24.88 | 21.50 |

Repeating the first pair in the other order gave the same segment numbers,
to the hundredth, so the 0.09 / 0.12 loss is real, not noise. Hand variants
stay off (`T3_FIGHTER_HAND_VARIANTS`).

Without the option, neither the channels nor the pack rows are built in
(`T3_PSX_HAND_CHANNELS`, `--hand-variants`). The cost has three parts:

- Simulating the channels without drawing them cost 0.07 img/s.
- Carrying the rows in the packs' hot section cost 0.14 more.
- Drawing the variants costs the rest.

The shipped default is above every earlier control (24.79–24.83 / 21.43).

Not done, because each one needs work the port doesn't have yet
(3dmk-models.md):

- **True Ogre's row 22:** it follows channel 0, but it belongs to
  attachment part 19. The port draws only parts 0–17 (`drawn_rows`).
- **True Ogre's wings (row 2):** they play their own frame lists
  (`0x80095CAC` idle, `0x80095CC0` flap). In `ogre3/ogre4` the variant
  blocks are strides into the motion bank's runtime buffer, not file
  offsets, so they can't be exported the way the hand rows are.
- **Gon's blink-driven mouth and palette copy:** the copy swaps a 16-entry
  CLUT. It needs the LUT4 textures (Phase 1, off) and the hand channels
  (off). The default build's baked textures would need a second texture
  set.

All of them build on options that already measured as a loss, so they can
only add cost.

## Phase 4: draw-order bias and mesh shadows

- **Draw-order bias:** this already ships. `T3_ORIGINAL_FIGHTER_OT` is on
  by default. `apply_original_fighter_ot` gives each fighter face the
  original's OT bucket, `(Z[a] + Z[b] − bias)` with the signed bias from
  flags bits 25–31 (3dmk-models.md), and keeps the original's culling and
  LIFO order.
- **Mesh shadows:** tried and rejected. They sit behind
  `T3_FIGHTER_MESH_SHADOWS` (off), which needs `T3_ORIGINAL_FIGHTER_OT`, and
  also `T3_HIGH_SORT_KEYS`, because Low WRAM has no room for the larger
  painter arrays.
  - The packs (version 6) carry the TK3psSDW mesh (`shadow_mesh` in
    `build_fighter_packs.py`): 64 faces per fighter, 65 for Mokujin.
  - Each stage's floor shear comes from FUN_80036180 (`shadow_shear`).
    `ps1_original_shadow_shear` checks the port against the original code
    on all 20 stages.
  - `fighter_shadow.c` projects each joint's section through
    camera × shear × joint. It queues every face as a flat quad in the
    stage's shadow colour, behind the whole scene.
  - Measured: 704 images in 280 s, 2 of 7 segments, marked 22.91 / worst
    22.86. The control made 2298 images at 24.75 / 21.46. The faces were
    queued (probe: 64 per call, none behind the eye), but no shadow was
    visible on the floor at frame 200.
  - Before this can ship, both the placement and the cost need work. The
    two-quad octagon (`drop_shadows` in `main.c`) stays.

## LibSaturn changes made for this work

- `SAT_SPRITE_FLAG_HIGH_SPEED_SHRINK`, accepted by the sprite, scene-face
  and indexed-render flag checks.
- `sat_vdp1_vram_address`: mapped access to a reserved VRAM region.
- `SAT_REFUSE` and a fifth panic line, `FROM <FILE> <LINE>`. These name the
  check that refused, not only the caller that panicked. Both earlier
  candidate crashes (`INVALID ARG MAIN.C 1394`) were flag checks deep
  inside the scene flush, which this line now names directly.
