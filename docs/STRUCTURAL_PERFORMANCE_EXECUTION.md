# Structural performance recovery

This is the active execution ledger for the revised plan, starting from
`3d8e461` and LibSaturn `10f995605e80a3903b345d1de029837168475a87`.
The objective remains 60 committed logic ticks/s and 30 distinct images/s.
No isolated benchmark or submission counter proves that objective.

## Required sequence and evidence

1. Preserve the baseline, check CI and account for discarded simulation time.
   Complete the execution budget with per-CPU useful work, handoff/join,
   command submission, VDP1 completion and actual display boundaries. FRT
   clocks on different CPUs are not assumed synchronized.
2. Measure exact local-pose repetition before dynamic blend/head tracking.
   Test bounded reuse across actors with complete pose/table identity keys,
   explicit lifetime/reset and serial fallback. Compare neutral, attacks and
   replay. Report lookup/copy overhead, memory and complete release cadence.
3. Audit the vertex path and rigid-influence distribution. Extend the existing
   procedural SDK ladder only for missing representative workloads. Test
   eliminating passes or preparing transforms by joint, preserving every
   required rounding step. Qualify any reusable SDK improvement in Dino and
   unchanged Paul; do not move Tekken-specific formats into the SDK.
4. Reassess CPU scheduling after identifying a different useful ownership
   boundary. Measure publication, contention and actual join savings. The
   previously rejected actor skin/geometry bundle and cache-enable-only
   experiments are evidence, not fresh candidates without a new reason.
5. Audit match/joint copying as a secondary target. Preserve atomic failure
   and original actor order. Its measured ~0.724 ms/tick is an investigation
   ceiling, not a promised recoverable saving.
6. Correct retained tick debt separately from speed work, preserving input
   edges and reset/replay order. Test finite-stall recovery and sustained
   overload. Qualify the integrated encounter over at least 60 seconds each
   for neutral, combat and round transitions, including actual display changes.

Before promoting authoritative core changes, repeat the full declared
16,334-tick original/host/Saturn gate. Retain matched-pose visual comparisons,
all live combat cases, stock-memory/link bounds, serial/worker comparisons
where relevant, and an actual Mednafen CD run. Timing builds remain separate
from correctness builds and intrusive profiles. Ymir's immediate-result DIVU
model remains a stated limitation; independent Mednafen controls qualify
latency-sensitive work. Commit/push every coherent passing increment and
inspect applicable CI. Record rejected experiments, not just successes.

## Discarded-time diagnostic

Diagnostics version 21 appends `elapsed_fields_last`, `observed_fields`,
`discarded_fields` and `scheduled_ticks`. The portable tick clock retains the
previous four-field clamp and PAL/NTSC fractional accounting. These counters
start at gameplay scheduling, excluding boot/loading; resetting a fight does
not reset elapsed-time history. Like the existing VBlank counters, they are
unsigned 32-bit counters. Reports reject wrapped/reset intervals.

`scheduled_ticks` counts the clock's decision before diagnostic frozen-tick
limits. It is deliberately distinct from committed `sim_ticks`. Reference
replay/frozen builds cannot use it as a live-speed certificate. Snapshot
publication includes the counters under the existing epoch protocol.

```sh
python3 scripts/report_saturn_runs.py --cadence \
  assets/imported/budget-accounting-600 \
  assets/imported/budget-accounting-1800
```

The report includes discarded fields, their percentage of observed fields and
their nominal duration. This exposes overload without claiming that retaining
debt would make the workload fit. Host tests cover a finite stall, sustained
overload, PAL fractional ticks and zero elapsed fields; probe tests retain
version-20 parsing and reject a truncated version-21 extension.

## Status

- Baseline `3d8e461`: both GitHub workflows passed.
- Time-accounting implementation: 44 host CTests and 30 Saturn tool tests pass.
- Uninstrumented cache-on fields 600–1800: 1,200 observed fields, 400
  discarded fields, 800 scheduled/committed ticks, 200 submissions. The
  current loop discards **33.33%** of elapsed gameplay time: 6.667 nominal
  seconds in this 20-second interval. Cadence remains 10 submissions/s and
  40 logic ticks/s. This diagnostic does not improve speed.
- Actual 35-second Mednafen CD boot succeeds; second 34 shows both actors,
  round/time HUD and the advancing frame counter. Evidence is in private
  `assets/imported/budget-accounting-mednafen/`.
- Remaining sequence: execution evidence is recorded as experiments complete;
  the items above are not implicitly satisfied by this first diagnostic.

The first structural candidate is qualified in
[bounded local FK/IK reuse](LOCAL_POSE_REUSE_PERFORMANCE.md). Small-cache active
regressions are explicit; the selected 128-entry cache passes all 16,334
continuous ticks, all 24 live combat cases, matched-pose pixels and actual
Mednafen CD boot. Complete measured median work falls 87 to 79 ms; neutral
release cadence rises 40.100 to 44.867 ticks/s over roughly 60 seconds.
Vertex influence distribution and repeated-list VDP1 behavior are recorded
there to constrain the following renderer/harness work.

The shared SDK harness now measures exact composited video changes. Its
`1ae0283` commit and CI pass. The port's following
[complete-list presentation](VDP1_PRESENTATION.md) experiment selects the
existing ON_SUBMIT policy already used by Dino, eliminating excess image
transitions without claiming a speed gain. The frozen renderer still needs
49 ms median. CPU scheduling, retained debt and the final milestone remain open.

The [native CPU/memory budget](NATIVE_CPU_MEMORY_BUDGET.md) now confirms enabled
caches on both CPUs during actual CD boot. Its frozen renderer diagnostic
shows a substantially larger Mednafen cost than Ymir injection, especially
in flush and placement. Memory experiments must retain native controls;
these observations do not qualify a renderer optimization.
