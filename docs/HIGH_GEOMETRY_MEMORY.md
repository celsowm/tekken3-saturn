# Hot actor geometry in High WRAM

The SDK's independent native cube controls identify actor vertex/projection
data as the first memory-placement candidate. They preserve capacity and
pixels while reducing the Mednafen geometry sample by about 19.2%; moving
only face records reduces it by about 3.6%. The reusable bank controls and
their qualification are published in LibSaturn `40fa9cf`, in
`docs/NATIVE_WRAM_STUDY.md`. That increment changes the benchmark and tools,
not the shared renderer runtime. Dino already keeps its projection scratch
in High WRAM; no new Dino gain is claimed.

## Consumer change

`actor_geometry_t` separates three mutable arrays from each actor's much
larger texture/material/topology state: mesh vertices, projected vertices
and world-space scratch. Actor descriptors point to fixed lifetime storage
initialized before mesh creation. Capacities, formats and projection math
are unchanged. The existing SDK publication path still publishes the
mesh and scratch ranges for Slave work; no cache-coherence rule is bypassed.

`T3_HIGH_GEOMETRY_SCRATCH` defaults ON and selects High WRAM for this group in the private
original-texture configuration. OFF keeps the independent Low WRAM control.
Existing CMake cache choices are preserved; select ON explicitly when
reconfiguring an older experiment that already cached OFF.
The public model's geometry already lived in High WRAM and retains that bank.
The change moves **35,584 bytes**, rather than trying to fit the entire
150,608-byte former actor array into the available High WRAM.

The uninstrumented private High build ends BSS at `0x060e2068`, leaving
122,776 bytes up to the bank end before the 16,384-byte linker stack reserve,
or 106,392 bytes after that reserve. This is a link-space budget, not a
runtime stack watermark. The linker still enforces stock-memory limits.

## Correctness

- Exact frozen tick-120 model pixels match both the new Low control and the
  previous presentation golden over 97 guest fields. RGB crop SHA-256 remains
  `48cdd00660bfecf7c9164266d052121b2986bc9777a9d164f18f93b3aa2fb896`.
- A cache-on verification build completes 105 paired preparations, comparing
  serial and Slave face contents and painter keys with zero divergence and
  zero serial fallback. Near-clipping fallback comparisons retain world
  coordinates; projected-safe comparisons exclude unspecified cold fields.
- Actual 27-second CD boots of both frozen controls display both actors,
  expected round state and advancing FRAME, with CCR 1 on both CPUs.
- The public cross-build passes image-size, startup-constructor and native
  disc checks. Combat and gameplay core functions are unchanged; this is a
  renderer storage change, not a new original-game parity milestone.
- All 24 live combat cases pass, including the two-KO match, replay,
  authored victory, terminal match/reset and both neutral timeout scoring
  checkpoints. Those correctness runs explicitly disable Ymir cache emulation;
  the separate cache-on serial/Slave and timing controls are described above.
- All 44 host CTests pass. The SDK bank controls pass 12 tool tests and both
  Cube pipeline bench and Harness build CI. The separate Phase 3 SDK gate
  previously failed on the unrelated missing bullet-hell palette; this is
  not represented as an all-SDK green qualification.

Instrumented stage HUDs occupy part of the model-comparison crop. Their
different numeric values correctly fail the exact image gate; visual
qualification uses separate builds with the stage HUD and timing ring OFF.
The comparator and its crop were not relaxed.

## Timing scope

At recorded second 26, corresponding frozen native CD diagnostics show:

| Last completed sample, ms | Low geometry | High geometry |
| --- | ---: | ---: |
| Skeleton | 1 | 0 |
| Skin | 9 | 9 |
| Placement | 7 | 3 |
| Geometry | 30 | 27 |
| Flush | 31 | 30 |
| Present | 7 | 7 |
| Complete work | 99 | 89 |

This is a 10.1% reduction of an individual instrumented work sample. It is
not a distribution, displayed FPS or physical hardware qualification.
Frozen Ymir medians are 51 ms complete work for both controls, with placement
3 to 2 ms and no scheduling-boundary improvement.

Uninstrumented cache-on Ymir endpoints at fields 1800 and 5400 give identical
neutral results in both banks: 3,600 VBlanks, 673 submissions, 2,692 logic
ticks and 908 discarded fields. This is 11.217 submissions/s and 44.867
ticks/s over a nominal 60-second interval, with 25.22% of elapsed time
discarded. It remains below the 60-logic/30-picture target.

Actual uninstrumented Mednafen CD recordings provide an independent
60-second neutral interval, from recorded second 18 through 78:

| Recorded second | Low FRAME | High FRAME |
| --- | ---: | ---: |
| 18 | 38 | 40 |
| 38 | 165 | 174 |
| 58 | 293 | 307 |
| 78 | 421 | 441 |

The visible counter advances **383 versus 401 submissions**, or 6.383 versus
6.683 submissions per recorded second: **4.70% more** in the High control.
These are manually read visible counter observations from the preserved
movies. They are not per-picture identity matching, a displayed-FPS
certificate, combat coverage or hardware timings. Both recordings show
the neutral encounter throughout; animation phases differ as time advances.
The gain is much smaller than the instrumented frozen-stage reduction and
must not be reported as a 10% live-game improvement.

The exact live ELF hashes are:

- Low: `6f25bdd41f95e0a018df6bb12a38ae454276b50b7170d6e3a12e6b26724879e2`.
- High: `aa5ded3e82f289b02d075b2df0f1f912c4dc4ca365fbe8ab45167f94f5e17f99`.

## Retained private artifacts

- Builds `build/high-geometry-{low,high}-{frozen,pixels,live}` preserve the
  exact CMake options and build commands through `build_saturn_variant.py`.
- `assets/imported/high-geometry-{low,high}-frozen/` hold Ymir stage dumps;
  `*-pixels/` hold the separate exact visual gate.
- `assets/imported/high-geometry-high-verify/` holds the paired/serial
  verification counters and panic dump.
- `assets/imported/high-geometry-combat-suite/suite.json` records all 24
  passing live control, combat, round and match checkpoints.
- `assets/imported/high-geometry-{low,high}-frozen-mednafen/` hold original
  CD movies, native build hashes and frames at seconds 18, 22 and 26.
- `assets/imported/high-geometry-{low,high}-live-{1800,5400}/` hold the
  uninstrumented cadence controls and their ELF/probe/BIOS provenance.
- `assets/imported/high-geometry-{low,high}-live-mednafen/` hold the separate
  79-second native CD movies and the four visible-counter observations above.

The next CPU budget work still includes command preparation/flush, simulation
copying, measured scheduling and retained tick debt. None is satisfied by
moving these arrays.

The subsequent [material-table control](MATERIAL_TABLE_MEMORY.md) separately
qualifies compiled prefixes and native texture descriptors in High WRAM,
including exact pixels and both consumers. Its live native result is separate
from the geometry gain above. Existing owner/residency checks remain in place;
the shared face ABI is unchanged.
