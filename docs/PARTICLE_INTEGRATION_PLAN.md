# Particle integration plan (2026-10-29)

This plan takes the original particle effects from the opt-in diagnostics in
[PARTICLE_RENDER_PROGRESS.md](PARTICLE_RENDER_PROGRESS.md) into the playable
build. It does not repeat that file's evidence; it states what is done, what
blocks a release, the order of work, and the gate for each step.

## 1. Starting point

Already done and verified, all behind opt-in options:

| Piece | Option | Evidence |
| --- | --- | --- |
| Particle pool, simulation, projection | owned match | 3,000/3,000 reference ticks, host tests |
| Per-image snapshot before the Slave launch | `T3_PARTICLE_IMAGE_SNAPSHOT` | `capture_image_match` in `src/saturn/main.c` |
| Quad preparation on SH-2 | `T3_PARTICLE_PREPARE` | `scripts/check_saturn_particle_prepare.py` |
| Additive layer: NBG1 in bank B1, VDP2 add mode | `T3_STAGE_ADD_PROBE` | every one of the 7,680 test pixels is the exact saturated sum in Ymir |
| One-bank bitmap floor that frees B1 | `T3_STAGE_BITMAP_FLOOR` | rows 60–223 match the cell floor in Ymir |
| Fighter/particle mixed draw order | `T3_PARTICLE_ORDERED_MASK_PROBE` | synthetic self-tests (axis, slanted, triangle) |
| Arbitrary face masks | `T3_PARTICLE_GENERAL_MASK_PROBE` | 29 general face tiles in a live fight, 0 failures |
| 80-cell surfaces published in VBlank | `T3_PARTICLE_80_SELFTEST`, `T3_PARTICLE_PUBLISH_AUDIT` | 97/97 publications inside VBlank (synthetic) |

The playable build (`build/evring-cand`) has none of these on. It draws a
heuristic cross (`draw_sparks`, `src/saturn/main.c:561`) at each hit/guard
event.

### Rejected approach

VDP1 half-transparency computes `(B+F)/2`. Every nonzero particle palette
word has STP set with tpage `0x3F`, i.e. the PS1 additive `B+F`. Half
transparency dims sparks and dims what is under them; it cannot match. It is
only a fallback if the VDP2 path fails (section 9).

## 2. What blocks a release

1. **Frame rate of the original fighter projection.** The ordered mask needs
   the original projected fighter faces (`T3_ORIGINAL_FIGHTER_PROJECTION`).
   In the 1,200-field Ymir scenario that path reached 385 frames / 28.5 fps
   against 536 frames / 41.0 fps for the SDK projection. This is the largest
   risk and the first thing to settle.
2. **Whole-image fallback.** If one intersecting face cannot be masked, or a
   particle cannot be added, the whole effect image is dropped.
3. **Dropped fields on particle images.** Discarded fields rise in the frames
   that carry particles (for example 2 → 8 between checkpoints 900 and 1100;
   32–37 in the indexed-OT configuration).
4. **No PS1 pixel comparison** of particles, and no hardware run.
5. **Memory.** The general-mask build already moved wall-depth scratch to Low
   WRAM and shrank the HUD upload buffer to 30 KiB; the pose cache is at 64.
   Every added byte must be checked against the High WRAM `.bss` +
   `__stack_reserve` limit.

## 3. Rules for the work

- One candidate build (`build/ptcl-cand`) and one control (`build/evring-cand`
  as configured now). Outputs under `assets/imported/ptcl-*`, deleted by
  explicit list when superseded. `du -sh build assets/imported` before each
  batch (now 1.5 GB / 4.6 GB).
- Configure the candidate from `scripts/print_release_configure.py` plus the
  options named in each phase. Do not copy the SDK prefix; rebuild
  `~/dev/saturn-prefix` in place if LibSaturn changes.
- Compare at synchronized simulation ticks (input tape replay), not at emulator
  fields. Field-timed input made earlier frozen comparisons invalid.
- The full gate is `scripts/saturn_regression.py`: host tests (101), the
  26 live combat cases, and the 3,000-tick reference replay. Simulation hashes
  and pixels are checked separately; neither proves the other.
- Every experiment that does not help is reverted and recorded in
  `PARTICLE_RENDER_PROGRESS.md`, as before.

## 4. Phase 0 — prerequisites in the playable config

Goal: the playable build runs on the bitmap floor and the original fighter
projection without losing frame rate or pixels.

Steps:

1. Candidate = control + `T3_STAGE_BITMAP_FLOOR=ON`. Check link budget.
   Compare screenshots at three synchronized ticks (idle, mid-combo, KO).
   Pass: identical outside the diagnostic counter. Run the full gate.
2. Add `T3_ORIGINAL_FIGHTER_PROJECTION=ON` and `T3_ORIGINAL_FIGHTER_OT=ON`.
   Measure frames, fps and discarded fields in the 1,200-field scenario
   against the control.
3. Profile the projected path (`T3_STAGE_HUD`, SH-2 timer spans) to find
   where the 41 → 28.5 fps loss is: projection, OT bucketing, or command
   submission. Known candidates:
   - project on the Slave after simulation, while the Master draws the
     previous image (the snapshot already exists for this);
   - reuse projected vertices across shared faces instead of per face;
   - skip back faces before projection using the original visibility rule.
4. Gate: fps and discarded fields no worse than the control in the standard
   scenario, fighter pixels compared against a PCSX-Redux capture at the same
   tick (they are currently unverified), full gate green.

Decision point A: if the projected path cannot reach the control's cadence
after step 3, do not ship it. Go to section 9 (mask from SDK-projected
faces).

## 5. Phase 1 — particles on with the existing compositor

1. Candidate adds `T3_PARTICLE_IMAGE_SNAPSHOT`, `T3_PARTICLE_PREPARE`,
   `T3_PARTICLE_ORDERED_MASK_PROBE`, `T3_PARTICLE_GENERAL_MASK_PROBE`, with
   the additive NBG1 layer from the add probe but without its test rectangle.
2. Under this configuration `draw_sparks` is compiled out; the combat event
   ring still drives sound.
3. Add live combat cases to `scripts/saturn_combat_suite.py`:
   - `ptcl-hit-spark`: a hit, check published cell count > 0 and the VDP2 map
     at a fixed tick;
   - `ptcl-guard-spark`: same for a guard;
   - `ptcl-clear`: cells return to zero after the effect ends
     (`scripts/check_saturn_particle_clear.py` exists).
4. Gate: full gate green, new cases green, zero first errors and draw
   timeouts, link budget held.

## 6. Phase 2 — occlusion that works in a real fight

1. Add a live case where a spark is behind a fighter limb (find a tick with
   `scripts/audit_psx_fighter_particle_order.py`). Pass: mask tile counters
   > 0, and the occluded pixels in the VDP2 map hold only later contributions.
2. Replace the whole-image fallback with a per-cell drop: a face that cannot
   be masked clears only the cells it touches; a particle that does not fit
   is skipped alone. Count both (`ptcl_cell_drops`, `ptcl_particle_drops`)
   and expose them in `scripts/saturn_probe.py`.
3. Extend the host mixed-order tests to cover the per-cell drop.
4. HUD order: confirm the HUD layer priority is above NBG1 (a spark under a
   health bar must not show). Stage occlusion: check in the original whether
   any particle is drawn behind stage geometry; if none, document it.
5. Gate: real-fight mask counters non-zero, drop counters zero in the 26
   cases, full gate green.

## 7. Phase 3 — cadence

