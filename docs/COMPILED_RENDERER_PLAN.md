# Full-Paul renderer: remaining architecture plan

## Subsequent stage integration — 2026-10-20

The user-selected [Paul street stage](PAUL_STAGE_VDP2.md) is implemented with
NBG0 panorama art and RBG0 ground, using SDK `6302bec`. It removes the old
VDP1 checkerboard/rails from the private stage mode. The independent renderer
still exceeds 33.33 ms; the target remains unmet. One warmed final CD pair
gives 633 versus 598 submissions, with the stage report preserving scope and
limitations. Do not describe the VDP2 floor as still wholly unimplemented.

## Execution outcome — 2026-10-20

The dense-command and simulation-overlap architecture below has now been
implemented and tested. Read the [experiment report](COMMAND_PREPARATION_EXPERIMENT.md)
for its source artifacts, measurements, memory budget and validation limits.
Three matched final Mednafen pairs give 612 versus 593 submissions per
60 seconds, only 3.20% above its experimental SDK control and 2.34% above the
qualified production result of 598. It fails the 1.5x continuation gate.

Keep the qualified frame-preparation overlap release and SDK `6302bec`.
Do not present command compaction or this Slave preparation window as an
untried route to 30 FPS. Further work must remove substantial useful work;
the VDP2 floor remains a bounded secondary possibility, and tick-debt retention
remains a separate correctness issue. Neither closes the target alone.

## Executed architecture plan — 2026-10-20

This section preserves the experiment's requirements and acceptance criteria.
The measured outcome above supersedes its proposed next actions.

The experiment used **owned visual snapshots plus CPU-only command preparation
overlapped with authoritative simulation**, preserving the existing two-CPU
actor geometry split. Dense command storage was tested first to avoid carrying
the preceding native prototype's upload and memory costs into the pipeline.

The target remains **60 committed simulation ticks and 30 newly completed,
displayed images per second**, with both original Paul models, original
textures and combat, unchanged resolution, and stock memory. This is still
unmet. The original proposal and rejected prototype below are historical;
this section records the executed architecture's acceptance criteria.

### Qualified control and remaining budget

- Start from the qualified [frame-preparation overlap](FRAME_PREPARATION_OVERLAP.md)
  release, using SDK `6302bec`. Keep `T3_OVERLAP_VDP1` enabled for the original
  encounter and both compiled/native prototype options disabled in the control.
- Three matched Mednafen comparisons measured **598 versus 507 submissions
  per 60 seconds**, a 17.95% gain. The new control is therefore approximately
  9.97 submission increments/s in that workload, not 30 verified pictures/s.
  The interval includes normal round transitions; retain the same-round
  40-second comparison as a separate check.
- CPU preparation already overlaps the preceding VDP1 draw/swap. That wait
  removal is complete and must not be counted again as a proposed gain.
- Independent frozen-renderer measurements still exceed 33.33 ms. Reuse the
  existing stage controls to measure the new release's budget; do not combine
  timings from different builds into an additive prediction.
- Profiles identify Slave idle periods during Master simulation and
  finalization. They establish a scheduling opportunity, not a recoverable
  percentage of frame time. Existing actor geometry already uses both CPUs.
- The four-field clamp still discards elapsed time. Submission gains do not
  establish sustained 60-tick simulation while that overload remains.

### 1. Establish the dependency and memory budget

Use the existing per-CPU and stage diagnostics; add no new general profiling
framework. Identify a concrete CPU-only preparation phase that can run while
Master advances authoritative simulation, its inputs, its dependencies on both
actor jobs, and the point at which Master must consume its output.

Budget the complete schedule, including simulation, geometry, painter work,
command preparation, publication, waits, floor, HUD and submission. Recompute
the budget after removing work: redistribution alone does not make the current
rendering path fit. Reject a schedule that merely transfers both actors to
Slave and loses their existing parallel preparation.

Before allocating snapshots, account for both WRAM banks, mutable geometry,
command staging, cache maintenance and stack reserves. The qualified release
already ends High BSS at `0x060f90c8`; a second full renderer allocation is not
assumed to fit. Replace storage or prove bounded ownership and reuse.

### 2. Prepare dense commands without the rejected upload overhead

- Isolate command preparation from compiled deformation so the exact C32 asset
  plan is not a prerequisite. Keep the original SDK path as the control.
- Give each CPU a disjoint bounded output range and pack only admitted
  commands within it. Finalize the used spans into a dense submitted list;
  measure any compaction, index remapping and relinking costs.
- Preserve global stable painter order, equal-depth source order, clipping
  fallback, residency, rollback, HUD/END capacity and complete-list ownership.
