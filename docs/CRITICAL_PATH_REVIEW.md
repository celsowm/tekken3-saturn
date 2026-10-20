# Whole-frame review, 2026-10-20

This reviews port `e97fad7` and installed SDK `6302bec`, retaining the original
556-vertex, 773-face Paul mesh, both actors, original combat, textures and stock
RAM. It changes diagnostic coverage, not release rendering or simulation policy.
The target remains 60 committed ticks and 30 new complete images per second.

## What the previous plan got wrong

The Slave's large polling share is not a directly recoverable frame-time budget.
The existing renderer already overlaps the two actors' skinning and then their
geometry. Moving both actors' geometry to one worker removes that parallelism.
Snapshots could expose another scheduling boundary, but are not a demonstrated
solution while the independent frozen renderer exceeds 33.33 ms.

The earlier near-30 results used the procedural model. The camera-space C,
pure-projection and MAC.W variants were tested on the original Paul pair and
rejected. The exact Q16 MAC.L kernel, generic guarded pose-matrix kernel,
full native branch cache, radix painter and 88-byte face union are also already
rejected experiments. None becomes a new candidate by renaming it.

## Fresh stage isolation

Separate instrumented controls run for 1,200 Ymir fields, after warmup 300.
Cache emulation and both CCR enable bits are ON. The retained compiler, SDK,
models and release switches match. Profiling and drawing-removal controls are
diagnostics, not qualified gameplay configurations.

| Observation | Full encounter | Frozen two-Paul renderer | Simulation only |
| --- | ---: | ---: | ---: |
| Retained samples | 170 | 298 | 747 |
| Complete work, median ms | 75 | 45 | 9 |
| Simulation, median ms | 30 | 0 | 8 |
| Simulation aggregate ms/tick | 8.326 | — | 8.336 |
| Skin, median ms | 4 | 4 | 0 |
| Geometry including join, median ms | 20 | 20 | 0 |
| Flush, median ms | 10 | 10 | 0 |

These are instrumented distributions, not release FPS. Stage medians are not
additive. The ring excludes the initial VBlank and begin-frame waits.

Version 2 of the optional timing ring measures camera/scene setup, static stage
submission and diagnostic publication separately. A second matched full/frozen
matrix retains 75/45 ms median work. The floor and rails alone take 4 ms median,
5 ms p95. Camera and diagnostic publication each round to 0 ms median and 1 ms
p95; zero at millisecond resolution does not mean zero cost. The reader still
decodes archived version-1 rings and never invents zero values for these formerly
unmeasured stages. The extra storage is 12,288 bytes of Low WRAM in timing builds
only. Release builds contain neither its storage nor its timing calls.

An independent actual Mednafen CD capture of the new frozen diagnostic shows,
at movie second 24: skin 5 ms, placement 3 ms, geometry 22 ms, flush 14 ms,
floor 4 ms, present 7 ms and complete work 64 ms. Camera and diagnostics round
to zero. The preceding diagnostic shows 64 ms complete work too. These are
individual last-completed samples with additional profiler HUD work, not a
native timing distribution. A separate 35-wall-second full diagnostic recording
contains only 5.83 recorded seconds and does not establish gameplay coverage;
it is excluded. The recorder now rejects missing requested screenshots instead
of accepting ffmpeg's successful exit without an output image.

The replacement full-game CD recording contains 64.871 recorded seconds and
visibly reaches gameplay. Its inspected version-1 HUD samples show 102–111 ms
complete work. The [call-span guide](SATURN_PROFILE_CALL_SPANS.md) records the
sample times, artifact hashes and separate release-binary control: all release
application bytes match the qualified build except 35 panic source-line
literals. These controls establish coverage without claiming a speed gain.

Private evidence: `structural-v2-critical-study`, `structural-v2-critical-detail`,
`structural-v2-critical-native-frozen` and `structural-v2-critical-native-detail`
under `assets/imported/`. The preceding uninstrumented 60-second native release
interval remains 507 submission-counter increments; this study claims no new
release speedup.

## Completed calls, not exclusive-PC percentages

The new analyzer pairs observed SH-2 calls with their exact PC+4 return address.
Each CPU retains its own interpreter-cost clock. A cost spans pre-call through
pre-RTS, including children/interrupts and excluding the final RTS/delay slot.
Nested costs overlap. They are not scheduler timestamps or hardware cycles.
Do not align the two CPU clocks to construct a global execution timeline.

The complete 36-field trace at fields 1160–1195 contains seven completed render
calls, seven geometry joins and 29 completed owned simulation steps. All event
classes reconcile with metadata; no frames, events or PC observations are lost.
No calls are skipped by nonlocal returns. Boundary calls remain open and are
excluded. Unmatched returns, including GCC's movstr helper entry jumps, are
reported rather than incorrectly popping their callers. A preceding 60-field
attempt overflowed the Slave event buffer and is explicitly rejected.