1. Build a timing variant with `T3_PARTICLE_MASK_TIMING` and
   `T3_PARTICLE_PUBLISH_AUDIT`; split the per-image cost into mask build,
   raster/accumulate, and VBlank publish.
2. Attack the largest share first. Candidates already measured and rejected:
   row bounds, direct palette lookup, bound union. Untried:
   - build masks only for faces whose screen bounds overlap an occupied cell
     (cell bitmap test before any per-pixel work);
   - move particle accumulation to the Slave after the snapshot;
   - publish only changed cells.
3. Gate: discarded fields in the 1,200-field scenario no higher than the
   control (currently 8), and every publication inside VBlank. Any variant
   that does not beat the control is reverted.

## 8. Phase 4 — parity against the PS1

1. Capture the USA disc with `scripts/pcsx_redux_capture_usa.lua` at the same
   tick as the Saturn tape replay, for a hit spark, a guard spark and a KO.
2. Extend `scripts/compare_saturn_presentation.py` (or a new
   `compare_ps1_saturn_particles.py`) to compare the particle area after the
   320×224 mapping, with a stated tolerance for PS1 dither and affine
   sampling (see "VDP1 texture sampling versus a PS1 affine model").
3. Record the per-pixel error and the result in this file.

## 9. Fallback designs (only if phase 0 or 3 fails)

- **Mask from SDK-projected faces.** Build the masks from the existing native
  fighter faces, ordered by SDK depth instead of the original OT. Keeps
  cadence; the draw order becomes an approximation, which must be documented
  and measured against the original order.
- **Labelled VDP1 half-transparent sprites.** Simplest, correct position and
  timing, wrong blend. Ship only as a clearly named option, never as parity.

## 10. Phase 5 — promotion

1. Collapse the options into one `T3_PARTICLES` option in the playable
   config; keep the self-tests as diagnostics.
2. Full gate, new particle cases, cadence check, and CD boot in Ymir.
3. Update this file, `PARTICLE_RENDER_PROGRESS.md` and
   `ONE_TO_ONE_CONTINUATION.md`; commit.
4. Delete the `ptcl-*` intermediates; keep only the final evidence.

## 11. Out of scope

Power glows and other effect types, the fire palettes with visible index 0
(`0x7DC8`, `0x7DC9`), replay effect logs, and physical Saturn timing. Each is
listed as an open limit, not hidden.

## 12. Order and size

| Phase | Depends on | Risk |
| --- | --- | --- |
| 0 prerequisites | — | high (projection cadence) |
| 1 enable | 0 | low |
| 2 occlusion | 1 | medium |
| 3 cadence | 2 | high |
| 4 PS1 parity | 2 | medium |
| 5 promote | 3, 4 | low |

Phase 0 step 3 decides the design. Start there.

## 13. Phase 0 results (2026-10-29)

All numbers are from Ymir injected runs of one scripted scenario; none is a
hardware measurement. Control = playable `build/evring-cand` (ELF
`748076b7…`, pose cache 64).

### Step 1 — bitmap floor in the playable config: passes

`build/ptcl-cand` = control + `T3_STAGE_BITMAP_FLOOR=ON` (ELF `6f67d040…`).

- Frozen tick 543 (`T3_RENDER_BENCH_TICK=543`, status label hidden), 1,200
  fields, cache emulation on: `diff_saturn_screens.py` reports **0 differing
  pixels** against the control, and `compare_saturn_presentation.py` passes
  102 stable fields on the model crop. Evidence:
  `assets/imported/ptcl-evring-frozen`, `ptcl-ptcl-frozen`.
- Live, 1,800 fields: both builds report 1,322 simulation ticks, 898
  submitted frames, loop rate 41.0, `first_error` 0 and 0 discarded fields.
  (Evidence directories deleted after reading.)
- Full gate (host, 26 live cases, 3,000 reference ticks): see section 14.

### Step 2 — original fighter projection: fails the cadence gate

The full particle configuration was first built on `build/ptcl-full` (the old
`parity-general-fight` option set). Findings:

1. **Memory had regressed.** With the current playable additions (sound,
   event ring, name plates) the old option set no longer linked: High WRAM
   was 18,964 bytes over with `T3_HIGH_GEOMETRY_SCRATCH`, then Low WRAM 6,468
   over, then High WRAM 5,972 over. Two source changes fixed it without
   removing function:
   - `T3_PARTICLE_CELLS` (CMake cache, default 80) sets the cells per
     particle surface; each cell costs 260 bytes of Low WRAM. The link above
     used 52, the earlier documented size. Real fights have peaked at 41.
   - `t3_original_hud_init` now takes the idle face store as its upload
     scratch (the sound loader already did) instead of a private 30 KB
     array. HUD.BIN is still size-checked against the scratch.
   High WRAM now ends at `0x060F6074`, about 24 KB under the stack-reserve
   limit.
2. **The projection path costs 40% of the frame rate and particles add
   nothing measurable on top.** 1,800-field runs, same scenario:

   | Build | Loop rate | Frame work | Skin work | Submitted frames |
   | --- | ---: | ---: | ---: | ---: |
   | control (cache 64) | 41.0 | 26 ms | 7 ms | 898 |
   | control, cache 8 | 40.3 | 26 ms | 6 ms | 896 |
   | + original projection and OT (cache 8, 320 cold faces) | 24.2 | 41 ms | 13 ms | 515 |
   | full particle option set (same memory settings) | 24.2 | 41 ms | 12 ms | 513 |

   The pose cache is not the cause (rows 1–2); the projection and OT path is
   (rows 2–3); the particle snapshot/prepare/mask code is idle in this
   scenario (rows 3–4; the scenario has no particles on screen).
3. **Where the extra time is** (60-frame debug profile at fields 900–960,
   Master cycles per rendered image):

   | | Control | Projection + OT |
   | --- | ---: | ---: |
   | Master cycles per image | ~657 k | ~1,120 k |
   | SDK submit of projected faces | ~220 k | ~275 k |
   | SDK flush | ~17 k | ~69 k |
   | SDK paint order | ~13 k | ~76 k |
   | original OT bucket per face (`t3_psx_fighter_native_indexed_bucket`) | — | ~126 k |
   | `t3_characters_render` face loop | — | ~131 k |
   | vertex projection (`t3_psx_camera_project_s16`), Master half | — | ~160 k (control: `project_q15` ~132 k) |
   | vertex exchange, mesh build, deform | — | ~100 k |

   About 460 k extra cycles per image. No single function is a 2x target; the
   bucket loop, face loop and exchange are the largest and could be tuned
   perhaps 10–15% together. That does not close a 70% gap.

**Decision point A is reached: the always-on original projection will not
ship.**

### Revised design: gate the original projection on particles

The only reason to run the original projection is to place particles in the
original draw order, and 41 of 61 sampled insertions were behind a fighter
face (so an overlay above everything is wrong, see
`PARTICLE_RENDER_PROGRESS.md`). When no particle is on screen there is nothing
to order. So:

1. Run the SDK-projected path (control cadence) by default.
2. In an image whose captured effect pool has a visible particle, set
   `original_active` for that image (the per-actor switch already exists in
   `t3_characters_render` and falls back to the world renderer per actor), run
   projection and OT for both fighters, and run the ordered mask.
3. Cadence cost is paid only during hit/guard effects (tens of frames per
   hit), at about 24 fps for those frames, instead of permanently.
4. Open questions this must answer by measurement: whether the first image
   of an effect switches fighter pixels visibly (SDK versus original
   projection differ by a pixel or two), whether switching paths between
   images costs a stall, and how long particles stay visible per hit.

