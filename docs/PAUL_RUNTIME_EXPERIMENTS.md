# Runtime experiments after stage isolation

**Workload correction:** the sparse projection prefetch, affine matrix and
Q15 dispatch controls at fields 360–365 used the procedural frontend model
(143 vertices/151 faces, owned match OFF). Their approximately 30 submissions/s
do not describe the original Paul pair. See
[the corrected identities and current execution ledger](STRUCTURAL_PERFORMANCE_V2.md).

Latest game-side increment: [simulation-to-presentation bridge](PRESENTATION_BRIDGE_PERFORMANCE.md).
Owned pose publication now occurs once per submitted image, and per-tick binary
tracing requires `T3_POSE_TRACE=ON`. Median measured work improves 91 to 87 ms;
short uninstrumented cadence improves only 1.18%. This is a boundary cleanup,
not the pending 30-render/60-logic milestone.

The intermediate 30-render/60-logic gate remains open. None of these experiments
meets that target or certifies physical hardware timing.
The Q15 baseline used LibSaturn `a46598b24ddd386ee73cadbd3eebf42a811ea082`;
subsequent renderer, memory and publication experiments are recorded below.
The current game package is LibSaturn `14559f9d4001ea078f855ead3213869dd5088d86`;
the earlier `4cf7c09224ed57929d831b5c0b2a8fee82a9604c` package remains the
historical base for the experiments below.

## SH-2 projection divider overlap (2026-10-19)

LibSaturn now starts the X-axis DIVU, prepares the Y clip coordinate and
bounded numerator while the divider runs, then consumes X and starts Y. The
existing clamp and exact signed quotient remain unchanged. The port's fixed
camera selects this sparse-axis path.

