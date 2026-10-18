# Paul presentation and runtime recovery

This is the next execution sequence after commit `1574098`, superseding the
broad optimization order in [the performance record](SATURN_PERFORMANCE_PLAN.md).
The current image and speed are both below acceptance. A 3.6% submission gain
does not qualify this milestone.

## Starting point and outcome

The paired cache-on neutral interval measured 8.43 application submissions/s
and 33.72 simulation ticks/s. A fixed-pose sample spent 59 ms in render/HUD;
this is one sample, not a percentile or a sustained budget. The reviewed
320x224 screenshot makes both Paul models small and their details difficult
to read. It does not establish a palette, UV or pose-conversion defect.

Deliver recognizable Paul models at an appropriate fighting-game camera scale,
with authored poses and the existing verified combat behavior. The intermediate
runtime gate is sustained 60 simulation ticks/s and at least 30 presentations/s.
Keep 60 presentations/s as a later measured target. Preserve original geometry
and texture content during these experiments.

## 1. Split the cost before selecting another optimization

Use one frozen private asset set and record ELF, SDK, input and harness hashes.
Run the same five cases: idle, approach, repeated supported attacks, simultaneous
contact, and KO/replay. Keep serial and paired configurations comparable.

Add bounded diagnostic modes for these experiments:

1. Simulation and required contact geometry, with character drawing disabled.
2. Frozen authored pose, full two-character rendering, no simulation updates.
3. One rendered actor, then two, with identical camera and pose.
4. Full gameplay, including input, arena, HUD and presentation.

Mark simulation, pose/FK/IK, skin/world conversion, projection/face preparation,
sorting/emission, input, and frame waits separately. Record per-submission work
and costs normalized per simulated tick; four catch-up ticks are not one tick.
Use cycle samples to explain costs and untraced runs to confirm cadence.
Report median, p95, maximum, late swaps, rejected faces and draw timeouts over
at least 600 fields per case, with warmup and measurement windows identified.
Bound instrumentation storage and keep expensive collection out of timed builds.

**Decision:** if simulation alone misses 60 Hz, optimize it before adding
render work. If frozen rendering misses 30 Hz, isolate its dominant stage.
Use the measurements to allocate Master/Slave work and the frame budget.
Do not assume the short profile's exclusive PC percentages are stage timings.

## 2. Correct the visible Paul presentation

Capture original PS1 Paul and Saturn Paul in idle, a supported attack and a
reaction at matching authored move/frame checkpoints. Use the existing
PCSX-Redux capture and pose comparison tools. Normalize comparison crops;
retain the complete screenshots so crop scaling cannot hide camera differences.

Check model/part selection, silhouette, proportions, orientation, feet/ground,
palette and transparency, face UVs, two-sided flags, seams and depth order.
Fix demonstrated importer or rendering defects individually. Compare a nearer
camera with distance-based framing that keeps both fighters and their feet
visible during approach, retreat, attacks and knockdown. Set its dimensions
from the reference captures rather than guessing a new model scale.

**Acceptance:** reviewed before/after captures show readable faces, hair and
costume at normal output size; limbs remain intact in all selected poses and
both fighters remain framed. Remeasure the larger on-screen model because it
can increase VDP1 fill cost. Do not claim asset fidelity from numerical pose
parity alone.

## 3. Remove the dominant measured work

Choose the first experiment from step 1, rather than implementing all of these:

- Simulation: check repeated branch scans, copies and repeated pose/FK/IK
  evaluation within one tick. Cache immutable decoded move data where its
  measured benefit justifies RAM. Preserve head tracking, blend, roots and
  contact dependencies; do not reuse a stale pose across gameplay changes.
- Rendering: inspect repeated skin/world/view transforms, face/material copies
  and generic per-vertex arithmetic. Evaluate a rigid single-joint path only
  after confirming imported weights qualify. Specialize its arithmetic without
  changing rounding, clipping or depth order.
- Memory: compare placement of hot code, matrices and vertex buffers in High
  and Low WRAM. Check Slave cache visibility and contention; moving data must
  fit stock RAM and retain stack reserves.
- Parallelism: move a larger independent unit, such as a complete actor's
  presentation preparation, only if its inputs can remain immutable until
  join. Keep VDP emission on Master. Measure useful worker work and join time.

Put reusable runtime changes in sibling `../lib-saturn`; rebuild its installed
package before benchmarking the game. Keep each experiment separately
switchable. Compare output and full-game cadence against the same baseline.
Aim for at least 20% improvement in the dominant stage per retained performance
experiment, then verify its whole-path benefit. Treat small changes as minor
cleanup; abandon an approach that cannot plausibly meet the measured budget.

## 4. Preserve elapsed time and prove runtime pacing

The current `elapsed > 4` clamp discards elapsed fields. Add explicit accumulated
logic debt and overload diagnostics; a bounded amount of catch-up work may
leave debt for later rather than silently erase it. Test recovery after a
finite injected stall. If work cannot keep pace, expose persistent overload;
raising the catch-up bound cannot be reported as a speed improvement.

Select render scheduling from the measured simulation budget. Preserve input
edge semantics and replay tick order. Prove sustained 60 Hz simulation without
growing debt and >=30 Hz presentations over a 60-second gameplay window,
including a fight and round transition. Derive rates from actual elapsed
fields/time and application submissions, not automatic VDP redraws.

## 5. Qualify and publish the result

After authoritative core changes, rerun the complete 16,334-tick source/host/
Saturn gate. For presentation changes, compare consumed face data, command
order and selected screenshots; intentional camera changes require reference
review rather than an identical-pixel expectation. Run the affected host gates
and all 24 current combat/KO/replay/reset/timeout cases on the final build.

Boot the final disc in Mednafen, retaining a complete encounter recording.
Check stock RAM, stack reserve, texture residency, command peaks and zero
render errors. Keep private game data outside Git. Commit and push each coherent
passing increment, then check applicable CI in both repositories. The unrelated
SDK Phase 3 missing-palette issue remains tracked separately; do not call that
workflow green until repaired.

Each delivery must state the actual visual improvement, before/after full-path
rates, configuration and evidence, and which acceptance gates remain open.