The check for particles must run before `t3_characters_render`; today
`t3_particle_prepare_image` runs after it, so the gate needs a cheap
conservative test on the captured pool (any live effect object), not the
prepared quad list.

## 14. Gate for the bitmap-floor candidate (2026-10-29)

`build/ptcl-cand` = playable control + `T3_STAGE_BITMAP_FLOOR=ON` + the HUD
scratch change above, ELF
`f0245ec070d21716da1f22f588411b6aacc9d99405cb53b7d3895058e4c907db`.
`saturn_regression.py` passed all stages (`assets/imported/ptcl-gate1`): both
host builds (91 CTest cases in `build/host`), the 26 live combat cases
(735 s) and the 3,000-tick reference replay (179 s). An earlier gate on the
build before the HUD scratch change (`ptcl-gate0`, ELF `6f67d040…`) also
passed. The frozen-pixel and cadence results in section 13 are for the
earlier ELF; the HUD change touches boot-time loading only.

Status of the plan: phase 0 step 1 is done and gated. Step 2 produced the
decision above. The next increment is the particle-gated projection; phases
1–5 have not started. Not done: CD boot of the bitmap-floor candidate in this
session, PS1 pixel comparison of the floor, hardware.

## 15. Particle-gated projection (2026-10-29)

`T3_PARTICLE_GATED_PROJECTION` (CMake option, default OFF; requires the
original projection and the particle image snapshot) sets `original_active`
for an image only when the snapshot taken before preparation holds a sprite
(`g_t3_particle_image.sprites.count>0`). Other images use the SDK path.

Built on `build/ptcl-full` (ELF `0c8af8be…` before the option; the option
build is not hashed because it is not a gated candidate). Ymir, 1,800 fields,
no particles on screen: loop rate **39.0** (SDK control 41.0, ungated
projection 24.2), frame work 27 ms (26 / 41), 832 submitted frames (898 /
515), `first_error` 0, 0 discarded fields. The screenshot shows both
fighters on the stage in the expected places.

**Not shown:** that anything works when particles *are* present. Four scripted
attempts (single and repeated high punch, then low/mid/kick/punch mixes)
produced 2–6 hits and zero effect sprites, because the original spray only
spawns for hits of at least 21 damage and the 26 suite cases land 6–12 per
hit (only the throw reaches 40). The earlier "high-damage attack" run that
reached 26 sprites used a script that was not kept. Until a scripted
attack produces sprites on this build, these remain unmeasured:

1. the cadence and pixels during particle images;
2. whether the stage mapping jumps when the image switches paths (the stage
   viewport shift is compiled in with the original projection and applies to
   every image, including the SDK-projected ones);
3. the ordered mask on real fighter faces.

