# Paul stage isolation and readable framing

This implements the first diagnostic step of
[the recovery plan](PAUL_PERFORMANCE_RECOVERY.md) and a camera correction.
The intermediate 60 Hz logic / 30 Hz presentation gate is still open.
Subsequent experiments and the retained Q15 conversion are recorded in
[the runtime experiment report](PAUL_RUNTIME_EXPERIMENTS.md); full-game cadence
remains effectively unchanged.

## Measurement method

The new opt-in stage ring has 1024 outer-loop sample slots,
each with VBlank field, actual tick count, unclamped elapsed fields and stage
durations in integer milliseconds. It adds 53,260 bytes of diagnostic storage
in Low WRAM. It is absent from ordinary builds. It excludes the outer
VBlank/begin-frame wait and includes the end-frame call in `present`/`work`.
Geometry timing includes dispatch, Master preparation, worker join and merge.
Skeleton timing is presentation bridge/base-skeleton work, not authoritative
simulation FK/IK. Simulation includes required core pose/contact work and the
current per-tick observer. Additional PC profiling is needed to divide that
stage further. Stage medians and p95 values must not be added as if they were
one particular frame.
When the ring is full, the decoder excludes the next write slot because the
CPU may be paused while overwriting it. At most 1023 stable rows are then
returned. Inconsistent publication metadata is rejected.

Runs use the same private Paul imports, GCC 13.2 Release core at `-O3`, frontend
`-O2`, the GC SDK, paired geometry, cache on and no instruction tracing.
Frozen-pose runs stop at tick 120. Each runs 1200 fields and selects completed
samples whose start field is >=300. Exact retained endpoints and distributions
are in the private `stages.json` files. Cadence qualification uses separate
uninstrumented captures.

| Case | Samples | Simulation ms/tick | Skin median | Geometry median | Flush median | Work median / p95 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Simulation without scene/HUD drawing | 298 | 12.08 | 0 | 0 | 0 | 34 / 46 |
| Frozen pose, one actor, old camera | 298 | no ticks | 5 | 18 | 8 | 40 / 40 |
| Frozen pose, two actors, old camera | 223 | no ticks | 10 | 26 | 15 | 62 / 62 |
| Full neutral game, old camera | 122 | 12.07 | 10 | 26 | 15 | 106 / 118 |

The simulation-only loop performs multiple simulation ticks per sample: its
34 ms work median is not one tick's cost. Required logic already consumes most
of a 16.67 ms tick budget. Frozen drawing alone also misses 30 Hz. Both paths
need substantial improvement; simply raising catch-up limits cannot fix this.
The corrected full-game window has 890 elapsed fields but only 488 simulation
ticks: 402 fields exceed the current four-field-per-loop clamp. The simulation
is losing elapsed time, not merely drawing fewer pictures. Simulation-only has
894 elapsed fields and 894 ticks in its selected window. This increment makes
that loss observable; it does not change scheduling or claim to fix debt.

At roughly 12 ms per tick, 60 Hz logic would consume about 720 ms of each
second before drawing. A 30 Hz picture would then have only about 9 ms left
for all remaining work. These are approximate planning budgets, not a proof
that independent samples add to a sustainable frame. They demonstrate why the
current ~60 ms rendering path needs a substantial redesign, alongside logic
cost reduction; a few percent improvement cannot close this gap.

Artifacts under ignored imported assets:
`recovery-simulation-stages`, `recovery-render-one-stages`,
`recovery-render-two-stages`, `recovery-full-o3-stages`.
The earlier `recovery-full-stages` run is excluded: a failed initial compiler
configuration left its Release C flags empty. The corrected O3 run above is
the accepted comparison, and future symbol captures retain `CMakeCache.txt`.

## Rejected experiment

Moving only actor matrices and numerical vertex/projection buffers into High
WRAM retained identical screenshot bytes but changed frozen two-actor work
from 62 to 61 ms. This did not justify the placement and pointer complexity.
The experimental code was removed. Evidence remains in `recovery-high-stages`.
It does not establish that every possible memory-layout optimization is useless.

## Camera correction

The reviewed PS1 capture `ps1-paul-start-phases/screen.png` shows Paul occupying
a large share of usable screen height; the old port fixed-pose image leaves
him about 54 pixels tall. This identifies a framing problem, independently of
numerical pose parity or texture fidelity.

The new 45-degree camera tracks the fighter midpoint and increases its distance
with separation. At the same tick-120 pose it renders Paul approximately
130 pixels tall, with feet and both characters visible. It changes neither
model scale nor authoritative collision. `T3_CLOSE_CAMERA=OFF` retains the old
view for A/B comparisons. Screenshots remain private in
`recovery-camera-framing` and `recovery-render-two-stages`.
Final framing uses a 5.6-unit minimum distance. At round end it pulls back to
at least 7 units and lowers its target to retain the fallen fighter above the
bottom HUD; the initial closer-camera movie exposed that overlap.