| CPU / completed call | Calls | Median modeled cost |
| --- | ---: | ---: |
| Master owned simulation step | 29 | 200,715 |
| Master `t3_characters_render` | 7 | 716,342 |
| Master actor geometry submission | 7 | 409,243 |
| Master geometry join | 7 | 94,437 |
| Master skinning range | 7 | 94,614 |
| Master face flush | 7 | 248,223 |
| Slave prepared geometry body | 7 | 416,740 |
| Slave skinning range | 7 | 94,626 |

For scale only, this Ymir trace advances approximately 26.952 million modeled
cost units per nominal second on each active CPU. The geometry join therefore
corresponds to about 3.5 ms of modeled active time, not the entire 20 ms geometry
stage. Worker publication/cache operations also cost time; moving the actor
boundary does not automatically eliminate them.

A deliberately optimistic scheduling control removes the entire Master's
geometry join and replaces its blocking skin-for call with only its useful skin
range. Add Master actor rendering and flush, then the Slave task bodies; use
per-invocation **means**, not a sum of medians. The selected costs are:

- Master render/flush after those subtractions: 851,654 cost units/image.
- Slave task bodies: 587,941 cost units/image.
- Owned simulation: 200,714 cost units/tick.

At 30 images and 60 ticks this is about 55.23 million modeled units per second,
against 53.90 million for two continuously active CPUs in this control. Floor,
HUD, camera, input and final submission are still omitted. This is an
optimistic fixed-work scheduling calculation, not an FPS prediction or a
universal lower bound after changing kernels. It shows why balancing the
existing work alone is insufficient in this model. The real divider and memory
costs require the independent Mednafen controls; their timing gap is substantial.

## VF2 reference: what can actually be compared

The existing real-BIOS VF2 fight capture has both CCR values ON, clock factor
1/1, 704x448 interlaced output, PTMR=2 and BGON=23. The Paul capture has
320x224 progressive output, PTMR=1 and BGON=0. Their median system field spans
are 476,842 and 449,204 cycles respectively. Raw frame-cycle totals are not
equivalent across these modes.

The 120 retained VF2 latest-completed-list samples have median draw span
0.02833 fields and maximum 0.04246 fields. The seven Paul list samples have
median 0.13233 fields and maximum 0.13261 fields. Paul includes the manual
mode's clear polygon; VF2 uses a different presentation policy. These are
sampled emulator draws, not equal fill workloads or a measurement of VF2's
new complete-image rate. Automatic redraw/interlace remain explicit limits.

Even the Paul lists finish in a small fraction of a field in this narrow
scene. CPU preparation, not a demonstrated saturated VDP1, dominates these
captures. VF2's enabled VDP2 layers and the port's CPU-projected checkerboard
are an architectural difference to investigate. Neither proves how AM2 splits
its simulation/geometry, and its retail code is not copied into the port.

## Two concrete opportunities identified in the asset and arithmetic

`audit_kmd_topology.py` uses the existing importer/compiler and exports only
counts and bounds. It confirms:

- 453 rigid vertices, 103 blended vertices; **all 103 blends are two equal
  halves**, not general three-influence weighted blends.
- 410 faces have all corners on one rigid joint; 406 permit backface culling.
- 277 faces include blended vertices and 86 cross rigid joints. They require
  the general path.
- 235 vertices occur exclusively in the 406 eligible faces. This is a static
  upper bound on an opportunity, not a count of vertices actually removable
  for a camera or a saving estimate.

First, a compiled per-part visibility plan could classify those rigid faces
before skinning/projection. Test conservative decisions and actual avoided
vertices before building a runtime default. Original fixed-point world-normal
rounding, double-sided faces, degeneracies and near clipping need independent
oracles/fallbacks. The rejected screen-winding substitution is not this proof.

Second, the original visual bridge has an **exact** factor not used by the
generic Q16 skin kernel. Each rotation coefficient is `16 * R12`; a Q15 input
coordinate becomes `2 * q`. Consequently, for each axis before translation:

```text
((sum((16 * R12) * (2 * q)) + 32768) >> 16)
    == ((sum(R12 * q) + 1024) >> 11)
```

No coefficient quantization or rounding change is involved. The maximum
influence-position L1 norm is 5,488. Even full signed-16-bit rotations bound the
absolute three-product sum by **179,830,784**, leaving room for the rounding
constant in signed 32 bits. This applies to immutable local positions across
poses; it is not an assumption that rotations always lie in +/-4096. Translation
and the half-blend need their own output bounds and exact negative-floor rules.
The current renderer stores Q16 matrices and performs 64-bit sums instead.

