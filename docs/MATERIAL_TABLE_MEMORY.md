# Material tables in High WRAM

The next memory control separates compiled material-command prefixes and
native texture descriptors from the remaining actor state. Both are created
once, before the material binding/source plan, and retain their original
lifetime and descriptor identity. Their pointers are initialized before any
upload or material compilation. Existing residency/owner checks, source
publication, clipping and painter behavior remain in the shared SDK paths.

`T3_HIGH_MATERIAL_PROGRAMS` and `T3_HIGH_TEXTURE_DESCRIPTORS` now default ON.
OFF retains each independent Low-WRAM control. Older CMake caches preserve
their selected settings; reconfigure them explicitly when required. The
public procedural configuration already placed these tables in High WRAM.
The private original-texture build moves **23,168 + 17,376 = 40,544 bytes**.
A named `ACTOR_COUNT` sizes the actor-owned storage consistently.

The uninstrumented High build ends BSS at `0x060ebec8`, leaving 82,232 bytes
before the 16,384-byte linker stack reserve, or 65,848 after it. These are
static link-space bounds, not a stack watermark. Each array remains bounded
by the imported texture count; no allocation occurs during a frame.

## Native timing controls

All controls retain High-WRAM actor geometry, the 128-entry local pose cache,
the same original model/textures, camera, gameplay and complete-list VDP1
presentation. Separate same-source builds select Low, prefixes only,
descriptors only, or both High. At recorded second 26, selected frozen
tick-120 stage HUD samples read:

| Table selection | Flush, ms | Complete work, ms |
| --- | ---: | ---: |
| Both Low | 30 | 89 |
| Prefixes High | 27 | 86 |
| Descriptors High | 28 | 87 |
| Both High | 24 | 84 |

These individual instrumented samples establish a useful emission-memory
control, not a distribution or live FPS improvement. Skin and other stages
can vary by a millisecond with layout/cache effects; the isolated stage gain
must not be substituted for the complete path.

Independent **uninstrumented** actual Mednafen CD movies provide the live
neutral control. Manually read FRAME values from the original recordings:

| Recorded second | Both Low | Both High |
| --- | ---: | ---: |
| 18 | 40 | 42 |
| 38 | 173 | 185 |
| 58 | 306 | 328 |
| 78 | 439 | 471 |

Over these 60 recorded seconds, the counters advance **399 versus 429**:
6.65 versus 7.15 submissions/s, or **7.52% more submissions**. Each 20-second
interval has the same increment (133 versus 143).
This is a neutral emulator control, not physical hardware measurement,
combat timing, per-picture identity proof or a 30-displayed-FPS milestone.
The game remains substantially below its intended speed.

Cache-on Ymir does not reproduce a scheduling improvement: fields 1800–5400
still give 673 submissions, 2,692 ticks and 908 discarded fields over 3,600
VBlanks. Frozen instrumented Ymir complete-work medians are 51 ms Low and
50 ms High; flush rounds to 10 ms in both. Native controls remain necessary.
Mednafen's retained log reports its Data-only CPU cache-emulation mode;
these emulator configurations must not be represented as physical Saturn
cache/memory timing equivalence.

## Correctness and execution

The separate timing-HUD-OFF pixel gate matches both the Low control and the
preceding High-geometry golden over 97 frozen guest fields. Crop SHA-256 is
still `48cdd00660bfecf7c9164266d052121b2986bc9777a9d164f18f93b3aa2fb896`.
A cache-on verification build completes 117 paired face/key comparisons
against serial preparation, with zero divergence or serial fallback.
The public cross-build passes stock image, constructor and native-disc checks.
All 44 host CTests and 27 targeted Saturn/report tool tests pass.

All 24 live combat cases also pass, including two KOs, replay, authored
victory, terminal match/reset and both neutral timeout checkpoints. These
correctness runs explicitly disable Ymir cache emulation; cache-on
serial/Slave correctness and release timing remain separate controls.

No authoritative combat or simulation function changes. This is not a new
16,334-tick original-game parity qualification. Native CD recordings and the
live combat suite remain separate from frozen visual and timing controls.

The SDK Dino experiment independently confirms exact pixels and lower native
flush cost for its smaller tables, while establishing **no Dino cadence
increase**. Its reusable example builder, checked table placement and
profiling HUD are documented in the SDK's `docs/MATERIAL_TABLE_MEMORY.md`.
Shared renderer APIs and runtime validation are unchanged.
The SDK changes are pushed at `dcafe703025a88acb0681117743a58f198ffe552`.

## Private evidence

- `build/material-banks-{low,programs,textures,both}-frozen/`: independent
  configurations and retained build commands.
- `assets/imported/material-banks-*-frozen-mednafen/`: actual CD recordings
  and snapshots at seconds 18, 22 and 26.
- `assets/imported/material-banks-{low,both}-pixels/`: exact frozen image
  controls without the intrusive stage HUD.
- `assets/imported/material-banks-both-verify/`: serial/Slave comparison
  counters and panic dump.
- `assets/imported/material-banks-combat-suite/suite.json`: all 24 passing
  live control, combat, round and match checkpoints.
- `assets/imported/material-banks-{low,both}-live-mednafen/`: the original
  79-second native recordings and four visible-counter observations above.
- `assets/imported/material-banks-both-live-{1800,5400}/`: independent Ymir
  uninstrumented endpoints.

After the named-count cleanup, rebuilding the High live configuration retains
the tested ELF SHA-256 exactly:
`b2af125aefc8ab98cfbda6bb1b294af583c8adbec7d7c43784c2ea02faec37f3`.

The next structural targets remain command preparation/flush and simulation
work. Retaining tick debt is still a separate scheduling correction; moving
these tables does not make the 60-logic/30-picture workload fit.