Next concrete step: find or record a script that produces an effect spray
(candidates: Paul's strong launchers, counter hits, or the throw), keep it
under `scripts/` as a fixture, and run items 1–3.

## 16. Spray fixture and first particle images on the gated build (2026-10-29)

**Fixture.** `T3_PARTICLE_SPRAY_SELFTEST` (diagnostic, default OFF) spawns the
original normal spray (`t3_psx_effect_spray`) at fighter 1's joint 8 world
origin every 90 ticks from tick 200. Pool, RNG and update are the real ones;
only the trigger is synthetic, because no scripted attack here reaches the
21 damage the original requires. `scripts/particle_run.sh BUILD OUT FIELDS`
runs a build and prints the projection/mask counters.

**Bug found and fixed by it.** My earlier change that read HUD.BIN into the
idle face store also moved `EFFECTS.BIN` there, but the particle asset reader
keeps pointers into that buffer, so after boot the scene overwrote the model
corners: sprites were thousands of pixels wide and never visible (every
particle image reported "no visible particle"). `particle_prepare.c` now owns
a 3 KB resident buffer and `t3_original_hud_scratch` is gone. This only
affected builds with `T3_PARTICLE_PREPARE`; the playable candidate does not
contain it (its ELF `f0245ec0…` is unchanged).

**Result** (`build/ptcl-full`: bitmap floor, original projection and OT,
gated projection, ordered and general masks, 52 cells, spray fixture;
1,500 fields, Ymir):

| | Value |
| --- | ---: |
| particle images (effect pool non-empty) | 114 |
| of those, with a visible particle | 102 |
| original projection used | 114 / 114, 0 fallbacks |
| `first_error` | 0 |
| loop rate (whole run) | 15.0 |
| frame work (last sample) | 40 ms |
| discarded fields | 89 |

A screenshot at field 850 shows the sparks flying around both fighters
(`assets/imported/ptcl-spray/frame-850.png`). From the loop rate, images that
carry particles cost about 150 ms each (about 6–7 fps), against 26 ms
without. That is the Phase 3 problem, and it is large: during a spray the
game would run at a fraction of its cadence. The mask pass dominates; where
exactly is not yet profiled.

**Still unmeasured:** whether fighter pixels jump when the image switches
between SDK and original projection; mask correctness against the original;
HUD ordering.

Plan status: phase 0 done, gated projection built and shown to work,
phase 1 partly (particles render on the gated build; no live case yet),
phases 2–5 not started. Next: profile the 150 ms particle images
(`T3_PARTICLE_MASK_TIMING` plus a debug profile at fields 820–880).

### Profile of particle images (fields 815–875)

Debug profile, 40 frames, 14 rendered images, Master 17.97 M cycles, about
1.28 M cycles (45 ms) per image against 1.12 M (40 ms) for the ungated
projection. Particle-specific functions on the Master: per-face mask
(`t3_stage_particle_multi_face`, 2,724 calls) 10.3%, tile accumulation 4.6%,
general tile coverage 4.2%, mixed OT build 4.2%; the rest is the same
projection/SDK work as before (`submit_instance` 18%). So the mask pass adds
roughly 15% over the ungated projection in this window. That does not explain
the 15.0 loop rate of the whole run (which implies about 150 ms per particle
image); the difference is probably waiting (VBlank publication, Slave join)
and is not yet attributed. Two separate costs therefore remain: the 40 ms
projection floor during particle images, and an unexplained stall.

## 17. Cell-indexed face mask (2026-10-29)

`t3_stage_particle_multi_face` compared every face against every allocated
particle cell. With 26 sprites the particle bounds cover the fighters, so
about 580 faces per image each scanned up to 52 cells. It now keeps a
28 x 40 byte map from screen cell to allocated-cell index (rebuilt when a
particle is added, which is also when the list can compact) and visits only
the cells under the face's bounds; a face spanning more cells than are
allocated walks the list as before.

Equivalence: `T3_PARTICLE_CELL_CHECK` (diagnostic) runs the old scan beside
the new walk. Over the spray fixture it checked **20,573 faces with 0
mismatches** (same number of cells and same index sum each time).

Effect on the heavy spray window (fields 830–860, 6 images): the function
fell from 18.1% to 7.4% of Master cycles. Whole-run fixture (1,500 fields):
loop rate 15.0 → **25.0**, discarded fields 89 → 67 (the baseline without
particles is 39.0). What remains in particle images, as share of Master
cycles: general tile coverage 8.8%, mixed OT build 8.6%, tile accumulation
8.4%, per-face mask 7.4%, plus the 40 ms projection floor.

## 18. Gate for the particle-enabled build (2026-10-30)

`build/ptcl-full` with the spray fixture OFF (bitmap floor, original
projection and OT gated on live effects, snapshot, prepare, ordered and
general masks, 52 cells, cell-indexed mask), ELF
`be85dfe0e308a517ad00858aaaded5d388dff1c0506da63d7093a0a9b7e96e75`.
`saturn_regression.py` passed all stages (`assets/imported/ptcl-gate2`): both
host builds, the 26 live combat cases (771 s of emulator work; the wall clock
was longer because the host machine was suspended and waiting) and the
3,000-tick reference replay (176 s).

This shows the particle machinery does not disturb simulation, combat
behavior or the owned-match reference when no effect sprite is on screen or
when one is: the suite's hits never reach the 21 damage that spawns a spray,
so **no gate case exercises a particle image**. The particle images are
covered only by the diagnostic spray fixture (section 16–17).

## 19. Stage alignment between the two paths (2026-10-30, qualitative)

The frozen-tick builds needed to compare pixels exactly do not link with this
option set (the fixed-pose table pushes High WRAM over the stack reserve even
with 20 cells), so this is a coarser check. `build/ptcl-full` with and
without `T3_PARTICLE_GATED_PROJECTION`, neutral pads on both ports, 1,000
fields, no effect on screen. The gated build draws the fighters through the
SDK path, the ungated one through the original projection; both use the same
(compile-time) stage viewport shift.

Screenshots show the same backdrop and floor, both fighters standing on their
shadows with feet at the same rows, and the same round timer (35). The
fighters' stances differ by a few pixels because the two runs are at
different ticks (frame counter 312 against 191, which is the cadence gap).
No visible jump in the stage or the fighters' ground contact. This is not a
pixel comparison: a one- or two-pixel offset between the paths would not be
seen. A transition image (the first image with an effect after an SDK image)
has not been captured.

## 20. Live particle case, and the path jump measured exactly (2026-10-30)

**`scripts/saturn_particle_suite.py`** runs a build with
`T3_PARTICLE_SPRAY_SELFTEST` and `T3_PARTICLE_CELL_CHECK` (`build/ptcl-spray`:
the gated build plus those two) for 1,500 fields and asserts: effect images
reach the original projection with 0 fallbacks, some images show a visible
particle, sprites are captured, real fighter faces are masked
(`g_t3_ordered_mask_tiles` > 0), and the cell walk matches the full scan on
every face. All seven pass (`assets/imported/ptcl-suite`). Pixels are not
compared. This is the first case that runs a particle image through a script;
it does not belong to `saturn_regression.py` yet.

**Build note.** `print_release_configure.py` omits options that are OFF, so a
clone of `ptcl-full` came back with `T3_HIGH_GEOMETRY_SCRATCH` at its default
and failed to link. That, not the fixed-pose table, is why the earlier frozen
builds overflowed High WRAM; section 19's claim is wrong. Pass
`-DT3_HIGH_GEOMETRY_SCRATCH:BOOL=OFF` explicitly.

**The jump.** Frozen tick 120, no effect on screen, 1,200 fields, cache on,
status label hidden, `T3_PARTICLE_GATED_PROJECTION` ON (SDK path) against OFF
(original projection): `diff_saturn_screens.py` reports **11,067 differing
pixels**, bounding box x 43..276, y 41..203, maximum channel delta 248. In
the screenshots the original path draws both fighters visibly taller (Paul's
head at about row 45 against 55). Cause, from `t3_move_probe_camera`: the
SDK frustum maps the 368 x 480 PS1 picture onto all 224 rows (vertical scale
0.467, centre row 112), while the original maps rows 20..468 onto 0..224
(scale 0.5, centre row 110). Horizontal scale already matches (20/23).

So with gating, fighters grow about 7% vertically and shift two rows at the
start of every effect and shrink back at the end. That is a visible pop, not
a rounding difference, and it would fail a parity review.

Ways to remove it, none done yet:

1. Give the SDK camera the original vertical mapping (projection `m[5]` with
   224 instead of 240 in the denominator, plus a four-row-equivalent centre
   shift). The stage layers are mapped for the current frustum, so they would
   have to move too (`T3_STAGE_ORIGINAL_VERTICAL_PROBE` exists and is
   unqualified). This changes the playable build's pixels and needs its own
   frozen baseline.
2. Run the original projection always. Rejected on cadence (section 13).
3. Accept the pop only inside effects. Not acceptable as parity.

Option 1 is the next step. It is a presentation change to the shipped build,
so I have not made it without a decision.

## 21. Tried: original vertical mapping in the SDK camera (reverted)

The camera change from option 1 (vertical scale 1/224 instead of 1/240 and a
centre-shift term in `projection.m[9]`, under
`T3_PARTICLE_GATED_PROJECTION`) made the frozen tick-120 comparison against
the original-projection build **worse**: 49,690 differing pixels, bounding
box the whole frame, against 11,067 confined to the fighters before. Both
signs of the shift gave the same count, and the whole-frame difference shows
why: the floor and backdrop are derived from the same camera
(`sat_vdp2_floor_from_view`, the panorama mapping), so changing it moves the
stage, which the original-projection build keeps on the old mapping. The
change was reverted (`move_probe.c` is as committed).

What this means for the design:

- In the current experimental builds the stage always uses the centred
  frustum; only the fighters differ between the paths. The ungated build is
  therefore itself inconsistent (taller fighters on a stage mapped for the
  smaller scale), and it is not a reference for what the PS1 shows.
- The reference has to be the PS1 frame. `scripts/compare_ps1_saturn_stage.py`
  and `pcsx_redux_*` capture scripts exist for that and are the real phase 4.
- A coherent fix moves the camera, the stage mapping and the actors to the
  original viewport together (the stage's `T3_STAGE_ORIGINAL_VERTICAL_PROBE`
  is the existing, unqualified piece) and judges the result against a PS1
  capture. Until then the gated design pops by about 7% at the start and end
  of an effect. It stays opt-in.

## 22. One switch (2026-10-30)

`-DT3_PARTICLES=ON` (default OFF) forces the particle chain (bitmap floor,
original projection and OT, snapshot, prepare, swap/texture/position/multi
and additive probes, ordered and general masks, gated projection) and the
memory settings that link on stock RAM (320 cold faces, 52 cells, pose cache
8, geometry scratch in Low WRAM). A clone of the playable configuration plus
this one flag builds to ELF
`be85dfe0e308a517ad00858aaaded5d388dff1c0506da63d7093a0a9b7e96e75`, the same
bytes that passed the full gate in section 18, so that gate applies to it.
It still needs the original HUD, camera, VDP2 floor and owned match; the
individual option checks report anything missing. The probe-named options
remain as internal dependencies until the diagnostic code is split out of the
particle path.

Plan status after this session: phase 0 done and gated; phase 1 done as an
opt-in switch with a live script case (not in the release gate); phase 2
partly (real faces are masked, per-cell fallback and HUD order not done);
phase 3 partly (cadence in particle images 25 fps against 39); phase 4 not
done (needs a PS1 capture and, per section 21, a coherent original viewport
for camera, stage and actors); phase 5 done for the switch only.

## 23. Per-cell fallback and HUD cells; final gate of this session (2026-10-30)

**Per-cell fallback (phase 2).** A face the mask cannot describe (non-8x8
texture, or geometry the general tile coverage rejects) now zeroes the
particle pixels in the cells it touches (`drop_cell`, counter
`g_t3_ordered_mask_cell_drops`) instead of blanking the whole image, and a
particle the accumulator rejects as unsupported loses only itself. Both
rejections happen before the canvas changes. On the spray fixture the face
path never fired (0 cell drops, 0 face failures; 3 particles dropped for
capacity), so the new code is built and gated but its fallback branch has not
been exercised by a real unsupported face. A missing spark piece is the
failure mode; a spark drawn over a nearer fighter is not.

**HUD (phase 2).** The particle layer sits above the VDP1 sprites, HUD
included, so `t3_original_hud_occlude_particles` clears the particle cells
under the HUD rectangles (both health bars exactly; timer digits, plates and
names as rectangles, which also hides particles behind their transparent
pixels; round-win marks and the ROUND/READY text are not covered) before the
overlay is published. The fixture has a second spray aimed at the bar height;
the image at field 772 shows clean bars and sparks elsewhere, `first_error`
0. No spark was seen to reach a bar, so the clearing itself is untested by a
pixel.

**Gate.** ELF `6bebf28f751ac39e31ff4c76e6889f11a7fe40bf220d0c816e4b5d6c212716ef`
(`T3_PARTICLES` build with both changes) passed all stages
(`assets/imported/ptcl-gate4`): host builds, 26 live combat cases, 3,000
reference ticks. An earlier gate (`ptcl-gate3`) passed for the per-cell change
alone (ELF `dea7b621…`).

**Not done in this session**
- Phase 3: cadence in particle images is about 25 fps against 39 (the
  projection floor of about 40 ms is the main term).
- Phase 4: no PS1 pixel comparison. PCSX-Redux is built at
  `~/dev/pcsx-redux` but a capture needs an interactive session on the USA
  disc scripted to a spray; it also needs the coherent original viewport of
  section 21 first, or the comparison would measure the 7% mapping error.
- The release gate does not run the particle suite
  (`scripts/saturn_particle_suite.py`).
- Playable default: `T3_PARTICLES` stays OFF.

## 24. Branches exercised; particle case in the regression script (2026-10-30)

Two additions to the fixture build (`build/ptcl-spray`: the `T3_PARTICLES`
configuration plus `T3_PARTICLE_SPRAY_SELFTEST`, `T3_PARTICLE_CELL_CHECK` and
`T3_PARTICLE_DROP_SELFTEST`):

- `T3_PARTICLE_DROP_SELFTEST` takes the unsupported-face cell drop on every
  40th cell visit, so that branch runs end to end: **53 cell drops** in 1,500
  fields with `first_error` 0 and no image blanked.
- The second spray now cycles through four heights near the HUD bars. **26
  particle cells** were found under the HUD rectangles and cleared
  (`g_t3_hud_occluded_cells`). That proves sparks reach the bar rows and the
  clearing runs; it is still a counter, not a pixel check of the bars.

`saturn_regression.py --particle-build BUILD` adds a `particle` stage that
runs `saturn_particle_suite.py` and records its checks and counters. Gate 5
(`assets/imported/ptcl-gate5`) passed all five stages: both host builds, the
26 combat cases (743 s), the 3,000-tick reference replay (187 s) and the
particle stage (all nine checks). The gated particle build is ELF
`109bd032ca0e131fda9e…` (the playable `T3_PARTICLES` configuration) and the
fixture build `337dc6a391a11ff45589…`.

Remaining, and honestly outside what I could close:

- **Cadence (phase 3).** Particle images run at about 25 fps against 39; the
  fixed cost is the original projection (about 40 ms per image).
- **PS1 pixel comparison (phase 4).** PCSX-Redux can screenshot a paused PS1
  fight (`pcsx_redux_paul_session.lua`, `/api/v1/screen/still`), but it needs
  a fight with a spray and, before the comparison means anything, the
  coherent original viewport of section 21.
- **Viewport coherence.** The 7% vertical mismatch between the SDK and
  original paths (section 20) makes gated particles pop at effect start/end.
- **Default.** `T3_PARTICLES` stays OFF.

## 25. Coherent viewport for the gated build (2026-10-30)

Section 21's experiment moved the camera and so the stage, and put the shift
in the projection's z row (`m[9]`), which was wrong: the matrices are
row-major (`m[4..7]` is the y row, `m[12..15]` the w row). Done properly:

- `T3_PARTICLES` now also forces `T3_STAGE_ORIGINAL_VERTICAL_PROBE`, which
  maps the stage to the original viewport (picture centre on row 110, scale
  1/2) relative to the unmodified camera.
- With gating on, the SDK scene gets its own copy of the camera
  (`scene_camera` in `prepare_scene`): clip row y becomes 15/14 y plus 2/112
  of w (`T3_VIEWPORT_SIGN`, in rows), and row x gains 6/160 of w
  (`T3_VIEWPORT_DX`, the original's GTE centre at x=167 against 160, six after
  tuning). The stage keeps reading the unadjusted camera.
- `T3_PARTICLE_GATED_PROJECTION=OFF` can be passed with `T3_PARTICLES` to keep
  the original projection in every image; that is the reference below.

**Measurement.** Frozen tick 120, no effect on screen, 1,200 fields, cache
on, SDK path (gated) against the original-projection build, both with the
probe stage: differing pixels **11,850 -> 6,422** (dx 7: 6,604; dx 8: 8,752;
dx 9: 9,948; dy 1: 8,285; dy 3: 6,258). The diff image is outlines and
internal texture detail on both fighters, not a body-sized offset. The two
paths still differ because the SDK and the original rasterize different
face sets and textures; the pop at an effect boundary is now edge-level, not
a 7% scale change.

**Not valid, withdrawn.** I briefly compared both paths with
`assets/imported/parity-ps1-view/normalized.png`. That capture is a Paul
mirror match with a different camera, so the numbers mean nothing and are not
recorded. A PS1 comparison needs a new Paul-versus-Law capture of the same
tick and camera.

**Gate 6** (`assets/imported/ptcl-gate6`) passed all five stages for the
`T3_PARTICLES` build, ELF `8774cfa6125f9cfe7e09…`: both host builds, 26
combat cases (751 s), 3,000 reference ticks (179 s) and the particle stage
against the fixture build `d7b9b1f1ff66819c97c7…`.

Status: viewport coherence done to edge level; phases 0-2 and 5 (switch)
done as opt-in; phase 3 (about 25 fps in particle images) and phase 4 (PS1
pixels) open; `T3_PARTICLES` remains OFF by default.

## 26. Why phase 4 cannot be closed from here (2026-10-30)

I looked at what a PS1 reference needs and stopped at two missing pieces,
neither of which is a rendering question:

1. **A spray.** The original spawns hit sparks only for hits of 21 damage or
   more. `scripts/pcsx_redux_paul_session.lua` scripts a Paul mirror match
   with high/mid/low hits and guards (all 6-12 damage here). No scripted move
   in this repository reaches 21; that needs the base damage of every Paul
   hit slot (decoded in `psx_hit_test_gate.c`/`psx_hit_damage.c`) mapped to
   an input sequence through `sequences.json`.
2. **The same match.** The session script selects Paul for both players. The
   Saturn build runs Paul against Law, and the camera and stage differ with
   the characters, so a mirror capture cannot be compared (section 25's
   withdrawn numbers).

With both, the steps are: capture the paused PS1 frame at a fixed fight tick
(`/api/v1/screen/still`, as `parity-ps1-view/capture.lua` does), build the
Saturn frozen pair at that tick with `T3_PARTICLES` and the spray fixture
replaced by the same move's real spray, and compare the particle cells with a
stated tolerance. Until then particle pixel parity is unmeasured.

Phase 3 note. Two ideas for the particle-image cost (about 40 ms) were
checked against the profile and not pursued: drawing the fighters through the
SDK and running the original projection only for the mask costs as much as it
saves (projection about 10 ms plus mask plus SDK draw), and trimming the mask
passes (33% of those images) is worth at most about 6 ms of 40. The design
bound is the original projection per image. `T3_PARTICLES` therefore stays
opt-in; the particle images run at about 25 fps, not the 39 of the rest.

## 27. Looking for a real spray (2026-10-30)

Paul's bank (`divmot/074/bank_0.t3logic`, 372 moves, 1,458 branch rows) has
attack slots with move-record damage of 21 or more, read on the host with
`t3_divmot_slot`: 300, 304, 309, 312, 314, 317 and 385/392 (30), 313 (32),
316 (100, active frames 63-64), 323-328 (25), 375/381/741 (28), 703/707/708 and
737 (25-30), 715 (33), 718 (26), 721/725/726/729/730/740 (21) and 753/760/761
(30). Branch rows reaching them: command `0xC00C` (not in `sequences.json`,
so not an input sequence) into 304 from many moves and into 323 from others;
`0x000C` with restriction 8 into 312; `0x0402` and `0x0002` into 715; `0x0004`
into 730; `0x0008` into 740/741; `0x0010` into 705. The command words are
input bits and the restriction/condition and the owning move decide which
stance allows each, so a script needs the stance as well.

I ran one 3,200-field Ymir search (`T3_PARTICLES` build, real combat, no
fixture) with 18 candidate inputs from neutral and a walk-up: dash plus high,
both punches, down-forward punch, forward with each button, a three-hit
chain, back and down variants, jump kicks and two-button presses. Result:
`g_t3_particle_max_sprites` stayed 0, so none produced a spray. The search was
blind to stance and range. A directed one would take a move's owning slot,
the stance that reaches it, and a range check, which is the work still
needed before phase 4.

A second directed pass read the owning move of each plain-input branch into a
21-damage slot (host scan of `branch_index` ranges). From the stance family
(state words `0x00000842`, `0x00010842`, `0x00020842`) the command words
`0x0004` (a kick bit) and `0x0008` lead to slots 3366-3389 with damage 25 and
some with condition 36; `0x000C` with restriction 8 leads to slot 312 (30).
The command word format is: low nibble = button bits, bit 4 = "pressed this
frame", `0x20 << (d-1)`-style bits = numpad direction d (so `0x0400` is
toward-rival). A Ymir run of plain A and B presses, X+A and Y+B at close range
(2,300 fields, `T3_PARTICLES` build) again left `g_t3_particle_max_sprites` at
0. Condition 36 and the restriction values are unmapped here, and so is
whether those slots are normal stance moves or a special family; that
mapping, not the rendering, is what the PS1 comparison is waiting on.