The isolated two-cube projection stage uses 866 vertices per actor. Two
independent Mednafen CD captures per build measured geometry at **4,960/4,961
FRT ticks before** and **4,666/4,666 after** (5.94% lower). The model-region
pixel crop is identical. The reusable cube runner now has `--timing-hud` to
show this interval in a native CD capture. Public measurements and image hashes
are recorded in the [LibSaturn report](https://github.com/celsowm/libsaturn/blob/a73d0c82f57dfd3dd668195f085cc51eeb44a90c/harness/studies/projection-divider-overlap/mednafen.json).

Ymir reports the isolated interval **5.9% slower** (13.637 to 14.444 ms) and
complete stage work 3.0% slower. The pinned Ymir core returns DIVU results
immediately and does not model the SH-2's 39-cycle division latency or early
result-read waits; this is the added instruction cost without the latency being
hidden. The Mednafen control matches the manual-backed division probe but does
not replace hardware qualification. The 5.94% result is for this projection
workload, not a whole-frame speedup; the 30-render/60-logic target remains open.

The installed package builds the private live port and passes all **24** combat
cases, including KO/replay/victory, match reset and both timeout checkpoints
(`projection-divider-full-suite/suite.json`). A 35-second actual Mednafen CD
boot exits successfully; reviewed second 34 shows both actors and `FRAME 132`
(`projection-divider-live-cd/`). The follow-up SDK constants cleanup centralizes
the DIVU register addresses and produces the identical application binary:
`6af4363d7134c0e2fd002bb7ac404c1fd982a417f2af24797473b979fc460686`.

SDK host/tool tests and all five package CTests pass locally. Package, Harness
and Cube pipeline CI pass for the projection implementation. The broad Phase 3
cross-build still fails because `bullet_hell_stress` lacks
`assets/palette_bgr555_be.bin`; this is an open repository gate, separate from
the passing projection and consumer checks.

## Sparse projection vertex prefetch (2026-10-19)

Follow-up to the divider-overlap path: during the current vertex's Y-axis
DIVU, LibSaturn computes the next vertex's independent clip-W and clip-X rows.
The scalar oracle matches all 257 front-facing vertices in the added sparse
camera test. On the native-CD two-cube projection control (866 vertices per
actor), two Mednafen runs improved geometry from 4,666/4,666 to 4,322/4,321
FRT ticks (7.38%) with identical model pixels. The installed candidate also
builds and boots the Tekken CD with `T3_PARALLEL_GEOMETRY=ON` and
`T3_VERIFY_PARALLEL_GEOMETRY=ON`, matching the baseline. Its screenshots are
pixel-identical at seconds 15, 30 and 39; both show `FRAME 666` at second 15.
The matched six-field Ymir profile also has 403 simulation ticks and 201
submissions in both builds.

Ymir remains a counter-signal: it reports 27.816 ms before and 29.046 ms after,
because its immediate-result DIVU model omits the SH-2 latency this path hides.
Its matched game profile reports projection cost +9.09% Master and +10.38%
Slave cycles, with the same DIVU-model limitation. Keep the claim scoped to
projection work; this is not a whole-game speedup or physical hardware
qualification. The LibSaturn implementation and retained measurement are in
[commit `4e4c0b0`](https://github.com/celsowm/libsaturn/commit/4e4c0b0) and
[`projection-prefetch`](https://github.com/celsowm/libsaturn/blob/4e4c0b0/harness/studies/projection-prefetch/mednafen.json).

## Affine matrix multiplication (2026-10-19)

The next matched Ymir profile identified `sat_mat4_multiply` as 5.14% of
Master modeled cycles. LibSaturn now uses a specialized path when both inputs
have the exact affine bottom row `[0, 0, 0, 1]`; all other inputs retain the
generic implementation. The existing reference test covers 1,000 affine and
non-affine combinations, including output aliasing.

In the same six-field profile window, the routine fell from 138,471 to 91,656
Master cycles (33.8%); total Master instructions fell 0.14%. Both builds
produced 403 simulation ticks and 201 submissions. The 3,600-field control
also matched at 3,583 ticks and 1,791 submissions, with no timeouts or geometry
errors. Two independent 26-second Mednafen CD runs exited successfully and
their screenshots at seconds 15 and 24 are byte-identical. The optimization
adds 824 bytes of text. These measurements establish less work in one kernel;
they do not show a whole-game cadence gain or qualify physical Saturn timing.
The implementation and detailed hashes are in [LibSaturn's affine matrix
study](https://github.com/celsowm/libsaturn/blob/beed598/harness/studies/affine-matrix/tekken.json)
and [renderer study](https://github.com/celsowm/libsaturn/blob/beed598/docs/RENDERER_PIPELINE_STUDY.md#affine-matrix-multiplication).

The follow-up attempted to widen the exact face-visibility fast path for
larger edges. It passed the mesh oracle but raised `quad_visible` cost 2.8% on
both CPUs, with no cadence change, so it was rejected. The tested patch and
matched Ymir results are in [LibSaturn's rejection report](https://github.com/celsowm/libsaturn/blob/e2b4646/harness/studies/quad-visible-bounded-edge/tekken.json).

## Q15 skinning dispatch (2026-10-19)

LibSaturn now selects the Q15-specific skin loop once for the entire
deformation range instead of testing the same conversion mode for every joint
influence. On a matched six-field Ymir profile, Master skin deformation fell
7.95% (225,375 to 207,462 cycles). The fixed profile window retained 403 ticks
and 201 submissions, with the saved work absorbed as extra VBlank wait; no
whole-game cadence gain is claimed. The 3,600-field control matched ticks,
faces and commands with no errors. A 26-second native CD boot exited 0 and
the second-15/24 screenshots were pixel-identical. ELF text grew by 1,456
bytes. Details are in [LibSaturn commit
`14559f9`](https://github.com/celsowm/libsaturn/commit/14559f9d4001ea078f855ead3213869dd5088d86) and its
[measurement report](https://github.com/celsowm/libsaturn/blob/14559f9d4001ea078f855ead3213869dd5088d86/harness/studies/skin-q15-dispatch/tekken.json).

## Selection and rejected changes

A fresh six-field Ymir profile at fields 360-365 reconciles both CPU histograms
with no dropped records (`recovery-next-hotspots`). Master exclusive modeled
execution costs include skin 8.11%, projection 7.13%, geometric visibility
5.59%, VDP1 sprite push 5.00% and branch search 4.60%. More than 67% of Slave
cost is polling/executor bookkeeping in that window. This identifies idle
capacity; it is not a recoverable whole-game percentage or inclusive timing.

The checked-in Saturn overview's CPU section documents the small 4 KiB SH-2
cache and multiplier/divider. The VDP1 address-map section documents VRAM
arbitration and recommends accessing it outside drawing. The existing SDK
handoff study already rejected DMA for this workload. These constrain further
experiments; neither DMA nor removing draw/swap protections was adopted here.

| Experiment | Measured result | Decision |
| --- | --- | --- |
| Bundle second-actor skinning with its geometry task | Frozen work 65 -> 61 ms; full work 109 -> 106 ms median | Removed; insufficient return for the added adapter and cache ownership |
| High WRAM for match/config/motion cache | Simulation-only 12.08 -> 11.94 ms/tick | Removed; insufficient benefit for extra High WRAM usage |
| Decode branch targets only after eligibility | Simulation-only 11.75 ms/tick | Removed; insufficient benefit to retain an authoritative-core change |
| Q12 coefficient cancellation with Q15 anchors | Frozen skin 12-14 ms, versus 10 ms baseline; integer comparisons passed | Removed; arithmetic equivalence alone does not establish SH-2 speed |
| Constant Q15 anchor conversion | Frozen skin 10 -> 8 ms median/p95 | Retained as a small library arithmetic improvement |

Rejected source patches remain private; none of the additional actor adapter,
High WRAM policy or branch evaluator changes ships. Their private evidence is
`recovery-actor-pipeline-matrix`, `recovery-fast-logic-matrix`,
`recovery-lazy-branch-matrix`, `recovery-q12-skin-matrix`,
`recovery-q12-shift-matrix` and `recovery-q12-cache-matrix`.

## Retained conversion and final measurements

The compiler generated three variable-shift helper calls per skin influence.
Using a constant `q * 2` when `pos_shift == 15` removes those calls and preserves
the same full-width dot products and rounding. Other precisions retain their
generic conversion. Independent integer host comparisons cover 128 fixtures,
all 16 precisions, signed extrema, large coefficients, rigid vertices,
one/two/three influences, unequal and half weights, and split ranges.

The final instrumented 1200-field run after field 300 gives frozen two-actor
work median/p95 64/65 ms, versus 65/66 ms before; skin is 8/8 ms, geometry
27 ms and flush 17 ms median. Full neutral work is 108/121 ms, versus 109/122;
simulation is 12.05 ms/tick. Stage medians are not additive frame samples.
Evidence: `recovery-q15-final-matrix`.

Final untraced endpoints (`recovery-q15-final-{600,1200}`) are
(588 VBlanks, 320 logic ticks, 79 submissions) and (1182, 644, 160).
The interval gives **8.18 submissions/s and 32.73 logic ticks/s**, effectively
unchanged from 8.17/32.69. Submission counters do not certify distinct display
swaps. The four-field clamp still discards elapsed time and remains unresolved.

A 35-second actual Mednafen CD boot exits successfully and its reviewed frame
34 shows both actors and `FRAME 117` (`recovery-q15-final-cd`). The geometry,
textures, poses, combat rules and camera are unchanged. The library host suite
and all five package CTests pass. The final 24-scenario live suite passes,
including KO/replay/victory, match reset and both neutral timeouts
(`recovery-q15-qualified-combat-suite`). A stop at field 155 caught an odd
snapshot publication epoch; its raw artifacts are retained and the bounded
coherent-capture helper passed the same reaction assertion at field 156. Only
torn publications may retry, with at most two additional fields; native panic,
guest errors and all gameplay assertions remain immediate failures. Host CTest
passes 42 tests; the Saturn Python suites pass 21 and saved-run reports pass 4.

Further work must reduce the larger projection/visibility/emission costs and
the approximately 12 ms authoritative tick. This increment does not justify a
claim that the current generic renderer can reach the target through scheduling
alone.

## Accumulated renderer update and bounded admission

The game now pins LibSaturn `e9e88927a20531c5d0ef94d0fa189a58668762fd` in
both CI workflows. This brings the exact visibility/projection optimizations,
fused opaque projected-sprite command writer and specialized proven-admission
kernel into the installed package consumed by the port. The SDK's optional
immutable-source publication plan is exercised by Dino; the game has not opted
its geometry items into that plan. All gains below include the accumulated
renderer changes since the previous `a46598b` package, rather than attributing
them to the final admission specialization alone.

A fresh run of the existing matrix script uses the previous optimized private
configuration, the same camera and imports, 1200 fields and the usual field-300
warmup (`recovery-sdk-cull-matrix`). Diagnostic stage times retain millisecond
resolution and overlapping stage distributions must not be summed.

| Case / median interval | Previous ms | Updated ms |
| --- | ---: | ---: |
| Frozen two-actor geometry | 27 | 24 |
| Frozen two-actor flush | 17 | 14 |
| Frozen two-actor complete work | 64 | 57 |
| Full neutral geometry | 27 | 24 |
| Full neutral flush | 17 | 13 |
| Full neutral complete work | 108 | 100 |

Frozen work improves by 10.9%; full work improves by 7.4%. Skin remains 8 ms.
Full neutral work p95 falls from 121 to 113 ms; simulation remains approximately
12 ms/tick. The updated neutral diagnostic window contains 520 ticks over 889
elapsed fields, so the four-field clamp still discards substantial elapsed
time. This does not meet the 60 Hz logic or 30 Hz presentation target.

Separate uninstrumented captures of the same updated ELF at 600 and 1200 fields
retain endpoints (586 VBlanks, 356 ticks, 88 submissions) and (1184, 724, 180).
The interval gives **9.23 submissions/s and 36.92 logic ticks/s**, versus the
previous 8.18 and 32.73. These are nominal 60 Hz Ymir counter differences, not
physical-hardware measurements or verified distinct display swaps. Evidence:
`recovery-sdk-cull-live-{600,1200}`. Reproduce the cadence calculation with:

```sh
python3 scripts/report_saturn_runs.py --cadence \
  assets/imported/recovery-sdk-cull-live-600 \
  assets/imported/recovery-sdk-cull-live-1200
```

An independent verification build checks Slave output against serial admission
over 1800 scripted fields: 226 preparations agree, with no native panic or
rendering failure (`recovery-sdk-cull-verified`). The 42 host CTests and the
SDK's complete local suite and five package CTests pass. A 35-second actual
Mednafen CD boot exits successfully; the reviewed frame at second 34 shows both
actors and the FRAME counter (`recovery-sdk-cull-cd`).

The SDK also rejected two arithmetic/layout experiments after measuring them:
an 88-byte union face record saved memory but increased complete Dino work, and
a 32-bit exact mean increased geometry cost. The retained face ABI is still
104 bytes on Saturn. Its renderer study records those results and the smaller
admission/culling code that was retained. Further work should target the
remaining geometry and authoritative tick costs, using these captures as the
new baseline.

A fresh six-field debugger capture at fields 360..365 has no dropped frames or
PC observations (`recovery-sdk-cull-hotspots/cpu-profile.json`). Exclusive
modeled Master execution costs include skin 8.00%, projection 5.59%, projected
sprite writing 4.97%, branch search 4.60% and `memcpy` 4.52%. Slave cost is still
dominated by polling/executor bookkeeping. These are a bounded workload sample,
not inclusive call times or percentages automatically recoverable as speedup.
The next experiments should attribute the copies and skin/projection work
before moving more data or changing dispatch policy.

The final live combat suite passes all 24 cases, including KO/replay/victory,
match reset and both neutral timeout checkpoints
(`recovery-sdk-cull-combat-suite`). All cases check native panic and runtime
health; this is Saturn behavior regression coverage, not aligned PS1 parity.


## Copy alignment and immutable material publication

The retained SDK copy kernel uses aligned halfwords when source/destination
parity matches but their modulo-four alignments differ. The six-field entry
capture records 828 memcpy calls, including 401 that could use halfwords but
previously used bytes. Most small copies carry 18-byte rotation matrices.
The SDK's guarded 512-case target matrix and native-disc Mednafen HUD confirm
correct content and improvement independently; its renderer study records the
measurements. For the selected High-to-High 18-byte case, Mednafen falls from
7.245 to 4.683 us/call after calibration. This is not a physical hardware result.

The game now also compiles a source-publication plan for the second actor's
application-lifetime topology, material table and texture descriptors after
upload and binding validation. Each batch borrows that plan. Pose vertices,
world matrices, face masks and scratch remain in the dynamic publication path;
receiver invalidation and frame-time texture residency checks remain active.
The array lives in Low Work RAM and the build retains stock RAM limits.

| Diagnostic median | Renderer baseline | Halfword copy | Copy + publication plan |
| --- | ---: | ---: | ---: |
| Frozen geometry ms | 24 | 24 | 21 |
| Frozen complete work ms | 57 | 57 | 53 |
| Full neutral complete work ms | 100 | 99 | 96 |
| Full neutral work p95 ms | 113 | 112 | 108 |

The three retained matrices are `recovery-sdk-cull-matrix`,
`recovery-halfword-v2-matrix` and `recovery-source-plan-matrix`. The copy-only
full neutral simulation interval is 11.727 ms/tick versus 12.021 previously.
A separate uninstrumented copy-only interval gives 9.42 submissions/s and
37.66 logic ticks/s. The final copy+publication interval gives **9.47
submissions/s and 37.87 logic ticks/s** (`recovery-source-plan-live-{600,1200}`).
The former 8.18/32.73 baseline and all subsequent results remain below the
30-render/60-logic target; submission counters do not certify display swaps.

The final source-plan build passes all 24 live combat cases, including
KO/replay/victory, reset and both timeouts (`recovery-source-plan-combat-suite`).
An independent 1800-field scripted serial verifier matches 233 worker results
(`recovery-source-plan-verified`). The 42 host CTests and strict C11 frontend
syntax check pass. Native Mednafen CD boot exits successfully with both actors
and the FRAME counter (`recovery-source-plan-cd`). Copy-only combat qualification
also passes all 24 cases (`recovery-halfword-v2-combat-suite`).

The investigation exposed a harness limit: pinned Ymir computes DIVU results
immediately instead of modeling the SH7604's 39-cycle normal latency and result
access waiting. A new native diagnostic exhibits early-NOP overlap in Mednafen
and a linear NOP cost in Ymir while both return correct quotients. The harness
now labels its division timing model explicitly. Existing Ymir timing/cadence
numbers describe that model and cannot qualify hardware division overlap.
The SDK also retains reproducible cache/work-split experiments: reducing worker
cost alone did not shorten the frame, and a 50/50 face split regressed it.
No experimental Slave-cache policy is enabled in this game package.

## Immutable native material programs

The port now compiles one opaque command prefix per uploaded material and actor
through LibSaturn `32fb84dbf0a8e03efa036c914f36d35cfc3dba7f`. The table is attached
before compiling the second actor's immutable-source publication plan. Materials
and descriptors stay resident and immutable for the application lifetime;
projected corners, poses, world transforms and face masks remain dynamic. The
serial verifier compares the active face union member, including the borrowed
program pointer. Native command assembly and cache publication live in the SDK.

The controlled stage matrix compares the previous source-plan package with this
change, using the same camera, imported assets, optimized configuration, 1200
fields and field-300 warmup:

| Median interval | Source plan | Compiled materials |
| --- | ---: | ---: |
| Frozen two-actor geometry | 21 ms | 20 ms |
| Frozen two-actor flush | 14 ms | 10 ms |
| Frozen two-actor work | 53 ms | 49 ms |
| Full neutral simulation | 43 ms | 43 ms |
| Full neutral work | 96 ms | 91 ms |
| Full neutral work p95 | 108 ms | 104 ms |

Evidence: `recovery-source-plan-matrix` and
`recovery-material-program-split-matrix`. These instrumented millisecond
intervals isolate work and must not be summed as a synthetic frame. The fresh
uninstrumented endpoints `recovery-material-program-split-live-{600,1200}` give
95 submissions and 380 logic ticks over 602 VBlanks: **9.47 submissions/s and
37.87 logic ticks/s**, unchanged. Reducing emission has not crossed enough
field boundaries to improve this full-game interval.

The 1800-field scripted verifier matches 246 serial/worker results
(`recovery-material-program-split-verified`). All 42 host CTests and the strict
C11 SH-2 frontend check pass. The native CD runs for 30 seconds in Mednafen;
reviewed frame 29 shows both actors and FRAME 102
(`recovery-material-program-split-cd`). All 24 live combat cases pass, including
KO, replay, victory animation, next-round restoration, match completion, reset
and both timeout cases (`recovery-material-program-split-combat-suite`). This
validates the port's current behavior, not PS1 input-replay or round-flow parity.
The SDK separately qualifies painter
order, command bytes, mixed fallback materials, clipping, invalid residency,
stale owners, command capacity and publication to receiving CPUs.

This is an opt-in indexed-material optimization. The SDK study reports a
remaining roughly 2% work regression for non-compiling dense RGB cubes, along
with the Dino 36.709-to-34.236 ms improvement and unchanged 20/s cadence. No
Slave-cache experiment, rendering shortcut or simulation rule change ships.
The known Ymir immediate DIVU model still limits timing interpretation.

A fresh complete trace of the retained build at fields 1160-1171 has no dropped
frames, PC observations, events or snapshots (`recovery-material-program-hotspots`).
Master exclusive modeled costs include skinning 8.00%, branch search 6.13%,
projection 5.59%, compiled instance preparation 5.07%, matrix multiplication
3.83%, part composition 3.61%, visibility 3.49% and prepared command writing
3.02%. More than 70% of Slave costs remain executor polling/control. These are
exclusive shares of the observed CPU window, not inclusive simulation stage
costs or additive recoverable frame-time gains. Prioritize controlled
simulation/pose experiments and useful Slave work; profile evidence does not
justify a blanket renderer or VDP1 performance explanation.

## Shared integer floor shifts and bounded scalar products

LibSaturn `0bf55566a9fa046c8f1e142f7fa536f58574b6de` provides the standalone
`saturn/int_math.h` header. The core now consumes its signed 32/64-bit floor
shifts and deletes the local `fixed_shift.h`. Native host builds need only the
SDK headers; installed-package headers supply the Saturn build. CI checks out
the same pinned SDK for both paths. No Saturn runtime is linked into host tests.

The i32 helper is used for already-wrapped 32-bit values and individual signed
16-bit products. Euler intermediates retain the original saturation and wrap
points. General three-term matrix dot products remain 64-bit: their full signed
16-bit input domain can overflow a 32-bit sum. No pose precision is reduced.
SH-2 symbol size for `t3_psx_euler_matrix` falls from 872 to 612 bytes; the
general matrix multiply remains 780 bytes.

A rejected experiment checked a complete operand against +/-16384 before
selecting an exact 32-bit matrix kernel. Its matrix function grew from 780 to
1328 bytes and full neutral work worsened from 91 to 92 ms, with simulation
11.920 ms/tick (`recovery-pose32-matrix`). The duplicated kernel and bounds
checks are removed. Arithmetic equivalence alone did not justify retaining it.

The retained 1200-field matrix (`recovery-shared-int-final-matrix`, warmup 300)
has full neutral simulation median/p95 43/54 ms, work 92/102 ms and aggregate
simulation 11.562 ms/tick. The previous material-program matrix gives 43/56 ms,
91/104 ms and 11.761 ms/tick. Stage medians do not show a uniform frame gain.

Fresh uninstrumented endpoint comparisons extend the observation to field 1800:

| Interval after field 600 | Previous materials | Shared integer math |
| --- | ---: | ---: |
| VBlanks | 1204 | 1199 |
| Submissions | 190 | 195 |
| Logic ticks | 760 | 780 |
| Submissions/s at nominal 60 Hz | 9.47 | 9.76 |
| Logic ticks/s at nominal 60 Hz | 37.87 | 39.03 |

The roughly 3.1% cadence improvement is small and does not meet either target.
Evidence: `recovery-shared-int-before-long-report.json` and
`recovery-shared-int-long-report.json`. These are Ymir observations with its
known immediate DIVU model, not distinct-picture or physical hardware timing.
The four-field recovery clamp and its loss of elapsed logic time remain.

Qualification includes all 42 host CTests, 20000 matrix fixtures with wide
independent dot-product references and both output aliases, 20000 Euler
fixtures with physical and arbitrary signed sine tables, and ASan/UBSan runs.
The SDK separately checks shift extrema and counts through/beyond both widths;
its available host gates and all five installed-package CTests pass (the native
macOS fixed-WRAM ABI mapping gate remains explicitly unavailable).

The 1800-field scripted target verifier matches 247 serial/worker results
(`recovery-shared-int-verified`). All 14 basic live combat cases pass
(`recovery-shared-int-combat-suite`); this run does not requalify KO/replay or
timeout flows. An actual 30-second Mednafen CD boot exits normally and reviewed
frame 29 shows both actors and FRAME 102 (`recovery-shared-int-cd`). Slave task
scheduling and authoritative match order are unchanged.

## Nested simulation diagnostics

The opt-in profiler and its first controlled results are documented in
[SIMULATION_PROFILING.md](SIMULATION_PROFILING.md). Its generic accumulator is
part of LibSaturn `f11b277bed2ddda963fa3dba0da08656baf08322`; release timing and
authoritative arithmetic are unchanged. The continuous 16334-tick gate passes,
and disabled release cadence remains 9.758 submissions/s / 39.033 logic ticks/s.
The next experiment is selected from measured branch/pose work, not from an
assumption that filling Slave idle time automatically shortens the frame.

## Native branches and SH-2 matrix control

The [next controlled experiments](BRANCH_AND_MATRIX_EXPERIMENTS.md) reject a
48 KiB native branch cache after only 1.88% release cadence improvement. The
reusable SDK MAC.W benchmark passes 33792 checks in Ymir and Mednafen but its
best 11.43% procedural gain does not justify a gameplay arithmetic replacement.
Release behavior, cadence and SDK runtime stayed unchanged in that increment.

## Scene-wide emission dispatch

LibSaturn `4cf7c09224ed57929d831b5c0b2a8fee82a9604c` specializes the emission
loop after the existing complete-queue residency preflight. Ordinary queues
omit compiled-program dispatch; mixed/compiled queues preserve the general
path. Owner/material validation still precedes every draw. Face layout remains
104 bytes and no simulation, pose, visibility or scheduling policy changes.
The [SDK report](https://github.com/celsowm/libsaturn/blob/4cf7c09224ed57929d831b5c0b2a8fee82a9604c/docs/SHARED_RENDERER_PERFORMANCE.md#classify-emission-once-per-complete-queue)
records measured benefits and rejected controls.

The same-density procedural RGB cube work falls from 61.838 to 61.311 ms;
textured work falls from 62.578 to 62.080 ms. Animated Dino emission improves
4.04% (4.938 to 4.739 ms), while complete work improves only 0.44% (34.236 to
34.084 ms). Cube cadence stays 15/s; Dino stays 20/s in all three scenarios.
Static cube and paused Dino crops are identical, and a fresh Dino native disc
boots in Mednafen. These are modest emulator CPU-work gains, not hardware or
distinct-picture qualification.

The installed Release SDK is consumed by a fresh `build/recovery-flush-dispatch`
port. Uninstrumented neutral endpoints at 600/1800 fields give 196 submissions
and 784 logic ticks over 1197 VBlanks: **9.825 submissions/s and 39.298 ticks/s**,
versus 9.758/39.033 before. The submission gain is only about **0.68%**.
A longer 600/6000-field comparison confirms a small gain: the baseline submits
881 frames / 3524 ticks over 5401 VBlanks (9.787 / 39.148 per nominal second),
versus 885 / 3540 over 5395 VBlanks (9.842 / 39.370). This is **0.57%** more
submissions, still far below the recovery target. This longer interval avoids
judging the result from one additional submission in the short window.
The scripted 1800-field verifier matches 245 serial/worker geometry results.
A fresh 30-second native Mednafen boot exits normally; reviewed frame 29 shows
both actors and `FRAME 113`. Host regressions remain 43/43 passing.
All 24 live combat/KO/replay/victory/reset/timeout cases pass on this release
(`recovery-flush-dispatch-combat-suite/suite.json`). SDK supported host/tool
checks and all five installed-package CTests pass locally. Remote CI was still
queued at publication; the earlier unrelated Phase 3 missing-palette failure
has not been silently waived or labeled fixed.

The authoritative core and arithmetic are unchanged; these renderer checks do
not repeat the preceding 16,334-tick continuous PS1 comparison.

Private evidence: `recovery-flush-dispatch-{600,1800,6000,verified,cd}`,
`recovery-flush-baseline-6000`, `recovery-flush-dispatch-cadence.json` and the
two `recovery-flush-*-long-cadence.json` reports. The four-corner mean, bounded painter
conversion and 88-byte face union experiments are discarded. The union had
already been rejected in the SDK pipeline study and its repeated measurement
does not justify an ABI refactor. No claimed Paul speedup comes from that build.
The next measured boundary remains dynamic renderer geometry/face preparation.

## Signed-16-bit world culling

LibSaturn `454ebc4e0f3d291569884dbfacd8814f9510c461` widens the exact small-face
visibility guard from signed 15-bit to signed 16-bit edge differences. It keeps
normal precision, centre truncation, collapsed-corner fallback, global painter
order and the 104-byte face ABI. Named constants derive the repeated guard
bias/span and the wide kernel's split/mask. The SDK includes an independent
128-bit oracle and a reusable `visibility_ranges.py` register-checkpoint
analyzer. See its
[arithmetic proof and measurements](https://github.com/celsowm/libsaturn/blob/454ebc4e0f3d291569884dbfacd8814f9510c461/docs/SHARED_RENDERER_PERFORMANCE.md#exact-signed-16-bit-visibility-kernel).

This improves Dino rather than materially changing Paul. Dino's instrumented
Master geometry drops 19.706 to 18.775 ms, worker geometry 16.828 to 15.840 ms,
and whole work 34.084 to 33.181 ms (2.65%). Its separate release runs measure
23.6 animated and 24.0 paused submissions/s versus 20.0, while the control
scenario remains 20.0. Paused/unlit pixels are identical. Dense cube controls
retain 15/s and identical crops with less than 0.02% work variation.

Release Paul endpoints at requested fields 600 and 6000 produce 886 submissions
and 3544 ticks over 5400 VBlanks: **9.84444 submissions/s and 39.37778 ticks/s**.
The preceding flush-dispatch build gives 9.84245 / 39.36979 in its equivalent
long interval. That approximately 0.02% variation is effectively unchanged,
not a useful Paul speedup or a recovery milestone.

Exact register snapshots over six fields starting at 1100, using unchanged
guest binaries and reviewed target assembly, explain the eligibility difference:
427 of 1614 Dino observations newly fit the guard, versus 30 of 698 for Paul.
These count edge ranges, including zero normals that still fall back; they
are not completed fast-path calls. The current Paul Master profile assigns
8.00% of exclusive execution costs to skin deformation, 5.58% to projection
and 5.08% to instance submission. The short observation window and included
waits do not support an inclusive cost or physical-hardware FPS claim.

The final named-constant SDK rebuild yields identical release Dino and Paul
application bytes to the measured candidate. Local SDK supported host/tool
checks and all five installed-package CTests pass; the port passes 43 host
tests and 247 scripted serial/worker geometry comparisons. Native Dino and
Paul CDs boot and render in independent 30-second Mednafen runs; reviewed
Paul frame 29 contains both actors and `FRAME 102`. The FRAME value is a debug
checkpoint, not a host-recording FPS comparison. Authoritative core arithmetic
is unchanged, so the preceding 16,334-tick continuous gate is not rerun.
All 24 live combat/KO/replay/victory/match-reset/timeout cases pass on the
release candidate (`recovery-cull16-combat-suite/suite.json`). Remote SDK CI
remains queued at publication; the existing unrelated Phase 3 missing-palette
failure has not been waived or declared fixed.

Private evidence: `build/recovery-cull16{,-final,-verified}` and
`assets/imported/recovery-cull16-{600,6000,verified,cd,long-cadence.json}`.
SDK numeric evidence and edge-register counts are public under
`harness/studies/renderer-cull16/`. This increment retains stock RAM and all
simulation/input/scheduling behavior. The matching Q16 skinning `MAC.L` control
is complete: 7,544 exact comparisons passed, but its full-range timing was
38.60% slower than the current SDK API. See the sibling SDK's
[`SKINNING_MAC_BENCH.md`](https://github.com/celsowm/libsaturn/blob/main/docs/SKINNING_MAC_BENCH.md).
The next isolated target is projection, followed by the existing whole-path
controls before any candidate is retained.

## Clean two-Paul performance checkpoint

2026-10-20: rebuilt the current project revision `1c8ac15` from clean CMake
directories with the complete local Paul data set and matched gameplay flags.
The retained configuration enables combat, owned match and skin preview,
128-entry local-pose caching, parallel geometry, frame-on-submit, and High-WRAM
geometry/material/texture banks. The clean image is 848,456 bytes and passes
the standard 983,040-byte image limit and linker stack reserve.

The 5,400-field Ymir injection run completes 1,002 submissions and 4,009
simulation ticks: 11.13 submissions/s and 44.54 ticks/s over the full requested
window. It records 84 ms frame work, 8 ms skin work and 46 ms render work, with
zero draw timeouts, rejected faces or guest errors. These values belong to
this exact full-Paul configuration and must not be generalized to other
builds. Ymir's super-debugger trace covers six fields (5,390–5,395) on both
CPUs, with no dropped samples. Its modeled Master costs put DIVMOT branch
search at 7.66%, skin deformation at 7.01%, projection at 6.33%, instance
submission at 5.06%, pose composition at 4.60% and rotation multiplication at
4.06%. Ymir reports about 449,204 modeled execution cycles per CPU per field;
its trace excludes sleep and separately advanced DMA and is not a physical
Saturn cycle certification.

Two cache variants were tested and discarded. The boot diagnostic reads CCR
`0x01` on both Master and Slave in this injected run, so a second Slave cache
enable is redundant. Skipping scene-source invalidation only when the receiving
CPU reports its cache disabled therefore leaves this workload on the unchanged
path. The variants preserve 1,002 submissions, 4,009 ticks and 84 ms work, and
do not justify a runtime change.

A clean build against the older installed prefix `saturn-prefix-gc` also
completes 1,002 submissions and 4,009 ticks with 84 ms work (render rounds to
45 ms instead of 46 ms). That one-millisecond diagnostic difference is below
the timer's millisecond reporting precision; it does not show an SDK speedup
or regression. The old installed prefix has no embedded source revision, so
this is recorded as a package-snapshot comparison, not an exact SDK commit
comparison. Its screenshot was captured one submission serial ahead, so the
image comparison is not a matched-pose visual gate.

The clean current build also boots as a real CD in Mednafen. Its 29.84-second
recording contains both Paul models and an advancing HUD frame counter: the
retained screenshots show `FRAME 21` at 15 seconds and `FRAME 114` at 28
seconds. This confirms CD boot and continued rendering for this image; those
checkpoints are not a physical Saturn rate measurement.

### Isolating the renderer from simulation

2026-10-20: two clean diagnostic builds reused the same assets, gameplay
options, SDK package, BIOS and 5,400-field Ymir injection as the full build.
`T3_PERF_MODE=1` skips drawing; `T3_PERF_MODE=2` draws one actor while both
fighters continue to simulate. The complete two-actor run above is the
baseline. Results:

| Profile | Sim ticks | Submitted frames | Frame work | Skin work | Render work | Faces in final snapshot |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Two actors | 4,009 | 1,002 | 84 ms | 8 ms | 46 ms | 774 |
| One rendered actor | 4,964 | 1,241 | 61 ms | 4 ms | 31 ms | 408 |
| Drawing disabled | 5,380 | 4,547 | 2 ms | 0 ms | 1 ms | 0 |

These values show that rendering is the dominant cost in this build: disabling
drawing restores nearly one simulation tick per observed field, and drawing
one actor lands between the two-actor and no-draw cases. The one-actor profile
still simulates both fighters. Because the slower full-render run accumulates
more discarded fields and the HUD/replay state depends on simulation progress,
compare throughput and phase costs here, not final gameplay state. These are
diagnostic profiles, not candidate game configurations. They narrow the next
optimization to shared per-actor skin/geometry/face preparation; changing
combat logic or enabling another cache does not address the measured dominant
cost. The run artifacts are `/tmp/paul-perf-nodraw-5400/observation.json` and
`/tmp/paul-perf-one-actor-5400/observation.json`.

Private captures stay under `/tmp/paul-q15-high4-{5400,debug}`,
`/tmp/paul-q15-high4-mednafen` and `/tmp/paul-old-sdk-5400`; the clean build
directories are ignored under `build/paul-q15-high4` and `build/paul-old-sdk`.
No source optimization is promoted from this checkpoint. The next candidate
must attack the measured simulation/geometry work and pass a matched
5,400-field Paul run before it is called a performance improvement.

### Parallel geometry control

The same full-game configuration was also built with only
`T3_PARALLEL_GEOMETRY=OFF`; two identical 5,400-field Ymir runs produced the
same report. Compared with the parallel build, the serial control completes
3,565 instead of 4,009 simulation ticks and 891 instead of 1,002 submissions.
Its frame-work diagnostic rises from 84 to 86 ms and render work from 46 to
56 ms. The enabled Slave geometry path therefore improves this workload by
12.5% in simulated ticks over the requested window; keep parallel geometry on.
The binaries have no draw timeouts or guest errors. Evidence is in
`/tmp/paul-perf-serial-geometry-5400/observation.json` and
`/tmp/paul-perf-serial-geometry-repeat-5400/observation.json`.

### Parallel vertex skinning

The next measured change uses `sat_parallel_for` to split the existing,
unchanged `sat_skin_deform_range` work across Master and Slave. Skeleton pose
calculation remains on the Master; each CPU writes a disjoint vertex range,
and the existing parallel API synchronizes the completed output before
projection. `T3_PARALLEL_SKINNING` defaults to the geometry setting, so the
existing two-CPU build picks it up while serial builds keep their current path.

Two repeated 5,400-field active-game probes match exactly: the serial skin
control completes 4,009 simulation ticks and 1,002 submissions; parallel
skinning completes 4,125 ticks and 1,031 submissions. Frame work moves from
84 to 81 ms, skin from 8 to 5 ms and render from 46 to 43 ms. Ymir's video
observer sees 1,008 versus 1,037 fields with changed fighter/arena pixels
(HUD excluded), a 2.9% increase. Both builds report zero draw timeouts and
guest errors. The candidate records 149 late swaps versus 0 in the control;
that cadence cost is retained in the report rather than hidden by submission
counts.

For a same-pose image gate, both builds stop simulation at tick 3,600. The
fighter-and-arena crop is pixel-identical; only the HUD differs. The candidate
reaches the stable image 116 fields earlier, with the same 907 changed fields
and 117 distinct region images across the run. The release image grows by 848
bytes (849,304 total), below the 983,040-byte limit. The final image also
boots from CD in Mednafen with both Paul models and a running FRAME counter
(`/tmp/paul-parallel-skinning-final-cd`). Evidence is under
`/tmp/paul-parallel-skinning*5400`.