- Remove redundant records where possible. Keep hot metadata in High WRAM
  only if the complete memory map permits it; include any bank relocation in
  the performance comparison.
- The rejected endpoint used **1,773 command slots versus 861** in its control.
  Verify that unused reservations no longer become uploaded holes. Slot
  reduction alone is not acceptance: measure complete gameplay throughput.

Use reusable LibSaturn primitives for command storage and publication, keeping
Tekken asset and pose rules in the port. Follow the SDK's worker restrictions:
Slave performs CPU-only work in owned WRAM; Master retains hardware access and
submission. Moving `sat_scene_flush` wholesale to Slave is not this experiment.

### 3. Overlap command preparation with simulation

- Publish a complete visual snapshot after the final authoritative tick for
  that image: both actor poses and placement, camera, HUD, round and replay
  facts. Snapshot every mutable input consumed by the selected worker phase.
- Keep authoritative fighter ordering and shared motion/pose caches serial.
  Do not run the two fighters' simulation independently.
- Give snapshots and worker output explicit acquire, publish and release
  ownership. Later ticks must not overwrite matrices, vertices or scene data
  still in use. A timeout does not release storage while a worker can write it.
- Use at most two presentation snapshots. Keep additional presentation age
  within the authorized one-frame limit, approximately 33 ms; verify actual
  pose age rather than assuming that two buffers satisfy the limit.
- Preserve the useful two-CPU geometry phases. Specify when they join before
  assigning subsequent CPU-only preparation to Slave, and include that
  transition and publication cost in the measurement.
- Compare dense commands alone, the pipeline alone where supported, and their
  combined path against the same qualified control. This separates removed
  work from hidden waits without building another permanent harness.

### 4. Decide whether the architecture is worth continuing

Run three alternating warmed 60-second neutral Mednafen CD comparisons per
variant, plus the same-round interval. Pin executable, SDK, assets, BIOS,
compiler and emulator settings. Check committed ticks, observed fields,
submissions, displayed pose epochs, late swaps, errors, timeouts and workload
identity; repeated lists or frozen logic are invalid gains.

Discard individual regressions. Require the combined architectural candidate
to reach **at least 1.5× the qualified overlap control's submission throughput**,
without reducing committed tick throughput or failing correctness, before
expanding it. If the control reproduces 598 submissions, this means at least
897 per 60 seconds. This is an intermediate continuation gate, not 30 FPS.
The earlier 505/507 controls are historical comparisons, not the new denominator.

If it fails, keep the qualified production default and record the remaining
CPU, dependency and memory costs. Do not promote isolated kernel wins or
continue indefinitely with small variations of the rejected architecture.

### 5. Treat the VDP2 floor as a bounded secondary experiment

The historical CPU floor/stage preparation cost is approximately 4–5 ms.
Measure its remaining cost in the qualified release before selecting a
conversion. The native prototype's optional camera cache is not selected in
the production control. Prototype a VDP2 floor only with a concrete projection/material
mapping and a complete memory budget. Qualify moving cameras, actor occlusion,
layer ordering and the existing pixel/coordinate allowance.

Measure the complete game against the current qualified control. Do not count
the historical 4–5 ms as guaranteed savings, or use the floor as a substitute
for a failed primary architecture gate. It cannot close the target alone.

### 6. Scheduling correctness and final qualification

Repair retained tick debt and input-edge ownership as a separate correctness
change: carry elapsed time instead of deleting it, preserve serial actor order,
and reset debt and pending input together. Debt retention exposes overload;
it does not create CPU capacity.

Run the relevant arithmetic, geometry, command-order and serial/worker checks;
exact matched-pose pixels for exact paths; all 24 live combat cases; and all
16,334 differential ticks after scheduling changes. Include SDK/package,
Dino and cube controls when shared SDK behavior changes. Check initialized
image size, both WRAM banks and stack reserves on the final release.

Final acceptance requires separate 60-second neutral, combat and round-transition
runs sustaining **60 committed ticks/s and 30 newly completed displayed
images/s**, with advancing pose epochs, bounded presentation age, no growing
debt, no missing world geometry and no draw timeouts. Validate both Ymir and
actual Mednafen CD execution. Report emulator results as such; physical Saturn
qualification remains separate.

## Original proposal and prototype record

The sections below preserve the original implementation plan and its results.
Completed or rejected work here is not a new task in the execution plan above.

## Summary

Build an opt-in renderer specialized for the original Tekken assets, using
reusable LibSaturn primitives for command storage, synchronization, and
presentation.

