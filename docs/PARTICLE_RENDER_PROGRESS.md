# Original particle asset and projection checkpoint

2026-10-27. This follows [EFFECT_POOL_INTEGRATION.md](EFFECT_POOL_INTEGRATION.md).
The owned pool is simulated; this checkpoint supplies original drawing
inputs and a verified C billboard projection. Native particle drawing is
still missing. The playable ELF is unchanged by this work.

## Private assets

`scripts/extract_psx_particle_assets.py` fingerprints the USA executable,
decodes its resident system TIM archive using the pinned remake's research
decoder, and maps model addresses through the USA layout. With `--rev1`, it
also fingerprints Japan Rev.1 and requires every extracted byte to match.

```sh
build/asset-venv/bin/python scripts/extract_psx_particle_assets.py \
  --rev1 third_party/Tekken3Remake/work/jp_rev1/exe.bin
build/asset-venv/bin/python scripts/audit_psx_particle_assets.py \
  --disc 'roms/Tekken 3 USA/Tekken 3 (USA).cue' \
  --assets assets/imported/tekken3-usa/particles/EFFECTS.BIN
```

Both pass. The independent audit uses the canonical `Disc`/`Vram` reader,
instead of the extractor's raw executable mapping and word/nibble decoder.
It compares all 512 texels, 592 original palette words including STP bits,
592 converted RGB LUT words, and 80 model words. Result:
`build/parity-particle-assets-audit.json`.

The private output is 2,948 bytes, SHA-256
`4ec0a9eda50fa085e9f1869d4cfab37a9f366ed651a42b842304ea0b2c7e898e`.
It contains two 16×16 LUT4 patterns at original UV (128,144)/(128,128),
37 palettes, and five four-corner SVECTOR models. Numeric model IDs match
`psx_spark.h`: normal spark, power spark, glow, rising glow, ember.
Original SVECTOR order is top-left, top-right, bottom-left, bottom-right;
the native quad API's vertex order must be checked before submission.

The `T3FX001` little-endian format is:

| Section | Size | Contents |
| --- | ---: | --- |
| Header | 16 | eight-byte `T3FX001\0`, u16 counts 2/37/5, u16 sprite size 16 |
| Patterns | 256 | two 128-byte sheets; first texel occupies the high nibble |
| Palettes | 2,516 | 37 records: u16 CLUT ID, u16 zero, 16 original u16 words, 16 VDP1 RGB u16 words |
| Models | 160 | five models, each four original `(x,y,z,pad)` s16 vectors |

No converted asset is tracked in Git. The manifest records source identity,
the extracted corners and per-palette transparency/STP counts.

## Portable runtime asset reader

`src/game/psx_particle_assets.[ch]` provides a zero-copy view of the private
file for the future renderer. It checks the complete header, file length,
CLUT order and converted palette words before accepting the view, and reads
little-endian model coordinates and original CLUT words explicitly on both
host and big-endian SH-2. It preserves STP, transparent black and visible
index zero; it does not claim that VDP1 LUT4 directly renders every palette.
The caller must keep the backing buffer alive and unchanged. Failure to open
leaves the supplied view unchanged.

`ps1_particle_asset_reader` tests synthetic edge cases, corrupt headers and
palettes, model bounds and signed coordinates. When the private file exists,
it also opens it and checks original model geometry and both transparent and
visible index-zero palettes. The reader builds with the SH-2 cross compiler.
This is an asset-access component; it is not yet wired to CD loading or native
particle drawing, and the previously qualified playable ELF is unchanged.

## Stable image inputs

`src/game/psx_particle_snapshot.[ch]` copies the live sprite records in pool
order, including original slot, flags, model, position and **both** CLUTs.
It excludes objects without the original sprite flag and validates the linked
list before changing the output. The renderer must select the displayed
primitive buffer; the snapshot does not guess that timing. The copy must be
called while the pool is stable, before the deferred simulation worker starts.
An opt-in native capture has since been connected before dispatch of the
simulation worker; the default playable build still excludes it. The Master
copies the live list, the owned camera and frame counter together after the
existing Slave cache synchronization. The captured data is not yet projected
or displayed by the native renderer.

`ps1_particle_snapshot` checks ordering, filtering, independent retained
state after pool mutation, all 80 slots, and rejection of corrupt/cyclic
lists without modifying the last snapshot. The component also compiles with
the SH-2 compiler. It carries inputs only: projection, ordering against fighter
faces and additive composition remain separate integration work.

### Native snapshot experiment

`T3_PARTICLE_IMAGE_SNAPSHOT` captures in `capture_image_match`, before the
fused or ordinary deferred Slave simulation starts. It requires the owned
match, original camera and Slave simulation; it retains both CLUTs, camera and
frame counter and records a count and high-water mark for inspection. The
experimental bitmap-floor candidate uses a 64-entry local pose cache to fit
stock RAM. Its ELF SHA-256 is
`afa69403f749fae65c3a5bbb97c187a525f9a2d23efc206a312cd9e3c4768ad4`.

In Ymir, a scripted high-damage attack reached 26 visible sprite records;
at emulator field 800 the retained image had seven live sprites, original
frame counter 394, camera H=500, and no reported runtime error. The record
contains both per-buffer CLUTs (some differ), flags and original coordinates.
The longer run reaches one confirmed hit and preserves a high-water mark of
26. Evidence: `assets/imported/parity-snapshot-frame` and
`assets/imported/parity-snapshot-try-a`; their recorded input scripts and
symbol dumps identify the executable and run. A low-damage hit did not produce
this sprite type. This checks native capture on one emulator scenario, not a
race proof across all timing modes, image publication, projection cost or
on-screen particles. Unneeded low-damage and alternate-attack trial outputs
were removed after reading them.

## Preparing captured sprites

`src/game/psx_particle_prepare.[ch]` joins a stable snapshot item to its
private original model and selected CLUT, then applies the verified billboard
projection. The caller supplies the original primitive buffer explicitly;
the result retains PS1 framebuffer coordinates, original OT bucket and
culling, palette ID and pattern. Unknown models or CLUTs fail without changing
the output. It does not allocate, map to the Saturn viewport, sort against
fighters or draw pixels.

The read-only original hook now captures CLUT and UV directly from the actual
primitive passed to `FUN_8006E734`. `ps1_particle_prepare_retail` compares the
combined C preparation with all **234** captured retail sprites: coordinates,
bucket, culling predicate, primitive CLUT and UV pattern match. The captures
cover normal hit sparks over frames 222–230; they do not independently
exercise power/glow patterns. In this fixture, buffer equals frame parity and
the post-update pool positions match the draw calls. This observation does
not establish buffer timing for every game mode or replay. The test requires
that capture, the private executable and the private asset file; it skips
when they are absent. The component also cross-compiles for SH-2. No native
renderer or playable ELF was changed by this preparation.

## Particle ordering in the original ordering table

`src/game/psx_particle_order.[ch]` orders prepared visible particles by PS1
ordering-table bucket, with later insertions first inside a bucket. The output
is a list of indices; invalid arguments leave it unchanged. It neither merges
fighter/HUD primitives nor solves their occlusion.

`ps1_particle_original_order` runs the original unpatched `EffectsUpdate`
with up to 80 live objects, follows the actual links from every OT bucket,
and compares the traversal to the C result. In 80 varied cases it matches
1,473 linked primitives, including 408 buckets with multiple primitives.
Offscreen and depth-biased objects are included. The original hook supplies
the input bucket and the original actual link supplies visibility; the test
therefore covers sorting and equal-bucket insertion, not the separate
projection/culling implementation. The component cross-compiles for SH-2 and
is not yet connected to the native painter.

## Native asset loading and projection experiment

With `T3_PARTICLE_PREPARE` and `T3_PARTICLE_IMAGE_SNAPSHOT`, the build extracts
private `EFFECTS.BIN` from the specified USA executable and places it on the
experimental disc. After HUD textures are uploaded, the game loads and
validates this 2,948-byte file into the former HUD upload scratch; it remains
resident there. For each image the Master projects the captured sprite list
using its captured camera and `frame_counter & 1` as the current CLUT buffer.
That buffer convention is supported by the observed fight fixture and the
owned pool update, but has not been proven across all game modes. Preparation
runs after the snapshot/worker launch and has no rendering side effect.

The native candidate ELF SHA-256 is
`e80b8d46c685454e601e494a19b4fcba3322598abe89d22d742dec6d2d76af45`.
A Ymir attack test captured a coherent source frame 385 with nine particles,
two passing the original cull. `scripts/check_saturn_particle_prepare.py`
reads both SH-2 snapshots and runs the host evaluator on the exact same input:
all nine match in four screen positions, OT bucket, visibility, CLUT and
texture pattern (12 output fields each). The host implementation has a
separate finite comparison against original PS1 instructions, described
below. This same-input SH-2 check does not itself compare to original MIPS,
check depths, measure hardware speed or draw particles.

Capture data and run identity are in
`assets/imported/parity-particle-prepare-sh2`; the comparison is
`build/parity-particle-prepare-sh2-comparison.json`. Emulator checkpoints
that stopped during diagnostic publication or between captured and prepared
images were discarded. A separate 4,200-field Ymir BIOS/CD run reached the
fight with the new asset included (`assets/imported/parity-particle-prepare-cd`);
it does not establish physical-drive timing. This is diagnostic work: the 64-entry cache and
per-image CPU work need independent cadence qualification, and no default build
loads these assets or displays the projected quads.

## Important blending and indexing evidence

All **378 nonzero palette words have STP set**. For sprites using original
tpage `0x3F`, this selects additive PS1 blending, not ordinary opaque drawing.
The original 50% page `0x1F` has a different blend equation. VDP1's half
transparency is `(B+F)/2` against its own framebuffer; it does not implement
the PS1 additive `B+F` equation, nor blend against the underlying VDP2 plane
in the same way. A half-transparent sprite alone cannot qualify these pixels.

Two fixed fire palettes, `0x7DC8` and `0x7DC9`, have **visible index zero**.
The asset preserves this instead of rejecting or silently dropping it.
The converted LUT is color data, not proof that a direct LUT4 upload handles
every CLUT: those future fire sprites require explicit handling of VDP1's
index-zero transparency. Current spark/glow palettes have transparent zero.
Fade palettes also contain zero words at nonzero indices, and `0x8000` is a
nontransparent PS1 black with STP; preserve these distinct cases.

## Original projection capture

`scripts/audit_psx_effect_rng.py --projection` adds read-only instruction
hooks at original `FUN_8006E734`, immediately before its culling/OT insertion.
It saves each actual GTE-projected quad, model pointer, object position,
camera globals, GTE control registers, original packed depth word and object
descriptor word used by the insertion routine. It does not alter execution.

```sh
third_party/Tekken3Remake/work/venv/bin/python scripts/audit_psx_effect_rng.py \
  --case build/parity-replay-sounds-retail-fight --start 220 --end 230 \
  --projection --out build/parity-particle-projection-original.json
```

The 11-frame window contains **234 projected quads**, 26 each at source
frames 222–230. The documented culling predicate accepts 202. This predicate
is derived from the original routine; the hook does not independently prove
that the primitive was linked into the ordering table. These are original
reference coordinates. The C comparison below now includes all 234; native
rendering is not yet compared.

The first normal spark at source frame 222 projects to
`(295,198), (306,198), (295,215), (306,215)` in PS1 coordinates.
Original object position is `(2598,-1318,0)` and camera position is
`(1840,-1112,-5280)`, with pitch/roll zero and yaw 47.

## C projection proof

`src/game/psx_particle_projection.[ch]` implements the camera-relative
billboard matrix, including all three camera angles, intermediate fixed-point
rounding, signed 16-bit wrapped subtraction, MAC translation, four GTE screen
coordinates/depths, the ordering-table bucket and culling. It reuses the
existing portable exact GTE reciprocal/projection code. The model is supplied
by the caller; no original geometry or sine table is embedded in source.
The output preserves PS1's TL/TR/BL/BR order.

```sh
cmake --build build/host-law --target t3_psx_particle_projection_eval
third_party/Tekken3Remake/work/venv/bin/python tests/test_psx_particle_projection.py \
  --runner build/host-law/t3_psx_particle_projection_eval
```

The test runs **unpatched original EffectsUpdate (`0x8006F8A8`)** with one
live sprite and its lifetime update disabled (the original draw-only call).
Read-only hooks record the actual scaled camera matrix, four screen vertices
and four SZ depths. Visibility comes from the actual linked original OT head,
and every linked primitive's coordinates must match the captured GTE outputs;
the test does not substitute its own culling formula as the expected result.

All **1,514 cases** match, **23 fields per case**: 80 explicit depth/H/bias
boundary combinations, 1,200 varied cases and the 234 actual fight quads.
Random cases include nonzero roll, wrapped 32-bit world positions, the
16-bit H range via boundary/random samples, all five supplied model shapes
and arbitrary signed-16 SVECTOR geometry. The original links 476 tested quads;
rejected cases must also match their projected coordinates and depths.
Without the private fight capture, the portable suite still checks the 1,280
synthetic cases and reports zero captured quads. Results are recorded in
`build/parity-particle-projection-c.log`; CTest registers this as
`ps1_original_particle_projection`.

This proves finite C arithmetic and insertion-rule parity against original
instructions. It does not establish SH-2 performance, image snapshot timing,
native particle submission, texture sampling or color blending.

Full host suites pass **67/67 Paul and 77/77 Paul–Law** after this addition.
Their logs are `build/parity-particle-projection-host-ctest.log` and
`build/parity-particle-projection-host-law-ctest.log`. The preceding full
native gate still applies to its recorded `8faa85fa…` playable ELF; no native
binary has been rebuilt or newly qualified by these host checks.

## Native composition constraints checked in the installed SDK

VDP2 does support saturated addition (`sat_vdp2_color_calc_set_add`, CCCTL
CCMD), and the SDK exposes it. A color-calculated VDP1 sprite only adds to
the next **VDP2** image. Its underlying fighter pixels have already been
flattened into the same VDP1 framebuffer and cannot serve as that second
image. Consequently this alone would add against the stage and replace the
fighter underneath the sprite. This is a composition/occlusion issue, not
simply an absent add-mode API.

A separate VDP2 emission layer could add above both actors and stage, but
would also need correct per-particle actor/HUD occlusion and overlapping-spark
accumulation. It must be validated before adopting it. The current stage
occupies NBG0's 256 KiB panorama at VRAM `0x00000..0x3FFFF`, RBG0 tile data at
`0x40000` and its names at `0x60000`; the panorama's spare bottom rows also
hold active tables. **Correction:** the apparent spare bytes in banks B0/B1
are not available to another NBG: RBG0 reserves the entire bank for each role
(RAMCTL RDBS; VDP2 manual 6.2 and LibSaturn `vdp2_layers.h`). The earlier
suggestion to put NBG1 cells in those banks alongside the current tiled floor
cannot work, regardless of the unused addresses.

A possible bank-preserving alternative is an RBG0 **512×256 4-bit bitmap**
in B0. The present Paul floor is an identical 64×64 pattern repeated across
its 512×512 cell map, so a 512×256 bitmap repeating this pattern has the same
texel at every integer coordinate while consuming 64 KiB and one RBG bank.
`src/game/stage_floor_bitmap.[ch]` constructs each 256-byte bitmap row from
the current cell data without storing a full bitmap. `stage_floor_bitmap_exact`
checks all 131,072 pixels against the cell layout using both synthetic data and
the private Paul stage file; the routine also cross-compiles for SH-2. This
establishes texture-index equivalence only. It does not yet prove rotation
sampling, palette selection, correct repeat behavior on hardware, or visual
and timing parity.

The sibling LibSaturn source now has a managed RBG0 bitmap configuration API,
including bitmap bank reservation, address and table validation, palette and
register encoding. Its existing pure rotation test checks the 4-bit B0 floor,
NBG0's 1024×256 panorama, and a two-word-name RGB NBG1 in B1 together: the
SDK cycle planner accepts the three-layer schedule. It rejects a misaligned
bitmap, an incorrect bank mask and a parameter table in the bitmap bank. Both
changed SDK compilation units also compile with the SH-2 compiler. These are
host policy and compiler checks. The SDK prefix was subsequently rebuilt in
place and the game's experimental `T3_STAGE_BITMAP_FLOOR` option was added.
It uploads the repeated floor bitmap at boot and configures managed bitmap
rotation; the default cell floor remains unchanged. A stock-RAM candidate
required reducing the local pose cache from 96 to 64 entries to preserve the
16 KiB Master stack. Its ELF SHA-256 is
`9eff5b45d631cf53db7704570c7a2adf975622d52d163d8086858fe73430971b`.

In a 1,200-field Ymir injected run against the last qualified `8faa85fa…`
control, both reached simulation tick 795. At the last screenshot, all pixels
in rows 60–223 matched exactly, including the complete visible floor. The
only 44 changed pixels were the diagnostic frame counter at y=47–53: the
candidate submitted 541 images versus the control's 556. Cache size and SDK
rebuild both differ, so this comparison does not isolate the cause. The check
is one matching image in one deterministic run, not proof of all rotations,
hardware or cadence. An additional 4,200-field Ymir BIOS/CD boot reaches a
fight with the bitmap floor visible (`assets/imported/parity-bitmap-floor-cd`);
it is not a cadence or hardware certificate. The earlier 1,800-field CD
capture was still loading and was removed before the longer run.
`assets/imported/parity-bitmap-floor-test` and
`parity-bitmap-floor-control` retain the screenshots and measurements. The
64-entry cache's timing and the bitmap floor need further qualification before
any default change. If native testing succeeds, B1 would be free for a sparse NBG1 layer,
but its 128 KiB bank, allocation, cycle schedule and publication timing still
need a complete design; two full RGB frames do not fit there. Image publication
must share the existing stage swap handler, so floor/backdrop and particle
layers remain aligned with the VDP1 image.

