# Paul's stage on NBG0 and RBG0

The private Paul encounter can now use the original `stg_a` street panorama
and repeating floor texture. NBG0 displays the panorama; RBG0 projects the
floor from the same camera as the fighters. VDP1 draws the fighters and HUD.
The earlier checkerboard and its perimeter rails are removed in this mode.
The 60-tick/30-image performance target remains unmet.

## Build and asset ownership

The existing installed SDK `6302bec` provides every API used here. This
integration changes no SDK source and does not complete its advanced VDP2 plan.

Pass `T3_LOCAL_PAUL_STAGE` the existing private USA import directory, containing
`raw/036.bin`, `raw/056.bin` and `SLUS_004.02`. A fresh configuration with this
path enables `T3_VDP2_FLOOR`; explicitly setting that option OFF retains the
checkerboard control. The stage requires the original owned encounter.
Public builds with no stage directory retain their previous asset selection.

For example, replicate an existing optimized private encounter:

```sh
python3 scripts/build_saturn_variant.py \
  --baseline build/command-pipeline-restored-control \
  --out build/my-paul-stage \
  --cmake-option=-DT3_LOCAL_PAUL_STAGE=/absolute/path/to/private/tekken3-usa \
  --cmake-option=-DT3_VDP2_FLOOR=ON \
  --workload two-paul-release
```

`build_local_paul_stage.py` validates the USA executable and the exact Paul
ARC/TMD fingerprints. It resolves 311 4-bit TIM uploads through their actual
VRAM destinations, preserves floor orientation, and bakes the 514 panorama
packets in reverse packet order with their UVs, palettes and modulation.
The 100 floor entries select the same repeating 64x64 tile.

Generated art stays in the ignored build directory. `PAUL.STG` (format
version 2) is a separate 253,952-byte disc file with sector-aligned sections:
the floor tile and its 16 colours, then the panorama as one 1024x240 8-bit
cylinder picture and its 256 colours (see "Moving panorama"). It is not embedded in
the resident executable. CDFS reads it at boot through a 16 KiB Low-WRAM
buffer. Gameplay performs no disc reads. The loading screen labels this
phase `PAUL STAGE`. Neither the private file nor derived pictures belong in Git.
The existing workload recorder hashes the built stage file and records its
mapping limitations alongside the model and SDK identities.

## Drawing and presentation

The floor inverts the ground-plane homography of the actual fixed-point
`view_proj` matrix. Its texel scale is the original 64 texels per 1,800 PS1
units, converted through the encounter's existing world scale. The original
world midpoint offsets texture phase, so moving the centered fight does not
drag the floor with the fighters. Per-line coefficients provide perspective.

An unchanged camera/midpoint mapping reuses its tables and preserves the
qualified CPU/VDP1 overlap. A changed mapping waits for the matching completed
VDP1 image, then uploads and selects the other parameter/coefficient pair
within VBlank. This deliberately uses the SDK's existing swap synchronization;
it does not publish a new floor under an older fighter image. Moving-camera
frames consequently retain a wait that unchanged-camera frames avoid.

The panorama follows the original's moving box: a 360-degree cylinder picture
on NBG0, scrolled line by line from the real camera and the original backdrop
turn (see "Moving panorama"). Each floor line ends at the depth where that
line meets the panorama walls, or at the camera's near/far depths. Floor
distance shading and the original stage lighting are not reproduced yet.

## Floor distance shading (not done, measured)

Stage 0 is a kind-0 floor with shade factor -51 (`floor_shading` in the stage
manifest). `FloorDrawGrid` darkens a vertex to `0x80 + table[d] * -51 / 128`,
where the 1,024-entry table at `0x8001DF6C` ramps from 0 to 127. Brightness is
therefore 100% at the fighters' midpoint depth and falls to about 60% at the
10,000-unit fade distance. The VDP2 has one colour-calculation ratio per layer.
A line-colour blend toward black would darken the near lines too, and blending
toward a per-line average colour halves the floor texture's contrast, so no
per-line brightness ramp is built. A faithful ramp needs per-line palettes or
the VDP1 floor path.

## Drop shadows (approximation)

