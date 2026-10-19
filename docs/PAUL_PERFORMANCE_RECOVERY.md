# Paul performance recovery: execution plan

Updated 2026-10-19. This is the next execution sequence after port `5204aec`
and LibSaturn `0bf5556`. It supersedes the older optimization order in
[SATURN_PERFORMANCE_PLAN.md](SATURN_PERFORMANCE_PLAN.md). Measurements and
rejected experiments remain in [PAUL_RUNTIME_EXPERIMENTS.md](PAUL_RUNTIME_EXPERIMENTS.md).
Later implementation gates remain planned; current qualifications are listed below.

Execution update: the baseline continuous comparison and first bounded
simulation report now pass; see [SIMULATION_PROFILING.md](SIMULATION_PROFILING.md).
The diagnostic build also passes all 24 live combat cases and a Mednafen boot.
The next experiments are now recorded in
[BRANCH_AND_MATRIX_EXPERIMENTS.md](BRANCH_AND_MATRIX_EXPERIMENTS.md): the native
branch cache is rejected (1.88% release cadence for 48 KiB), and the reusable
SDK matrix control passes 33792 checks with an 11.43% best procedural gain.
Neither is promoted to gameplay. The next selected boundary is renderer
transform/projection/face admission; recovery and scheduling gates remain open.
The first renderer increment now uses the shared scene-wide emission dispatch;
see [the runtime results](PAUL_RUNTIME_EXPERIMENTS.md#scene-wide-emission-dispatch).
It saves 4.04% of Dino emission but only 0.44% of its whole work. Paul neutral
cadence over the longer interval is 9.842 submissions/s / 39.370 ticks/s,
about 0.57% better. The large
dynamic geometry and simulation budgets remain open; this does not meet the
recovery milestone.
The follow-up exact signed-16-bit visibility guard is now qualified in the
installed SDK `454ebc4`: Dino's animated release cadence improves from 20 to
23.6 submissions/s, but Paul remains effectively unchanged at 9.844 / 39.378.
Register checkpoints find only 30 newly eligible Paul faces among 698 observed
calls, versus 427 among 1614 for Dino. See the
[culling qualification](PAUL_RUNTIME_EXPERIMENTS.md#signed-16-bit-world-culling).
These renderer increments leave the later optimization, scheduling and pacing
gates open. Historical instructions below describe the entry
requirements; do not rerun passed captures without a relevant source change.

## Baseline and budget

| Current observation | Result and scope |
| --- | --- |
| Uninstrumented neutral, fields 600–1800 | 9.76 submissions/s; 39.03 logic ticks/s |
| Full neutral simulation | 43 ms median per submission; several ticks per submission |
| Simulation normalized per tick | 11.562 ms aggregate in the instrumented window |
| Frozen two-actor complete work | Approximately 49 ms in the preceding renderer qualification |
| Full neutral complete work | 92 ms median; 102 ms p95 |
| Short complete CPU trace | Slave polling/control exceeds 70% of its modeled cost |

The frozen renderer already exceeds the 33.33 ms available for 30 submissions/s.
Simulation optimization alone cannot close the target. At 60 ticks/s, the
observed single-tick simulation cost would consume roughly 694 ms of CPU time
per second before drawing. These are planning constraints, not additive frame
predictions: stages overlap, workloads differ, and instrumented medians must
not be summed into synthetic FPS. Measure the actual critical path.

The intermediate acceptance target is sustained 60 logic ticks/s and at least
30 submissions/s, followed by verification of distinct displayed pictures.
Physical timing remains unqualified. Ymir's pinned immediate-result DIVU model
must stay visible in every timing report. Keep geometry, textures, authored
poses, camera, combat rules and stock-RAM configuration fixed during experiments.

## 1. Establish a qualified baseline and measure the missing stages

First verify the applicable CI for the two pinned commits and preserve their
binaries. Requalify current authoritative math against the existing continuous
[original/host/Saturn comparison](SATURN_REFERENCE_REPLAY.md): all three sequences,
16,334 committed ticks. The latest increment passed 42 host checks, 14 basic live
cases, 247 serial/worker geometry comparisons and a Mednafen boot; it did not
repeat the complete continuous gate or all 24 live cases. Close that evidence
gap before using this revision as the correctness oracle for another core change.

Extend the existing bounded stage ring and profile analyzer; keep it optional
and outside release timing builds. Record raw FRT ticks and frequency, call
counts and explicit units for:

- Move begin/ground update and root sampling/placement.
- Motion decoding, FK, IK, blend, head tracking and part composition separately.
- Body/contact queries, hit resolution and branch/command evaluation.
- Match-state copying and diagnostic/presentation bridging.
- Skin/world conversion, projection, admission, merge, painter and emission.
- Worker dispatch/publication, useful work, join and idle intervals.

Account for nested intervals without double-counting. Record per-tick and
per-submission distributions separately; label actor, normal/replay state and
number of catch-up ticks. Bound storage, detect dropped/overflowed records and
retain a separate uninstrumented build. Instrumentation must not enable Slave
cache experiments or change release scheduling.

Use deterministic idle, approach, repeated supported attacks, simultaneous
contact and KO/replay. Separate simulation-only, frozen one/two-actor rendering
and full gameplay. Use matching simulation checkpoints for behavior; fixed
VBlank input scripts alone are insufficient when a faster build advances more
ticks. Retain hashes, warmup, video mode, camera, CCR policy and memory layout.

**Deliverable:** a reusable script producing a critical-path report, inclusive
stage costs, calls/bytes and a defensible upper bound for each proposed saving.
Select the next experiment from this report. Do not treat short exclusive PC
percentages as recoverable frame-time percentages.

## 2. Run one bounded simulation experiment at a time

Two candidates are justified for measurement, not automatic implementation:

### Immutable branch data

Measure rows visited, repeated little-endian decoding and command matching
separately. If decoding is material, compile a bounded native representation at
bank initialization, with explicit memory cost and immutable-bank lifetime.
Keep dynamic input, frame windows, restrictions and opponent facts live.
Preserve traversal order, common call/return (`C00C`/`C00D`), terminators,
conditional skip flags `0x2C`, malformed-input rejection and walk limits.

Compare chosen branches and next VM state against the existing walker at every
tested tick, including missing matches and malformed banks. Keep original data
and an independent reference available for differential testing. Merely delaying
target decoding was already tried with insufficient return; do not repeat it.
Tekken-specific branch formats remain in this repository.

### Exact matrix/pose arithmetic

Only proceed if inclusive FK/IK/composition time provides a useful saving
ceiling. Prototype a procedural generic signed-16-bit dot/matrix benchmark in
LibSaturn, starting with three-term products and then a parent chain. Compare
current C with SH-2 multiply/accumulate alternatives using the local SH-1/SH-2
programming manual. Account for SR.S saturation behavior, MACH/MACL clobbers,
alignment, register constraints and interrupt preservation. Preserve full-width
sums, negative-floor rounding, saturation, aliases and failure behavior.

Run independent wide references on signed extrema, cancellation and randomized
inputs; execute the same procedural cases in Ymir and Mednafen. Measure code
size, High/Low WRAM placement, cache behavior and complete caller cost. Only
promote a generic primitive to the SDK after target correctness and measured
benefit. Keep GTE-specific rules in the port. The recently rejected guarded
32-bit matrix kernel grew code and did not help; do not restore it.

**Decision gate:** aim for at least 20% reduction in the selected dominant
stage and at least 5% improvement in the complete workload/cadence. A smaller
reusable cleanup can ship with honest labeling, but does not close a performance
milestone. If the measured upper bound is too small, move to the next candidate.

## 3. Extract independent work before changing Slave scheduling

Do not assign one authoritative fighter to each CPU. `advance_core` currently
places/composes actor 0 before actor 1; `compose` reads the opponent's world
pose through head tracking. Configuration also holds a mutable shared motion
sample cache, and placement can update shared round blend hold. Running those
functions concurrently would change order or introduce races.

Map exact read/write ownership and failure/commit boundaries. A possible first
unit is raw local FK/IK from already-decoded immutable pose channels. Keep root
placement, blending, head tracking, contacts and authoritative publication in
original order until independence is proven. Evaluate input preparation,
publication, task overhead and the first actual join point before building an
adapter. Avoid publishing the complete match or sharing mutable scratch/cache.

Require an unchanged serial extraction to pass the full continuous gate first.
Then use caller-owned inputs/outputs and existing SDK task/cache APIs; preserve
atomic failure and serial fallback. Register no new scheduler and expose no
Tekken-specific task in the SDK. Put any missing generic primitive there.
Compare every worker result with serial output and test repeated dispatch,
reset and failure paths. Measure a net critical-path reduction after dispatch,
cache publication and join, not worker occupancy alone.

The earlier bundled skinning/geometry experiment and High-WRAM/cache/split
experiments gave limited or negative returns. Revisit only with a new measured
ownership boundary or bottleneck; enabling Slave cache alone is not the plan.

## 4. Reduce the renderer's independent 49 ms limit

Use the existing procedural cube benchmark rather than starting another
unrelated demo. Extend its workload matrix to isolate transformation/projection,
visibility/admission, painter, command construction and transfer. Compare plain
RGB and indexed materials, one/two actors, visible/culled/near-clipped faces and
increasing counts with stable on-screen coverage. Include command-only and
preprojected controls where they isolate a measured cost.

Choose one kernel or pipeline boundary from these measurements. Verify byte
commands, painter order, clipping and residency before testing Dino, procedural
humanoids and the unchanged Paul pair. Inspect draw-completion/wait counters
and coverage-dependent cost separately from CPU preparation. Reuse existing
comparison/capture scripts. A cube result cannot establish Paul or VF2 parity.

Use the supplied local VF2 disc as a separate reference capture in the same
emulator configuration: inspect useful work on each SH-2, VDP1 command counts,
draw completion and VDP2 usage over a repeatable encounter. Compare architecture
and work budgets, not raw model appearance or mismatched camera/fill conditions.
Do not infer its implementation from its visual FPS. Keep all disc bytes private.

Qualify SDK changes across these consumers. Record and investigate the existing
roughly 2% dense-RGB regression from the material-program work. Do not accept a
large generic regression for a favorable Paul-only result. Moving geometry,
skinning or publication to Slave must beat the complete current pipeline,
including memory contention and synchronization.

For cycle-sensitive conclusions, reuse the procedural DIVU-latency probe and
independent Mednafen comparison. Do not add 39 cycles per observed divide to
estimate hardware cost: independent instructions can overlap division latency.
Fixing the emulator's timing model is a separate validated change, not an
assumption hidden inside a renderer result.

## 5. Repair pacing and qualify the complete result

First add explicit pending/deleted logic-tick and overload diagnostics. Replace
the silent four-field clamp with retained debt in a separately measured change:
bound work per iteration while carrying unfinished ticks forward. Preserve input
press-edge semantics, reset, replay ordering and rendering progress. Test a
finite injected stall and recovery, plus sustained overload with visible growing
debt. The diagnostic can land before speed improves; an accumulating debt queue
is never a claim of 60 Hz simulation.

After reducing measured CPU costs, select the presentation cadence from the
actual remaining budget. Run at least 60 seconds of neutral, active combat and
round transitions with no growing debt, 60 logic ticks/s and at least 30
submissions/s. Establish distinct displayed pictures separately. Keep 60 visual
FPS as a later measured objective, not a promised consequence of these steps.

For each authoritative optimization, pass the complete continuous 16,334-tick
gate; for final integration, rerun all 24 live combat/KO/replay/reset/timeout
cases, serial/worker comparisons and an actual Mednafen CD encounter. Check
image/load bounds, stock High/Low WRAM, both stacks, command peaks and zero
runtime errors. Preserve visual comparisons at matching poses and camera.

Commit and push each coherent passing increment, then inspect applicable CI.
SDK changes are installed and consumed by a separately rebuilt port before
measurement. Keep retail data/BIOS private. Keep the unrelated SDK Phase 3
missing-palette failure explicit; do not weaken gates to turn it green.

## Immediate next action

Baseline qualification, branch/matrix controls and the first renderer increment
are complete. Return to the larger dynamic geometry budget using the refreshed
cube ladder and Dino phase captures; projection, world visibility and face
admission still dominate over the small retained emission improvement. Read the
SDK pipeline study's rejected-experiment sections before choosing another
kernel. Keep the new static cube comparator as a configuration/pixel gate and
measure complete release Paul cadence after each candidate. A smaller record,
an assembly instruction or a faster isolated stage is insufficient evidence.

The signed-16-bit culling candidate is complete; do not repeat it as an open
experiment or attribute Dino's gain to Paul. Its host oracle, target assembly,
exact register eligibility, cube controls and full consumer runs are retained.
The six-field neutral Paul profile still places shared skin deformation at
8.00% of exclusive Master execution cost, projection at 5.58% and instance
submission at 5.08%. These include only the observed window; waits and
overlapping Slave work must not be added to fabricate an FPS ceiling.

Next isolate the shared skinning transform with its actual Q16 accumulation,
rounding and rigid/blended inputs in a procedural SH-2 control. The earlier
matrix MAC control is useful evidence, but its gain cannot be applied directly
to skinning or PS1 per-product truncation. Preserve exact results with an
independent wide oracle, inspect generated instructions and measure full Dino
and uninstrumented Paul before retaining a specialization. Use named constants
for fixed-point widths, masks, offsets and range proofs. Keep projection as
the next separate target if the complete skin path does not improve.

Slave scheduling and pacing remain separate changes after their dependencies
and critical-path costs are known.