SDK `sat_scene3d_faces_submit_projected_material` can accept the exact quad
in the actor painter, but ordinary VDP1 emission still needs the correct
blend solution. Also qualify the mapping from the original framebuffer to
the native viewport: the current native fighter camera uses a centered,
480-line frustum, whereas HUD mapping uses the original `(0,20,368,448)`
rectangle. The new C code deliberately keeps PS1 coordinates and does not
hide this presentation difference by applying a guessed offset.

## Controlled additive-layer experiment

The opt-in `T3_STAGE_ADD_PROBE` requires the experimental bitmap floor and
places a fixed RGB(5,7,9) rectangle on an NBG1 cell layer in B1, at priority 7
above VDP1 priority 6. It enables VDP2 add mode for that layer. This is a
controlled diagnostic, not a particle renderer; it does not draw by depth or
implement actor/HUD occlusion. The two-word names occupy `0x60000..0x63FFF`;
transparent and solid 8×8 RGB cells start at `0x64000`.

A 1,200-field injected Ymir run of ELF SHA-256
`5d596d9258d9f0322b9b23826515d1d6f01ee3f3c2089fb24fea261e38470625`
was compared to the prior bitmap-floor candidate at the same field. Both report
795 simulated ticks, 541 prepared images and no first error. The same scene
and fighter pose appear in both screenshots. `scripts/check_saturn_add_probe.py`
checks all 71,680 screenshot pixels: every one of the 7,680 rectangle pixels
matches Ymir's saturated eight-bit RGB sum (+40,+56,+72), including pixels on
Paul and the stage, and all 64,000 pixels outside the rectangle are identical.
The comparison is `build/parity-add-probe-comparison.json`; screenshots and
run metadata are in `assets/imported/parity-add-probe-test` and
`assets/imported/parity-bitmap-floor-test`.

This confirms this emulator configuration combines a single top layer with
the already drawn VDP1 and stage pixel in this scene. It does not prove actual
hardware behavior, PS1 sampling/dither, overlapping particles, an occlusion
mask, or runtime upload costs. The rectangular test covers the fighter even
when an object would have been behind it; real particles need the missing
painter-order mask. No default build enables the test.

## Original spark texture on the additive layer

With `T3_PARTICLE_TEXTURE_PROBE` plus the particle preparation and additive
probe options, the experimental game converts the original normal-spark
16×16 LUT4 texture and CLUT `0x7F94` into four RGB555 NBG1 cells after loading
`EFFECTS.BIN`. The fixed sprite appears at (80,64), above the fighter. The
conversion preserves original zero-word transparency and keeps nonzero
`0x8000` black distinct, but the selected texture/palette combination does
not exercise a black texel. The probe has no scaling, animation, depth or
occlusion and remains disabled by default.

At field 798, Ymir's screenshot matches the control throughout all 71,680
pixels: 155 transparent texels leave the background untouched, and 101
colored texels produce the expected saturated RGB addition. The other 71,424
pixels are unchanged. `scripts/check_saturn_particle_texture.py` derives the
expected colors directly from the private asset; the comparison is
`build/parity-particle-texture-probe-comparison.json`. Screenshots and native
identities are in `assets/imported/parity-particle-texture-probe` and
`assets/imported/parity-particle-prepare-sh2`. This tests one texture, palette,
scene, emulator and static one-to-one mapping; it does not validate original
PS1 sampling, dynamic uploads, hardware or the painter's occlusion.

## Publishing a live sprite with the VDP1 swap

`T3_PARTICLE_SWAP_PROBE` extends the stationary texture diagnostic. Each
prepared image selects its first visible original particle, uses that
particle's captured pattern and CLUT, and prepares four RGB cells in one of
two Master-owned RAM slots. With no visible particle it prepares transparent
cells. `t3_stage_vdp2_present` associates that slot with the next VDP1 list
serial, independently of whether the backdrop tables changed; the existing
VDP1 swap handler publishes the 512 bytes at VBlank and releases the slot. A
busy pending slot is refused rather than overwritten. This is still a fixed
16×16 diagnostic at (80,64), not the particle's actual screen position or an
occlusion implementation.

On Ymir, experimental ELF SHA-256
`baf5a608c84430f4591bc6212cdc5a37c8ecf00e9bd416c344a092f0b301e0a3`
ran a scripted hit without a reported runtime error. At the active checkpoint,
261 images had been prepared and 260 published; the latest published frame
384 used CLUT `0x7F9B`, and all 256 actual VDP2 VRAM words matched the
original private pattern and palette (45 nonzero). At a later checkpoint,
342 images had been published and all 256 words were zero after the effect
expired. `scripts/check_saturn_particle_swap.py` checks the emulator VRAM and
publication metadata; reports are `build/parity-particle-swap-active.json`
and `build/parity-particle-swap-expired.json`, with full captures in
`assets/imported/parity-particle-swap-probe` and
`assets/imported/parity-particle-swap-expired`. The preliminary run before
adding the VRAM metadata was removed.

These two observations demonstrate serial-associated publication and clear
behavior for this test in Ymir. They do not establish image-wide atomicity on
hardware, the time required for real sparse particle layers, exact PS1 pixels,
or the ordering/coverage needed to hide effects behind fighter faces. The
experiment is excluded from the default build.

## Positioned single-particle diagnostic

`T3_PARTICLE_POSITION_PROBE` extends the swap experiment to place and scale
**the first visible particle** using its captured projected quad. The original
368×448 visible rectangle is mapped to 320×224 using the existing HUD mapping;
the texture uses integer nearest-neighbour sampling. These choices are
provisional: the native fighter viewport differs, and neither PS1 GPU coverage
nor interpolation has been verified. It is a diagnostic visual, not a 1:1
renderer. It supports axis-aligned nonempty rectangles up to 32×32 pixels and
at most 25 8×8 cells; unsupported shapes fail explicitly. It does not combine
particles or mask those behind fighter faces.

The two RAM slots now carry the RGB cells and their screen tile coordinates.
On VDP1 swap the handler clears the previously published name entries, copies
the new cells, then assigns their name entries. Empty images clear the old
entries. The extra diagnostic buffers require a 32-entry pose cache in the
stock-RAM test build; performance has not been qualified. The option remains
excluded from the default game.

On Ymir, ELF SHA-256
`ad613ecb0788c44c6284aaeec51f09d9c58901f9665c6da19f81f480c74863c5`
published the original frame-386 quad `(230,129)..(241,148)` at diagnostic
rectangle `(200,54)..(210,64)` over four cells. Independent
`scripts/check_saturn_particle_position.py` verified **all 8,192 map words**
and **256 cell words** in the emulator's VDP2 VRAM against the original
private pattern, CLUT and published quad; 15 texels were nonzero. At a later
checkpoint the map contained only the transparent cell, proving the old
entries were cleared in that run. Both runs reported no runtime error.
Evidence: `assets/imported/parity-particle-position-probe`,
`assets/imported/parity-particle-position-expired`, and
`build/parity-particle-position-probe-comparison.json`. The first candidate
capture before adding its quad metadata was removed. Host test
`ps1_particle_diagnostic_tiles` also covers clipping, tile boundaries and
oversize rejection. These checks establish this implementation's data flow,
not pixel fidelity, physical hardware behavior or adequate cadence.

## Multi-particle additive diagnostic

### Fighter texture opacity and index-zero correction

The original Saturn scene material draws indexed textures with VDP1's
transparent-index-zero mode. The KMD exporter previously reserved palette
index zero only if the source model contained transparent pixels. Law's mesh
is fully opaque, so its most frequent color could be assigned index zero and
incorrectly disappear on Saturn. In the previous private archive, 183 of 768
Law faces contained 2,026 opaque baked texels encoded as zero. The generator
now reserves zero for every character and verifies that every opaque source
texel has a nonzero index. This limits the opaque palette to 255 colors and can
alter quantized colors; it does not improve the original 8-by-8 sampling.

`t3_face_index8_coverage` exposes an eight-row opacity mask from these texture
patches, respecting the actual command's transparent-zero setting. The private
`ps1_fighter_baked_texture_coverage` test decodes both generated SKA archives
and compares all 98,624 texels in 1,541 Paul/Law face patches with texture
VRAM and UVs read by the pinned remake parser. It checks opacity at the baked
texture samples, not rasterization or per-screen-pixel coverage. In an
otherwise matching injected 1,200-field Ymir scripted-hit run, the corrected
ELF `673fd285…` retained 268 frames, one hit and zero recorded errors or
rejected faces; the endpoint screenshot differed at 92 pixels inside Law's
area. This comparison does not prove original visual parity, CD boot or
hardware behavior. Texture-to-screen mapping and mask publication are still
unfinished.

The sparse additive surface now also accepts an 8-by-8 opaque coverage mask
for a later painter entry. Covered pixels clear their accumulated additive
contribution; uncovered pixels remain, and particles drawn afterwards can add
again. This is the required operation when an opaque fighter texel lies between
two additive particles in scene order. It relies on the caller supplying the
actual per-pixel fighter coverage, including texture transparency. It does not
rasterize a fighter, handle translucent fighter texels, establish PS1/VDP1
sampling equivalence or apply HUD occlusion.

The host tile test compares 100 sequences of 30 overlapping additions and
opaque masks with an independent per-pixel RGB555 composition, including
saturation and mask holes. Invalid tile coordinates leave the surface intact;
an absent tile is a no-op. The new stage wrapper also cross-compiles for SH-2
with the multi-particle probe enabled. No runtime path currently supplies
fighter coverage masks, so this change does not yet mask particles in the game.

### VDP1 screen-coverage experiment

`T3_VDP1_COVERAGE_PROBE` draws authored magenta LUT4 sprites with a known 8-by-8
transparent pattern; `T3_VDP1_COVERAGE_RECTS` selects seven axis-aligned sizes.
The Ymir screenshots are private run outputs; the tracked fixture contains
only binary masks extracted from this authored pattern. The experimental
`t3_face_rect_tile_coverage` maps an 8-by-8 index patch to one screen cell for
non-flipped axis-aligned rectangles.
In four captured expanded rectangles it matches all 2,368 sampled cell bits,
including inclusive right/bottom borders and texture holes. The captured
shrunken 4-by-4 and 2-by-2 sprites do **not** follow that expansion formula.
The follow-up `T3_VDP1_COVERAGE_MAPPING` probe renders authored color-coded
U and V textures for all 64 width/height combinations from one to eight
output pixels. All 2,592 encoded coordinate samples match center sampling
for reduction, and the resulting coverage test checks 2,117 tiles across
the reduced and expanded fixtures. This is finite Ymir evidence, not proof
for every larger rectangle, arbitrary distortion, INDEX8 sampling, PS1 GPU
rasterization or physical Saturn hardware. The mixed fighter/effect renderer
does not use this function yet.

For non-axis-aligned sprites, `T3_VDP1_COVERAGE_QUADS` captured U and V with
two separate authored, color-coded textures. The eight shapes include skewed,
rotated, narrow and triangular commands; the fixture contains 5,745 covered
pixels and their sampled coordinates. A simple pair of interpolated edges and
line spans failed on several shapes, leaving missing pixels or choosing
different texels, so it was not enabled as a general rasterizer.

`T3_VDP1_COVERAGE_TRIS` adds eight authored triangles. A separately implemented
experimental path now handles C=D, a horizontal A-B edge, and edges whose
horizontal displacement is no greater than the vertical span. For the four
captured triangles in that subset, `saturn_vdp1_triangle_coverage` compares
1,184 tile masks against the captured U/V data, including transparent texel
patterns. Other triangles are explicitly rejected: a shallow case and a case
whose apex extends past the top edge showed differences in preliminary tests.
Both capture sets are finite Ymir observations; they do not prove behavior for
untested shapes, hardware, or PS1 rasterization. Neither path is connected
to fighter/effect composition, and the probe options remain disabled in the
restored experimental fight build.

For the parallelograms in that probe, an independently derived forward-line
experiment fills the intermediate pixel at a diagonal step. This matched six
of the eight authored quads in both their encoded-coordinate captures and a
separate transparent-pattern capture. The remaining skewed non-parallelogram
case still differed, so `t3_face_parallelogram_tile_coverage` is deliberately
limited to parallelograms with nonnegative Y directions, horizontal-major
spans, vertical-major edges, and spans no greater than 64 pixels. Its 150
captured tile masks pass, including overlaps and transparent holes. It remains
a finite Ymir test; neither general VDP1 rasterization nor real hardware is
established, and it is not yet used by the fighter/effect renderer.

To determine whether those restricted cases matter in a real fight,
`T3_FACE_COVERAGE_CENSUS` counts visible original-OT faces by their actual
projected screen shapes. In one 1,200-field scripted Ymir attack run, 177,293
visible face instances were classified: 814 rectangles, 3,760 supported
triangles, 1,428 additional supported parallelograms, and 171,291 other
shapes. The supported categories total about 3.4% of that finite run, so
general quadrilaterals remain the priority. The run reached one hit and
reported no first error or rejected faces; classification overhead and
emulator timing make its endpoint unsuitable as a release cadence result.

The subsequent bounded general-quad experiment independently advances both
vertical-major sides, draws each horizontal-major span, and includes the
extra pixel observed at diagonal span steps. `saturn_vdp1_quad_coverage`
matches all eight authored quad captures, including the previously mismatched
non-parallelogram, using both opaque and transparent test patterns: 444 tile
masks match Ymir. The function rejects geometry outside its bounded orientation
and size requirements, and has not been qualified on arbitrary quads, physical
hardware or PS1 pixels. It is not yet used for fighter/effect composition.

The diagnostic census now also applies the general path's geometric preflight
to remaining faces. One 1,200-field attack run classified 130,963 visible face
instances; 17,925 (about 13.7%) passed one of the experimental shape gates.
That is an eligibility count, not a visual accuracy claim for those faces. The
diagnostic reported one hit, no first error and no rejected faces, but its
endpoint was 13.3 fps with 74 ms frame work; per-face geometry checking needs
optimization before any runtime use. A different amount of simulated time was
reached than in the earlier census, so the two percentages are not a controlled
performance comparison.

The general-quad eligibility preflight now advances its rounded edge positions
with integer remainders rather than repeating divisions for every scanline.
`saturn_face_preflight_equivalence` compares the new implementation with the
previous formula for 400,000 generated cases, including accepted and rejected
shapes; the existing captured-coverage tests also pass. In an otherwise
matching 1,200-field injected Ymir attack run, the preflight build
`3deee60f…` produced 197 images and reported 15.0 fps / 65 ms at the endpoint,
versus 177 images and 13.3 fps / 74 ms for the previous implementation. Both
registered one hit and no first error or rejected faces, but their simulated
tick endpoints differ. This is diagnostic Ymir evidence, not a controlled
hardware benchmark or proof of output pixels. A further endpoint-bound shortcut
did not show a clear gain and was discarded; the per-face raster itself and
integration with particles remain unfinished.

The probe reflects the VDP1's distorted-sprite path; the [Sega VDP1 manual](https://antime.kapsi.fi/sega/files/ST-013-R3-061694.pdf)
documents this command's four-coordinate sprite geometry. Original code from
the GPL-licensed emulator was not incorporated into the project.

`T3_PARTICLE_MULTI_PROBE` accumulates every visible prepared sprite into a
shared sparse RGB555 cell set. When texels overlap, each 5-bit RGB component
is added and clamped; transparent zero leaves the previous value, while
nonzero black retains a valid cell pixel. Only additive STP palettes are
accepted. A preflight rejects an unsupported shape, non-STP color or tile
capacity overflow without changing that sprite's accumulated surface. The
80-cell capacity is an experimental RAM limit, **not** a supported maximum
for arbitrary fight scenes: overflow stops the diagnostic instead of dropping
particles. Per-sprite 32×32 limit, provisional viewport and nearest sampling
also remain. The layer still ignores actor and HUD occlusion.

A host test covers overlap, saturation, transparency, invalid palettes and
capacity rejection. An initial 48-cell run reached its limit after 25 sprites
and stopped with a capacity error; that rejected run was removed. The current
80-cell candidate fits by reducing the pose cache to eight entries, a clear
performance compromise requiring replacement or qualification. It is disabled
in the default build. Its ELF SHA-256 is
`cccd33df0a5e0033a1e9ee969b7f5b003a26393330c4d46e3b594ca1ae6af64f`.

In a Ymir scripted hit, source frame 366 had seven visible sprites covering 26
cells. `scripts/check_saturn_particle_multi.py` independently composes their
captured quads and checks the complete 8,192-word map and 1,664 used cell
words against emulator VRAM; all matched. That captured frame had no
nontransparent overlap, so the overlap claim is supported by the host test,
not this scene. At source frame 386 two visible sprites and ten cells also
matched. Both recorded runs reported no runtime error, draw timeout or rejected
faces. Reports are `build/parity-particle-multi-dense-comparison.json` and
`build/parity-particle-multi-comparison.json`; captures are in
`assets/imported/parity-particle-multi-dense` and
`assets/imported/parity-particle-multi-probe`. Other transient checkpoints were
deleted. These finite emulator checks do not establish original PS1 pixels,
worst-case capacity, VBlank transfer time, hardware behavior or cadence.

## Original fighter and particle ordering