The goal remains **60 committed simulation ticks and 30 newly completed images
per second**, with two original Paul models, original textures and combat,
unchanged resolution, and stock RAM.

Treat this as one architectural experiment. Its first continuation gate is
**at least 50% higher full-game submission throughput in matched Mednafen runs**.
That is an intermediate gate, not completion of the 30-image target.

## 1. Establish the control and compile the asset plan

- Preserve the current qualified renderer as the reference and fallback. Add
  one experimental renderer selection, defaulting to the existing path until
  qualification.
- Pin port, SDK, compiler, assets, BIOS, emulator settings, and gameplay
  configuration. Reproduce the current release baseline using the existing
  workload guard and recording scripts.
- Extend the existing KMD conversion pipeline to emit an immutable rendering
  plan alongside SKA1: rigid-joint vertex groups, equal-half seam records,
  face indices, material references, original source ordering, double-sided
  flags, and vertex-to-face dependencies.
- Validate the plan once at initialization. Keep original vertex and face
  identities so results can be compared directly with the reference.
- Budget both WRAM banks and VDP1 storage before adding runtime buffers.
  Preserve the existing stack reserves and image-size limit; replace redundant
  storage as the prototype develops.

No new profiling framework or VF2 capture campaign is part of this phase.

## 2. Build the specialized preparation path

Implement the following as successive parts of the same prototype, measuring
their cumulative effect.

### Exact deformation and placement

- Consume the original joint rotations and translations directly, avoiding
  the Q16 matrix expansion for supported original poses.
- Use the proven Q12/Q15 factorization with bounded 32-bit C arithmetic.
  Validate immutable position bounds once and pack the referenced joints once
  per pose.
- Preserve the reference's rounding, translation, equal-half blend, and
  subsequent sixfold placement order. Include sign conversion and negative
  rounding in the proof.
- Write final world coordinates directly, eliminating the separate placement
  traversal. Retain existing projection arithmetic initially.
- Fall back to the reference for unsupported assets, poses, or bounds. Do not
  repeat the rejected quantized camera composition or MAC.W experiments.

### Avoid unnecessary vertex work

- Use compiled face dependencies to identify vertices required by potentially
  visible faces.
- For single-joint rigid faces, implement conservative early rejection using
  a bound that includes the reference's fixed-point rounding error. Reject
  only when invisibility is proven.
- Retain ambiguous, degenerate, double-sided, cross-joint, and blended faces
  for the existing visibility rules.
- Transform and project each required vertex once. Measure actual avoided
  vertices and total preparation cost, including classification.
- Remove early classification from the candidate if its overhead exceeds its
  savings.

Keep the existing two-CPU actor geometry parallelism during this work. Do not
move all rendering onto the Slave.

## 3. Prepare native commands without intermediate face records

- Add an optional LibSaturn command-batch interface: reserve disjoint ranges
  in the current frame's staged command arena, prepare commands, publish
  completed ranges, and finalize their order.
- Keep Tekken-specific asset structures and arithmetic in the port. Existing
  scene APIs remain compatible.
- For eligible actor faces, patch coordinates into compiled material command
  templates directly. Store painter keys and command indices instead of
  constructing the existing 24-byte projected face record and converting it
  again during flush.
- Reuse the existing stable global painter algorithm across both actors and
  the stage. Preserve pass ordering and equal-depth source order.
- Link commands in painter order through CMDLINK. Keep command records in
  their prepared locations; do not introduce another full command queue.
- Route clipping and unsupported effects through the existing preparation
  kernels into the same ordered command batch. Preserve rollback, texture
  residency checks, HUD reservation, END capacity, and complete-list
  presentation.
- Workers write only their reserved ranges. Master finalization waits for
  publication; timed-out worker storage remains owned until execution ends.
- Include unused reserved slots, upload bytes, cache maintenance, and
  command-link overhead in the measurement.
- Reuse the existing camera-keyed baked-view facility for the static floor
  and rails, invalidating on relevant view or material changes. Defer a VDP2
  floor conversion.

## 4. Apply performance gates, then repair scheduling

Run matched release controls after the complete specialized path works:

- Use three warmed 60-second neutral Mednafen intervals per variant. Compare
  median submission throughput, committed ticks, late swaps, errors, and
  timeouts.
- Require **at least 1.5× baseline submission throughput**, with no correctness
  failure or reduced simulation throughput, before expanding the architecture.
- If it fails, keep the production default unchanged and publish one concise
  result identifying remaining dominant costs. Do not continue this effort
  as an open-ended series of small kernel changes.
- If it passes, recompute the complete CPU and dependency budget using
  existing full-game, frozen-renderer, and simulation-only controls.

