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

None of the candidates meets the rule, so none ships. All three are
available for later work:

- `T3_FIGHTER_TEXTURE_CAP` (default 0)
- `SAT_SPRITE_FLAG_HIGH_SPEED_SHRINK` on the LUT4 materials
- `T3_FIGHTER_LIGHTING` (default OFF)
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

Not done: True Ogre's wings and row 22, Gon's blink-driven mouth, and
Gon's palette copy.

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
