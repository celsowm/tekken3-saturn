# Branch representation and exact matrix experiments

2026-10-19, following the [bounded simulation report](SIMULATION_PROFILING.md).
Neither experiment changes the retained gameplay implementation. The retained
release rate remains **9.758 submissions/s and 39.033 logic ticks/s**. These
experiments do not close the 30-render/60-logic recovery target.

## Native immutable branch rows: rejected

The private Paul bank contains 1458 branch rows (17496 source bytes), the
common move bank 1521 rows (18252 bytes), and the EXE common table 672 rows
(8064 bytes). A complete native representation therefore needs 43812 bytes,
with the same 12-byte logical row shape. The prototype reserved 4096 rows,
**49152 additional Low-WRAM bytes**, shared by both actors. It decoded at bank
initialization, bound storage to immutable bank lifetime and kept the original
walker for traversal, live restrictions, conditions, commands and frame windows.
It preserved call/return, END and conditional skip handling in source.

This was a distinct experiment from the previously rejected delayed-target
conversion: it removed repeated endian decoding and temporary row copies for
all visited rows. It did not preselect branches or remove dynamic checks.

| Measurement | Retained baseline | Native-row prototype |
| --- | ---: | ---: |
| Diagnostic root, ms/tick | 12.847 | 12.512 |
| Diagnostic branch span, ms/tick | 3.096 | 2.745 |
| Visited rows/tick, neutral | 280 | 280 |
| Command queries/tick | 144.178 | 144.177 |
| Release interval, submitted frames | 195 | 198 |
| Release interval, VBlanks | 1199 | 1195 |
| Release interval, logic ticks | 780 | 792 |
| Release submissions/s | 9.758 | 9.941 |
| Release logic ticks/s | 39.033 | 39.766 |

Both release intervals use independent endpoints at requested fields 600 and
1800 with the same BIOS, harness, cache policy, assets and runtime settings.
Diagnostic captures use 1200 requested fields and warmup 300. Their frequencies
are normalized individually. Instrumented branch span improves 11.36%, root
2.61%, and release cadence 1.88%. Hook/cache layout changes and unequal tick
windows remain limitations; the measured branch gain is not a decoder-only cost.
The neutral input workload does not qualify attacks, malformed banks or KO.

The optimized host experiment passes 43 tests, but those existing host callers
do not activate native row preparation. This is not differential qualification
of the cached walker. The full 16334-tick reference gate and 24 live cases were
**not** rerun for this rejected prototype. It is removed before promotion:
its measured benefit misses both the selected 20% stage and 5% complete-path
targets and consumes substantial RAM. Arithmetic and authoritative release
logic stay at the previously qualified implementation.

Private evidence is retained under `assets/imported/recovery-native-branches*`,
including the source patch, build metadata, raw symbol dumps, endpoint snapshots
and comparison JSON. `build/recovery-native-branches*` belongs to the rejected
patch; rebuilding it from current source would not reproduce the candidate.
The source patch is local research evidence, not an enabled shipping feature.

## Reusable comparison

`scripts/compare_simulation_profiles.py` reads and revalidates the **raw** nested
captures, checks BIOS/harness provenance and warmup, and reports inclusive and
exclusive cost changes with per-tick call counts. Event counters carry no
claimed timing reduction. Optional saved release endpoints remain a separate
section, with the existing per-ELF cadence validation. The comparison does not
certify matching workloads, uninstrumented flags, source equivalence, combat
correctness or distinct displayed buffers; establish those independently.

For the retained local experiment:

```sh
python3 scripts/compare_simulation_profiles.py \
  --baseline assets/imported/recovery-substage-matrix/neutral/capture-0 \
  --candidate assets/imported/recovery-native-branches-profile/capture-0 \
  --baseline-cadence assets/imported/recovery-substage-disabled-600 \
                     assets/imported/recovery-substage-disabled-1800 \
  --candidate-cadence assets/imported/recovery-native-branches-600 \
                      assets/imported/recovery-native-branches-1800 \
  --out assets/imported/recovery-native-branches-comparison.json
```

Inclusive intervals overlap; never sum their reductions into a synthetic FPS.
The existing parser tests also verify comparison units, differing sample counts,
event handling and rejection of mismatched provenance/warmup.

## Exact SH-2 matrix arithmetic: procedural control retained

LibSaturn `4a610f6206a434dce8947c6c833a27b09221c37d` now has
`examples/matrix_mac_bench`, a reusable runner and
[the complete experiment contract](https://github.com/celsowm/libsaturn/blob/4a610f6206a434dce8947c6c833a27b09221c37d/docs/MATRIX_MAC_BENCH.md).
It compares C and SH-2 `MAC.W` with full-width sums, plus a separate exact
bounded conversion that clamps before narrowing to a 32-bit shift. SR.S must
be cleared for 64-bit `MAC.W`; the kernel saves/restores it, and interrupt MAC
register preservation is checked against the tested SCU wrapper disassembly.
All transpose, alias-safe output and rounding work remains inside each call.

All four kernels pass 33792 comparisons in Ymir and the independent Mednafen
CD boot: signed extrema/random values, raw wide dot sums, negative-floor
rounding, output aliases, incoming S=0/S=1 and S restoration. The timing corpus
uses Q12-scale coefficients and single calls/18-link chains, with High/Low-WRAM
input controls. O2 and O3 results stay separate.

For O3 and High-WRAM inputs, Ymir's 18-link normalized times are 33.182 µs per
matrix for C64, 31.170 for MAC64, 31.665 for C32 and 29.389 for MAC32. The best
procedural gain is **11.43%**. The final Mednafen image shows chain-batch raw
FRT medians 964 / 908 / 923 / 847, CHECKS 33792, ERRORS 0 and FRAME 972: 12.14%
for the combined candidate. No Mednafen FPS inference follows from those values.

The release debugger's earlier 3.830% exclusive share for `psx_mul_rot` is only
a prioritization clue. Multiplying it by a procedural kernel gain is not an
inclusive whole-game saving prediction. Neither this control nor the branch
experiment justifies the expected recovery milestone. No new assembly public
API is promoted, and no Paul/Dino runtime benefit is claimed.

## Next selected boundary

Return to the independent frozen-renderer cost, approximately 49 ms before
simulation. Extend the existing procedural cube controls around dynamic world
transform/projection, visibility and face admission; retain fixed geometry and
screen coverage and compare RGB/indexed paths. Separate reusable SDK work from
Tekken-specific pose/logic. Carry a measured renderer change through Dino and
unchanged Paul, then remeasure uninstrumented complete cadence. Do not repeat
the full native branch cache or promote MAC arithmetic solely because it uses
lower-level instructions.

Pinned Ymir DIVU results remain immediate; these are emulator observations,
not physical Saturn qualification. Port CI at `dbe85e6` passed both workflows.
SDK `f11b277` passed Package and Skeletal gates; its broad Phase 3 job fails
because `examples/bullet_hell_stress/assets/palette_bgr555_be.bin` is absent,
before any matrix benchmark change. This existing failure is not labeled green.
