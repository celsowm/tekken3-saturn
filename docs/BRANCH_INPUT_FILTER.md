# Immutable current-input branch filter

The 60-logic-tick/30-new-image target remains unmet. The qualified filter removes
provably impossible command rows before the ordered DIVMOT walk. It is opt-in
outside the optimized original-texture VDP2-stage configuration with whole-actor
preparation, where `T3_BRANCH_INPUT_FILTER` now defaults ON. Explicit OFF remains
the control. No model, texture, combat arithmetic or actor ordering changes.

## Work removed and ownership

Each immutable bank has ten bitmaps: InputClear, plus each numpad direction
with no newly pressed buttons. A bit retains a possible command, never a
selected branch. Button edges and unsupported histories use the original
walker. CALL, RETURN and END remain in every bitmap. Matching candidates still
run the original frame-window, restriction, condition and history checks in
source order. Eligible `0x2C` rows keep their original scan-to-END behavior.
Skipped rows count toward the original traversal bound.

Sequence filtering checks only a necessary current-step condition using the
same first matching sequence definition. Earlier steps remain live. Banks bind
the exact row and sequence-table identities; reopening a bank or replacing its
common table detaches the affected index. Metadata, rows, sequences and bitmap
storage must stay immutable while attached.

The three original banks require 1,150 words, or 4,600 bytes, shared by both
actors. The port reserves 8 KiB in Low WRAM and fails closed if construction
does not fit. Release observations now require prepared storage and an actual
row skip, rather than accepting the selected CMake flag as execution evidence.

## Evidence so far

- All 48 host CTests pass. The dedicated differential test covers every
  16-bit command/current-class necessary-condition pair with all 16 held-button
  masks, temporal sequences,
  structural commands, live facts, word boundaries, table replacement and
  3,000 deterministic generated programs. Both successful records and closed
  failures are compared with the unfiltered walker.
- All 40 Saturn tool tests pass, including rejection of selected filters that
  never prepare storage or skip rows.
- All 24 live combat cases pass, including replay, authored victory, next
  round, second KO, terminal match, reset and both neutral timeouts. Every
  case records candidate ELF
  `4e1219e48a12cb3b00ee63fdcc7f3b438c55c11750c291c9b65c52ea0da6bce1`.
- Frozen tick 120 matches all 97 stable fields and every model/stage crop
  pixel. RGB SHA-256:
  `dbd1dcf895998266165aed2f9e3a3cfbe9626638b3a864f1f2a3678a800a2c2a`.
- The complete 12-field debugger sample reduces median branch-walk cost from
  approximately 31,963 to 4,684 interpreter units. Mean owned-step cost is
  146,967 versus the preceding sample's 211,276. These are modeled CPU costs,
  not hardware elapsed times or additive FPS estimates.

Three alternating actual Mednafen CD pairs, with neutral pads and movie seconds
30/50/70/90, give identical endpoints in every pair:

| Variant | FRAME endpoints | Increments over 60 seconds |
| --- | --- | ---: |
| Same-source filter OFF | 166 / 387 / 608 / 832 | 666 |
| Filter ON | 181 / 426 / 671 / 916 | 735 |

The median gain is 10.36% more submission increments, approximately
12.25/s versus 11.10/s. It includes round transitions and does not establish
30 displayed images/s. Pair orders are ON/OFF, OFF/ON and ON/OFF. Both variants
remain in round one at second 50; that separate 20-second interval gives
245 versus 221 increments (+10.86%).

All three original/host/Saturn reference cases pass every tick and field count:
5,481 + 5,632 + 5,221 = **16,334 ticks**, with zero divergence. Fresh independent
host/original audits of the same three captures also pass all 16,334 ticks.
The full 24-case combat campaign also passes, with actual filter use in every
case. The
reference executable initially exceeded the checked High-WRAM stack reserve;
the existing `T3_HIGH_TEXTURE_DESCRIPTORS=OFF` option moves its descriptors to
Low WRAM for correctness diagnostics only. The ordinary performance candidate
keeps High-WRAM descriptors. No stack reserve or rendered reference gate was
reduced.