The original flattens each fighter's PSSDW mesh onto the floor with the stage
light's shear and draws it as opaque `POLY_F3`/`POLY_F4` in the stage record's
shadow colour (`stages.md`, `pssdw-member.md`). `drop_shadows` in
`src/saturn/main.c` instead submits two quads per fighter (a square and a
diamond, an octagon of radius about 420 PS1 units) at floor height under the
root, in stage 0's shadow colour (54, 24, 16). It follows the root, not the
pose, and ignores the light direction. It adds four faces and the probe's
cadence is unchanged (`loop_rate_x10` 290, `assets/imported/t30-law-shadow`).
The exact mesh shadow needs the PSSDW sections converted and projected per
joint; it is not done.

To make room in the full High Work RAM, the Slave stack went from 16 KiB to
8 KiB (deepest measured use 4,472 bytes; `SLAVE_STACK_BYTES` in
`scripts/saturn_workload.py` is checked against the probe's guard).

## Stock-memory layout

| Allocation | Location / size |
| --- | --- |
| NBG0 8-bit bitmap | A0/A1, 1024x256 allocation; cylinder rows 0-239 (`0x00000`-`0x3bfff`) |
| NBG0 palette | CRAM bank 7, 256 entries |
| Two rotation/coefficient pairs | A1, bytes `0x3c000` through `0x3c97f`, bitmap rows 240-242 |
| NBG0 line scroll table | A1, bytes `0x3d000` through `0x3da7f` (224 x 12 bytes), bitmap rows 244-246 |
| RBG0 character data | B0, `0x40000`, 2,048 bytes |
| RBG0 pattern names | B1, `0x60000`, 8,192 bytes |
| RBG0 palette | CRAM bank 6, entries 1536-1551 |

The bitmap's rows 240-255 hold the tables and must never be displayed: the
mapping clamps every line's row to the cylinder's 0-239. The SDK owns bank
roles, cycle scheduling and layer priority; the port writes no VDP registers.
Sprites use priority 6, RBG0 priority 2 and NBG0 priority 1, with a transparent
VDP1 erase.

The port moves the unused original-encounter world-transform scratch to Low
WRAM, retaining that scratch for generic fallback. Skinned and projected
vertices, texture descriptors and material programs remain in High WRAM.
Scene capacity is bounded at two complete actor face counts plus 16 slots;
VDP2 stage elements consume no scene slots. The final release initializes
872,872 application bytes; High BSS ends at `0x060f9b78` and Low WRAM at
`0x002818f0`, preserving the checked 16 KiB Master stack reserve.

## Validation

- All 47 host CTests pass, including five synthetic stage parser/packing/
  orientation cases. All 38 Saturn tool tests pass.
- The first complete integration passes all 24 existing live combat,
  KO/replay/victory, next-round, reset and timeout cases. After aligning disc
  sections and improving the loading label, the final release separately
  passes all 14 basic cases. The full 24-case campaign belongs to the first
  integration; it is not relabeled as a final-binary run.
- Combat scripts and their endpoints are both delayed by 160 emulator fields
  for stage loading. Input intervals and gameplay assertions are unchanged.
- Frozen tick 120 retains exactly the same 710 native textured actor command
  records, including every coordinate, texture, shading field and flag.
  Sixty former stage commands disappear. Removing their depth span changes
  the existing painter's bucket boundaries, so actor traversal and some
  overlap pixels differ; an identical framebuffer is not claimed.
- Checks against the frozen and a changed retreat camera/midpoint cover
  610 and 595 ground samples, including VDP2 parameter truncation. Maximum
  reprojection error is 0.00432 pixels. These are sampled projection checks,
  not exhaustive texel or panorama parity.
- The final 5,400-field cache-on run commits 4,878 ticks and 1,221 submissions,
  with 415 discarded fields and no guest error or draw timeout. All 1,222
  selected preparation/geometry calls execute; the neutral mapping updates
  only three times, including round camera changes. Time is still discarded.
  The warmed model/stage crop changes RGB 833 times over 3,600 nominal fields,
  or 13.88 changes/s. This excludes the FRAME HUD and is not a unique-pose or
  physical-hardware FPS count.
- Actual Mednafen CD boot displays both fighters, the street panorama and the
  textured floor. Warmed comparisons start after stage loading, at movie
  second 30. The first pilot's second-18 image was still loading and is not
  used as a gameplay endpoint.

One alternating final Mednafen pair, same installed SDK, compiler, BIOS,
neutral pads and movie seconds 30-90:

| Variant | FRAME at 30 / 50 / 70 / 90 seconds | 60-second increments |
| --- | --- | ---: |
| Checkerboard control | 178 / 376 / 575 / 776 | 598 |
| Paul stage | 158 / 368 / 579 / 791 | 633 |

This is 5.85% more submission increments, approximately 10.55/s versus 9.97/s.
Both complete rounds normally; the 60-second interval includes a transition.
The first-round 30-to-70 interval separately gives 421 versus 397 increments,
a 6.05% difference; both second-70 pictures still show round 1 with two seconds.
The earlier stage boot explains its lower initial counter. This is one pair,
not three-pair performance qualification or a count of unique displayed poses.
It compares the complete new stage with the prior checkerboard, rather than
equal artwork on two renderer implementations. The first CD pilot separately
gives 206 -> 627 over movie seconds 38-78; it predates final loading changes
and is not substituted for the final pair.

Private release builds are `build/paul-stage-qualified` and
`build/paul-stage-control`; frozen and combat captures retain the same prefix.
The earlier integration is `build/paul-stage-vdp2-release` and its complete
suite is `assets/imported/paul-stage-vdp2-combat/suite.json`. Source snapshots,
hashes, command comparison and ground reprojection checks are retained under
`assets/imported/vdp2-stage-source`.

## Moving panorama

The original draws the panorama box scaled by 8 around the fighters' root
midpoint, lowered by a per-stage height and turned by `Ry(-turn)`, where the
turn is the camera director's backdrop angle (Tekken3Remake
`docs/research/code/stages.md`). The Saturn cannot draw that box on VDP1 in
budget, so it is baked once into a cylinder and drawn by VDP2:

- **Bake** (`scripts/build_local_paul_stage.py`, `scripts/panorama_raycast.py`).
  From the point 1,100 units above the midpoint, column `c` of 1,024 looks
  along yaw `-c * 4096 / 1024` and row `r` of 240 at elevation tangent
  `0.8 - r / 200`. Each texel ray-casts the scaled box (half-width 9,608
  units) and takes the nearest non-transparent TMD texel. The picture keeps
  at most 255 colours (median cut beyond that) plus transparent 0.
- **Mapping** (`src/game/stage_backdrop.c`, adapted from the remake with
  attribution, see `NOTICE.md`). On every 32nd screen line, five rays from the
  real camera, spread across the line, are followed into the turned box
  walls. Each hit is mapped to the column and row the bake point sees there.
  The line's horizontal scroll and increment are the least-squares fit of the
  five columns, and its row is their mean. The lines between are
  interpolated. The increment is rounded to the VDP2's 8 fractional bits
  before the scroll is taken, so the screen centre stays exact.
- **VDP2.** NBG0 enables per-line horizontal scroll, vertical scroll and
  horizontal increment. The vertical table value *replaces* the layer's
  vertical coordinate, which then advances one row per line. This was
  verified in Ymir's `VDP2UpdateLineScreenScroll` after a first Mednafen run
  showed a mirrored, magnified panorama. So each entry holds the absolute
  row of its own line.
- **Floor.** The mapping also returns each line's depth to the wall along the
  view axis. `sat_vdp2_floor_t.far_lines` (SDK) hides floor beyond it. This
  matches the original, where the floor grid ends at the panorama box.

`tests/test_stage_backdrop.c` checks the integer mapping against a
double-precision version of the same rule on synthetic cameras (no game
data).

### Accuracy

`scripts/check_backdrop_mapping.py` ray-casts the true box for eight cameras
from a golden match dump (`t3_match_camera_eval`) and compares what the line
scroll shows. Excluded are dots the floor hides. Figures are 95th-percentile
texel errors:

| Frame | Turn | Column | Row | Combined | Best any line scroll could do |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 0 | 0 | 2.83 | 3.95 | 4.42 | 4.68 |
| 795 | 190 | 3.41 | 4.08 | 5.05 | 5.04 |
| 1590 | 321 | 4.08 | 4.37 | 5.83 | 5.77 |
| 2385 | 141 | 2.77 | 5.34 | 5.78 | 6.80 |
| 3165 | 43 | 0.74 | 2.09 | 2.13 | 2.24 |
| 3960 | 54 | 2.96 | 4.05 | 4.74 | 5.01 |
| 4755 | 45 | 3.05 | 4.00 | 4.78 | 4.83 |
| 5550 | 24 | 3.46 | 11.99 | 2.46 | 5.98 |