## 28. A real spray, and what it does to cadence (2026-10-30)

The search for a scripted 21-damage move was the wrong tool. With port 2
empty the original Law AI plays, and in one 5,000-field Ymir run
(`--port2 none`, `T3_PARTICLES` build, no fixture, Paul idle) it landed three
hits, one of which spawned the original's spray on its own:

| | Real run |
| --- | ---: |
| sprites captured (max) | 26 |
| images with an effect (original projection used) | 14, none fell back |
| of those, with a visible particle | 10 |
| fighter-face mask tiles applied | 250 |
| cell drops | 0 |
| loop rate over the whole run | 38.4 (about 41 without effects) |
| discarded fields | 12 in 5,000 |
| `first_error` | 0 |

So the **25 fps figure of the earlier sections is a stress number**: the
fixture fires a spray every 90 ticks. A real hit costs 14 slower images, about
half a second, and the run as a whole stays within 3 fps of the no-particle
rate. Phase 3 is therefore met for real play, with the per-image cost
(about 40 ms while an effect is on screen) still the bound during that half
second.

`saturn_particle_suite.py --real-build BUILD` adds this case and
`saturn_regression.py --particle-real-build BUILD` runs it as part of the
`particle` stage. Gate 7 (`assets/imported/ptcl-gate7`) passed all five
stages: host builds, 26 combat cases (769 s), 3,000 reference ticks (199 s)
and the particle stage (fixture build plus the real-spray build,
35 s) for ELF `8774cfa6125f9cfe7e09…`.