The pinned fight harness normally stubs `DrawSkinnedPart` (`0x80036F00`).
`scripts/audit_psx_fighter_particle_order.py` restores its 2,920-byte retail
JP Rev.1 body and 102 GTE instruction hooks before a selected fight window,
then uses read-only instruction hooks to capture actual fighter and particle
stores into the scene OT. Fighter drawing was still stubbed before the window,
and other normal harness hardware/presentation stubs remain; this is not a
complete retail GPU run. The original fighter draw was restored from frame 221
through 225 in the recorded fight, with 175 part calls and **3,672 actual
fighter OT stores**. Effects linked 96 primitives during frames 222–225.

In that finite window, 61 particle insertions shared a bucket with at least
one fighter face inserted in the same frame. For **41** insertions the OT
head immediately before the particle store was a recorded fighter face from
that same bucket. The particle becomes the head and is traversed before that
face; the face is subsequently drawn over it. Depth comparison alone, or
putting all emission above the finished VDP1 image, cannot reproduce those
cases. The capture identifies packet links but does not render GPU pixels or
prove the exact coverage of a fighter texture.

`DrawSkinnedPart` quantizes vertex depths to `SZ >> 5`; each face's bucket is
`((first + last - signed_bias) & 0x7FE) >> 1`. For a two-sided face viewed
from the back (`NCLIP <= 0`), the seven-bit signed bias reverses. The first
and last selected vertices are dictated by the original packed triangle or
quad descriptor; this is not the current native painter's average of four
linear depths. `src/game/psx_fighter_ot.[ch]` implements this bucket arithmetic
when the caller supplies those already selected depths and flags. The
`ps1_fighter_original_ot` test compares it to all 3,672 actual OT stores,
including 2,058 triangles, 1,614 quads, positive/negative biases and back
faces. It also cross-compiles for SH-2. The original packed face selection and
private metadata are addressed below; exact fighter projection, integration
with the native painter and the texture coverage mask remain unfinished.

### Quad orientation correction

The original quad NCLIP input is an important correction to the pinned remake's
`3dmk-models.md` description: in this retail capture it uses original packet
vertices **0, 1 and 3**, not 0, 1 and 2. For triangles it uses 0, 1 and 2.
Using the first three quad vertices would produce a different NCLIP value for
1,314 of the 1,614 captured quads. The projected-face helpers now calculate
the original signed NCLIP and bucket directly from projected coordinates and
depths; a second entry point accepts the Saturn perimeter order, where the
last two quad corners are swapped. Both paths match all 3,672 captured stores,
including NCLIP and bucket. This finite trace comparison does not yet change
Saturn face culling or painter order: the SDK currently culls quads by the
area of all four viewport-scaled corners, and uses averaged linear depth with
stable ties. Correct integration must also preserve original LIFO order within
each bucket and account for particles and clipping; merely replacing the SDK's
depth key would not reproduce the original ordering.

### Experimental Saturn fighter ordering

The combined scene OT can now be reconstructed independently by
`t3_psx_scene_ot_order`: it takes fighter and particle entries in their actual
insertion order and follows 1,024 LIFO bucket heads from far to near. The
retail capture records a common insertion serial for both types; in this
particular five-frame window the fighter stores precede particle stores.
`ps1_original_mixed_scene_ot` follows the original packet links rather than
assuming sorted input. It matches 3,768 primitives and 3,611 predecessor links,
including 41 buckets containing both fighters and particles, plus 100 varied
synthetic cases. Other fight phases can have different insertion schedules.

With `T3_MIXED_OT_DIAGNOSTIC`, the Saturn build reconstructs that mixed order
after preparing the captured particle snapshot, reusing fighter scratch after
its face keys have been computed. It checks every produced order for descending
bucket and reverse insertion ties. In an injected 1,200-field Ymir attack run,
268 images passed the diagnostic; 165 visible particle instances were included,
with 85 adjacent cross-kind ties and a peak of 26 visible particles in one
image. The run registered one hit, no first error and no rejected faces; its
endpoint reported 20.7 fps and 48 ms. With cache emulation enabled, 232 images
passed, including 140 particle instances and 68 cross-kind ties. These are
counts across images, not distinct particles. This diagnostic does not submit
particles to the painter or draw them, and assumes the observed insertion
schedule; additive blending and particle/fighter pixel coverage remain open.
The experimental build moves the capture, preparation and sort storage to Low
WRAM to fit the existing memory budget.

Reproduce the ordered original capture and comparison with:

```sh
third_party/Tekken3Remake/work/venv/bin/python scripts/audit_psx_fighter_particle_order.py \
  --case build/parity-replay-sounds-retail-fight --start 221 --end 225 \
  --out build/parity-fighter-particle-sequenced.json
ctest --test-dir build/host-law -R '^ps1_original_mixed_scene_ot$' --output-on-failure
```

`T3_ORIGINAL_FIGHTER_OT=ON` now retains the original projected vertices,
submits the two meshes without SDK winding culling, and replaces fighter queue
keys using the original NCLIP, double-sided flag, signed bias and OT bucket.
Faces rejected by the original winding rule are marked skipped. The two
consecutive fighter queue runs are reversed before the SDK's stable bucket
sort to reproduce later-insertion-first for ties between those faces. The
retail capture validates 3,545 consecutive same-bucket fighter links, as well
as the 3,672 face NCLIP and bucket values. It does not prove the complete
Saturn queue matches the original: stage/shadow depths are approximated in the
same scale, particle faces are not merged into this ordering, the SDK's
preprojected path can fall back near clipping boundaries, and this code has not
been compared against the original rendered pixels across fights.

The opt-in two-fighter experimental ELF `ebe89d32…` ran for 1,200 injected
Ymir fields, with 296 OT frames, 215,884 retained and 240,252 culled faces
(exactly 1,541 source faces per OT frame). It recorded zero original-projection
fallbacks, rejected faces and first errors. The endpoint reported 22.6 fps and
44 ms; with cache emulation enabled, 270 OT frames reported 20.7 fps and 47 ms,
again without recorded fallback or error. These are single-scenario endpoint
measurements and do not validate CD boot or real hardware. The extra cost makes
this an experiment rather than a replacement for the playable renderer.

### Original vertex selection and KMD metadata

The same capture now records each face's original packed words, all selected
scratchpad depths and the coordinates actually written to the primitive.
The audit checks that those original packet vertices equal the selected
scratch slots. `t3_psx_fighter_face_ot` decodes the four groups FT3, FT4, GT3
and GT4, retains the flags word from the correct descriptor word, selects the
first and last original slots and applies the OT calculation. The host test
`ps1_fighter_original_face_ot` matches the 3,672 original linked faces across
all four groups; the C component cross-compiles for SH-2. This verifies finite
original observations, not the native fighter projection or culling.

The private KMD converter now preserves each face's two-byte `(group,
flags_high_byte)` metadata in its existing face traversal order. The optional
`T3_PARTICLE_FIGHTER_OT` build emits these private arrays for both fighters;
no converted asset is tracked. The current native code does not reference them,
so linker garbage collection removes them from the resulting ELF. An
independent comparison with the pinned remake parser checked all 1,541 Paul
and Law faces, including types, vertex slots, double-sided bits and signed
biases. The optional `--generated` check also compared the generated arrays in
face order. On the VDP1 perimeter conversion, original vertex 3 of a quad maps
to native vertex 2; for a triangle native vertex 2 is also the original last
vertex. Native per-vertex PS1 depth and per-face packet coverage are still
needed before this metadata can determine particle occlusion.

Reproduce the private capture with:

```sh
third_party/Tekken3Remake/work/venv/bin/python scripts/audit_psx_fighter_particle_order.py \
  --case build/parity-replay-sounds-retail-fight --start 221 --end 225 \
  --out build/parity-fighter-particle-order-original.json
ctest --test-dir build/host-law -R '^ps1_fighter_original_ot$' --output-on-failure
```

## Projected fighter seam exchange

The original `DrawSkinnedPart` transforms a part's own vertices with the GTE,
then imports, combines and exports **projected packed XY and SZ/32** through
scratch/global exchange buffers. In particular, adding two packed XY words
can carry across the X/Y boundary, and halving a packed value uses the
original arithmetic shift and bit mask. Blending world-space vertices first
and projecting afterward is generally not equivalent. The existing native
skin-to-screen path has not been established as bit-exact for these seams.

`src/game/psx_vertex_exchange.[ch]` ports the projected exchange operations,
including their sequential in-place import/export order, signed half-depth
rounding and 16/32-bit wrapping. It accepts caller-projected own vertices and
caller-owned scratch/global buffers; it does not draw the result.
`src/game/psx_vertex_project.[ch]` now projects the part vertices through the
GTE-compatible camera before applying this exchange. Its preflight rejects malformed/odd offsets, scratch offset
zero and blocks exceeding its 128-slot representation without modifying state.
That zero-offset rejection is a current restriction; it has not been proven
safe for all fighters, although the private Paul and Law models used here do
not use it for scratch imports or scratch exchanges. Input arrays must not
overlap the mutable state.

`ps1_vertex_exchange_original` runs the original unpatched
`DrawSkinnedPart` on the private USA Paul and Law KMDs with the Japan Rev.1
executable. Five deterministic transform sequences cover 185 part calls,
all six post-operation groups, depth saturation and varied orientations. It
compares **18,005 scratch/global slot values** against the C port, both with
pre-projected input and with C projection integrated; all match. The separate
`ps1_camera_joint_original` test compares 1,200 camera/joint compositions
against the unmodified original `FUN_80037AB0`, including saturated matrix
values and wrapping translations.
`ps1_vertex_exchange_bounds` separately checks invalid input remains atomic
and signed packed-coordinate/depth halving. The source also cross-compiles for
SH-2. These are finite host comparisons, not full-scene rendering, native
runtime timing, or proof for untested KMD models. The new component is not yet
connected to the Saturn fighter renderer.

The private KMD generator now retains both the SKA index and a distinct
logical index for each original projected slot, together with face indices
using the logical vertices in VDP1 corner order. This distinction is needed:
the SKA exporter merges equal skin records, but the original can round their
projected seam slots differently. The independent private-data audit checks
all vertex blocks and both face mappings against the reference KMD parser and
the generated SKA. For the current USA Paul and Law meshes it checks 692 and
733 logical slots respectively; 123 and 133 compiled indices are shared by
multiple slots. The experimental SH-2 build compiles this data, but it remains
unreferenced by the renderer and therefore does not yet change the image.

`t3_psx_projected_mesh_step` now joins the original projection and seam
exchange to that distinct logical output. The original-code harness exercises
it over the same 185 real KMD blocks and varied transforms as the exchange
test, checking each scattered result and comparing the exchange slots against
the original. The host bounds test also checks that an invalid destination
leaves state and output unchanged. The SH-2 compilation succeeds; this still
does not establish on-screen behavior or runtime cost. Connecting it requires
the original view and world-joint matrices, logical face topology and depth
conversion in the actor submission path, including its clipping fallback and
Master/Slave ownership.

`t3_psx_projected_mesh_build` now assembles a whole fighter from its view,
draw-part world joints and ordered KMD blocks, keeping exchange state across
parts and reusing the joint matrix for a second block on the same part. The
`ps1_projected_mesh_original` host test executes ten whole Paul/Law sequences
through the original `DrawSkinnedPart` and compares 8,150 resulting slots
over 185 blocks. It uses an identity view with varied joint matrices; the
separate camera/joint test covers non-identity view composition. This remains
a finite CPU comparison, not a rendered-frame or performance test. The mesh
builder was initially tested separately before the Saturn integration below.

### Experimental original fighter projection on Saturn

`T3_ORIGINAL_FIGHTER_PROJECTION=ON` now selects a separate logical topology
and builds each fighter from the presented camera and pose snapshot before
submitting its projected faces. The snapshot is published before the Slave
starts deferred simulation; the worker reads it through the uncached alias.
If the original build fails, or the SDK refuses projected submission near its
clipping limits, that actor returns to the existing world-space renderer. The
viewport maps the PS1's 368 by 480 picture to 320 by 224; SDK depth is derived
from the original projected depth, so SDK sorting is still not the original
fighter OT. Hand variants and attachment rows are also not implemented here.

This is an opt-in experiment, not the qualified playable build. With the
two-fighter bitmap-floor configuration, particle probes disabled, cold-face
capacity reduced to 320, and geometry placed in Low WRAM, it fits the linker
budget. Ymir injected execution for 1,200 fields produced 289 rendered frames
and projected both fighters 289 times, with zero recorded fallbacks or scene
errors; the endpoint reported 21.6 fps and 47 ms frame work. An otherwise
matching build with the option off produced 536 frames, reporting 41.0 fps and
22 ms at its endpoint. These are endpoint measurements of one idle scenario,
not a frame-time distribution or a hardware performance result. A separate
cache-emulation-on run produced 254 frames, with both projection counters at
255 and no fallbacks at the sample. Screenshots show both fighters but are not
pixel-matched to the original, and neither run validates CD boot. The
experimental renderer is currently too slow to substitute for the existing
playable path.

The projection arithmetic has since been optimized without changing its
integer results: precomputed reciprocal seeds replace a division per vertex,
normalization uses four tests, and signed-16 KMD coordinates use the SH-2
multiply-accumulate path. A 65,536-depth test checks the reciprocal and axis
calculation against the prior arithmetic with varied focal lengths; 50,000
signed-coordinate cases compare the specialized and general projection.
Thirteen camera, particle and fighter tests also pass, including the original
whole-mesh comparisons. In the same 1,200-field Ymir scenario the optimized
ELF `fa4b1ad3…` produced 375 frames, reported 28.5 fps and 34 ms at the
endpoint, and projected each fighter 375 times without a recorded fallback.
That compares with 289 frames and 21.6 fps before optimization, and 536
frames and 41.0 fps for the SDK projection control. It is still an injected,
single-scenario measurement, with original OT and visual parity unverified.

The SH-2 specialization now reads original signed-16 coordinates directly
with three `MAC.W` operations per row. With S clear, SH-2 accumulates these
products in the 64-bit MACH:MACL pair; see the [SH-1/SH-2 programming manual,
section 6.31](https://www.bitsavers.org/components/hitachi/superH/1995_SH1_SH2_Programming_Manual_3rd_Edition.pdf).
The optional `T3_VERIFY_ORIGINAL_PROJECT_DOT` diagnostic compares every result
with the previous longword-MAC kernel. An instrumented Ymir run projected both
fighters 347 times and recorded no mismatch; the diagnostic is off in the
normal experimental build. This only tests the poses that were exercised.

At frozen simulation tick 120, the old and new SH-2 kernels produced identical
screen pixels outside an eight-pixel difference in the frame counter (difference
bounding box x=170..182, y=48..53). Memory dumps at arbitrary field endpoints
cannot establish per-vertex equality because the worker may be writing the
shared buffer, so that screenshot comparison is not presented as such. In a
separate live 1,200-field run, ELF `6f8c2ffd…` produced 385 frames, reported
28.5 fps and 33 ms at the endpoint, projected each fighter 385 times, and
recorded no fallback. This is a small throughput increase from the previous
375-frame run, not evidence of a stable frame rate or hardware performance.

Run the audit after building the experimental two-fighter assets:

```sh
build/asset-venv/bin/python tests/test_kmd_projected_data.py \
  --generated build/parity-bitmap-floor/generated/characters/t3_generated_character.c \
  --preview build/parity-bitmap-floor/generated/characters
