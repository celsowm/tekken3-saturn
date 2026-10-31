# Strong-hit slowdown investigation

The published title build enables the individual ordered-particle options even
though its cached umbrella option `T3_PARTICLES` is OFF. Strong hits of at
least 21 damage can spawn 20 plus 6 sparks and the attacker's flipbook, as
documented in the pinned Tekken3Remake `docs/research/code/effects.md`.

On the published ELF `a2e7ce58c20e89df0ba7d43271e0a7ef9d4e3cfe602261138fdcd7526fddc35e`,
a 5,000-field Ymir injection with idle Paul and CPU Law captured a maximum of
26 sprites, 14 images through the mixed particle/fighter ordering path, 10
images with drawn particles and 231 fighter mask tiles. Both fighters used the
original projection for those 14 images. The run reported 14 discarded fields
and no first error. These are cumulative counters; they do not establish that
the discarded fields occurred during the effect or measure the effect's cost
on this ELF. Evidence: `assets/imported/slowhit-release/observation.json` and
`symbol-dumps.json` (private, ignored). Ymir injection is not a CD boot or
hardware measurement.

The strongest existing timing evidence is the earlier real-spray experiment in
`PARTICLE_INTEGRATION_PLAN.md` section 28, on a different ELF: 26 sprites,
14 effect images, 12 discarded fields over 5,000 fields, and roughly 40 ms
per effect image in the associated profiling. That work identified original
fighter projection and the ordered mask/composition as expensive. A separate
older stress profile had substantial unexplained waiting, so these numbers
must not be attributed directly to the published build. The path projects the
fighters, sorts their faces with particles, rasterizes additive tiles and masks
the tiles against overlapping fighter faces.

Hit freeze is also present in the original combat rules (`move_clock.c` and
Tekken3Remake `combat.md`): it holds the reaction after impact. That is distinct
from a missed rendering or simulation deadline. To attribute the user's exact
slowdown and target a fix, capture per-image timing, particle count, scheduled
ticks and hit-freeze state around the same strong hit on the published build,
then compare a synchronized control with drawing disabled while retaining the
effect simulation and RNG. Check audio and CD boot separately from injection.

## Published build: field-by-field measurement

The same encounter was repeated with the Ymir write watch on
`g_t3_ordered_mask_drawn_images`; it incremented at fields 4232, 4240, 4246,
4251, 4257, 4262, 4267, 4272, 4277 and 4281 (including 90 boot fields).
A second deterministic run watched `g_render_submissions` starting at field
4170. `scripts/analyze_slowhit_watch.py` compares the two logs:

| Window | Submitted images | Fields | Images/s at nominal 60 Hz |
| --- | ---: | ---: | ---: |
| Before the first particle image | 32 | 57 | 33.7 |
| After first through last particle-image event | 10 | 54 | 11.1 |
| After the last particle-image event | 21 | 43 | 29.3 |

During the effect the observed interval is 4–7 fields per submission, versus
1–2 fields in the preceding window. The two counters are written at different
points in the loop, so their boundaries do not perfectly delimit image work.
These are emulator submission intervals, not proven display swaps or hardware
timing. The particle window correlates with an actual submission-rate drop,
not merely an intentionally frozen fighter animation. The measurement does
not isolate raster, projection, masking, synchronization or simulation costs.
Evidence: `assets/imported/slowhit-watch.log` and `slowhit-cadence.log`
(private and ignored).

A separate intrusive 15-field Ymir CPU trace on that exact ELF during the
same spray completed two mixed-order composition calls. Together they cost
2,780,247 emulated Master execution cycles including their callees: 1,324,425
cycles in fighter-face masking, 584,866 in particle addition and 387,538 in
HUD occlusion (nested call costs are not additive with their parent). The
trace also completed two projected-mesh builds costing 585,124 cycles total.
These are CPU execution costs in a bounded emulator trace, not independent
wall-clock times or physical-Saturn measurements. The profile identifies the
mixed particle/fighter composition as a concrete optimization target.
Evidence: `assets/imported/slowhit-profile/profile-summary.json`.

## Candidate optimizations

The candidate skips HUD mask construction for unallocated cells and uses the
existing cell index directly. Quad masking also skips lines and horizontal
pixels that cannot reach the destination cell, while retaining the extra
diagonal-fill pixel. The host preflight test compares the clipped quad mask
with a complete un-clipped reference across thousands of generated cases.