Then change scheduling:

- Retain elapsed tick debt instead of deleting time at the four-field clamp.
  Commit at most four recovery ticks per scheduling pass and carry the
  remainder.
- Preserve input edges until consumed by one committed tick. Reset clears
  debt and pending input together.
- Introduce at most two owned presentation snapshots only if the revised
  budget shows useful overlap. Each snapshot includes both poses, placement,
  camera, HUD, and round/replay state.
- Preserve authoritative actor ordering and serial simulation. Additional
  presentation latency must remain within the previously authorized one
  presentation frame.

## 5. Qualification and completion

- **Arithmetic:** independent wide-reference checks for full signed rotation
  inputs, sign conversion, translation limits, half blends, negative rounding,
  and fallback behavior.
- **Geometry:** compare reference and specialized vertex results, visible-face
  sets, painter keys, and ordering across neutral, attacks, throws, knockdowns,
  actor crossings, moving cameras, and near clipping.
- **Commands:** verify traversal order, equal-depth stability, complete
  termination, HUD capacity, rollback, stale residency, and serial/worker
  equivalence.
- **Presentation:** require exact matched-pose pixels for the exact path.
  Any use of the existing one-pixel coordinate allowance must be explicitly
  bounded and qualified.
- **Integration:** run applicable SDK/package checks, port tests, all 24 live
  combat scenarios, and the 16,334-tick differential gate after scheduling
  changes. Check Dino and cube controls where SDK code changes apply.
- **Final acceptance:** sustain 60 committed ticks/s and 30 completed image
  submissions/s for separate 60-second neutral, combat, and round-transition
  runs, without growing debt, dropped world geometry, or draw timeouts.
  Correlate completion and swaps with advancing pose epochs; submission
  counters alone do not qualify.
- Validate in both Ymir and actual Mednafen CD execution. Report emulator
  results explicitly; physical Saturn performance remains unverified until
  hardware testing.

The final deliverable is a qualified renderer and measured gameplay cadence,
or a rejected architectural prototype with a concrete remaining budget.
Neither outcome is replaced by a collection of isolated benchmark wins.

## Prototype result — 2026-10-20

**The implemented candidate is rejected for promotion.** Three matched
Mednafen CD runs give a median of **487 submissions per 60 seconds**, against
**505** for the reference using the same experimental SDK: **3.56% fewer**.
The 1.5× continuation gate would require at least 758 submissions in that
interval. Both port options remain OFF, and the production SDK pin remains
`6302bec`.

### Implemented scope

- The KMD compiler emits and embeds an immutable plan with joint groups,
  unique vertex/face dependencies, face identities and rigid-face eligibility.
  Existing SKA1 anchors, indices, materials and two-sided flags are reused.
  The original Paul SKA1 hash remains unchanged.
- `T3_COMPILED_RENDERER` packs original joint coefficients once per pose,
  uses exact bounded C32 deformation and equal-half blends, and writes the
  final placed vertices in one pass. It reuses the actor's matrix storage
  and retains generic fallback and the existing two-CPU range split.
- `T3_NATIVE_COMMANDS` adds direct staged-command preparation to the same
  prototype. LibSaturn reserves one arena with disjoint worker ranges;
  Master links completed commands in the existing stable painter order.
  Cold draws join that traversal, and flush failure cancels the whole arena.
  HUD/END quotas, frame identity and worker ownership remain checked.
- The same option uses the existing exact-camera view cache for the static
  floor and rails. Unsupported quads keep ordinary preparation, in source
  order. The tick-120 capture replays 48 stage faces; the selected live
  endpoint replays 50.

The native prototype retains the 24-byte queue allocation for compatibility
metadata, but common actor coordinates are written only into native commands.
Conservative early face classification and skipping unused vertices were
**not implemented**. Scheduling, retained tick debt and presentation snapshots
were not changed. This result covers the implemented prototype, not completion
of every phase above or a proof about all possible compiled renderers.

### Matched measurements

All six CD recordings boot the original two-Paul encounter with neutral pads,
the same BIOS, compiler, SDK archive and release settings. HUD endpoints are
read at movie seconds 18 and 78, after loading. Rows are independent repeated
boots; control and candidate runs alternate.

| Run | Reference FRAME | Native prototype FRAME | Reference delta | Candidate delta |
| --- | --- | --- | ---: | ---: |
| 1 | 50 → 555 | 48 → 535 | 505 | 487 |
| 2 | 50 → 555 | 48 → 535 | 505 | 487 |
| 3 | 50 → 555 | 48 → 535 | 505 | 487 |