```

## Next implementation requirements

The C implementation wraps the object-minus-camera delta to signed 16 bits
before rotating, and scales the camera matrix and billboard matrix's second
row by **20/12**, truncating toward zero. Keep this distinct from the fighter
view's different aspect factor when integrating it.

Capture projected particle state before launching deferred Slave simulation,
so Master drawing cannot race pool updates. Integrate particles into the
actor depth ordering, resolve the additive blend requirement, and measure
stock-RAM code/data cost before accepting a native renderer. Validate both
simulation hashes and pixels; neither proves the other. Active power glows,
other effect types and replay effect logs remain separate unfinished work.

## Particle-local face census (2026-10-27)

`T3_FACE_PARTICLE_CENSUS` is an opt-in diagnostic requiring the mixed OT and
face census. It runs the expensive face-shape classification only when a
visible particle's conservative mapped bounds intersect the fighter face's
screen bounds. It uses the prepared particle snapshot after the fighter queue
is reversed, and recovers the face's queue entry before testing visibility.
No depth rejection is applied. Bounds include one extra pixel around the
particle, are clipped to the 320 x 224 viewport, and use the same experimental
coordinate mapping as the diagnostic tile renderer. The host tile test checks
that emitted pixels lie within the bounds and checks touching rectangles.
This only classifies candidates: it does not yet draw or occlude particles,
and is not proof of full VDP1 or PS1 raster coverage.

With the same 1,200-field Ymir input script and cache configuration as the
unfiltered preflight control, the filtered ELF SHA-256
`1fc3fd9e9de358847b015c54756338362fa3e80966da4f2e1bd0351b14472fc4`
submitted 262 images versus 197, with 775 simulation ticks and one hit in
both runs. The sampled work time was 48 ms versus 65 ms; first_error, rejected
faces and draw timeouts were zero. Discarded fields were four versus one.
These are injected emulator measurements, not a CD boot or hardware result.
In the filtered run, 12 images had visible on-screen particle bounds. Among
8,740 visible fighter faces in those images, 519 intersected the conservative
particle bounds; 36 of those met the current triangle, parallelogram or
bounded-general coverage gates. Different image cadences mean the two runs
sample different poses, so these counts are not paired face-by-face proof.
The original candidate capture was superseded by the instrumented sample below;
the control is `assets/imported/parity-coverage-preflight`. Host CTest passed 97/97.
The remaining work is accurate coverage for the other candidate shapes,
texture transparency and integration into the actual mixed ordering.


### Small rotated triangle samples

An instrumented repeat, ELF `37df36c5936032a554e6995552827b2912f187942ec682ca56157a8967740ce6`,
recorded the first 32 face geometries intersecting a particle in a private
Ymir capture (now superseded): 21 were degenerate quads (triangles) and
11 were quadrilaterals. This is a biased first sample from one fight, not a
population estimate. Projected game geometry remains private. The repeat
submitted 262 images, advanced 774 ticks, recorded one hit and first_error 0.

The new `T3_VDP1_COVERAGE_SMALL` probe draws eight independently authored
small triangles in varied orientations, using separate U and V textures.
With the additive stage probe disabled, the 900-field Ymir run (ELF
`b38f3068521c1b86b2437333aa3cba545a19aa96ccce4f05e3fef5153f0aafab`)
reported first_error 0. `scripts/capture_vdp1_small_triangles.py` extracts
330 pixel positions with paired texture coordinates into the tracked fixture
`tests/fixtures/vdp1_small_triangles_uv.json`; it rejects mismatched U and V
coverage, which catches the additive overlay observed in the first attempt.
The fixture contains authored probe geometry, not game data. These finite
emulator captures are reference data for the next rasterizer work, not proof
of triangle parity or physical hardware behavior.


### Rotated triangle preflight correction

The first authored small triangle (`A=(30,22)`, `B=(36,18)`,
`C=D=(33,29)`) passed the general quad preflight, but its predicted masks
differed from the captured UV planes in 35 bits across the separate U and V
texel-selection checks. The difference counts multiple selected-texel mask
bits, not 35 distinct screen pixels. A coordinate reflection experiment also
failed on two other shapes, so symmetry alone is not a validated fix.

`t3_face_quad_supported` now rejects C=D triangles whose A-B edge is not
horizontal; the coverage function consequently rejects them without modifying
the caller's mask. The authored quad fixture test asserts that the captured
triangle is rejected, and the independent 400,000-case preflight comparison
includes the new rule. The five existing rectangle, triangle, parallelogram,
quad and preflight tests pass. This does not prove the remaining accepted
orientations; those still need broader UV and hardware validation.

The opt-in census run `assets/imported/parity-coverage-guard` used ELF
`c7d62c9f2d81c56bf35a86fbaedaa0bff09288ae7f02d227aef7c66f8e92e3a1`
and reached 262 submitted frames, 774 simulation ticks and one hit in 1,200
Ymir fields, with first_error, rejected faces and draw timeouts zero. In this
run 519 faces intersected particle bounds and 13 passed the general-quad gate.
The prior sample was deleted after the new run superseded it; its aggregate
counts above remain historical evidence. The HUD, game rendering and particle
composition paths do not yet consume these diagnostic masks.

### Experimental mixed-order particle publication

`T3_PARTICLE_ORDERED_MASK_PROBE` connects the prepared particle sprites to
mixed fighter/particle OT traversal. Particle entries add their texture to the
sparse overlay; later fighter entries test existing tiles and apply a coverage
mask when their geometry and 8 x 8 texture are supported. Indexed8 and LUT4
texture data are read from the already loaded character asset. If an
intersecting face cannot be rasterized, or an additive particle cannot fit,
the entire overlay for that image is cleared before publication. This avoids
publishing a partially ordered composition, but visibly drops effects in those
images and is not suitable as the default or as a parity claim. Stage and HUD
occlusion, PS1 sampling, unsupported geometry and physical hardware remain
open.

The two 40-tile buffers and the stage's line-scroll scratch reuse the 16 KiB
stage upload buffer after stage initialization. This keeps the instrumented
stock-RAM build inside its linker limits and retains the 16 KiB Master stack;
it lowers this probe's per-image capacity from the earlier 80-tile diagnostic.
The buffer is only reused after the last stage asset upload. The SDK and
normal non-probe builds are unchanged.

The 1,201-field injected Ymir run `assets/imported/parity-ordered-particles`,
ELF `89b4c64971afca250b1d47c2505d7db05009d4deb36925d84a8b6f4ae6a9a32f`,
reached 203 submitted images, 778 simulation ticks and one hit, with zero
first errors, rejected faces or draw timeouts. The mixed composer accepted 200
images, including nine with 40 particle additions; three images fell back on
unsupported intersecting fighter faces. No fighter mask tile succeeded in this
run, so it verifies the scheduling and fallback counters, **not** actual
fighter occlusion. The endpoint's sampled work time was 65 ms; this is not a
release cadence measurement. The existing host tests for additive tiles, mixed
OT order and quad coverage pass. The initial 1,200-field probe stopped during
diagnostic publication; its incomplete capture was deleted before the
1,201-field repeat. The earlier coverage-census control is
`assets/imported/parity-coverage-guard`.

### Native masked-overlay self-test

`T3_ORDERED_MASK_SELFTEST` replaces the fight overlay with an authored, fixed
8 x 8 test: a red particle, a later checkerboard face with transparent holes,
and then a green particle. It uses the same stage add, face-mask and swap
publication functions as the ordered experiment. The face alternates between
indexed8 and packed LUT4 on consecutive prepared images; all inputs are
synthetic, not game assets.

Two Ymir captures at 900 and 903 fields, ELF SHA-256
`98d6de84ee44bc568ca2c27dcf44d56aa966b19a790339d5a84db58ce9fac5cb`,
show consecutive published images (158 and 159). The checker
`scripts/check_saturn_ordered_mask_selftest.py` compares all 8,192 VDP2 map
words and 64 output texels for each capture: all checkerboard opaque pixels
retain only the later green contribution, while transparent holes retain the
red plus green contributions. The per-format mask counters show both indexed8
and LUT4 paths ran; the alternating final images and their output were both
checked. The guest reported no first error. Captures are retained as final
experiment evidence in `assets/imported/parity-ordered-mask-selftest` and
`assets/imported/parity-ordered-mask-second`.

This checks a single axis-aligned mask, synthetic assets and Ymir's VDP2 VRAM.
It does not show that actual fighter faces are accepted, validate arbitrary
triangles, compare the PS1 GPU or prove physical Saturn output. The build was
restored with the self-test disabled afterward.

### Slanted face in ordered particle composition (2026-10-28)

The optional `T3_ORDERED_MASK_QUAD_SELFTEST` replaces the rectangular face in
that diagnostic with an independently captured, textured VDP1 parallelogram
(`tests/fixtures/vdp1_parallelogram_masks.json`, vertices (20,70), (52,75),
(56,100), (24,95)). Two solid additive particles surround the face in painter
order. The checker `scripts/check_saturn_ordered_mask_quad.py` compares every
one of the 8,192 VDP2 name words and 1,280 pixel words with the independently
captured face mask, including transparent texels and the particle bounds.

Ymir captures at 900 and 910 fields both pass, with 36 and 37 published
images respectively. The alternating synthetic face uses both indexed-8 and
LUT4 textures; the two captures check successive formats. Evidence is in
`assets/imported/parity-ordered-quad-900` and `parity-ordered-quad-910`;
the test ELF SHA-256 is
`672327ca3b6667a495908825c48cf501c451b31e521d3c187aecc31859e733cc`.
The synthetic options are off again in the rebuilt candidate. This establishes
composition for this reference shape; it does not establish general VDP1 face
coverage or full particle fidelity in actual fights.

### Reject only occupied particle pixels (2026-10-28)

The experimental ordered compositor now ignores an allocated particle tile
when its nonzero pixels do not intersect the later face's bounding rectangle.
This avoids rejecting an entire effect for transparent texels or pixels already
cleared by an earlier face. The rectangle remains conservative: an intersection
still requires supported face geometry and texture, and unsupported cases
still clear the whole overlay. Host tests cover an empty pixel beside an occupied
one, a fully cleared tile, out-of-range indices and re-accumulation.

A 1,201-field Ymir fight with the same input script as the earlier ordered
control reports first_error 0, 200 successful compositions, nine with particles
(42 particle additions), three unsupported-face fallbacks and zero masked face
tiles. It therefore does not demonstrate a reduction in fallbacks for this
particular fight, nor establish real-game face coverage. Evidence is
`assets/imported/parity-ordered-occupied`, ELF SHA-256
`8f13ec82d10f930beb2403e32b5f4f4ec879c2e9cc817973421852ed83726542`.
The added SH-2 code exhausted the existing High WRAM margin; in this opt-in
configuration HUD descriptors and snapshot fields move to Low WRAM, preserving
the existing stack-reserve linker check. The candidate links successfully.

### Rotated-triangle investigation (2026-10-28)

The rotated-triangle regression now checks all eight independently authored
UV captures, rather than just the first: each must remain rejected without
emitting a mask until its sampling is verified. The captures contain 330
positions and include both positive and negative edge directions. In three
of the eight cases the longest absolute edge component differs from the
largest signed vertical component currently used by the forward-only model;
one of those even has a negative largest vertical component. This makes a
simple extension of the current vertical loop insufficient.

Inspection of the pinned Ymir software renderer identifies a broader model to
investigate: both side edges advance against a common major-axis count, then
each cross-line can advance along either screen axis, with direction-dependent
rounding and independently stepped texture coordinates. This observation is
about that emulator's implementation, not a claim about Saturn hardware. The
next implementation must be developed independently and compared against all
captured UV points, including overwrites where lines overlap, and against new
shapes before enabling those faces in the experimental compositor. No Ymir
source code is incorporated. The updated quad coverage, particle tile and
mixed-order host tests pass.

### One-hot transparent texture planes (2026-10-28)

Opaque U/V screenshots record only the final texel at each pixel, while a
transparent texel leaves earlier writes visible. For a slanted authored quad,
`T3_VDP1_COVERAGE_PLANES` therefore draws sixteen independently transparent
LUT4 textures: each isolates one U or V coordinate. The additive stage probe
is disabled during capture. `scripts/capture_vdp1_texture_planes.py` extracts
399 tile masks into `tests/fixtures/vdp1_texture_planes.json`; these are
synthetic patterns and contain no game data. All 16 planes match the current
bounded quad coverage implementation exactly in `saturn_vdp1_quad_coverage`.
This extends evidence for transparent overlap at one shape, not general quad
or rotated-triangle parity. Ymir capture: `assets/imported/parity-vdp1-planes`.
The ordered-particle candidate was rebuilt with the synthetic probe off.

### Experimental rotated raster and transparent triangle (2026-10-28)

A host-only experiment in `scripts/experiment_vdp1_rotated.py` interpolates
both sides by a shared major-axis count and draws cross-lines in either screen
direction. It reproduces every recorded position and final U/V value for two
of the eight small authored triangles. The other six still disagree, so this
is not connected to fighter masks or enabled on Saturn.

A fresh Ymir probe isolated all eight U and eight V coordinates of the first
rotated triangle using independently transparent LUT4 textures. Its 63 captured
tile masks are stored in `tests/fixtures/vdp1_triangle_planes.json`; the
experimental host sampler matches every plane, including overlap from earlier
writes. The capture, with the additive layer off, is
`assets/imported/parity-vdp1-triangle-planes`, first_error 0, test ELF SHA-256
`6a59ac2a5df77101311810cebcd8d3cb53b3871cd175b6eb976fc8cf9dc25fd3`.
CTest `saturn_vdp1_rotated_experiment` preserves the two matching opaque cases
and all transparent planes while reporting the six outstanding mismatches.
This is finite emulator evidence, not hardware or full-scene parity. The
synthetic options were turned off and the ordered-particle candidate rebuilt.

### Arbitrary-direction raster experiment (2026-10-28)

Direction-dependent rounding and cross-line coverage were added to the
independently written host prototype. It matches all 6,678 recorded opaque
pixel/UV samples across 24 authored shapes, plus 32 one-hot transparent
planes from one rotated triangle and one quad. Eight of the triangles are
newly authored holdout shapes, captured only after choosing the initial rules:
`assets/imported/parity-vdp1-triangles-new`, first_error 0, ELF SHA-256
`f698a86006a1fbc0b265b459cec1b61283086176365715ac8d4606d5ea2d5ff5`.
Their synthetic UV fixture is `tests/fixtures/vdp1_small_triangles_new_uv.json`.

`t3_face_general_tile_coverage` implements the same bounded experiment in C.
Its host test compares 772 masks from those captures, including the transparent
planes; the Python oracle additionally checks final opaque U/V values. The C
routine is not yet used by the compositor, and needs SH-2 cost measurements,
additional geometry/transparency cases and physical hardware evidence. The
previous bounded routine and atomic fallback remain in use. The synthetic
capture flags are off in the rebuilt fight candidate.

### General masks in mixed-order compositor (2026-10-28)

`T3_PARTICLE_GENERAL_MASK_PROBE` adds the bounded arbitrary-direction C path
as a fallback after the previous quad path in the experimental particle
compositor. Unsupported inputs still clear the complete overlay. To fit the
stock memory map with the same eight-entry pose cache, this configuration moves
the wall-depth scratch to Low WRAM and reduces the HUD upload buffer from 32
KiB to 30 KiB; loading checks its actual size. The measured private HUD and
particle files are 24,428 and 2,948 bytes respectively. These memory choices
are experimental and may reject a larger HUD file.

A native synthetic particle/rotated-triangle/particle test compared 8,192 map
words and 256 pixel words against the separately captured one-hot triangle
mask. Both indexed-8 and LUT4 textures were exercised: 508 general tile masks,
256 indexed-8 and 252 LUT4, first_error 0. The checker is
`scripts/check_saturn_ordered_mask_triangle.py`; capture
`assets/imported/parity-general-triangle-selftest`, ELF
`d4675ff9c494ebffd6dbddf6bccf73f1c7ee099486a4a05234c872080454f3e7`.

In the 1,201-field scripted Ymir fight, the experimental ELF
`b9767cd496839494883e46ea9adffd9d26e54e509b4fde80def59eb6ed0306a3`
processed 29 general face tiles and one previously supported tile, with zero
face failures. Three images still fell back because particle accumulation
failed; nine successful images contained 45 particle additions. The earlier
occupied-pixel control instead recorded three face failures and no face-mask
tiles. Candidate/control reached 200/201 submitted images and 771/774 logic
ticks; both sampled 65 ms work time, one hit and first_error 0. Different
render cadence means these are not paired per-face comparisons or a benchmark
of every scene. Capture `assets/imported/parity-general-fight`. This remains
opt-in: emulator coverage is finite, particle mapping is diagnostic, hardware
and full fight parity are unproven, and particle failures remain to investigate.
Five relevant host tests pass.

### Sparse particle tile allocation (2026-10-28)

The last failed quad in the prior 1,201-field run encountered 39 allocated
cells after 15 particle additions; its bounding box required more cells than
remained in the 40-cell limit. The accumulator now checks actual sampled
palette values before reserving a new cell. A completely transparent sprite
allocates none, and a partially transparent sprite allocates only cells that
receive nonzero colors. Existing cells and visible black (`0x8000`) retain
their previous semantics. The capacity preflight still precedes mutation, so
an actual overflow leaves the canvas unchanged. Host tests cover transparency,
one-quadrant allocation and full-capacity rejection.

With the same 1,201-field Ymir input script, the sparse candidate
`63164627fac539002b9f5438e6859707f467c5a761a0597e7c0709122e439dcb`
reported one particle failure instead of three, 11 successful particle images
instead of nine and 91 particle additions instead of 45. The remaining failure
occurred with all 40 cells allocated, after 24 additions. Both runs submitted
200 images, recorded one hit and first_error 0, and sampled 65 ms work time;
they advanced 768 and 771 ticks, so the frames and poses are not exactly paired.
The mask path still processed 29 general tiles. Candidate evidence is
`assets/imported/parity-general-sparse`, control `parity-general-fight`.
These emulator measurements do not establish hardware performance or full
particle fidelity, and the 40-cell limit still discards some effects.

### Reclaiming cleared tiles and increasing the experimental pool

The accumulator can compact cells cleared entirely by later fighter faces when
space is needed. It first verifies the new sprite will fit after compaction;
otherwise it leaves the entire surface unchanged. Tests cover reusing four
cleared cells, preserving occupied cells and rejecting a genuinely full pool.
In the scripted fight the 40-cell result was unchanged: the remaining failure
had no recoverable capacity. That intermediate capture was removed.

The existing Low WRAM boot-upload scratch union has room for two 52-cell
particle surfaces and the stage line table; its compile-time size assertion
continues to enforce that bound. The experimental limit was raised from 40 to
52, without increasing that union. The extra bookkeeping needed slightly more
High WRAM; the general-mask build's checked HUD upload capacity is now 30,464
bytes (the measured HUD file is 24,428 bytes). Other builds retain 32 KiB.

In the same 1,201-field Ymir fight, the 52-cell candidate
`105edcce2dc785fa04de845b721fc4cc10b6aa721dcb5f74a73661687d4be668`
reported zero particle/face fallbacks, 12 images containing 117 particle
additions, and 29 general mask tiles. The 40-cell control had one fallback,
11 images and 91 additions. Candidate/control submitted 199/200 images,
advanced 767/768 ticks, each had one hit and first_error 0, with 65 ms sampled
work time. These are not perfectly paired frames and do not prove all effects
fit or run at the intended rate. Captures are `parity-general-52` and
`parity-general-sparse`. The host tile tests also pass when compiled explicitly
with the same 52-cell limit as the Saturn experiment. Full hardware, visuals,
and performance validation remain pending.

### Incremental cross-line stepping (2026-10-28)

The experimental C rasterizer now advances cross-line X, Y and texture U by
integer remainder steps rather than dividing for every pixel. It preserves the
half-down position rounding and the reduced-texture center sampling, including
single-pixel lines. The captured-mask tests still pass. A deterministic 2,000
additional arbitrary-geometry and binary-transparency comparison checks the
incremental C implementation against the independent direct-formula Python
model; this checks equivalence, not new Saturn hardware behavior.

The 1,201-field Ymir fight capture `assets/imported/parity-general-incremental`
(ELF `135f6d11ae96159099c7eefd5fe438905160bef652e79a93fd8be57d3463b1d5`)
matched the preceding `parity-general-52` sample's endpoint counters: 199
submitted images, 767 ticks, 117 particle additions across 12 images, 29
general face tiles, no fallback, one hit, first_error 0, and sampled 65 ms
work time. These coarse endpoint counters show no measurable frame-rate gain;
they do not isolate raster execution time. The application binary was 200 bytes
smaller. Five relevant host tests pass. The general path remains experimental.

### Rejecting cross-lines outside the particle tile (2026-10-28)

The general face mask skips cross-lines whose endpoint bounds do not intersect
the target 8×8 tile. Diagonal fill pixels also lie within those bounds, so this
preserves coverage; 772 captured masks and 2,000 deterministic randomized
comparisons against the direct-formula model pass. The unrelated bounded quad
and rotated experiment tests also pass.

For an isolated SH-2 timer sample, `T3_PARTICLE_MASK_TIMING` accumulated
free-running counter ticks around each general mask call. A 1,201-field Ymir
control with `T3_GENERAL_MASK_NO_LINE_CLIP` recorded 3,224 ticks and the
candidate recorded 2,777, both for 30 successful general tile masks, 12
particle images, 117 particle additions, one hit, no fallbacks and first_error
0. They submitted 199 and 200 images and advanced 767 and 768 ticks, so the
faces are not proven identical and the 13.9% difference is a scenario-specific
measurement, not a general benchmark or frame-rate claim. Captures:
`assets/imported/parity-mask-timing-control` (ELF
`c1a8dd1e6e9f83b3021edf75662009ddc3e2577899561bc199d7e953cd2266cf`) and
`parity-mask-timing-candidate` (ELF
`21772199c3a5a78b2334b9dbff66ee2cdd2c392a21ab70afe467b0d6474c7b31`).
The timing and control switches were turned off in the rebuilt candidate.

### Incremental edge and vertical texture stepping (2026-10-28)

The arbitrary-direction mask now advances the two side edges and texture V by
integer remainders rather than dividing repeatedly for each cross-line. It
maintains direction-dependent tie handling, and advances even for cross-lines
rejected outside the target tile. The prior captured masks and 2,000 randomized
comparisons pass; explicit point and line degeneracies were added. Five related
host tests pass.

With the same timer instrumentation and 1,201-field Ymir input, the previous
line-clipped control (`parity-mask-timing-candidate`) reported 2,777 SH-2 timer
ticks around 30 successful general mask tiles; the incremental-edge candidate
reported 1,296. Both samples submitted 200 images, advanced 768 ticks, recorded
12 particle images, 117 additions, one hit, no fallbacks and first_error 0.
That 53% decrease is for these measured calls, not a full-frame speedup or proof
that every individual face matched. Sampled full-frame work time was 65 ms in
both runs. Candidate capture: `assets/imported/parity-mask-timing-edges`, ELF
`c5be313edce503570b70b1ff81883f0d937263f059e9f3c0fd0d4ada0c97e01c`.
The timing option is off in the rebuilt fight candidate; physical hardware and
full particle fidelity remain unverified.

### Shared framebuffer mapping for fighters and particles (2026-10-28)

The experimental original-fighter projection used `(x-192)*320/368` and
`(y-240)*224/480` around the Saturn screen center, while particle quads and
HUD used the original displayed 2D rectangle `(0,20,368,448)`. Thus an
identical original projected point, such as (192,240), became (160,112) for a
fighter but (167,110) for a particle. Their relative placement and occlusion
could not be consistent.

`src/game/psx_viewport.h` now provides the existing 2D rectangle mapping to
both paths. The original fighter vertices map through it before conversion to
the SDK's centered coordinates; particles and HUD use the same helper. This
changes only the original-projection experiment, not the SDK projection path.
It follows the original display table evidence described in `ORIGINAL_HUD.md`,
but still requires direct PS1 rendered-image comparison, including stage
alignment and sampling; it is not proof of pixel parity.

A 1,201-field scripted Ymir run (`assets/imported/parity-shared-viewport`, ELF
`afb27f233e8564ebdb330e085e46d1e6741e1a62e809d29403b219f0fc42e41c`)
submitted 203 images, advanced 766 ticks, recorded one hit, 12 particle images,
121 additions, 24 general face-mask tiles, no fallback and first_error 0.
The preceding `parity-general-incremental` sample submitted 199 images over
767 ticks with one hit; different mapping and cadence make it an imperfect
performance control. Both sampled 65 ms frame work. Four relevant host tests,
including original-code particle projection, pass. This does not prove the
visible alignment of all poses or physical Saturn output.

### Horizontal stage origin in the original-projection experiment

The stage's SDK camera still used a horizontal center of x=160, while the
shared original framebuffer mapping places the GTE origin at x=167. In the
original-fighter-projection configuration, panorama line scroll now samples
seven screen dots earlier, with cylinder wrapping; the VDP2 floor's X start is
shifted by the same seven dots. Other projection configurations retain their
previous mapping. `T3_STAGE_NO_VIEWPORT_SHIFT` is a diagnostic control only.

Matched 900-field Ymir captures have byte-identical camera snapshots and 479
simulation ticks. The candidate's floor parameter differs only in X start by
-7.0 in 16.16, and `scripts/check_saturn_stage_horizontal_shift.py` verifies
1,120 selected panorama/floor pixels against the control shifted seven pixels.
Evidence: `parity-stage-shift-control` ELF
`afb27f233e8564ebdb330e085e46d1e6741e1a62e809d29403b219f0fc42e41c`
and `parity-stage-shift-candidate` ELF
`b5248dcf3d2d996a356a5fba2a6a15674ac17975fc69d9cb784d5fd71c7f22d2`.
The scripted fight (`parity-stage-shift-fight`) ran 1,201 fields, 203 submitted
images, 766 ticks and one hit, with no particle/face fallback and first_error
0; its sampled work time was 65 ms. Four stage/particle host tests pass.

This corrects the horizontal origin only. The stage camera still uses the
older 480-line vertical framing, while the mapped fighter/particle viewport
uses the 448 visible lines; direct PS1 image comparison, vertical perspective,
and physical hardware validation remain open. The seven-dot shift is an
integer approximation of the mapped original center, not pixel-parity proof.

### Experimental original vertical stage viewport (2026-10-28)

`T3_STAGE_ORIGINAL_VERTICAL_PROBE` adjusts the VDP2 stage to the same
448-visible-line mapping as the original-projected fighters and particles.
The panorama uses a 15/14 vertical focal adjustment and samples two Saturn
lines lower. Its new offset API retains the original zero-offset wrapper; the
independent floating-point backdrop test covers multiple synthetic cameras,
pitches, rotations, grouping steps and both offsets.

The floor uses the existing SDK floor projection with a 228-line virtual view:
its focal row is scaled 20/19 and lines 4–227 are displayed, giving center
line 110 and the intended original focal scale. Its vertical parameter origin
is advanced by four line increments. To avoid placing 1.8 KiB of temporary
arrays on the Slave's small SDK stack, far depths and generated coefficients
share one 228-word table: the SDK reads each far value before overwriting that
same element. The optional build adds four words to each stage slot. It also
reduces the checked HUD upload buffer to 29,696 bytes to fit code in High WRAM;
the measured HUD file is 24,428 bytes. A host test compiled against the local
LibSaturn `floor.cpp` checks aliased against separate arrays and compares 16,281
floor points to independently calculated camera rays, with maximum error
0.02135 map units. Four relevant host tests pass.

In a 1,201-field scripted Ymir run, `parity-stage-vertical-final` (ELF
`f4ab2dd038f1794e6678b7149930fc6b80fd5c75eb8482a473656f2f261ecf7d`)
submitted 203 images, advanced 767 ticks, recorded one hit and no particle or
face fallback, with first_error 0 and a 64 ms endpoint work sample. The prior
horizontal-only control `parity-stage-shift-fight` submitted 203 images, 766
ticks and sampled 65 ms. A second diagnostic forced stage preparation onto
the Slave (`parity-stage-vertical-slave-inplace`, ELF
`726c7ecf4bba19639365990de059f123b097be030a597d61281c455b37b5df32`),
completed the same 1,201 fields without a reported error. The forced-Slave
option was turned off in the rebuilt candidate. The simulation-stack watermark
reported by the probe does not cover the SDK Slave stack used for stage work;
this run therefore is not a proof of stack headroom or hardware stability.

This remains opt-in and does not prove pixel parity: the backdrop is a fitted
cylinder, the floor still lacks original shading, and direct PS1 image and
physical hardware comparisons remain required.

### Original PS1 screen and Slave stack samples

PCSX-Redux ran the USA disc with `scripts/pcsx_redux_view_capture.lua`, using
pad-only navigation to a neutral Paul mirror fight. At 360 cumulative Vsyncs
in fight state the observer paused on guest fight tick 447; the original
368×480 screenshot is `assets/imported/parity-ps1-view/screen.png`, with the
script and emulator log. The local emulator SHA-256 is
`66eaa03dbf01877a385a4c20f1823d35dbd51549b6f97a16f33ee321d1beb946`;
the imported USA EXE SHA-256 is
`fbda8b68e5799dbef4af39a161783bc670c15b0aa0e87dce65e210717da19b8c`.
The observed pair is Paul/Paul, while the Saturn sample is Paul/Law, and their
camera and pose have not been matched. They cannot support a stage or fighter
pixel-parity claim.

`scripts/check_ps1_saturn_hud_alignment.py` crops PS1 rows 20–467 and maps
368×448 to 320×224 with nearest-neighbor sampling. At row 14 the original's
full cyan fill spans are 12–142 and 177–307; the neutral Saturn capture
`parity-stage-shift-candidate` gives 11–142 and 178–309. The largest bound
difference is two pixels. This directly confirms approximate full-bar
placement for these captures; it does not compare damaged bars, colors, text,
or all display modes.

`T3_STAGE_STACK_PROBE`, used with forced Slave stage preparation, sampled
r15 after the stage and panorama functions' stack frames were allocated. In a
1,201-field Ymir run (`parity-stage-slave-stack`, ELF
`334ce9ced7122a786be92eb7608d8ff377805c4d3eb2a479fbd833666615e0f4`),
first_error was zero and the lowest sampled values were `0x06000e78` in stage
preparation and `0x06000bd4` in panorama preparation. The latter is 980
bytes above the SDK's Slave vector-table end at `0x06000800`. The SH-2
compiler's `-fstack-usage` reports 244 bytes for stage preparation, 676 for the
panorama function and 168 for the SDK floor function under their recorded build
flags. These samples do not measure every nested library call or interrupt, and
the observed remaining space is not a physical-hardware or worst-case stack
proof. The stack and forced-Slave probes are off in the rebuilt candidate.

### Matched original/experimental mirror snapshot

The neutral USA PS1 capture above was reproduced byte-for-byte, with 2 MiB
paused RAM retained as `parity-ps1-view/paused-ram.bin` (SHA-256
`185b1d13a1856255aa9fef2d19cfbbe0a14007a06fd9adf3e246bc2d1d39bc26`).
A temporary Saturn Paul/Paul build used both Paul models and one shared Paul
motion/camera bank, forced X on both controllers at startup to select entry
slot 0, froze at local tick 51, and hid the diagnostic frame/benchmark text.
The experimental viewport and general particle paths were enabled; sound was
disabled in this mirror-only build. The private image and build configuration
are retained in `assets/imported/parity-mirror-view-final`; ELF SHA-256
`e8efcc5849e50f76bad4ab42ff2c74d23966b89c909fbce8152bd9085a9fe6f2`.
The temporary build and superseded trial captures were removed.

`scripts/check_ps1_saturn_mirror_view.py` checks the paused original against
Saturn diagnostics and the camera used by the stage. Both fighters' roots are
(-1000,0,0)/(1000,0,0), move slots 0/0 and pose frames 51/51; the original
camera eye is (0,-966,-7862), H=500, and the Saturn eye and camera axes
exactly match the original view matrix under the port's 96-unit axis mapping.
The Saturn pose snapshot reports local tick 51; the PS1 global counter is 447,
so these are different clock domains. The guest reports first_error 0.

The original screen is `parity-ps1-view/normalized.png`; the Saturn image is
`parity-mirror-view-final/screen.png`. These are a more meaningful visual
comparison than the earlier unrelated fight, but do not establish identical
full state, GPU presentation phase or pixel parity: original P1 wears a black
costume while the diagnostic Saturn mirror uses the same red asset for both,
shadows/lighting/floor remain approximations, and the PS1 capture was taken
at Vsync. The stage and fighter silhouettes visibly differ and need direct
quantitative investigation. This is one matched state, not a complete 1:1
validation or hardware test.

### Matched PS1 floor and panorama measurement (2026-10-28)

The saved original screenshot and mirror capture both pass
`check_ps1_saturn_mirror_view.py`. A second fixed-tick mirror build disabled
only the experimental horizontal stage shift; its observation and screenshot
are `parity-stage-mirror-control`, ELF
`a7b7baa2b4c95be46e0cef3008ce6fb70ad255a5a5d2ce6bd35b2a980a59620b`.
The temporary control build was removed. Both screenshots have the same
verified camera eye and axes, fighter roots and move/pose counters; the PS1
costume, GPU phase, lighting and rasterization are not matched.

`scripts/compare_ps1_saturn_stage.py` crops the original visible area, scales
it with nearest-neighbor and reports raw RGB mean absolute error on specified
rectangles. For panorama rows 35–59, columns 30–289, shifted/unshifted error
was 15.272/15.744 per channel; for near-floor rows 190–217 it was 13.130/12.300.
Thus the shift improves that panorama metric but worsens the near floor for
this single frame. The measurements do not isolate perspective, original GPU
sampling, color quantization or absent distance shading, and do not justify a
global choice of horizontal offset. The horizontal shift remains experimental;
the floor discrepancy must be investigated across matched cameras and poses.

The vertical experiment's four extra coefficient words are scratch storage;
VDP2 publication now copies exactly the 224 displayed words. A 1,201-field
Ymir fight (`parity-stage-coeff-224`, ELF
`b0e5d8f0d09465dbdf2b34d3c4cc9a11cf2ecabfadcb7e457b1703f65d92e564`)
reported first_error 0, 203 images, 767 ticks and one hit. Three relevant host
tests pass. This does not establish full-floor parity or physical hardware
behavior.

### Why the matched floor differs (2026-10-28)

The USA RAM snapshot contains the floor's two 100-packet `POLY_GT4` buffers.
`scripts/audit_ps1_floor_affine.py` maps the documented Japan Rev.1 addresses
through the USA layout, verifies the private executable hash and the standard
10×10 grid, and extracts buffer 0. It retains only packets whose four captured
screen vertices agree within two PS1 pixels with the current recorded camera
and the documented 1,800-unit grid; clipped or stale packets are excluded.
No game-derived packet or texture data is added to git.

The PlayStation interpolates the quad's two triangles affinely in screen space;
the Saturn RBG0 floor instead follows perspective-correct ground rays. At the
60 on-screen triangle centroids in the filtered snapshot, their calculated UVs
differ by a mean of 2.6794 texels and a maximum of 5.8165 texels; all 60 exceed
one texel and 42 exceed two. This is a geometric model comparison, not a
pixel-by-pixel PS1 GPU emulation. It shows that a global horizontal shift
cannot make the two floor mappings identical. The captured Gouraud vertex
colors also show a separate issue: the ten sampled near-floor triangle
centroids all have brightness 128, while 50 farther centroids range from 88.33
to 126.67 (mean 107.95). The current unshaded bitmap therefore especially
misses the distance fade; its near-floor discrepancy cannot be attributed to
that fade alone.

The data are from one paused Vsync and the buffer was selected by comparison
to the current camera, so this is finite evidence rather than a synchronized
GPU packet trace. A 1:1 floor needs a renderer that reproduces the original
triangle interpolation and per-vertex shading, or another independently
validated Saturn approximation; the present RBG0 planar mapping cannot meet
that requirement through viewport offsets alone.

### VDP1 Gouraud shading feasibility (2026-10-28)

The VDP1 distorted-sprite API supports a four-corner Gouraud table with an
RGB CLUT, but its color correction is additive. The PS1 floor's textured
Gouraud modulation is multiplicative, so the same correction cannot in
general reproduce all colors in the tile. The audit can now read the private
generated `PAUL.STG` and compare these operations for the 60 previously
validated triangle centroid brightnesses. It uses the actual tile's palette
frequencies and rounded 5-bit channel values. Leaving the tile unshaded has
an average modeled absolute error of 1.2254 channel levels; independently
choosing the best additive correction per channel and centroid reduces that
to 0.1707 levels. This is an optimistic lower bound for that model, not a
hardware result: a real polygon must interpolate a shared set of corner
corrections, its sampling is spatially correlated with the texture, and PS1
and Saturn rasterization and quantization can differ.

Run the audit with `--stage build/parity-bitmap-floor/generated/private_stage/PAUL.STG`
alongside the `--ram` and `--exe` arguments described above. It reports only
aggregate measurements and does not commit game art. The result supports a
VDP1 polygon-floor prototype as a potentially useful approximation, but does
not establish 1:1 shading or that its distortion, clipping, ordering, and
performance match the original. In particular, replacing RBG0 with VDP1 on
the strength of this color estimate alone would be premature.

### VDP1 polygon-floor probe (2026-10-28)

`T3_VDP1_FLOOR_PROBE` is an opt-in experiment for the Paul bitmap-stage
configuration with original fighter projection and without native command
batching. It loads the privately generated floor tile and RGB CLUT into VDP1,
projects the documented 10x10 grid with the current original camera, and
draws each surviving tile as an opaque distorted sprite behind the fighters.
RBG0's floor is hidden in this experiment. Texture coordinates and color
lookup come from the private stage file; no original art is checked in.

The SH-2 build and injected Ymir run completed. Against the USA PS1 paused
capture, the Saturn snapshot has the same fighter roots, move frames and
camera eye and axes (`scripts/check_ps1_saturn_mirror_view.py`). At the same
800-field probe duration, the earlier RBG0 control rendered 156 overlap
frames and this VDP1 experiment 145; this is a workload observation, not a
controlled or physical-hardware frame-rate measurement. The same descriptive
RGB error metric as above gives far-floor 13.648 for RBG0 and 13.088 for VDP1;
near-floor 13.130 and 13.426 respectively. The images and observations are
in `assets/imported/parity-mirror-view-final` and
`assets/imported/parity-vdp1-floor-probe`; the probe ELF SHA-256 is
`85aacfb8e5e0c99c4b97274710f4376189b0336b9cbc5573ab4f47de17a0431d`.

This first prototype does not implement the PS1 floor's vertex fade or its
precise visibility and depth rules. At the time of this run it skipped a quad
when any corner was too close to the camera, and did not account for
the original grid's steep-camera displacement. VDP1's quad rasterization is
not assumed to match PS1's two textured triangles; particle ordering against
the new VDP1 floor also remains unresolved. It is therefore left disabled
by default. The mixed image results do not establish an improvement or 1:1
parity across cameras, and the lack of CD and physical-hardware validation
still applies.

### Floor distance-shading probe (2026-10-28)

`T3_VDP1_FLOOR_SHADE_PROBE` adds an optional Gouraud approximation to that
polygon-floor experiment. The private stage builder extracts the original
1,024-byte USA fade table and stores it in unused space in `PAUL.STG`, along
with additive RGB corrections optimized for the tile's palette frequencies.
The loader checks a format marker before using the extra data. The original
floor brightness uses the projected vertex depth and the camera's horizontal
distance from the fighter midpoint, including the original `SquareRoot0`
lookup. VDP1 still applies additive color offsets, so the output is an
approximation to the PS1's multiplicative texture shading.

For the 42 geometrically validated quads in the existing paused USA snapshot,
the new audit's independent reconstruction matches all 168 captured PS1
vertex brightness values exactly. Run `scripts/audit_ps1_floor_affine.py`
with the previous `--ram`, `--exe` and `--stage` arguments and add
`--sqrt assets/imported/tekken3-usa/pose-tables/psx_body.bin`. This validates
those source brightness values only, not interpolated or displayed pixels.

In the same matched Ymir scene, on rows 142–157 and the two lateral strips
`x=0..64` and `x=255..319`, the mean absolute RGB error against the PS1
capture fell from 27.4324 without the approximation to 17.3817 with it.
The previously reported floor regions below row 158 were unchanged in this
scene. Over the 800-field probe, overlap frames were 145 and 141 respectively;
this is not a controlled hardware timing benchmark. The new evidence is
`assets/imported/parity-vdp1-floor-shade` (ELF SHA-256
`78d71318d188093a653b3cc2b47934b7fdce30f2ca646ea040c1dc0a80b356e2`).
The same camera, fighter state and pose were checked against PS1 RAM. The
result is one image and does not resolve clipping, texture interpolation,
particle ordering, other cameras or physical-hardware performance.

### Floor visibility and near-plane behavior (2026-10-28)

Reviewing `FloorDrawGrid` and its `FUN_8004A5FC` GTE projection shows that
the original does not reject all vertices closer than H/2. The GTE saturates
the divide there; the floor routine rejects a quad only when a projected
vertex has zero depth. It then independently requires at least one X in
`[0,368)` and at least one Y in `[0,480)`; these need not belong to the
same vertex. The previous probe's H/2 cutoff and viewport bounding-box test
therefore rejected some original-eligible geometry. The opt-in probe now uses
the original depth and screen tests, prior to viewport conversion. It still
uses VDP1 distorted sprites and does not promise identical PS1 rasterization.

`stage_floor_visibility` covers zero and positive near depth, the separate
vertex tests, negative coordinates and both screen boundaries. The rebuilt
SH-2 program ran in the matched Ymir scene with no reported guest error;
its screenshot was identical to the previous shaded probe, which means this
one pose does not exercise a visibly affected polygon. Evidence is retained
in `assets/imported/parity-vdp1-floor-visibility`, ELF SHA-256
`59666b2a5f6297c2735791afeeaf3655d4a7076a681c636319742443d3bce0ba`.
Different and steep camera positions still require matched runtime captures.

### Floor grid centre under camera changes (2026-10-28)

`src/game/stage_floor_center.c` adapts the original normal-stage FloorDraw
centre calculation, including the upward-looking visibility condition, the
one-tile shift at negative pitch and the ground-intersection adjustment when
the camera is sufficiently high and looks down. The VDP1 floor probe now uses
that centre before snapping to the original 1,800-unit grid. Singular or
out-of-range arithmetic returns an unsupported status, for which the probe
keeps its previous midpoint fallback; these cases are not represented as
original-accurate.

`scripts/verify_stage_floor_center.py` calls Japan Rev.1 `0x800490F0` in the
original-code CPU harness and captures its argument to FloorDrawGrid, then
compares it with the C evaluator. It first checks that both private lookup
tables equal those in the Japanese executable. With seed `0x49290`, 8,671
of 10,000 camera/fighter-centre inputs matched exactly; 1,329 singular or
overflow cases were excluded by the C implementation. These are synthetic
bounded cases, not a proof for all camera inputs. To reproduce, build
`tekken3_fight` in `build/host`, compile `tools/stage_floor_center_eval.c`
against its static library, then invoke the verifier with the evaluator and
the private USA trig and body tables.

The Saturn build and an 800-field Ymir run also passed for the matched neutral
scene. Its screenshot was identical to the preceding visibility probe; that
pose does not test a visibly displaced grid. The new capture is
`assets/imported/parity-vdp1-floor-center`, ELF SHA-256
`562d7f2fd65b5f0dc9ed47c7c68c030d8be0548bc35ed0d38ad614a825851d14`.
Actual steep-camera Saturn/PS1 image comparison and the unsupported arithmetic
cases remain open.

### Zero cosine and centre edge cases (2026-10-28)

The earlier 1,329 exclusions were caused by an incorrect C guard: the cosine
in the ray calculation is a numerator, so zero is valid. Removing that guard
matches all 10,000 of the previous random cases against the original CPU.
The verifier now adds 784 cases at pitch, height and yaw-octant boundaries:
10,756 of the 10,784 total match, and 28 remain unsupported. Those 28 are
the synthetic pitch 1808 cases with heights -8000 and -1721, near the
positive-sine boundary, where the intermediate squared ray exceeds the C
port's supported signed range. The PS1 code is still executed for those
cases but its result is not claimed as reproduced; the existing fallback
remains. A 90-degree downward pitch (1024) is among the tested boundaries.

The rebuilt SH-2 candidate ran the matched neutral scene in Ymir without a
reported guest error and produced the same screenshot as the preceding probe.
Evidence is in `assets/imported/parity-vdp1-floor-center-zero`, ELF SHA-256
`ba1a750648190eb40e791e481506c3818922790680a9f92bc496c7c00afcb37b`.
This neutral screenshot does not exercise the corrected zero-cosine case;
the original-code oracle supplies that specific evidence.

### Wrapped floor-centre arithmetic (2026-10-28)

The original multiplies and adds in 32-bit MIPS registers before calling
SquareRoot0. The C port now preserves the low 32 bits for the squared ray,
the Q12 products and the final line intersection, instead of rejecting every
mathematical square larger than a signed 32-bit integer. All 10,784 cases
from the previous audit now match, including its 28 formerly excluded cases.

The audit additionally samples 1,000 near-horizon camera angles. Across all
11,784 inputs, 11,296 match original execution and 488 remain unsupported
because the wrapped SquareRoot0 input is negative. In that case the original
indexes memory before the 192-entry lookup table; this memory includes code
and differs between Japan Rev.1 and USA, so a Japan-only match cannot
establish the USA behavior. The probe retains its midpoint fallback for these
unsupported values. No game code or lookup data was added to version control.

The rebuilt SH-2 candidate ran the neutral Ymir scene without a guest error;
its screenshot equals the preceding control. The evidence is
`assets/imported/parity-vdp1-floor-wrap`, ELF SHA-256
`62d71e2267eef19fb25473d40e58875951d052194d2285a417b57cbdb5be90cc`.
The changed overflow paths are verified by the original-code CPU harness,
not by this neutral runtime screenshot.

### USA SquareRoot0 overflow behavior (2026-10-28)

The private Paul-stage builder now appends a marked sector containing the
640 bytes immediately before USA SquareRoot0's table. The experimental
VDP1 floor loads that window at boot and uses it when a wrapped negative
input makes the original routine read before the table. These executable
bytes are generated from the hash-qualified private USA disc import; they
are not in the source repository. The extra sector is checked by the stage
header and increases the private stage file by 2,048 bytes.

The centre verifier can now execute the USA executable directly using the
documented USA address map (`--usa-exe`). It uses the matching executable's
preceding window, and validates its sine and normal square-root tables before
running. All 11,784 generated cases matched USA original code, including the
488 previously unsupported near-horizon cases. The same 11,784 also matched
Japan Rev.1 when using the corresponding Japanese bytes. In the 1,000-case
near-horizon sample, 22 USA and Japanese results differ, confirming that the
version-specific window matters. This is finite CPU-oracle evidence, not a
proof for arbitrary camera values or a pixel comparison.

The resulting SH-2 candidate booted through the Ymir injection harness with
no guest error and produced the same matched-neutral screenshot as the prior
control; its overlap count was 140 versus 141 in that run. Evidence is
`assets/imported/parity-vdp1-floor-usa-sqrt`, ELF SHA-256
`2d7944b7b01d3ee0fc4ad9c91859d0a6d48c243b23a7c377056e7e647e5652a7`.
The extreme camera behavior remains untested visually on Saturn and PS1.

### Floor projection with translated grid origin (2026-10-28)

`FUN_8004B05C` transforms the grid centre into the camera translation first;
`FUN_8004A5FC` then projects the floor's signed-16 local vertices. The old
probe instead transformed each combined world coordinate, which performs the
fixed-point rounding in a different order when the grid centre is nonzero.
The probe now translates its original-format view matrix once and projects
the local grid vertices through the existing GTE-compatible path.

`scripts/verify_stage_floor_projection.py` executes both original Japan Rev.1
functions on synthetic matrices and grid centres and compares every x, y and
depth with `tools/stage_floor_projection_eval.c`. In 200 deterministic cases,
all 24,200 vertices match; 8,966 vertices differ when using the previous
combined transform. These synthetic matrices include values outside normal
fight cameras, so this is a numerical fidelity check rather than a claim
about the number of visibly affected game pixels. Independently,
`scripts/audit_ps1_floor_affine.py --projection-eval <evaluator>` compares
the same C path with the 42 filtered USA paused packets: all 168 x/y pairs
match. That capture's grid centre is zero and cannot exercise the corrected
translation rounding.

The SH-2 build and matched-neutral Ymir run passed with no reported guest
error. Its screenshot equals the preceding control; the capture is
`assets/imported/parity-vdp1-floor-projection`, ELF SHA-256
`e538c8bd9c2782e557d8a98c8d95bfc43272e1365f28370c82231b10aa7ccde0`.
An actual moving or steep-camera PS1/Saturn image comparison is still needed.

### VDP1 texture sampling versus a PS1 affine model (2026-10-28)

`scripts/audit_vdp1_ps1_uv.py` compares the existing authored 8x8 Ymir
distorted-sprite UV captures with the PS1 quad's two screen-space affine
triangles. It samples only the interiors of the modeled triangles, leaving
their edges out because the consoles' fill rules differ. For eight synthetic
shapes, 3,824 of 5,173 comparable VDP1 samples select a different texel
from this simple PS1 estimate. Even the axis-aligned 32x24 rectangle differs
at 515 of 706 interior samples, with mean Manhattan difference 0.9632 of an
8x8 texel; the skewed quadrilateral differs at 710 of 1,126, mean 0.7771.
The existing `experiment_vdp1_rotated.py` still reproduces all 6,678 recorded
opaque Ymir samples and 32 transparent planes in its 24-shape fixture.

These numbers compare real Ymir VDP1 sample coordinates with a geometric
PS1 estimate, not with a PS1 GPU capture. In particular, their integer
sampling rules and displayed geometry are not proven identical, and the
authored 8x8 patterns are not the 64x64 Paul floor. The result does show
why identical projected corner coordinates alone cannot establish texture
parity. A useful next experiment needs matched PS1 and VDP1 UV-coded draws
at representative floor dimensions, including triangle coverage, before
choosing between texture remapping, subdivision, or another rendering path.

### Four-sprite floor subdivision experiment (2026-10-28)

`T3_VDP1_FLOOR_SUBDIVIDE_PROBE` is an opt-in extension of the shaded VDP1
floor. At boot it uploads four 32x32 portions of the private 64x64 tile.
For each original-eligible quad it draws four distorted sprites, with the
screen-space centre on the PS1 quad's B–D triangle diagonal and halfway
interpolated VDP1 Gouraud corrections. Each four-command group is rolled
back on submission failure. This is an approximation: its midpoint rounding,
VDP1 sampling, interpolation and triangle coverage need not match PS1.

In the same matching neutral PS1/Ymir scene and the existing descriptive RGB
metrics, the far-floor error changed from 13.088 to 12.922 and the near-floor
error from 13.426 to 13.269. The panorama metric stayed 15.272. The snapshot
used 930 of 2,048 commands, compared with 804 for the one-sprite control;
both reported 739 flushed faces, zero rejected faces and no guest error. In
the 800-field run their observed overlap counts were 136 and 145 respectively;
this is not a physical-hardware or controlled frame-rate benchmark. The new
candidate is `assets/imported/parity-vdp1-floor-split`, ELF SHA-256
`24f677b77ef39fc1d1275f15d4102d72c64329bcb44f60164f8427417ad116b8`;
the same-state control is `parity-vdp1-floor-projection`.

The small image-score change from one pose does not establish general visual
improvement, and the extra commands and observed lower throughput make this
unsuitable as a default without further work. Moving/steep cameras, particle
ordering, larger scene command budgets, synchronized original frames, and
physical hardware remain unvalidated.

### Selective floor subdivision (2026-10-28)

The subdivision probe accepts `T3_VDP1_FLOOR_SPLIT_MIN_AREA`, a threshold for
the quad's screen-clipped bounding-box area. Below it the probe uses the
single shaded sprite; otherwise it uses four. If the four-command transaction
cannot fit, it rolls back and tries the single sprite. The threshold defaults
to zero, preserving the earlier all-subdivided probe, and is not enabled in
the ordinary game configuration.

With a threshold of 512 in the same matched neutral scene, the candidate used
858 commands rather than 930 for all-subdivided and 804 for single-sprite.
All three had 739 flushed fighter faces and zero rejected faces. The candidate
had 141 observed overlap frames over 800 fields, versus 136 and 145 for those
controls respectively; this is not a calibrated frame-rate comparison. The
prior far/near floor regions have the same descriptive RGB errors as the
all-subdivided image (12.922 and 13.269). The entire images are not equal:
2,789 pixels differ in rows 142–161. In the lateral strips of rows 142–157,
mean absolute RGB error was 17.3244 for selective, 17.1558 for all-subdivided
and 17.3817 for single-sprite. Thus the threshold trades some distant-floor
fidelity for fewer commands in this one image; it is not a general optimum.

The matched camera and fighter state were rechecked against the PS1 RAM; no
guest error was reported. Evidence is `assets/imported/parity-vdp1-floor-select`,
ELF SHA-256 `fe79d1b547aa20eb8d687a98a9a8f208169c0943c7fc6e711cda7a12c7d01fd7`.
The size threshold, visual behavior and fallback still require moving-camera,
capacity-pressure and hardware tests before use beyond this experiment.

### Selective subdivision during a moving fight (2026-10-28)

The selective 512-pixel probe and the single-sprite floor were built without
the frozen benchmark tick and run separately for 1,200 injected Ymir fields
with the same `build/parity-vdp1-floor-live.devices` controller schedule.
The schedule includes approach and attacks; both endpoint states report five
hits. The selective run reached 849 simulation ticks and the single-sprite
control 868, so their states and screenshots are not synchronized and cannot
be used for an image or pixel comparison. Their observed overlap-frame counts
were 213 and 219, respectively, without a claim about physical Saturn speed.

The selective build now exposes four diagnostic counters: successful
subdivided quads, successful single quads, capacity-triggered fallbacks and
the highest VDP1 command occupancy observed when diagnostics were published.
In this run they read 4,012, 3,146, 0 and 922 of 2,048 commands respectively.
The final state reported 856 commands, 739 flushed fighter faces, zero
rejected faces, zero draw timeouts and no first error. These are a finite
scenario and the command peak is sampled at diagnostics publication, not
an exhaustive proof for all frames or worst-case scenes. The reported
simulation/presentation pose lag at the endpoint was three ticks.

Evidence is `assets/imported/parity-vdp1-floor-live-select` (ELF SHA-256
`1bf9324cf2826f2684a5afb2e30b812bab93fbc4560caf28612ecbe5690334bd`) and
`assets/imported/parity-vdp1-floor-live-control` (ELF SHA-256
`ce5390cec2f10106910898fbc5d9bfe06f9147c2ace94d38718e18d080b9f5b3`).
The controller script, per-run build caches, selected screenshots and
diagnostics are retained. This does not test capacity fallback under
pressure, original PS1 pixel parity, or physical hardware.

### Subdivided floor capacity rollback (2026-10-28)

The opt-in `T3_VDP1_FLOOR_CAPACITY_SELFTEST` runs once after scene begin.
For each of one, two and three available world-command slots, it fills the
staged list with artificial commands, attempts the four-sprite floor tile and
requires `SAT_ERR_CAPACITY`. It then checks that the tile's checkpoint
restored both command and Gouraud-table counts, verifies that the single
sprite fits, and rolls the entire artificial sequence back before submitting
the frame. The four-word diagnostic result `(1,1,1,1)` records completion
and the three cases. The test requires subdivision and is off by default.

In an 800-field matched-neutral Ymir run the result was `(1,1,1,1)`, with
739 flushed fighter faces, zero rejected faces, 858 final commands, no draw
timeout and no first error. The resulting screenshot is identical to the
earlier selective control, and the PS1/Saturn camera and fighter state check
passed. Evidence: `assets/imported/parity-vdp1-floor-capacity`, ELF SHA-256
`43c0af5fb55279205cf40864a3752f4dc04c4302c4bd5f8f0ffae49390e4777e`.
This verifies the staged-list transaction and fallback for these controlled
limits in Ymir; it does not reserve the fighters' future command budget, test
the case with zero free slots, or establish physical-hardware behavior.

### Protecting the queued fighter command allowance (2026-10-28)

The VDP1 floor probe now captures its camera, grid centre and fade inputs
before actor preparation can start deferred simulation on the Slave. It
submits the floor after the actors and shadows are queued, but before their
scene flush, preserving the floor's background draw order. The floor budget
excludes the existing HUD reservation, the end command, one command for
every queued face, and seven extra commands for each cold record. This is a
conservative allowance, not a proven upper bound for every SDK material or
clipping path; particularly, tiled fallbacks can use more than eight
commands. The game must not treat it as a universal guarantee.

Before subdividing, the floor counts the remaining eligible tiles and holds
one command per tile. Only surplus commands may add the three extra sprites.
If the whole budget runs out, the remaining tiles are omitted; this avoids
spending scarce commands on subdivision while leaving more basic tiles
undrawn, but it is still a visible fallback, not 1:1 rendering.

With the diagnostic `T3_VDP1_FLOOR_FORCE_BUDGET=40`, the neutral scene's
42 eligible floor tiles received 40 single-sprite draws per sampled frame;
subdivision was suppressed and the final two tiles could not be drawn. The
endpoint retained 739 flushed fighter faces, zero rejected faces, no timeout
or first error. The force limit is disabled by default. Evidence is
`assets/imported/parity-vdp1-floor-budget-force`, ELF SHA-256
`972d7c46b916ba919b8936da2dbcd0e158bb6523eb1c5030ef9f67f711e4528e`.

Without the forced limit, the matching neutral run held 1,550 face commands
and 302 for the floor; no budget fallback occurred and its screenshot was
identical to the earlier selective control. Evidence:
`parity-vdp1-floor-budget-neutral`, ELF SHA-256
`ebfb131dba518ab8e513672f599bcfa34fa2199e9a95f0b5c148cf66984bc0f0`.
In the moving 1,200-field scenario, the final build likewise reported no
budget fallbacks, no rejected faces, five hits, 833 simulation ticks and 209
overlap frames. Its peak sampled command count was 933; evidence is
`parity-vdp1-floor-budget`, ELF SHA-256
`999a7467c1fdf7b584ca2ac9c04f2b7ed538ebba7e2a13373d288be15860bab0`.
These runs do not establish a physical-hardware frame rate, a worst-case
command bound, or moving-camera pixel parity with PS1.

### Material-aware floor command allowance (2026-10-28)

The earlier seven-extra-commands-per-cold-face allowance was not sufficient
for every LibSaturn tiled material. `floor_command_budget.h` now counts the
queued face representations: a projected-safe face reserves one command; a
non-projected ordinary face reserves 24, corresponding to at most four
near-plane pieces times six screen-clipped pieces; and a non-projected tiled
face reserves 25 per cell, for the SDK's 2x2, 4x4 or 8x8 grids. Unknown or
invalid cold entries suppress floor submission rather than assuming a smaller
allowance. The count includes queued faces that might later be culled, so it
can reserve more than necessary. It is tied to the checked SDK dispatch and
clip limits; later changes in that SDK require reevaluation. Other draws
outside this queued scene are not covered by this face allowance.

`floor_command_budget` tests the ordinary queue and indexed projected queue,
including 2x2, 4x4 and 8x8 tiled records, single-command safe faces, invalid
descriptors and capacity errors. In the same 1,200-field moving Ymir scenario
as the preceding run, the new build reported a final face allowance of 1,550
and floor budget of 302, five hits, 833 simulation ticks, zero rejected faces,
and no first error or draw timeout. All three retained intermediate screenshots
and the final screenshot were byte-identical to that previous run. Evidence is
`assets/imported/parity-vdp1-floor-cold-budget`, ELF SHA-256
`ff84d29876381219d5b6ed96824716a9df8d81673653c2ee0290d453cd6beea5`.
This particular fight did not demonstrate a clipped tiled material on Saturn;
the synthetic host checks and SDK bounds support that portion of the rule.

### Activating the reserved overlay commands (2026-10-28)

The scene reserves 192 VDP1 commands for the HUD and other overlay draws.
After flushing the scene, gameplay now enters the SDK overlay pass before
drawing the HUD, sparks and subsequent diagnostic overlays. Previously the
world limit remained active during those draws, so the reserved commands
were unavailable when the world list approached its limit.

The opt-in `T3_OVERLAY_RESERVATION_SELFTEST` fills the remaining world quota
with one-pixel rectangles for one submitted diagnostic frame, verifies that
another world command reaches capacity, enters the overlay pass and submits
one more rectangle. In an 800-field Ymir run its result was
`(1, 1023, 192, 1856, 2048)`: passed, 1,023 filler commands, 192 reserved,
1,856 commands used after the additional overlay command, and 2,048 total
capacity. Gameplay continued to the matched neutral state with 739 flushed
fighter faces, zero rejected faces, 858 final commands and no first error or
draw timeout. Camera and fighter state matched the original capture; the
final screenshot matched the earlier neutral control. Evidence:
`assets/imported/parity-overlay-reservation`, ELF SHA-256
`f2ab20db0df161cecf8c3b8ea38a9d6f615f53fd6f5c7ed8ffdd135c7233b162`.

With the self-test disabled, a 1,200-field moving Ymir run finished with
833 simulation ticks, five hits, 725 flushed faces, zero rejected faces, 839
commands, 209 overlap frames and no first error or draw timeout. Its final
screenshot matched the preceding material-aware control pixel for pixel.
Evidence: `assets/imported/parity-overlay-live`, ELF SHA-256
`ec814dff3d88bd85ce724214911d95d0a79eda95c116817b0615744d6574fdfc`.
The diagnostic establishes that reserved space becomes available after world
capacity is reached; it does not prove that all possible combinations of HUD,
sparks and diagnostic overlays fit in 192 commands or establish behavior on
physical Saturn hardware.

After building all host targets, the current `build/host` CTest suite passed
91/91 cases on 2026-10-28, including the floor command budget, floor
visibility, original camera, fighter ordering and particle checks. This is a
host regression result; it does not replace the Ymir or hardware checks above.

### Per-quarter shading model (2026-10-28)

`scripts/audit_floor_quarter_shading.py` compares the current global
palette-weighted additive corrections with independently optimized offsets
for each 32×32 quarter of the private Paul floor tile. Across the 1,024 USA
fade-table entries (52 distinct brightness levels), weighting each entry and
each texel equally, the model's mean absolute channel error is 0.179439
5-bit units for the current global correction and 0.177781 for independent
quarter corrections. This upper bound on the improvement within this model
is only 0.001658 units; implementing separate correction tables is therefore
not justified by this result alone. The calculation uses
`build/parity-vdp1-floor-probe/generated/private_stage/PAUL.STG` and does not
model how often levels occur in gameplay, interpolation, rasterization or
the actual PS1/Saturn pixels. Other stage textures could behave differently.
The audit was corrected subsequently to decode the stored 8×8 cell order;
for this particular texture its aggregate results above remain unchanged.

### Paul floor ordering (2026-10-28)

The Paul converter requires all 100 floor descriptor entries to be identical.
`FloorDrawGrid` walks the grid in ascending order and prepends each visible
tile to its ordering-table bucket, reversing that traversal at draw time.
The experimental VDP1 floor now submits tiles in descending grid order to
match that ordering. Other stages with multiple texture buckets would need
their own bucket grouping; this does not implement it. When the command
budget is exhausted, the subset of tiles retained also changes with the
traversal order, and the result remains an incomplete fallback.

In the 800-field neutral Ymir capture, camera, roots and pose match the
previous PS1 snapshot. Against the former Saturn control, 2,138 pixels
changed within rows 142–223, with 858 commands, 739 flushed faces, zero
rejected faces, no draw timeout and no first error. In the existing PS1
image comparison, far-floor mean RGB error changed from 12.922 to 12.914
and near-floor from 13.269 to 13.270; the panorama score stayed 15.272.
This is a single matched view, not proof of pixel parity or performance in
motion. Evidence: `assets/imported/parity-floor-ot-order`, ELF SHA-256
`37993c00c634d03c1f50064cb69d8cb0a2782e517525be529290591cdbf73252`.
The normal unfrozen experimental build was restored afterwards.

In a 1,200-field unfrozen Ymir run using the same approach-and-attack input
as `parity-overlay-live`, the reordered build recorded five hits, 833
simulation ticks, 725 flushed faces, zero rejected faces, 839 final commands,
no draw timeout and no first error. Its floor counters reported no budget or
capacity fallback, and the sampled command peak was 933. These values match
the control's corresponding endpoint values where available; the run is
not a synchronized moving PS1 comparison. Evidence:
`assets/imported/parity-floor-ot-live`, ELF SHA-256
`04a5b698bc364536b9c1814f406b87953089e05b4de2b3b4f6e0380d38eb64ce`.

The same endpoint exposes a significant performance limitation: the tick
clock observed 911 gameplay fields but discarded 78 because more than four
fields elapsed in some recovery intervals. The control also discarded 78;
neither run meets the project's no-tick-loss or 60 Hz acceptance target.
A six-field Ymir debug profile at fields 1000–1005 of the same input, with
cache emulation off and no dropped profile samples, attributes the largest
master execution shares to `faces.cpp.obj` (24.3%) and `characters.c.obj`
(23.8%); the Slave's largest shares include `frt.cpp.obj` (15.5%),
`psx_pose.c.obj` (12.2%) and `psx_motion.c.obj` (10.6%). The timer and
mailbox shares can include waiting; interpreter cycles and this short window
do not certify real hardware costs. Evidence:
`assets/imported/parity-floor-ot-profile`. A longer profile attempt exceeded
the trace limit and its incomplete output was removed before this run.

### Empty particle image shortcut (2026-10-28)

When ordered particle masking is enabled and no prepared particle is visible,
`t3_characters_mixed_ot` now skips sorting and traversing the fighter faces.
It still begins and ends an empty particle image, allowing the normal
double-buffer publication to clear the previous overlay. The synthetic mask
self-test and particle census retain their previous path. Mixed-OT verified
counts only include actual sorted checks; `g_t3_mixed_ot_empty_frames` counts
the shortcut separately.

In the same 1,200-field Ymir input scenario, the candidate recorded 258
empty shortcut images and 25 images containing drawn particles, with 303
visible particle records and zero masking fallbacks. It reached 873 logic
ticks, submitted 282 frames and discarded 40 fields, compared with 833,
208 and 78 respectively for the reordered-floor control. Both recorded five
hits, zero rejected faces, no timeout and no first error. Differing cadence
means their endpoint game states and screenshots are not synchronized; this
is not a pixel comparison or hardware timing measurement, and 40 discarded
fields still fails the no-tick-loss goal. Evidence:
`assets/imported/parity-empty-ot-live`, ELF SHA-256
`6bd3f245e4487984e2de3486bd0b22a548097c0ab1e720a4b10f8c7e3ddcc090`.

With the pose frozen at tick 51, the Ymir screenshot is pixel-identical to
the preceding reordered-floor control; camera and fighter state also match
the original PS1 capture. Evidence: `parity-empty-ot-neutral`, ELF SHA-256
`632fadb375a4893b40060f966544ab399ee477699007f02a524fc3e9c128ff93`.
Four host particle and mixed-order tests passed. The unfrozen build was
restored.

For the active-to-empty transition, two deterministic Ymir runs of the same
ELF and input schedule captured the full 64×64 VDP2 name map. At field 1050,
28 cells were assigned to the published particle image; at field 1200, all
4,096 entries were blank and the empty shortcut counter had advanced. The
28 previously occupied positions were cleared. Reproduce the map check with
`scripts/check_saturn_particle_clear.py` and captures
`assets/imported/parity-empty-clear-1050` and `parity-empty-clear-1200`.
The unused field-950 capture was removed. This checks Ymir's published VDP2
map across the transition, not physical hardware or pixel parity while the
particle is visible.

### Reusing floor vertex shades (2026-10-28)

The 10×10 floor shares 121 projected vertices. The experimental shaded
renderer now calculates the VDP1 correction once for each projected vertex
and reuses it for adjacent quads, instead of calculating it four times per
tile. The brightness function, depth inputs and Gouraud values are unchanged;
the extra cache uses 242 bytes of static RAM. A Ymir profile of the preceding
build sampled 800 calls to the shade function across two submitted images,
whereas the new loop makes 121 calls per drawable floor image.

In the same 1,200-field input scenario, the candidate recorded 877 ticks,
288 submitted frames and 37 discarded fields, versus 873, 282 and 40 in
the empty-particle shortcut control. Both recorded five hits, zero rejected
faces, no timeout and no first error. Their different cadence means the
endpoint states are not synchronized, and the result is not a physical
Saturn speed measurement. The remaining 37 discarded fields still fail the
no-loss target. Evidence: `assets/imported/parity-shade-cache-live`, ELF
SHA-256 `608db7e9f96255df38416cb2bcba3078572401d3e88bb92d34a7100b4a42c7fe`.

The 800-field frozen-neutral image matched `parity-empty-ot-neutral` pixel
for pixel, and camera and fighter state matched the PS1 capture. Evidence:
`assets/imported/parity-shade-cache-neutral`, ELF SHA-256
`af6f92c046940e62964300cbdc81dd71bd7d6425a8312fd2b6b187aa52e6b3bc`.
This single screenshot does not validate all moving-camera shading. The
unfrozen experimental build was restored after the comparison.

### Indexed fighter OT calculation (2026-10-28)

The native fighter OT calculation now reads the three projected vertices
directly from the shared array using the face indices, avoiding three
temporary structure copies per face. It uses the same NCLIP and depth
arithmetic as the previous path. The original-face test now exercises this
indexed path with nonconsecutive indices: all 3,672 captured original faces
across the four primitive groups matched the PS1 bucket and NCLIP values.

In the 1,200-field Ymir fight the indexed candidate submitted 299 images and
recorded 878 logic ticks and 37 discarded fields, compared with 288, 877
and 37 in the preceding control. Both registered five hits, no rejected
faces, no draw timeouts and no first error. These unsynchronized endpoints
do not establish hardware performance or eliminate tick loss. Evidence:
`assets/imported/parity-indexed-ot-live`, ELF SHA-256
`c7de792305e06a3176f2c0b38320f2176813f786f8b796fd722e09c38ebb8343`.
The frozen-neutral screenshot matched the previous control pixel for pixel;
the camera and fighter state matched the PS1 snapshot. Evidence:
`parity-indexed-ot-neutral`, ELF SHA-256
`f1eeb7b11a3960b2084a4bc70175127e6b9a8fb9988112ad38c57176093b3744`.
The ordinary unfrozen experiment was restored afterwards.

### Skipping faces before the first particle (2026-10-28)

For the indexed-OT control, Ymir checkpoints with the same input and ELF
showed zero discarded fields through field 800, 14 at field 900, still 14
at field 1000, and 37 at field 1100. The corresponding cumulative drawn
particle-image counts were 0, 12, 12 and 23. This correlates both periods
of loss with particle images in this run; it does not isolate their exact
cause. Evidence: `assets/imported/parity-tick-window-{800,900,1000,1100}`.

Within the sorted mixed-OT traversal, faces preceding the first particle now
skip mask construction. The freshly begun particle image contains no cells
at that point, so those faces have nothing to occlude; faces after an added
particle retain the original mask path. In a 1,200-field Ymir run the
candidate recorded 882 simulation ticks, 300 submitted images and 32
discarded fields, versus 878, 299 and 37 for the control. Both had five
hits, no rejected faces, no draw timeout, no first error and no particle mask
fallback; the candidate recorded 25 images with particles. Evidence:
`assets/imported/parity-preparticle-skip-live`, ELF SHA-256
`d2a19112ecb980d042b93c11f8256e3e3469b8bdc041d3a9306b2f2aca4f459f`.
The three host mixed-order and particle tests passed, but do not execute this
Saturn-only shortcut.

An attempted frozen tick-728 comparison produced 29 and 36 active tiles in
the control and candidate respectively: the controller script is tied to
emulator fields, so different render cadence led to different effect state
at that tick. Those captures cannot prove pixel equivalence and were removed,
along with the unused tick-600 capture and the four earlier timeline probes.
The shortcut still requires a synchronized active-particle visual comparison;
this timing result is not hardware certification and 32 lost fields remains
outside the acceptance target.

The existing private tick-indexed input replay was tried as a way to remove
the field-timed input divergence. With `paul-slot-encounter/mid/input-tape.json`,
frozen ticks 220, 225, 228 and 230 produced zero published particle tiles;
the first two had zero hits and the latter two one hit. With the longer
`paul-encounter-replay-aligned/input-tape.json`, frozen tick 400 had two
hits and zero published tiles. These checkpoints therefore cannot validate
the shortcut, even though tick-based replay removes the stated input-timing
ambiguity. The search captures were deleted and the unfrozen interactive
build restored. A further fixture must identify a tick with a nonempty
published particle image before comparing candidate and control.

An opt-in `T3_PARTICLE_TICK_AUDIT` now retains the first 32 prepared images
with visible particles: the main-thread game tick, snapshot frame counter and
visible count. In the live scripted fight it recorded 25 such images; the
first had game tick 539, source frame 536 and 26 visible particles. These
are prepared-image observations, not a guarantee that every image was
published. Evidence: `assets/imported/parity-tick-input-capture`, ELF
`57bd102d8642a357db1068d48a907ed636a5d426ba7cea54a5a52fa403b599df`.

An experimental Slave input capture from that run did not reproduce the same
fight when replayed: at frozen tick 543 the replay had zero hits and no
published particle tiles. Its captured tape must not be treated as a validated
fixture; the reason for the state divergence remains unresolved. The Slave
input capture changes were reverted, and the replay and no-particle search
outputs were removed. The audit option is off in the restored interactive
build. Synchronized active-particle visual verification remains open.

The divergence was traced to the probe wrapper: it always merged the CPU
tape into player 2 even when no AI calls occurred. In this two-pad run the
CPU tape contained only zero, overwriting the real second pad. The wrapper
now preserves both recorded pads when no AI calls occurred; CPU-only
captures and runs with AI calls retain the CPU merge. The capture's original
`input-tape.json` remains invalid, but `two-pad-inputs.json` was recovered
from its raw input records. The experimental Slave-side capture remains
reverted and is not claimed as a generally supported recording path.

Using those recovered inputs and freezing at tick 543, the candidate and a
control with the pre-particle face shortcut removed both recorded three hits,
the same fighter anchors, move slots, move frames and health, and 41 published
particle tiles. Their 320×224 screenshots matched pixel for pixel; the dumped
VDP2 name map and particle pattern range also matched word for word. Evidence:
`assets/imported/parity-tick-synced-candidate`, ELF SHA-256
`7e6e215c38673c0b36de561a72aaabf3cb75a3ddadbfbcd12e132d041c021a17`, and
`parity-tick-synced-control`, ELF SHA-256
`59f03af574e2f15974fc1647fc4cdc84dc6e52d8a6a52a68b644215f1e61f00e`.
This checks one active-particle state on Ymir, not all effects, original PS1
pixel parity or physical hardware. The normal interactive build was restored.

The CPU-tape merge is now factored into `merge_cpu_tape` and covered by host
tests for a two-pad capture with an unused CPU tape and for a CPU tape whose
changes interleave with player 1's inputs. All six `test_saturn_combat_suite`
cases pass. Running the corrected merge on the saved raw capture reproduces
the recovered `two-pad-inputs.json` exactly. This validates the merge in
these cases; it does not establish equivalence of every AI-driven replay.

### Reusing particle-mask work per face (2026-10-28)

When one fighter face intersects multiple particle cells, the diagnostic
ordered-mask path now unpacks its LUT4 texture at most once per face and
clears each intersecting cell by its already-known index. Previously it
unpacked the texture for every cell and searched the cell list again when
clearing. A host test checks indexed clearing against the coordinate lookup
and verifies out-of-range rejection.

At the recorded two-pad tick 543, the new Ymir build and the earlier
`parity-tick-synced-candidate` both published 41 particle tiles; the full
screenshots and the dumped name map and particle pattern range were equal.
Evidence: `assets/imported/parity-particle-indexed-neutral`, ELF SHA-256
`2ca7babe092f0af361d2c9e518591e429c93b11df4586fb86c7cab0d87060b0e`.
In the 1,200-field live scenario both the new build and the preceding
control recorded 882 ticks, 300 submitted frames, 32 discarded fields,
five hits, 25 particle images, no masking fallback, no rejected faces,
timeout or first error. Thus this run shows no measurable cadence gain and
still fails the no-tick-loss target. Evidence:
`assets/imported/parity-particle-indexed-live`, ELF SHA-256
`75ada2dd46c5f48388715bb6c2335f5628c4f38df09fa0b9f5426a58bc6f901d`.
One frozen effect state does not establish all masking or hardware parity.

### Cached particle sampling coordinates (2026-10-28)

In the tick-543 active-effect Ymir profile, six sampled fields included one
completed composition image. The Master attributed 23.7% of its exclusive
interpreter execution cost to `t3_stage_particle_multi_face`, 11.4% to
`__sdivsi3`, and 7.7% to `t3_particle_tile_accumulate`; the sampled window
is too short to characterize all gameplay. The Slave spent much of this
frozen interval polling for work. Evidence:
`assets/imported/parity-active-particle-profile`.

Particle accumulation now generates each sprite's 16-texel horizontal and
vertical nearest-neighbor lookup once with bounded integer increments,
instead of dividing for each sampled pixel. The host particle test compares
accumulation against the existing independent division-based tile renderer
for 1,120 source-width/height combinations, in addition to its clipping,
transparency, overlap and capacity checks. The tick-543 Ymir candidate kept
41 published cells and matched the preceding control's screenshot, name map
and particle pattern range. Evidence: `parity-particle-map-frozen`, ELF
SHA-256 `9e715390df1ffee03c082a9e68971098b57a2ef22cb80ebf681a3865559fbc7b`.

In the same 1,200-field moving input scenario, the candidate recorded 889
ticks, 304 submitted frames and 26 discarded fields versus 882, 300 and 32
in the preceding control. Both had five hits, no rejected faces, timeout,
first error or mask fallback. Their endpoints are not synchronized; this
finite emulator run does not establish physical hardware speed or eliminate
the remaining tick loss. Evidence: `parity-particle-map-live`, ELF SHA-256
`263f1783adda7c5864a75615bd8dc93d11f273501a9d3bb44fb832669dd55981`.

### Rejecting faces outside particle cells (2026-10-28)

The ordered-mask experiment now maintains a conservative bounding rectangle
of allocated particle cells, rebuilding it after each successful particle
addition because accumulation may compact cells. A fighter face completely
outside this rectangle cannot overlap any allocated cell and bypasses the
per-cell scan; overlapping faces still use the existing exact checks. Cleared
cells remain in the bound until compaction, so the bound may be larger than
necessary but must not exclude an allocated cell.

At the replayed tick 543, the candidate and preceding control both published
41 particle tiles, with identical screenshots and identical dumped VDP2 map
and particle pattern range. Evidence: `parity-particle-bounds-frozen`, ELF
SHA-256 `6f0ffbfceacb4d49134005f8fb74ae0f3463f5b64bb96af376c1f3d442b19684`.
The host particle and mixed-order tests passed; they do not exercise this
Saturn-only rejection directly.

In the same 1,200-field Ymir scenario the candidate recorded 904 ticks, 308
submitted frames and 10 discarded fields, compared with 889, 304 and 26 in
the control. Both registered five hits, zero rejected faces, no draw timeout,
first error or mask fallback. The candidate recorded 26 particle images,
versus 24 in the control. The different cadence means the endpoints are not
matched; the ten lost fields still fail the no-loss target, and these results
do not certify physical hardware. Evidence: `parity-particle-bounds-live`,
ELF SHA-256 `d2119c2c9614f0b4cc358b72708c3d197fd096a29d18b84fb368cb05de145cbf`.

### Reusing particle cell color checks (2026-10-28)

A six-field tick-543 profile of the bounds candidate contained two completed
composition images. Per completed image, the Master attributed about 10,261
exclusive interpreter cycles to `t3_stage_particle_multi_face`, versus
159,410 in the preceding one-image profile; those short windows differ in
phase and are not a controlled hardware measurement. Evidence:
`parity-bounds-active-profile`.

The accumulation path now caches whether each of a sprite's at most 25
overlapped cells contains a nontransparent texel, reusing the result across
capacity preflight, optional compaction and drawing. The host tile tests,
including capacity, compaction and the 1,120 sampling cases, passed. At
replayed tick 543 the candidate retained 41 particle cells and its screen,
VDP2 name map and captured pattern range matched the preceding control.
Evidence: `parity-particle-cellcache-frozen`, ELF SHA-256
`974093d2aa35bf677f858ed28055a380fd7bd4404bc0207e5da64020ed26f0f6`.

The 1,200-field live run produced 904 ticks, 308 submitted frames and nine
discarded fields versus 904, 308 and ten in the control. Both recorded five
hits, zero rejected faces, no draw timeout, first error or mask fallback.
This is a small difference in one unsynchronized emulator scenario, not
proof of a sustained speed gain or physical Saturn behavior. Evidence:
`parity-particle-cellcache-live`, ELF SHA-256
`887d6d77645614646bbb1b3c2990dc2b41d7a7d5308c2b7225c22d216667c0cf`.

### Reusing particle tile lookups (2026-10-28)

The particle accumulator now retains each cell's index from its capacity
preflight and uses it again when drawing. If compaction occurs, it performs
fresh lookups because compaction can move existing cells. The host particle
tests passed, including compaction, capacity and sampling coverage. The
tick-543 replay retained 41 particle cells and matched the preceding control
in the screenshot and both dumped VDP2 ranges. Evidence:
`parity-particle-lookup-frozen`, ELF SHA-256
`df21eb4d4b9fa2e3f193b2a3d0db18efaff58dca17a6e3033c807457f544441c`.

In the 1,200-field Ymir scenario the candidate recorded 906 simulation ticks,
309 submitted frames and eight discarded fields, versus 904, 308 and nine
for the preceding control. Both registered five hits, no rejected faces,
draw timeout, first error or mask fallback. This small difference in one
unsynchronized run does not establish a sustained or hardware speed gain;
eight discarded fields still fail the no-loss target. Evidence:
`parity-particle-lookup-live`, ELF SHA-256
`43f5a62180484ed04a61ef8a927863e7813972db9e4f1f74d9e1877369849be2`.

### Remaining lost-field checkpoints and row-bound experiment (2026-10-28)

Four fresh Ymir checkpoints of that same ELF and pad schedule found zero
discarded fields at emulator field 800, two at 900, still two at 1000 and
eight at 1100. Cumulative drawn-particle images were 0, 12, 12 and 23 at
those checkpoints. As before, the two increases coincide with particle
images in this input scenario; this correlation does not isolate the precise
cost or prove that other workloads behave the same. Evidence:
`parity-loss-window-{800,900,1000,1100}`.

A separate candidate maintained horizontal bounds per 8-pixel particle row
and used them to reject additional faces before scanning individual cells.
In the same 1,200-field Ymir scenario it produced 904 simulation ticks, 308
submitted images and nine discarded fields versus 906, 309 and eight for the
control; both had five hits and no reported rendering or mask errors. The
endpoints were not synchronized and the small difference does not prove a
hardware regression. The additional bookkeeping did not demonstrate a gain
in this run, so the experiment was reverted, its
`assets/imported/parity-particle-rowbounds-live` output deleted, and the
interactive control rebuilt. The row-bound candidate was not visually
qualified.

### Particle palette lookup experiment (2026-10-28)

An alternative mapped the three fixed particle CLUT ranges directly to their
asset indices rather than searching the 37 validated records. A host test
now checks all 65,536 possible CLUT identifiers using distinct synthetic
palette contents; it passes on both the experimental and restored lookup.
The experimental tick-543 screenshot and the dumped particle map and pattern
range matched the control, with 41 cells in each.

In the 1,200-field Ymir fight the candidate recorded 904 ticks, 309 submitted
images and ten discarded fields, against 906, 309 and eight for the control;
both had five hits, no rejected faces, timeout, first error or mask fallback.
These unsynchronized measurements do not establish a hardware regression,
but show no cadence improvement in this scenario. The direct lookup was
reverted, its `parity-palette-index-frozen` and `parity-palette-index-live`
outputs removed, and the original lookup rebuilt. The exhaustive test remains.

### Incremental particle bounds experiment (2026-10-28)

A candidate replaced the per-addition scan of allocated particle cells with
the union of conservative projected sprite bounds, rounded out to cell
boundaries. Old bounds were retained across compaction. In the 1,200-field
Ymir scenario it recorded 905 ticks, 309 submitted frames and ten discarded
fields, versus 906, 309 and eight in the control; both had five hits, 23
particle images and no reported mask or rendering error. These unsynchronized
results are not hardware timing evidence, but do not demonstrate an
improvement. The candidate was reverted without visual qualification, its
`parity-particle-boundunion-live` output deleted, and the control rebuilt.

### Overlapping particle accumulation regression (2026-10-28)

The host particle test now also compares 192 deterministically generated
sprites in 12 sequences against independent single-sprite division-based
sampling and a full-screen additive reference buffer. The sequences include
overlap, transparent palette entries and positions clipped at screen edges.
The test passed both the normal 80-cell host configuration and an explicitly
compiled 52-cell configuration matching the ordered-mask experiment. Existing
capacity and compaction checks passed in both. This strengthens regression
coverage of the current diagnostic renderer; it does not prove PS1 GPU pixel
parity or remove the 52-cell capacity limit and whole-image fallback.

### Enlarging the experimental particle surfaces (2026-10-28)

The ordered-mask build now allocates 80 cells in each of its two particle
surfaces, up from 52. The stage boot scratch union is allowed to extend past
its 16 KiB upload member to hold the larger live surfaces and line data; its
size is capped at 24 KiB, and the linker checks Low WRAM and Master stack
reservation. The current interactive ELF's Low WRAM end is `0x002b7324`
against the `0x002ffe00` linker limit. A compile-time bound also keeps the
80 RGB cells within VDP2 VRAM. This raises a cell budget, not the original
game's separate 80-object limit, and does not establish worst-case timing.

The host test exercises adding cells after 52 occupied cells and still checks
full-capacity rejection, compaction, overlap and sampling. An opt-in
`T3_PARTICLE_80_SELFTEST` (requiring `T3_ORDERED_MASK_SELFTEST`) publishes
80 synthetic 8×8 sprites in 80 separate cells. In an 800-field Ymir run,
the published count and particle count were 80, the captured VDP2 map had
80 distinct nonblank cells, and all 5,120 captured RGB words were `0x8001`;
there were no rejected faces, draw timeouts or first error. Evidence:
`parity-particle-80-full`, ELF SHA-256
`2efc1665e796589558e5090a0b13b3e812971d0087ecf1cbc44334efe526cef5`.
The earlier 60-cell stress output was superseded and removed.

At the synchronized tick-543 effect state, the enlarged candidate and the
52-cell control both published 41 cells; screenshot and dumped VDP2 ranges
matched. Evidence: `parity-particle-80-frozen`, ELF SHA-256
`2918ef22dc48783e06dce0458b47f5ab98aba9eb06e2fdcac27c5aecbac2b38c`.
In the 1,200-field interactive scenario, both reported 906 ticks, 309
submitted frames, eight discarded fields, five hits, 23 particle images,
no mask fallback, no rejected faces, timeout or first error. Evidence:
`parity-particle-80-live`, ELF SHA-256
`f6986acdbe64f6fc28f4908f1b38316cefeac53be1283566ec14c60f336feb62`.
The stress image is synthetic and neither demonstrates a real fight exceeding
52 occupied cells nor proves that an 80-cell VBlank upload fits real Saturn
timing. Unsupported masks and actual exhaustion still blank the whole effect
image. The interactive build was restored with the self-test disabled.

### VBlank samples for 80-cell publication (2026-10-28)

The opt-in `T3_PARTICLE_PUBLISH_AUDIT` samples VDP2 TVSTAT.VBLANK and the
SH-2 free-running timer at entry and exit of each particle swap callback;
it also records the largest elapsed timer delta and the largest delta when
a backdrop update was published in the same callback. With the synthetic
80-cell self-test, an 800-field Ymir injection observed 97 particle
publications: all 97 began and ended with the VBlank bit set. The maximum
16-bit timer delta was 263 ticks, at 80 cells, including a simultaneous
backdrop update. The captured map still contained 80 distinct nonblank
cells and all 5,120 particle RGB words were `0x8001`; no first error or draw
timeout was reported. Evidence: `parity-particle-vblank-audit`, ELF SHA-256
`bb6ed4729bca2d53102d63407fc5ec9e8a076612fb886cf4b861e418297d1431`.

These are entry/exit samples and modulo-16-bit timer deltas, not a continuous
trace or a hardware timing guarantee. The diagnostic itself adds overhead,
and Ymir's timing model and synthetic sprites need not reproduce a real
fight or Saturn bus contention. The self-test and audit were disabled in the
restored interactive build; physical hardware qualification remains open.
