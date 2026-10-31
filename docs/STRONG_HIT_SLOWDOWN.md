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