The larger image increases raster work: the first near-camera frozen sample
had a 65 ms work median versus 62 ms for the old camera. This is a visual
correction, not a performance gain. The current private asset still shares one
costume mesh/texture set across both actors; the black/red outfit distinction
in the original is not reproduced by this camera change. No texture, UV or
complete visual-parity claim is made.

The final camera's untraced neutral endpoints are
(581 VBlanks, 316 logic ticks, 78 submissions) at 600 requested fields and
(1183, 644, 160) at 1200. At nominal 60 Hz their interval gives **8.17
submissions/s and 32.69 logic ticks/s**. Artifacts:
`recovery-framed-untraced-{600,1200}`. Final frozen-pose timing through the
runner also gives a 65 ms work median, with 27 ms geometry and 17 ms flush,
in `recovery-framed-matrix/render-two/capture/stages.json`.

The reproducible matrix runner clones private asset options and compiler flags
from an existing optimized Release build, refuses missing optimization flags,
and writes exact commands, build logs, provenance, raw rings and reports into
a fresh private output directory. The `render-one` case was also executed
through this runner in `recovery-matrix-runner`.

The stage ring passed an injected 0xA5 Low WRAM initialization run, retaining
33 valid samples: explicit metadata initialization prevents stale RAM from
being mistaken for a published ring. A scripted 1800-field movement run with
the close camera verified all 202 worker preparations against serial results,
with zero reported rendering errors or timeouts.
Repeating that comparison after the KO framing correction passed all 203
worker preparations (`recovery-framed-verified`). Ordinary game builds contain
no `g_t3_perf` ring; the diagnostic build's symbol is exactly 53,260 bytes.

A 6000-field scripted full-game stage capture through the runner retained 788
samples from start fields 303–5980. It covers approach, repeated low attacks
and round completion; its final observation is round 2, score 1–0, with no
render errors or timeouts. Work median/p95/maximum are 109/125/132 ms; geometry
is 27/28/28 ms and flush 17/18/19 ms. This is instrumented workload evidence,
not playable cadence qualification. `--device-script` retains the input file
and its hash in the matrix output. Artifacts: `recovery-framed-combat-stages`.

## Native panic gating and FRT correction

The final camera candidate exposed a late failure in the neutral timeout
scenario: the last snapshot stayed at 5635 VBlanks, while the game displayed
native TIMEOUT at `main.c:193` during character preparation. Earlier diagnostic
records still said no error because panic prevented the next publication.
`saturn_probe.py` now always reads the SDK's native panic record when available,
saves a structured error and rejects the run even if gameplay counters are
stale. A synthetic signed/big-endian panic test protects this failure path.

The complete 12-field debugger window shows Slave geometry executing when
Master enters panic; a later slot dump contains successful worker completion.
The SDK's FRT high/low reader had an IRQ latch hazard: another counter read
could replace the latched low byte after a high-byte rollover, inventing a
large elapsed interval. LibSaturn `35647ed13af594f74f74a2c9d194b513b558ad31`
checks the high byte again and retries if it changed, without masking IRQs,
changing the prescaler or raising the timeout limit. Its host regression
simulates the torn read and the full 16-bit rollover.

After rebuilding with that SDK, the formerly failing 12000-field run reaches
11990 VBlanks and the expected terminal draw: round 2, score 2-2, timer zero,
both fighters at full health, no render error or timeout. Artifact:
`recovery-frt-fixed-timeout`. A fresh 35-second Mednafen CD boot also passes
(`recovery-frt-fixed-cd`). SDK `make test` and five package CTests pass. Earlier
timing tables above predate this fix; the final matrix is retained separately
in `recovery-frt-final-matrix`.

The repeated matrix with the fixed SDK reports these work median/p95/maximum
values: full neutral 109/122/127 ms, simulation-only 34/46/46 ms, frozen one
actor 42/43/43 ms, frozen two actors 65/66/66 ms. Full-neutral simulation costs
12.05 ms/tick. The uninstrumented 600/1200 interval still measures 8.17
submissions/s and 32.69 logic ticks/s (`recovery-frt-final-{600,1200}`).
The timer change fixes a false failure; it does not satisfy the speed gate.

## Final qualification

The fixed SDK and final camera pass all 24 scenarios in
`recovery-frt-final-combat-suite/suite.json`, including controls, combat, KOs,
replay, victory, match reset and both neutral timeout cases. Native panic
inspection is active throughout. Host CTest passes 42 tests, and the Python
probe and stage suites pass 10 and 4 tests respectively.

The final camera also completed a 160-second scripted Mednafen CD run
(`recovery-framed-encounter-cd`, before the SDK timer correction); the rebuilt
SDK completed the separate 35-second CD boot noted above. These are emulator
and recorded-input checks, not physical hardware or physical controller tests.
The larger fighter framing and timer correction are qualified improvements;
the 30-render/60-logic performance target remains unmet.