The last column is the per-line optimum: the best affine column fit and the
best single row for each line. The mapping reaches it. The remaining error
is the line scroll itself: a line can only scroll, while the true picture
bends at box corners and when the camera is far above or below the bake
point. Rows dominate. Frame 5550 looks steeply down; a quarter of its
visible dots lie off the walls (its row column includes them). The
"Combined" figure counts on-wall dots only.

Two alternatives were measured and rejected:

- Indexing rows by wall height: better on frontal views, but 9.6-11.4 texels
  at corners.
- Three rays per line instead of five: about 0.4 texel worse everywhere.

Fitting every 8th line instead of every 32nd changes the figures by under
0.07 texel. Every 56th starts to bend frame 5550 (2.39 -> 2.60).

### Cost

Measured with a bench build that remaps every image
(`-DT3_STAGE_BENCH_EVERY_FRAME=ON`), on the Ymir debug profile of the
diagnostic workload, frames 600-630, Master SH-2. Values are cycles per
mapping update; a 60 Hz field is about 449,000:

| Work | First version | Final |
| --- | ---: | ---: |
| `t3_stage_backdrop_lines` | 282,271 | 47,104 |
| `sat_vdp2_floor_from_view`, including libgcc `__divdi3` | ~105,000 | 30,006 |
| `sat_vdp2_line_scroll_write` (in VBlank) | 18,000 | 20,084 |
| `t3_stage_vdp2_prepare` (copy) | — | 5,191 |

The mapping keeps every value in 32 bits:

- Products use the SH-2's 32x32->64 multiplier, and divisions its 64/32
  divider.
- The horizontal distance to a wall point comes from the `atan2` ratio
  (`max * sqrt(1 + z^2)`, a degree-5 fit) instead of a square root.
- The lines between fitted ones are stepped.

The SDK floor now takes one hardware division per line; the depth tests
became multiplications. An unchanged camera still skips all of this. In a
fight the camera moves nearly every frame, so about 102,000 cycles (23% of a
field) remain per update.

Reproduce:

```sh
# Accuracy (host only): eight cameras from the golden dump, with truth |
# line scroll | difference images.
build/asset-venv/bin/python scripts/check_backdrop_mapping.py \
  --source assets/imported/tekken3-usa --stage build/backdrop/stage \
  --cameras build/backdrop/cameras.jsonl \
  --eval build/host/t3_stage_backdrop_eval --images build/backdrop/check \
  [--compare earlier.json] > build/backdrop/check.json

# Cost: a bench build, a debug profile, then the hot functions and the
# mapping's hottest instructions.
python scripts/saturn_probe.py --build build/<bench> --workload diagnostic \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --nm ../saturn-tools/bin/sh2eb-elf-nm --out assets/imported/<bench>-prof \
  --frames 660 --sh2-cache on --debug-profile --trace-from 600 --trace-frames 30
python3 scripts/saturn_pc_profile.py functions \
  assets/imported/<bench>-prof/debug.pc.csv build/<bench>/tekken3_saturn.elf
python3 scripts/saturn_pc_profile.py instructions \
  assets/imported/<bench>-prof/debug.pc.csv build/<bench>/tekken3_saturn.elf \
  t3_stage_backdrop_lines --blocks 128
```

On Mednafen (CD boot, neutral pads), `build/t30-cam` shows the street and
railway meeting the floor at the original low framing. Its screenshots match
those of the unoptimized mapping, except for single-texel floor and backdrop
differences.

## Earlier floor-cache screening

Before importing the original stage, the existing camera-bound quad cache
was decoupled from the rejected native-command prototype. Instrumented floor
preparation fell from 4-5 ms to 2 ms, and full work from 75 to 73 ms median.
A single CD screening recorded 611 increments in the old 18-to-78 interval;
there was no fresh paired control, so this is not a qualified gain. That
runtime experiment was archived and removed. Evidence remains under
`assets/imported/stage-cache-source` and `stage-cache-*`.