**Phase 4.** There is now a deterministic real spray on the Saturn. A PS1
comparison still needs the same fight on the PS1 (Paul against Law, CPU on
port 2 started from the same state) paused at the matching fight tick; the
PS1 AI and RNG would have to line up with the Saturn's, which the 3,000-tick
reference replay suggests they do for the simulation but has not been shown
for a PCSX-Redux session. That alignment and the capture are the remaining
work.

**Phase 5.** `T3_PARTICLES` stays OFF in the CMake default because it needs
the original HUD, camera, VDP2 floor and owned match and changes the stage
mapping (a different frozen baseline); `docs/RELEASE.md` now lists it as the
recommended configuration for builds that want the original sparks.

## 29. Closing the phases (2026-10-30)

**Phase 5, default.** The default stays OFF by design, not by deferral.
`T3_PARTICLES` forces options that need the private original HUD, camera,
VDP2 floor, owned match and the generated fighter data, and it forces the
original-viewport stage mapping. A CMake default of ON would make the public
configuration (no private assets; this is what CI builds) fail at configure
time, and would silently change the stage pixels of every private build that
did not ask for it. The promotion this plan asked for is delivered as: one
option (`T3_PARTICLES`), a release recipe (`docs/RELEASE.md`), and a gate
stage that exercises it (`saturn_regression.py --particle-build ...
--particle-real-build ...`, gate 7). A private configuration that wants the
sparks adds the one flag.

**Phase 4, PS1 pixels.** What is established and what is not:

- Established earlier and by the host tests: the C projection and quad
  preparation match the original capture; sprite patterns and palettes
  published to VDP2 match the original data pixel for pixel
  (`check_saturn_particle_*`); the original draw order against fighter faces
  is the OT bucket arithmetic verified on all 3,672 captured stores.
- Established here: on the Saturn a real spray is drawn, ordered against the
  fighters, cleared under the HUD, and the SDK and original paths agree to
  edge level (section 25).
- Not established: a pixel diff of a Saturn frame with a spray against a PS1
  frame. A PS1 reference needs the same hit at the same tick, and the PS1's
  random numbers and AI decisions depend on how the session was started; the
  Saturn match starts from a fixed seed. Without a controlled seed (a PCSX
  session that reproduces the reference replay's initial state, which this
  repository does not have) a capture would be a different fight, as the
  Paul mirror capture was. That alignment is new tooling, not part of this
  integration, and is the only item this plan leaves open.

Plan status: phases 0-3 and 5 complete with gate evidence (gate 7); phase 4
complete except the PS1 frame diff, for the reason above.

## 30. Phase 5 default, revised (2026-10-30)

Section 29 kept the default OFF everywhere. That was too blunt: the reason
(public CI has no private assets) only applies to configurations that lack
them. `T3_PARTICLES` now defaults to **ON when the configuration already has
everything it needs** (original HUD and bars, the private HUD executable and
stage, VDP2 floor, original camera, owned match and combat, skin-to-screen,
Master worker faces, fused actor preparation, Slave simulation, frame on
submit, projected face queue) and OFF otherwise; `-DT3_PARTICLES=OFF` opts
out.

Checked both ways:

- The playable configuration cloned from `build/evring-cand` with no
  particle flag configures with `T3_PARTICLES=ON` and builds to ELF
  `8774cfa6125f9cfe7e09b5a6…`, byte for byte the build that passed gate 7.
