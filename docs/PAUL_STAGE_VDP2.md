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

Generated art stays in the ignored build directory. `PAUL.STG` is a separate
235,520-byte disc file with sector-aligned sections; it is not embedded in
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

The panorama is a fixed-bearing perspective bake for the current encounter's
camera, **not a full port of the original moving panorama**. Source parallax,
panorama turning, floor distance shading and original stage lighting/shadows
are not reproduced by this increment. Camera movement still changes the
RBG0 ground mapping; it does not rebuild the panorama. The floor is bounded
by the current camera near/far depths.

## Stock-memory layout

| Allocation | Location / size |
| --- | --- |
| NBG0 RGB555 bitmap | A0/A1, 512x256 allocation; visible rows 0-223 |
| Two rotation/coefficient pairs | A1, bytes `0x3c000` through `0x3c97f`, outside visible bitmap rows |
| RBG0 character data | B0, `0x40000`, 2,048 bytes |
| RBG0 pattern names | B1, `0x60000`, 8,192 bytes |
| RBG0 palette | CRAM bank 6, entries 1536-1551 |

The bitmap's unused rows containing tables must stay outside NBG0's displayed
region. The new path does not scroll or zoom that bitmap. The SDK owns bank
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

## Earlier floor-cache screening

Before importing the original stage, the existing camera-bound quad cache
was decoupled from the rejected native-command prototype. Instrumented floor
preparation fell from 4-5 ms to 2 ms, and full work from 75 to 73 ms median.
A single CD screening recorded 611 increments in the old 18-to-78 interval;
there was no fresh paired control, so this is not a qualified gain. That
runtime experiment was archived and removed. Evidence remains under
`assets/imported/stage-cache-source` and `stage-cache-*`.
