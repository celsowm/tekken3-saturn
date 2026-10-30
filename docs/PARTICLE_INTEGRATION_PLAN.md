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