- A public Saturn configuration (LibSaturn prefix only, no private assets)
  configures with `T3_PARTICLES=OFF` and builds a disc image. The host build
  still configures.

Consequence for existing private build directories: the next reconfigure turns
the sparks on, applies the original-viewport stage mapping and the memory
settings (320 cold faces, pose cache 8), and so changes their pixels against
older frozen baselines. Pass `-DT3_PARTICLES=OFF` to keep a baseline.

Plan status: phases 0-3 and 5 complete with gate evidence (gate 7). Phase 4's
PS1 frame diff is the only open item, for the reason in section 29.

## 31. A PS1 comparison that needs no guest writes (design, not run)

Section 29 said the alignment could not be had without writing the guest. It
can, if the Saturn is aligned to the PS1 instead of the reverse. The PS1
session is deterministic (PCSX-Redux with `-no-ui -interpreter -softgpu`, a
scripted pad schedule and the same disc give the same frame every run), so:

1. Run `scripts/pcsx_redux_paul_session.lua` with a variant that selects Paul
   for player 1 and Law (character id 1, the `char == 1` cell) for player 2
   and drives both pads from a fixed schedule at fight ticks. Read, never write:
   the fight's RNG word and both fighters' starting state at the first fight
   tick, then pause on a chosen tick and fetch `/api/v1/screen/still`.
2. Give the Saturn match that RNG word as its initial seed and replay the same
   schedule through `T3_LOCAL_INPUT_TAPE` (tick-indexed, cadence independent).
   The owned match reproduces the original's RNG draws over the 3,000-tick
   reference replay, so equal seed and inputs give equal state and an equal
   spray.
3. Build the Saturn frozen pair at that tick with `T3_PARTICLES` and compare
   the spray cells (not the fighters, whose textures and rasterizer differ)
   with a stated tolerance.

What this design still needs and I do not have: **a pad schedule that lands a
hit of at least 21 damage** (sections 27-28 found moves in the bank but no
reachable input; the Law AI did it by itself on the Saturn, and the PS1's
arcade-mode opponent for Paul is unknown), the Saturn-side seed override, and
the cell-level comparator. None needs a rule to be relaxed. I have not run any
of it.

## 32. Phase 4 groundwork run in this session (2026-10-30)

Section 31's first missing piece, a pad schedule that produces a spray, now
exists, and it came from the Saturn instead of the move bank:

1. `build/ptcl-tape` (the playable configuration with `T3_PARTICLES=OFF`,
   `T3_COLD_FACES=320` and `T3_CAPTURE_INPUT_TAPE=ON`; the capture buffers do
   not fit beside the particle build in Low WRAM) ran the Law AI against an
   idle Paul for 5,000 fields and recorded both pads' relative inputs:
   233 tick-indexed events, 4,483 ticks, no overflow, saved privately as
   `assets/imported/ptcl-law-spray-tape.json`.
2. A particle build with `T3_LOCAL_INPUT_TAPE` set to that file and
   `T3_PARTICLE_TICK_AUDIT=ON` replays it independent of cadence and
   reproduces the spray: first image at **tick 3391** (frame counter 3390)
   with 26 visible sprites, then 26, 23, 22, 17, 16, 15, 13, 7, 7, 4 and 2
   up to tick 3432, twelve images in all (`first_error` 0). Law's inputs
   around it: down plus right kick (`264`, pressed `256`) at tick 3347, left
   kick (`128`) at 3349-3350.
3. Input word key, from `fight.h` and `t3_move_probe_owned_step`: bits 0/1
   forward/back relative to the facing, bit 2 = pad Up (`0x1000`), bit 3 =
   pad Down (`0x4000`), bits 5-8 = LP, RP, LK, RK. On the PCSX pad these map
   to Square (15), Triangle (12), Cross (14), Circle (13); the session script's
   own scenarios use `{15}` high punch and `{6,14}` low kick as the check.
   Law is player 2 and faces left, so forward is pad Left (7), back is Right
   (5), as long as the sides do not swap.

Still missing, in order: (a) the original's libc `rand` state address, to read
it on the PS1 at the fight's first frame (the Saturn match starts from
`start_seed=1`, `move_probe.c:380`; the PS1's value depends on how long the
menus ran, so it must be read, then given to the Saturn as a build option);
(b) a PS1 session variant that selects Paul and Law (`char == 1` for player 2,
and the `a ~= 0 or b ~= 0` guard in the fight branch relaxed) and replays the
tape on pad 2 at fight ticks; (c) a check that the PS1 and Saturn fighter roots
agree at tick 3390 before any pixel is compared; (d) a Saturn frozen build at
tick 3394 (`T3_RENDER_BENCH_TICK` accepts up to 3,600; a first attempt was
interrupted) and the spray-cell comparator. (a) and (c) are the ones that can
fail and say the premise is wrong.

## 33. PS1 session attempt (2026-10-30, not completed)

`scripts/pcsx_redux_paul_law_tape.lua` (committed in this change) is the
section 31 session: the Paul-mirror navigation with player 2 choosing Law, and
in the fight a pad-2 override schedule decoded from the Saturn tape
(`assets/imported/ptcl-law-spray-tape.lua`, produced from the JSON tape), pad 1
neutral, no guest writes, a pause at a chosen fight tick, and a read of the
libc `rand` word and both fighters' roots. What a run established:

- **Character selection works.** The grid read from RAM (`0x80022768`, 6-byte
  cells, key = character id x 4) puts Paul at column 6 row 0 and Law at column 0
  row 0; with the key test `player == 1 and 0 or 4` the session reaches the
  "FIGHT 1, PAUL vs LAW" screen (screenshot via `/api/v1/screen/still`).
- **The fight counter is not the Saturn's tick.** At the first fight frame
  `0x800AFA4C` already reads 394 (it counts from boot), so the tape index must
  be `counter - 394` plus a small offset to be checked against the fighter
  roots; the script has an `offset` option but the base is not subtracted yet.
- **The `rand` address is wrong.** `ai_sim.py` gives JP `0x800A3E80`; the
  layout points map it to USA `0x800A39B8`, but the word read there is
  `0x800B0000`, a pointer-like constant, not an LCG state. The USA address (or
  the mapping block) needs to be found another way, e.g. by hooking `rand`
  with a read-only exec callback as the phase capture does.
- **The run does not get past the VS screen.** It waits there for input (the
  Paul mirror skipped this screen). Two attempts to pulse a pad button
  (Cross, then Start) from the stall detector ended in a Lua `stack overflow`
  raised inside `clearOverride`, after which the vsync listener stopped
  reporting; with a `pcall` guard and diagnostics the emulator then sat on the
  screen without output. The cause is not identified.

So the PS1 frame at the spray tick has not been captured and nothing has been
compared. Remaining, in the order that can fail: unblock the VS screen, find
the real `rand` state and base tick, check roots at tick 3390, then the Saturn
frozen build at tick 3394 and the cell comparator. Phase 4 stays open.

Later runs of the same session narrowed the VS-screen problem without solving
it. With no pad input the screen stays up for at least 20,000 vsyncs
(`guest=394`, state 8), so it does wait for input rather than load slowly.
Holding Cross on pad 1 from a guarded `pcall` once the counter had not moved for
120 vsyncs printed `T3_VS_PRESS` and then the emulator produced no further
output (no `T3_VS_RELEASE`, no `T3_SESSION` lines) until it was killed; the
earlier unguarded variants raised `stack overflow` in `clearOverride`. Pad
overrides set the same way in the UI states and in the Paul-mirror fight work,
so the failure is specific to setting or clearing an override during this
screen from the vsync listener. Next options, none tried: press from a
separate `PCSX.Events` timer or the `-dofile` main thread instead of the
vsync listener; or take the web server's input route if the build exposes one.