The same CPU encounter, watch method and build options gave 11 submissions in
54 fields during the particle window, or **12.22 submissions/s**, compared
with 10 in 54 fields (11.11/s) for the published control. The preceding
window gave 33.82/s versus 33.68/s. The event boundaries and resulting image
counts differ slightly because execution cadence changes, so this is a short
emulator comparison, not a frame-matched benchmark. All 92 host CTests passed.
The slowdown remains severe; the candidate is an incremental improvement, not
a fix. Real hardware, CD boot and full screenshot parity remain unverified.
Candidate ELF SHA-256 and logs remain under `build/slowhit-candidate` and
`assets/imported/slowhit-final-{watch,cadence}.log`.

## Roster build re-measurement (2026-10-31)

ELF `e3a22c19…` (`build/roster`, cache emulation on, the attract fight of two CPU
fighters, 6,000 injected fields). The particle-image counter
`g_t3_ordered_mask_drawn_images` rose in five bursts, each about 12 images over 50
fields (watch fields 1866, 2905, 4697, 5009 and 5412). Evidence is private, in
`assets/imported/slowhit-study/`; it is emulator timing, not hardware.

### Cadence

`--watch-write` on `g_render_submissions` from just before each burst, 64 events each:

| Burst | Before | During | During intervals (fields) |
| --- | ---: | ---: | --- |
| 1 | 17.0/s | 13.9/s | 5,5,4,5,4,4,5,4,5,4,4,4,3 |
| 2 | 37.6/s | 13.8/s | 5,5,5,5,4,5,4,5,4,4,5,4,3,3 |
| 3 | 28.8/s | 14.0/s | 5,4,5,5,5,5,4,4,5,4,4,4,3,3 |
| 4 | 37.8/s | 14.1/s | 5,5,5,5,5,5,5,4,5,4,4,5,3,2,2 |
| 5 | 34.5/s | 12.9/s | 5,5,5,5,4,5,4,5,4 |

The effect costs about 20 images/s: from 1–2 fields per image to 4–5, in all five bursts.
The "before" rows for bursts 1 and 3 are low because other work overlapped them.

### The slowdown is capped at about 10–14%, and the picture suffers more

`T3_MAX_RECOVERY_FIELDS` is 4 (`tick_clock.h`). An image that takes 5 fields advances
the simulation 4 fields' worth and discards one. Counting the 5-field intervals above:
5 of 56, 7 of 61, 6 of 60, 9 of 64 and 6 of 42 fields were discarded, or 9–14% of game
time (about 89% speed). The visible change is larger: 14 images/s instead of 30–37.
This matches "a little bit of slowdown" with a stutter.

### Where the Master's extra time goes

Two 8-field `--debug-profile` windows inside burst 2, with the same encounter and
build (cycles are Ymir interpreter costs, not hardware cycles):

**Corrected.** This table first divided the peak window by two images. Its call counts
show one complete effect image (one projection, one 823k composition) beside two images
without particles, so the per-image costs below are per that one image.

| | Calm (trace 2790) | One effect image (trace 2845) |
| --- | ---: | ---: |
| `t3_characters_render`, per call | 457k | 987k |
| `t3_psx_projected_mesh_build`, one fighter per CPU | 0 | 346k |
| `t3_characters_mixed_ot` | 7k | 823k |
| of which particle add (`t3_particle_prepare_add`) | 0 | 274k |
| of which fighter-face work (`t3_stage_particle_multi_face`) | 0 | 258k |
| of which the sort (`t3_psx_scene_ot_order`) | 0 | 66k |
| HUD occlusion | 0 | 17k |

The effect adds about 1.35M Master cycles per image, three fields' worth: 530k in
`t3_characters_render` and 823k in composition.

### Findings that change the earlier conclusions

- The Slave is not spare time. The Master waited 150–285k cycles for it in each late
  image, because a 4-tick image ran one tick synchronously before drawing began. See
  `Fix` below.
- The particle image is the main cost, but not the only one. In a window just after
  burst 1 (no composition, `t3_characters_mixed_ot` under 6k cycles) the images were
  still slow: the Slave spent 1.5M cycles in `t3_psx_ai_update` (CPU move selection) and
  the burst-1 intervals after the effect stay at 23.7/s with 5-field gaps. In bursts 2
  to 4 the cadence recovers (35–38/s). Move-selection spikes are a second, smaller
  cause, and this attract fight has two CPU fighters.
