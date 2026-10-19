# Runtime experiments after stage isolation

The intermediate 30-render/60-logic gate remains open. None of these experiments
meets that target or certifies physical hardware timing.
The Q15 baseline used LibSaturn `a46598b24ddd386ee73cadbd3eebf42a811ea082`;
subsequent renderer, memory and publication experiments are recorded below.
The current retained package is `0bf55566a9fa046c8f1e142f7fa536f58574b6de`.

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