The 5,400-field cache-on release run commits 5,281 ticks and 1,593 submissions,
with 5,284 observed fields, three discarded fields, no guest error and no draw
timeout. In the warmed 3,600-field crop, RGB changes 1,068 times (17.8/s), with
a longest unchanged interval of 54 fields. Those RGB transitions are not unique
pose epochs or a hardware/displayed-image rate.

The ordinary candidate's initialized image is 875,800 bytes. High BSS ends at
`0x060fa748`, preserving the checked 16 KiB Master stack reserve; Low WRAM ends
at `0x00283908`. The installed SDK archive is unchanged.

## Native remaining budget

The existing stage-HUD diagnostic was rebuilt from the candidate and recorded
from actual Mednafen CD boot. At movie seconds 30/50, its preceding completed
samples show 31/29 ms simulation for four ticks, 4 ms Master skin work,
2/1 ms placement, 18/19 ms geometry including the join, 13 ms painter flush,
7 ms presentation and 82/81 ms complete work. These are two instrumented
samples, not stage distributions or release throughput. At second 70 the
round transition increases floor/presentation work, so that sample is kept
separate. Artifacts: `paul-branch-input-filter-native-budget`.

Rendering and simulation both need less work. Removing a scheduling wait alone
cannot establish the target. The next coupled experiment should:

- Retain this candidate's input filtering and exact original core.
- Evaluate a bounded derived cache for world-joint composition after dynamic
  blend/tracking. Full local/root-rotation inputs and immutable part-table
  identity must match; root translation can be factored only with an exact
  proof and the existing coordinate bounds. Contacts, tracking and actor order
  stay live. This is distinct from the retained raw local FK/IK cache.
- Reevaluate exact C32 deformation with whole-actor preparation, including
  placement once. Its old standalone result is already rejected; only the
  changed composition and complete critical path justify another measurement.
- After removing work, remeasure the archived serial-simulation/painter overlap
  on the combined build, with the original stack and image-age qualifications.
  Its old 8.83% result is not an untried optimization or an additive prediction.

The existing SCU DSP API was also inspected. Its built-in transform accepts
only 21 vectors per start and requires High-WRAM DMA buffers; it does not
perform the original blended skin/rounding, visibility or complete image
preparation. A direct batch substitution would leave the other actor's SH-2
preparation on the critical path. A future streaming DSP pipeline needs its
own complete schedule and exact arithmetic proof. No DSP runtime saving has
been measured for this game, and no new general harness is needed first.

Private evidence uses `assets/imported/paul-branch-input-filter-*` and matching
`build/` directories. Preserve their ELF identities when testing later source.
Source snapshots, immutable binaries and paired-result metadata are retained
in `assets/imported/branch-input-filter-source`.

Promotion is verified in `build/paul-branch-input-filter-promoted`: clearing its
cached filter option selects ON and reproduces both the exact qualified ELF
above and application SHA-256
`b98412b5c8d12a5318e0055fc27ab98cd4a86432a3d261331f37c156f4de59bf`.
The fresh public build selects OFF, compiles and executes 300 cache-on Ymir
fields without rendering error or draw timeout. All 40 Saturn tool tests and
eight reference-tool tests pass. No SDK source was changed for this increment.

## Remaining work

1. Keep the qualified default and explicit OFF control. Continuous reference,
   all 24 live cases, stock-memory bounds, frozen pixels and three matched CD
   pairs pass. Preserve the qualified binary when changing the next boundary.
2. Execute the coupled useful-work experiment above. Simulation still spends
   substantial work composing world joints; rendering still dominates each
   actor's CPU preparation. An isolated gain is not the final
   60-tick/30-new-image acceptance test.