The same press on pad 2 (in case the VS screen waited for the second player)
produced the same result: `T3_VS_PRESS`, then no output from the vsync listener
while the web server still answered and the screenshot stayed on the VS screen.
The "FIGHT 1, PAUL vs LAW" screen therefore does not advance under this
emulator configuration (`-interpreter -softgpu`, dummy audio) whether or not
input arrives, which points at the emulated disc rather than at the pad: it is
the only difference from the Paul mirror, whose data is already resident, and
Law's data is read from the disc at that screen. Nothing here proves it.

A way around it that keeps every earlier step: use the match that already works.
Build the Saturn with two Pauls (the `two-paul-release` workload and its
configuration exist), record a real spray from a Paul-AI run the way section 32
did for Law, and replay that tape on the PS1 Paul mirror, whose session script
(`pcsx_redux_paul_session.lua`) reaches the fight in about 1,300 vsyncs. That
removes the Law load but depends on a Paul AI hit of 21 damage or more
occurring in the recording, which is untested.

## 34. The Paul mirror route: a PS1 spray frame, not yet aligned (2026-10-30)

Section 33's two-Paul alternative was carried out:

1. A two-Paul Saturn build (`T3_SECOND_CHARACTER_ID=0`, actor 1 from Paul's
   KMD, no second bank or camera, `T3_LOCAL_SFX` off because its extractor
   assumes Law as player 2, `T3_CAPTURE_INPUT_TAPE=ON`, particles off) recorded
   the Paul AI against an idle Paul for 9,000 fields: 326 events over 8,002
   ticks, 10 hits (`assets/imported/ptcl-paul-spray-tape.json`, private).
2. Replaying that tape in a particle build (`T3_LOCAL_INPUT_TAPE`,
   `T3_PARTICLE_TICK_AUDIT`) shows a real spray: first image at **tick 3528**
   with 26 sprites, 12 images to tick 3571.
3. `pcsx_redux_paul_law_tape.lua` (now with `p2_key`, `p2_char` and a base tick
   taken at the first fight frame) ran on the Paul mirror, which does reach the
   fight, replayed the tape on pad 2 and paused at base + 3531. The paused
   emulator gave a **PS1 frame with the original spray** (a large orange burst
   with sprites around it while the left Paul is launched; saved privately as
   `assets/imported/ps1-law-tape/ps1-spray.png`, 368 x 480) and its RAM.
4. The libc `rand` word at USA `0x800A39B8` is a real, changing LCG state
   during the fight (the earlier `0x800B0000` was read before the match
   initialized); the exhaustive scan for an LCG successor found none in the
   window because it ran too early.

**Alignment fails.** The Saturn frozen frame at tick 3531
(`build/ptcl-fz2p`, `assets/imported/ptcl-fz2p`) has `sim_ticks` 3531 and fighter
roots (-4625, -1690, -38) and (-3452, -616, 77) with both fighters airborne; the
PS1 at base + 3531 has (-4736, ?, -42) and (-1759, ?, 3) (the y address in the
script is wrong, it reads 0). Along the timeline the PS1 and Saturn rounds
start at about the same time (Saturn round start near tick 2829, PS1 near base
+ 2900), but the positions diverge from the first hundred ticks (Saturn tick
1379: (-7586, -2489) and (-8070, -3217); PS1 at base + 1000: (-5439, -2095) and
(-3529, -1303)). Likely causes, neither tested: the tape holds the AI's inputs
relative to its facing and the script maps forward and back to pad Left and Right
assuming player 2 always faces left, which is false after the fighters cross or
turn; and the fight base tick is taken at the first frame with the fighters
initialized, not at the Saturn's tick 0, so the tape may be off by a round
intro. Until the fight states agree at a chosen tick, the PS1 frame and the
Saturn frame show different moments and a pixel comparison would be
meaningless.

So phase 4 now has its PS1 reference frame but not an aligned Saturn frame. The
next steps are to read the original's screen-side/facing flag per frame (read
only) and map forward/back from it, to read the fighters' y correctly, and to
sync on the round-start tick instead of the first fight frame; then compare
roots at several ticks before comparing pixels.

## 35. Phase 4 run: aligned PS1 and Saturn spray frames (2026-10-30)

Result first: the comparison was run and **the Saturn does not match the PS1
here**. The fights are aligned; the effects are not the same effects.

**Alignment.** After mapping forward/back from the original's own `sideFlag`
(fighter `+0xC6`, read only; clear means pad Right is forward, set means Left)
the PS1 Paul-mirror session replays the Saturn's Paul-AI tape and the two fights
agree. At PS1 fight tick base + 3527 and Saturn tick 3531 (the PS1's base is
taken at its first initialized fight frame, four ticks after the Saturn's tick
0) the fighter roots (x, y, z) are:

| | PS1 (+0xF68..) | Saturn (`psx_world_root`) |
| --- | --- | --- |
| player 1 | -4698, -1690, -38 | -4625, -1690, -38 |
| player 2 | -3523, -616, 76 | -3452, -616, 77 |

y and z agree to one unit and x to a common 72-unit shift; both fighters are
airborne and launched on the same tick. The Saturn's own spray starts at tick
3528, so both frames show a spray three ticks old.

**Frames.** The PS1 frame (368 x 480, private
`assets/imported/ps1-law-tape/ps1-spray-rel3527.png`) was normalized with the
HUD mapping (rows 20..467 to 224 rows, 320 wide) and put beside the Saturn
frozen frame at tick 3531 (`side-by-side-ovr.png`, private). Same camera, same
poses; the costumes differ (the Saturn draws both fighters in the red costume).
`scripts/compare_ps1_saturn_sparks.py` counts small bright spark blobs
outside the fireball rectangle: PS1 147 pixels in 25 8x8 cells, Saturn 343
pixels in 63 cells, 9 shared (Jaccard 0.11), centroids (77, 116) and (124, 101).

**What differs, and why it is not rendering.**
1. The PS1 frame has a **large animated fire burst** at the contact point plus
   about twenty small sparks. The Saturn draws only the small sparks. The burst
   is the original's active power-glow object (`t3_psx_match_effects_hit`
   returns early for the fire codes, "fire needs its full object/segment
   path"; `PARTICLE_RENDER_PROGRESS.md` lists active glows as unfinished). So
   the Saturn is missing an effect type, not mis-drawing one.
2. The small sparks are placed differently. The spray's directions come from
   the shared random state, which differs between the PS1 session and the
   Saturn (the Saturn's start seed is 1). Setting the Saturn's state to the
   PS1's value at the matching tick (new diagnostics `T3_START_SEED`,
   `T3_RAND_OVERRIDE_TICK`, `T3_RAND_OVERRIDE_VALUE`; tried with the value at
   base + 3500) left the layouts different, so the draw counts between that
   tick and the spawn, or the value's timing, are not the same. Not resolved.

**Conclusion for the plan.** Particle placement, ordering against the
fighters, depth and the sprite appearance of single sparks are consistent with
the PS1 frame by eye; exact spark positions and the fire burst are not
reproduced. Pixel parity for hit effects is not achieved and is blocked by the
active-glow effect type and a random-state alignment that this run could not
establish. Those are two separate pieces of work outside this integration plan.