These correspond to 8.42 and 8.12 submission increments/s. They do not
establish newly displayed pictures or physical Saturn FPS. Mednafen movies
do not expose the guest's late-swap/error counters; the separate Ymir runs
provide those checks.

The matched 1,800-field Ymir control completes 1,421 ticks/355 submissions.
The prototype completes 1,517/379, with 121 late swaps and no reported draw
timeouts/errors. Its apparent 6.76% Ymir throughput gain does not carry over
to Mednafen. An earlier exact-skin-only candidate gave 507 → 510 submissions
per 60 seconds against the qualified SDK control; that isolated 0.59% result
was not promoted.

### Remaining cost and memory

The selected live endpoints use 861 command slots in the reference and 1,773
in the candidate, excluding the hardware END appended at submission. The
candidate reserves 1,674 potential world-face slots. Unused holes remain in
the staged upload, so its command transfer covers roughly twice as many
bytes. The metadata queue also moves to Low WRAM. These are concrete extra
costs included in the result; their individual timing contributions were not
isolated.

The reference and candidate frozen tick-120 Ymir endpoints report 39 and
38 ms of render work respectively. Even the latter exceeds the 33.33 ms
image budget before simulation. Projection of every actor vertex and the
existing per-face visibility work remain. Moving work between CPUs or
carrying tick debt alone would not make this measured path meet the target.

The candidate links at 870,240 initialized bytes, below the 983,040-byte
limit. High BSS ends at `0x060f3210`, preserving the 16 KiB Master stack;
Low WRAM ends at `0x00286e0c`, below `0x002ffe00`. The immutable plan arrays
occupy 8,093 bytes. They stay in the loaded image: `.wram_l` is NOLOAD and
cannot hold initialized constants without an explicit copy.

### Validation and retained evidence

- All 46 port CTests, 13 KMD/converter checks, arithmetic ASan/UBSan checks
  and synthetic target compile checks pass.
- The combined candidate passes all 24 unchanged live combat scenarios,
  including KO, replay, match reset and both timeout checkpoints, with no
  reported rendering errors or draw timeouts.
- A diagnostic candidate checks 558 complete actor vertex sets in the
  neutral run, then 1,226 sets across 14 combat cases, against the SDK's wide
  skinning plus the original placement pass. Worker face comparisons also
  pass. All compared coordinates are exact.
- Frozen model/stage pixels are identical at tick 120. All 97 stable fields
  agree after explicitly adding the 48 published cached-stage faces to the
  candidate's ordinary submitted/flushed counts; geometry totals are not
  ignored. Ordinary comparison remains strict unless that reconciliation is
  explicitly requested.
- SDK supported host tests and all five package checks pass. One existing
  host test reports unavailable. Tool tests requiring Pillow pass with the
  existing asset virtual environment. Dino paused runtime and the animated
  864-face-per-actor cube control pass.

The earlier exact-skin-only extended suite did not pass its second-KO
checkpoint: that input schedule reached timeout with eight defender HP.
It is not counted as a passing 24-case control. The combined candidate's
passing suite is retained separately; no assertions were relaxed.

Private builds are `build/compiled-renderer-sdk-control`,
`build/compiled-renderer-native`, `build/compiled-renderer-native-frozen`
and `build/compiled-renderer-native-verify`. The experimental SDK is installed
only under `../lib-saturn/build/compiled-renderer-prefix`, built with
`LIBSATURN_NATIVE_SCENE_COMMANDS=ON`. GCC is SH-2 13.2.0; original assets,
combat, resolution and stock-memory limits are unchanged.

Recordings and their exact executable/SDK/BIOS hashes are retained under
`assets/imported/compiled-renderer-{sdk-control,native}-cd-{1,2,3}`. The
full behavior report is
`assets/imported/compiled-renderer-native-combat-full/suite.json`.
Arithmetic/worker verification is under
`assets/imported/compiled-renderer-native-verify-combat`. Source patches are
retained under `assets/imported/compiled-renderer-prototype-source`.

The experiment stops at this failed promotion gate. The 60-tick/30-image
goal remains unmet; visibility specialization and scheduling remain open
work, with no claim that this prototype completes the full renderer plan.

The subsequent [frame-preparation follow-up](FRAME_PREPARATION_OVERLAP.md)
tests and rejects two early-visibility implementations, then qualifies an
independent change that overlaps CPU preparation with the preceding VDP1 draw
and swap. Three native comparisons improve 507 to 598 submissions per 60 seconds
(17.95%). That scheduling increment leaves this compiled-renderer candidate OFF
and leaves the 60-tick/30-image target unmet.
