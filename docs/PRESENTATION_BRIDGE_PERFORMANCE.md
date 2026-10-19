# Simulation-to-presentation bridge

The owned-match bridge previously remapped both fighters' joints, reconstructed
diagnostic hurt cylinders and encoded two volatile debugger records on every
simulation tick. A catch-up image normally executes four ticks; only the final
visual snapshot is consumed by rendering. Combat uses the owned match's joint
slots directly and does not consume that visual copy.

`t3_move_probe_present()` now publishes the final owned pose before rendering.
Its cost is included in the existing `placement` timing stage. Move-event and
attack decoding, error accounting, animation clocks, collision and combat keep
their per-tick cadence. Legacy preview modes retain per-tick pose evaluation.

`T3_POSE_TRACE` defaults to OFF. Private oracle captures must explicitly enable
`-DT3_POSE_TRACE=ON`; this restores visual publication and paired debugger writes
on every tick, including intermediate ticks. The debugger ABI is unchanged.
The release ELF contains neither trace globals nor the trace encoder.

## Matching controls

Baseline source: `fe7d31b`. SDK: `10f995605e80a3903b345d1de029837168475a87`.
Both release controls use the same private assets, close camera, parallel
geometry and nominal 60 Hz Ymir configuration. Cache emulation is explicitly
ON; the generic probe's omitted `--sh2-cache` is not an equivalent control.
The pinned Ymir DIVU model returns division results immediately. These are
emulator observations, not hardware certification or wall-clock host timings.

Separate nested-FRT builds, 1,200 fields with aggregates after field 300:

| Variant | Tick root, ms/tick | Owned match, ms/tick | Bridge, ms/tick |
| --- | ---: | ---: | ---: |
| Original per-tick pose and trace | 12.837 | 11.392 | 1.256 |
| Trace disabled, pose still per tick | 11.930 | 11.406 | 0.335 |
| Trace disabled, pose once per image | 11.617 | 11.410 | 0.018 |

The 0.018 ms bridge excludes the pose publication moved into `placement`.
Most saved work comes from disabling unsolicited trace writes. The match's
simulation work remains effectively unchanged. Nested instrumentation includes
hooks and interrupts and changes scheduling; do not treat its tick-root change
as a release-frame improvement.

Separate whole-path stage builds, also 1,200 cache-on fields:

| Stage | Original median / p95, ms | Candidate median / p95, ms |
| --- | ---: | ---: |
| Simulation (usually four ticks) | 43 / 54 | 38 / 49 |
| Placement, including pose publication | 2 / 3 | 3 / 3 |
| Geometry, including worker join | 20 / 20 | 20 / 20 |
| Flush | 10 / 10 | 10 / 10 |
| Complete measured work | 91 / 103 | 87 / 98 |

Complete measured work fell **4.40% at the median**, below the prior 5%
whole-path acceptance target. Stage samples have millisecond resolution and
exclude the outer VBlank/begin-frame wait. Stage medians are not additive.

Uninstrumented release endpoints at 600 and 1,800 fields:

| Build | Interval VBlanks | Submissions | Logic ticks | Submissions/s | Ticks/s |
| --- | ---: | ---: | ---: | ---: | ---: |
| Original | 1,202 | 198 | 792 | 9.884 | 39.534 |
| Candidate | 1,200 | 200 | 800 | 10.000 | 40.000 |

The short-window cadence difference is only **1.18%**. It does not establish
a meaningful fluency milestone, a count of verified display swaps, or progress
to 30-render/60-logic by itself. Retain this change for removing unnecessary
debug work and making the presentation boundary explicit. The next bounded
investigation is the owned-match copy stage (~0.724 ms/tick in these profiles),
then joint composition (~5.9 ms/tick inclusive); neither may change combat state.

Release ELF SHA-256:

- Original: `fc51f5001462e54927a14deb825bcfb68cda3fb07e23e3b0027e2dd60f637bbc`
- Candidate: `b1662ed52c8482067afba355c41ebdba2454f461f91d6ec687d2c0cf7336f7ee`

## Correctness and retained evidence

At frozen tick 120, `compare_saturn_presentation.py` passes all 97 stable
snapshot fields and exact RGB pixels in the 320x135 model region below the
counter HUD. It rejects mismatched ticks, pose fields, emulator/cache settings
or model pixels. The crop hash is
`48cdd00660bfecf7c9164266d052121b2986bc9777a9d164f18f93b3aa2fb896`.
These frozen correctness captures use cache OFF on both sides and are separate
from the cache-ON timing controls. This is a snapshot check, not a continuous
original-PS1 oracle comparison.

With tracing enabled, the paired 1,128-byte pose record and epoch are byte-for-byte
identical to the original at tick 120. This validates that checkpoint; it does
not establish parity of every intervening trace record. All 43 host tests and
24 live combat/KO/replay/victory/reset/timeout scenarios pass. Public and private
Saturn builds succeed. An actual 35-second Mednafen CD boot exits successfully;
reviewed second 34 contains both actors and `FRAME 141`. That frame number is
a debug checkpoint, not an FPS estimate.

Private artifacts remain under ignored `assets/imported/`:

- `presentation-{baseline,trace-off,per-frame}-profile-cache/profile.json`
- `presentation-baseline-metrics-cache/stages.json`
- `presentation-per-frame-metrics/full/capture/stages.json`
- `presentation-{baseline,per-frame}-cache-{600,1800}/`
- `presentation-{baseline,per-frame}-frozen/`
- `presentation-{baseline-trace,trace-enabled}-frozen/`
- `presentation-per-frame-combat-suite/suite.json`
- `presentation-per-frame-mednafen/`

Use the existing variant builder on a preserved baseline checkout before
changing the source, then on the candidate. Explicitly configure
`T3_PERF_SAMPLES=OFF`, `T3_SIM_PROFILE=OFF`, `T3_POSE_TRACE=OFF` and
`T3_RENDER_BENCH_TICK=0` for release cadence. The original baseline predates the
trace option and always writes traces. Create separate profiling and frozen
variants; never rebuild the baseline using candidate source.

```sh
python3 scripts/saturn_probe.py --build build/presentation-per-frame \
  --bios "$SATURN_BIOS" --sh2-cache on --frames 600 \
  --out assets/imported/presentation-per-frame-cache-600
python3 scripts/saturn_probe.py --build build/presentation-per-frame \
  --bios "$SATURN_BIOS" --sh2-cache on --frames 1800 \
  --out assets/imported/presentation-per-frame-cache-1800
python3 scripts/report_saturn_runs.py --cadence \
  assets/imported/presentation-per-frame-cache-600 \
  assets/imported/presentation-per-frame-cache-1800
python3 scripts/compare_saturn_presentation.py \
  assets/imported/presentation-baseline-frozen \
  assets/imported/presentation-per-frame-frozen
```