This justifies a new **bounded C32 range benchmark** using original Q12
coefficients and a validated asset plan. It differs from the rejected camera
quantization/MAC.W path and the per-call generic matrix guard. Pack/validate
once per pose/range, include that cost, retain wide fallback and the exact
existing blend/placement behavior. It is not yet a measured optimization and
must not be presented as sufficient for 30 images/s.

## Revised execution order

1. Benchmark the exact factored Q12/Q15 range in the existing SDK skin ladder,
   with independent wide references, full signed rotations, half blends,
   translation/alias bounds, both emulators and packing/publication included.
   Reject it if complete-Paul release cadence fails to improve. Keep the SDK
   API generic; the original-format bridge stays in this repository.
2. Measure the compiled rigid-part visibility opportunity and the actual
   avoided vertex set. Separate it from painter/command work. The principal
   geometry stage is still 20 ms; a theoretical 235-vertex ceiling is not a win.
3. Measure command-native preparation separately: the manual permits stable
   painter traversal via CMDLINK without physically reordering command records.
   Prototype on the cube ladder, preserving list ownership, END/HUD quotas,
   cold clipping fallback and residency checks. Account for extra command
   holes/upload bytes and the nearly full High WRAM. Existing flush is only
   10 ms, including sorting: even eliminating all of it would not alone close
   the native frozen-renderer budget. Do not rebuild a second full queue.
4. Treat the 4–5 ms static floor as a bounded secondary target. Use the existing
   camera-keyed baked-view API first; invalidate on every relevant camera,
   viewport, near-plane or material change. Measure active camera movement too.
   A VDP2 floor requires its own projection/pixel qualification.
5. Recompute both CPU work and dependency budgets before selecting a snapshot
   pipeline. Preserve the already useful two-actor parallel geometry. A pipeline
   may remove passes/publication or expose real overlap; simply moving all
   rendering to Slave is not the proposed default.
6. Repair retained tick debt/input ownership separately and run the existing
   continuous and live integration gates. Deleting elapsed time currently hides
   overload; carrying debt does not make the work fit.

Every promoted runtime candidate still needs stock-memory checks, matched
pixels, serial/worker results, Dino where the shared path applies, all 24 combat
cases, actual CD execution and long neutral/combat/round-transition cadence.
Authoritative changes additionally need all 16,334 differential ticks. This
study closes none of the final 60-tick/30-picture gates.

## Reproduction and primary references

```sh
python3 scripts/run_saturn_stage_matrix.py \
  --baseline build/structural-v2-direct-qualified --bios "$SATURN_BIOS" \
  --cases full simulation render-two --frames 1200 \
  --out assets/imported/new-critical-study

python3 scripts/analyze_saturn_profile.py \
  --prefix assets/imported/structural-v2-critical-profile-36/debug \
  --elf assets/imported/structural-v2-critical-profile-36/profile.elf \
  --nm ../saturn-tools/bin/sh2eb-elf-nm \
  --span-symbol t3_characters_render --span-symbol sat_parallel_wait \
  --out assets/imported/new-inclusive-report.json

build/asset-venv/bin/python scripts/audit_kmd_topology.py \
  --kmd "$PRIVATE_PAUL_KMD" --out assets/imported/new-topology-report.json
```

Use the SDK's existing `renderer_pipeline_study.frame_metrics` for the retained
Paul and VF2 frame CSVs; it excludes stale/mismatched begin/end pairs. The
primary local manuals reviewed were the
[Dual CPU guide, sections 4 and 7](https://github.com/celsowm/libsaturn/blob/6302bec652ad5ef79da3a7c834ece9340e0e2b38/docs/sega_saturn_hardware/manuals/saturn-dual-cpu/01-sega-saturn-dual-cpu-user-s-guide.md),
[SH7604 cache chapter](https://github.com/celsowm/libsaturn/blob/6302bec652ad5ef79da3a7c834ece9340e0e2b38/docs/sega_saturn_hardware/manuals/sh7604/12-section-8-cache.md),
[VDP1 VRAM ownership/access rules](https://github.com/celsowm/libsaturn/blob/6302bec652ad5ef79da3a7c834ece9340e0e2b38/docs/sega_saturn_hardware/hard/vdp1/hon/p02_10.md),
[plot triggering](https://github.com/celsowm/libsaturn/blob/6302bec652ad5ef79da3a7c834ece9340e0e2b38/docs/sega_saturn_hardware/hard/vdp1/hon/p04_13.md)
and [command jump formats](https://github.com/celsowm/libsaturn/blob/6302bec652ad5ef79da3a7c834ece9340e0e2b38/docs/sega_saturn_hardware/hard/vdp1/hon/p06_10.md).
