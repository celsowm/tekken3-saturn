# Native CPU and renderer budget

The public LibSaturn cache diagnostic is required at SDK revision
`20a1c6f9aff6824f5945aef1d1d711c246f4c0ab` or later. Its Package and Skeletal
character workflows pass. The separate Phase 3 workflow still fails; this is
not a claim that all SDK gates are green.

## Read-only boot inspection

`T3_CPU_DIAGNOSTICS=ON` reads Master CCR at entry and after initialization,
and attempts one two-item parallel batch to read CCR on both processors.
The Slave result is accepted only with an initially healthy, empty queue and
exactly one additional submitted, completed and Slave task. Master fallback
is reported as unknown (`SCCR -1`). No cache register is changed by this probe.
The cache-enable flag is bit zero, per SH7604 manual section 8.

An actual Mednafen CD boot displays **MCCR 1 SCCR 1**. Cache-on Ymir dumps
also contain entry CCR 1, post-initialization values `[1,1]` and verified
Slave execution. Disabled cache is therefore not the explanation for this
configuration's slow renderer. Other CCR modes and real hardware remain
separate controls.

Retained private evidence:

- `assets/imported/boot-cache-inspect-clear-mednafen/`: 35-second CD boot;
  second 34 shows both cache values and FRAME 140.
- `assets/imported/boot-cache-inspect-clear-ymir/`: 600-field injected run.
- `assets/imported/boot-cache-inspect-frozen/`: 97-field matched-pose model
  crop equals the existing golden reference exactly. Diagnostic text is
  outside that crop.

## Stage timings visible during native boot

`T3_STAGE_HUD=ON` requires `T3_PERF_SAMPLES=ON` and displays the **last completed**
sample, avoiding a read of the sample currently being filled. Both options
default OFF. Values are integer milliseconds; the diagnostic adds HUD work.

Observations with identical renderer settings and the same frozen model at
tick 120 (the native build moves only the benchmark HUD label):

| Completed sample | Mednafen CD | Ymir cache-on injection |
| --- | ---: | ---: |
| Simulation | 0 | 0 |
| Skeleton | 2 | 1 |
| Skin | 9 | 8 |
| Placement | 7 | 3 |
| Geometry | 30 | 19 |
| Flush | 31 | 10 |
| Present | 8 | 2 |
| Complete work | 100 | 52 |

The native observation is second 26 of
`assets/imported/boot-stage-inspect-frozen-clear-mednafen/`; Ymir is field
1199 of `assets/imported/boot-stage-inspect-frozen-ymir/`. Moving the benchmark
label below the diagnostic column changes only HUD placement. These are
individual instrumented samples, not distributions, matched submission
serials, displayed FPS or physical Saturn measurements.

A live four-tick sample likewise reads 141 ms complete work in Mednafen
versus 90 ms in Ymir. The frozen control establishes that the discrepancy
persists without simulation. `FLUSH` includes ordering, material validation
and CPU command preparation; it is not a measurement of VRAM transfer alone.

## Consequence for the next renderer experiment

Use the procedural cube ladder to compare identical geometry, materials,
commands and model pixels with Low/High WRAM placement. Require complete-list
VDP1 presentation in both controls and retain native CD recordings. Earlier
Ymir-only placement controls showed roughly 2–3% CPU benefit; native results
must qualify memory changes independently. The available High WRAM cannot
hold an unrestricted copy of the current face array plus all game state.
Measure compact hot records or bounded staging before adopting a new layout.

No renderer speed improvement, Dino qualification, retained tick debt fix,
or 60-logic/30-picture milestone is claimed by these diagnostics.