- The earlier composition-only target was incomplete: projection, keys and face
  submission add about two thirds as much again.
- The candidate in `Candidate optimizations` is in the tree (`has_cell` skip in
  `occlude_rect`, `t3_particle_tile_occlude_index`); its HUD occlusion now costs 17k
  per burst window, so more work there gains little.

All measurements: the attract fight of CPU fighters, five bursts, two 8-field profile
windows. Real-hardware timing, and the human-versus-CPU case (half the AI work), are
unmeasured.

## Fix (2026-10-31)

ELF `4609f6e7…` against the control `e3a22c19…`, rebuilt from the same source state and
byte-identical to the study's ELF. Same attract fight, 6,000 injected fields, cache
emulation on. `scripts/burst_cadence.py` finds the bursts and measures them.

| Burst | Control images/s | Fixed images/s | Control discarded | Fixed discarded |
| --- | ---: | ---: | ---: | ---: |
| 1 | 13.5 | 17.3 | 5 of 49 | 0 of 45 |
| 2 | 12.9 | 16.7 | 7 of 51 | 1 of 43 |
| 3 | 13.2 | 17.3 | 6 of 50 | 0 of 45 |
| 4 | 12.7 | 16.1 | 8 of 52 | 0 of 41 |

The watch prints only 64 particle events, so burst 5 was not reached in the fixed run.

Over the whole run, discarded fields fell from 147 to 85, and scheduled ticks rose from
4,290 to 4,350 of about 4,436 observed fields. The average image rate went from 33.4 to
33.0 images/s, and the longest Master wait for the Slave from 45 to 62 ms. The other 85
discarded fields were not attributed.

What changed:

- **`DEFERRED_TICKS` 3 to 4 (the largest step).** A 4-tick image no longer runs one tick
  synchronously before drawing, so the simulation overlaps the Master's work. A late
  image's pose can now be 4 ticks old instead of 3 (`saturn_workload.py` accepts 4);
  images of 3 ticks or fewer are unchanged. This took images from about 14/s to 17/s.
- **Cheaper projection.** `psx_camera.c` is built with `-Os` for High WRAM, so the
  per-vertex functions now build with `-O2`, and the divide shifts in place.
- **Face work.** A culled face no longer computes its OT bucket. Faces behind the farthest
  particle, or outside every particle's bounds at 16-pixel resolution, stay out of the sort.
- **Smaller loops.** The sort walks only the buckets in use, a sprite and a face test only
  the part of a cell they reach, the cell map extends instead of being rebuilt per
  particle, and particles reuse the last palette.

Results are unchanged by the rewrites, within the limits of these checks:

- `T3_PARTICLE_COMPOSE_CHECK` composes each effect image with and without the face filter
  and compares every cell: 75 images, 0 mismatches. Fighters removed particle pixels in
  those images, so the comparison exercises occlusion.
- Host tests compare the sort, NCLIP and bucket, and the cell test against reference
  versions, and the GTE divide against its existing reference for all 65,536 depths.
- Not compared pixel for pixel: a frozen tick draws no particles, because sprites reach
  the snapshot only while ticks run. A live frame was checked by eye.

### Particles are cut in whole cells on roster builds

In these runs `g_t3_ordered_mask_tiles` is 0 and `g_t3_ordered_mask_cell_drops` is 210 over
3,000 fields. A fighter face in front of a particle removes the whole 8×8 cell, the
documented fallback, rather than the exact pixels. The cause was not traced. The likely
one is that roster packs do not keep fighter pixels in RAM after upload to VDP1
(`characters.c` refuses paths that read them after boot), which would leave the mask
nothing to read. Exact masks would need those pixels and would add cost: an earlier ELF
spent 1.3M cycles on them over two images.

### Remaining options, none implemented

1. **Move composition to the Slave.** Not obviously a gain: the Slave chain (actor, up to
   four ticks, stage) is already about as long as the Master's.
2. **Raise `T3_MAX_RECOVERY_FIELDS` to 5.** Bursts barely discard now, so this would only
   matter for the other discarded fields and for slow real hardware.
3. **Lower `T3_PARTICLE_CELLS`.** Particle add is still the largest composition item.

Everything here is emulator timing. On a real Saturn slow enough for images of 4 or more
fields, the deferred tick should help in the same way, but that is unmeasured.
