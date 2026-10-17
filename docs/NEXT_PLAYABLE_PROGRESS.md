# Paul playable milestone: execution evidence

The objective remains the complete [playable milestone](NEXT_PLAYABLE_MILESTONE.md).
It is **not complete**. `build/paul` remains the provisional animated preview;
`build/paul-owned` owns original movement/body/contact queries without live damage.
The newer `build/paul-combat` activates bounded original high/mid/low damage,
guard, reactions and USA VS KO collapse, with the temporary round policy below.
Free-running matched PS1 replay, pose details and performance remain open.
A private, visibly labeled input replay now demonstrates the complete bounded
encounter and next-round restoration from an actual Mednafen CD boot.

## Original VS scoring and round observation (2026-10-17)

- The owned normal-VS scorer now implements the pinned `RoundWins` and
  `MatchResult` rules: normalized health comparison, configured draw points,
  draw-dependent round cap, human/tie selection and terminal match result.
  Nonterminal calls preserve the original match-result bytes. The live path
  uses the observed two-human defaults (first to two, both receive draw points,
  tie choice P1) and a 2,400-tick timer. A drawn match displays `MATCH DRAW`;
  the pure scorer retains the original internal winner/loser selection too.
- The read-only observer records round globals and saves compact pre/post RAM
  at `RoundFlow`, `RoundWins` and `WinPoseStart`. An optional stop count pauses
  only at the enclosing `FightFrame` return. Neutral pad mode still navigates
  the original menus, then releases both pads throughout the encounter.
- The USA executable proves that `MatchResult` at `0x8003E800` writes the
  result/winner/loser bytes at mapped context `0x800AFA88 + 0x21/0x22/0x23`.
  Two isolated winner/loser points in the pinned layout disagree with those
  struct offsets. The preparer records this verified local override without
  changing the upstream submodule. It also uses the instruction widths for
  human mask, first-to threshold and tie choice. Regression coverage checks
  that compact ranges are mapped exactly once and contain every descriptor.
- Older private `ps1-paul-round-flow/` and `ps1-paul-draw-rounds/` captures
  have invalid round compact-range mapping; the latter also labels unrelated
  bytes as match winner/loser. They are retained as historical observations,
  excluded from the score audit, and must not support semantic parity claims.
  Their paused full RAM is separate evidence; it does not repair the old
  descriptors or compact ranges retroactively.
- Corrected `ps1-paul-draw-verified/` contains 106,199 observations, ending at
  source frame 4806. Independently fetched full paused RAM agrees with every
  final fighter/round JSON field. Original RoundWins calls at source frames
  2550 and 4806 award 1–1, then 2–2; the round cap changes 3→2→1 and the final
  match result is 3, with internal winner 0 / loser 1. The new
  `audit_psx_round_score.py` checks both call edges against compact RAM and
  invokes `t3_round_score_eval`, linked to the same scorer as the game. Both
  original draw awards match all checked outputs. Reports hash the trace,
  adapter and paired RAM snapshots; all retail evidence remains private.
- A second corrected capture, `ps1-paul-score-verified/`, ends at source frame
  5008 with 104,490 observations. Its independently fetched paused RAM agrees
  too. A timeout at source 2704 compares normalized health 4096 versus 1990
  and awards P1 one point; a KO at source 5008 compares 4096 versus 0 and ends
  the match 2–0. Both complete native pre/post scoring comparisons pass.
- Public validation passes 38/38 CTest checks and 91 Python checks with native
  adapters assigned. ASan/UBSan also passes the scorer's draw/tie/cap/atomic
  failure tests. These synthetic checks supplement the four original calls;
  they do not expand original observed coverage to every configurable policy.
- The current Saturn image has 961,780 initialized bytes; High WRAM BSS ends
  at `0x060F6FA8`, Low WRAM at `0x002ACDFC`, and the 16 KiB stack-reserve link
  assertion passes. Its ELF SHA256 is
  `b91558de9dfe82c5b63bc492ffc9400073c939f2b1ff27ab3c0bd7f68365993d`.
  `paul-vs-round-score-final-mednafen/` records an actual CD boot, exit 0,
  1,428 video frames over 23.8685 seconds. Inspected second 22 shows both
  animated Pauls and readable HUD. This is a neutral boot check; the earlier
  complete encounter recording remains a separate build's evidence.
- All 20 final Ymir combat cases pass with that same ELF in
  `paul-vs-round-score-final-suite/`. The existing 18 attack/guard/reaction,
  two-pad, KO, restart and reset cases remain passing. Two additional neutral
  pad cases run without RAM edits: at 6,000 emulator frames the match is in
  round 2 with 1–1 points and full HP; at 12,000 frames it has ended at 2–2,
  timer 0 and display winner −1. The inspected final screenshot shows
  `MATCH DRAW`. These frame checkpoints certify bounded runtime behavior,
  not original round-transition timing or hardware performance.
- Scoring agreement does **not** establish original round-flow parity. The
  live wrapper still omits intros, replay, victory poses and original timing
  of next-round restoration. The full continuous replay and the 30 Hz render /
  60 Hz logic performance gate remain open.

## Dense scene preparation and controlled render comparison (2026-10-16)

- A separate profiling SDK enables LibSaturn's existing FRT stage counters.
  The baseline reports 3,152 preparation ticks and 1,017 projection ticks for
  the last actor, plus 470 painter and 2,088 emission ticks for the scene flush.
  At NTSC FRT/128 these are approximately 14.1, 4.5, 2.1 and 9.3 ms. These are
  last-completed-stage observations, not an atomic breakdown of one frame.
  `render_work_ms` includes skinning and HUD; `skin_work_ms` is a subset and
  must not be added to it as an independent cost.
- The new opt-in `T3_RENDER_BENCH_TICK=120` advances the real owned state to
  tick 120, then freezes simulation while continuing rendering. It visibly
  labels the image `RENDER BENCHMARK`; default 0 keeps live gameplay. This
  isolates pose-dependent rendering cost without crediting frozen simulation
  toward the playable milestone. Every variant submits 793 commands and
  reaches the same authored pose. Decoded screenshot pixels are identical.

  | Fixed-pose variant | Render/HUD work sample |
  | --- | --- |
  | Previous face module at `-O2` | 73 ms |
  | Face module at `-O3` | 68 ms |
  | `-O3` plus conservative projected bounds | 66 ms |

- LibSaturn `cdc0d53` computes a projected-bounds proof once for dense meshes.
  Faces reuse the proof only if all vertices satisfy the original depth and
  coordinate limits; otherwise the original per-face checks remain. RGB and
  indexed limits remain separate, unused vertices cannot cull valid faces, and
  world-normal culling, sorting and clipping fallbacks are preserved. Both
  build systems apply `-O3` only to this measured module, with Make's variant
  key updated. No additional caller buffer or public ABI change is required.
- Forty-eight host boundary cases compare dense instances to individual quad
  submissions. The face API suite passes at `-O3` and with ASan/UBSan; the
  skinned-scene test was rebuilt and passes too. Available library host/tool
  checks, three header/manifest gates, installed SDK and a Make example build
  pass. Compiler stack usage reports 312 bytes for instance submission and
  240 for world visibility, excluding callees; this is not a measured total
  stack high-water mark.
- The normal `build/paul-memory` uses benchmark 0. Its 600-frame observation
  reports 65 ms render/HUD (including 11 ms skin), 124 ms last work sample,
  8.4 reported work loops/s, 316 logic ticks and 780/2048 commands, with zero
  rejected faces, draw timeouts or runtime errors. Comparing its 600/1200-frame
  observations across exactly 600 VBlanks gives **8.1 submissions/s and 32.4
  logic steps/s** at nominal 60 Hz. Submissions are not distinct presented
  images or hardware FPS. The 30 Hz render / 60 Hz logic gate still fails.
- All 18 combat/round cases, all 23 procedural input cases and 37 CTest checks
  pass. Final ELF SHA256 is
  `f7fd1843fa9900ec832433387ddfc0e3dbe894679c33a27830115ddc28ab1a0c`.
  Initialized image grows by 8,336 bytes to 960,912; High WRAM BSS ends at
  `0x060F6C38`, Low WRAM at `0x002ACDF8`, preserving the 16 KiB stack reserve.
  The same image boots from CD in Mednafen, exit 0, recording 1,429 frames over
  23.8853 seconds. Inspected second 22 shows both animated Pauls and readable
  HUD. This remains a neutral boot check, not a new full-encounter video.
- Private evidence: `paul-scene-split-profile/`, `paul-scene-split-metrics/`,
  `paul-render-bench-o2/`, `paul-render-bench-o3/`, `paul-render-bench-bounds/`,
  `paul-scene-fast-profile/`, `paul-scene-fast-long-profile/`,
  `paul-scene-fast-full-suite/`, `paul-scene-fast-baseline-input/` and
  `paul-scene-fast-mednafen/`. Physics/replay semantics are unchanged; the
  original victory/next-round mismatch remains open.

## InputClear and aligned memory operations (2026-10-16)

- The owned core now consumes `T3_PSX_INPUT_CLEAR` after physics. This source
  control clears all 60 input-history entries and pressed/held state. It keeps
  original raw `in_dir=5` distinct from normal neutral's `1 << 9`, so a neutral
  return branch cannot fire on the clear tick. A subsequent pad update restores
  normal command matching. The provisional round-result hold also uses clear,
  discarding prior button/motion history instead of appending neutral samples.
- `paul-clear-direction-replay/frame-replay-audit.json` executes **all 2,690
  captured ticks**, including 205 clear-input frames per actor, from one seed.
  Every checked category matches for the first **2,487 ticks**. First divergence
  is tick 2,488/source 2795: the original starts P1 victory pose 3432 and holds
  P2's pose clock. At tick 2,605 it restores next-round HP. Those original
  RoundFlow transitions are absent from the bounded native policy. Full replay
  therefore remains **failed**, with all later differences retained; the new
  input support does not redefine the capture boundary to claim a pass.
- A matched-ELF 600-frame PC sample attributes 56 samples (9.3%) to the previous
  byte-at-a-time `memcpy`. This is sampled-PC attribution, not exclusive cycle
  accounting. LibSaturn `749424f` uses aligned 32-bit copy/fill blocks, with
  byte handling for incompatible alignment, leading bytes and tails. Host
  comparisons cover 65,600 copy and 32,800 fill cases against libc, with guards
  around the written span. ASan/UBSan, available native/tool tests and the SH-2
  installed package pass. The rebuilt jukebox also passes all four SCSP tests
  over 1,100 seek/pause frames.
- The new private `build/paul-memory` preserves the authored data and owned
  match configuration. Neutral profiling improves from a 128 ms work sample/
  7.0 loops/s to 120 ms/7.4 loops/s, with 71 ms scene/HUD, 11 ms skin and 288
  logic ticks per 600 emulator frames. Command use is 766/2048, with no rejected
  faces, draw timeouts or runtime errors. A second run reproduces the complete
  diagnostic state. Pose-dependent samples are not isolated kernel benchmarks;
  **30 Hz rendering / 60 Hz logic still fails**.
- Both the clear-only image and the final memory-optimized image pass all 18
  combat/round cases. The procedural build passes all 23 input scenarios after
  the shared library change. All 37 CTest and 86 Python tests pass, including
  clear-history, branch discrimination, phase order and adapter rejection cases.
  Public CI includes the new synthetic adapter checks.
- Final ELF SHA256:
  `79648cd304b418aecc4bf74da873dde3e1cf7f1664ef33a1b65700755ec8f9a7`;
  initialized image 952,576 bytes, High WRAM BSS end `0x060F4BA8`, Low WRAM end
  `0x002ACDF8`, with the unchanged 16 KiB stack reserve. Its actual Mednafen CD
  boot exits 0 and records 1,428 frames/23.8685 seconds; inspected second 22
  shows both animated Paul instances and readable HUD. It is a neutral boot
  check, not a new full-encounter video.
- Private evidence: `paul-clear-full-suite/`, `paul-memory-full-suite/`,
  `paul-memory-baseline-input/`, `paul-memory-profile/`,
  `paul-memory-profile-repeat/`, `paul-memory-mednafen/` and
  `libsaturn-memory-check/`. Profiling now retains the matching ELF/map beside
  samples, preventing later builds from invalidating symbol attribution.

## Procedural head tracking (2026-10-16)

- The normal Paul match now applies original head look-at after retaining the
  blended local matrices and before world composition. It uses previous world
  joints, move/active-window gating, original atan/sqrt/trig arithmetic, tilt
  limits, target hold, bounded angular steps, snap and animated-head angle
  decomposition on activation. The original Euler builder is shared with FK.
  Non-fight cameras, Gon eyes and original intro flow remain outside this slice.
  At native reset the first authored composition initializes the previous world
  joints; look-at starts on the following tick with snap enabled.
- The private evaluator additionally seeds initial world joints, no-look-at
  state and the 20-byte head record from the full entry RAM. Verified USA layout
  maps JP `0x8009E920` to `0x8009E578` and camera phase to `0x8009546C` (must be 2).
  Expected exits still compare only, never reseed. The final report at
  `paul-head-final-replay/frame-replay-audit.json` has **zero differences in any
  checked category for all 2,396 executed ticks**, including every world-joint
  rotation. No tolerance or head-specific exclusion was added. This checks the
  head's matrix results, not its uncaptured intermediate global scalar state.
  Overall replay remains failed because the original clear-input mode at
  source frame 2704 prevents the remaining captured ticks from executing.
- LibSaturn `c5861ba` moves the 33,184-byte audio-stream registry, dominated by
  staging buffers, into existing Low WRAM. Reset/CRT clearing, Sound RAM
  locations and refill algorithms are unchanged. Available native tests, tools,
  header/manifest gates and installed SH-2 package build pass. The rebuilt
  jukebox passes all four Ymir audio assertions both at 300 frames and at 1,100
  with seeks/pause: active-half protection, slot format and stereo phase.
  These are emulator invariants, not hardware timing measurements.
- The private Saturn initialized image is 951,984 bytes. High WRAM BSS ends
  at `0x060F4958` and Low WRAM at `0x002ACDF4`, preserving the 16 KiB stack
  reservation. The image's ELF SHA256 is
  `55f1dac0afc7dedcc7e0bab0e4945f86281ab5a67a32124979f9553f7515680f`.
  All 37 CTest checks and 83 Python tests pass. The new head lifecycle test also
  passes ASan/UBSan. The procedural baseline passes all 23 input scenarios.
- All 18 combat/round scenarios pass on this final ELF, including controlled
  guards/hits, P2, simultaneous contact, KO, next round, 2–0 match completion and
  Start reset (`paul-head-final-suite/suite.json`). The earlier preliminary head
  suite is retained separately; these final results all have the hash above.
  The same final image boots from CD in Mednafen with exit 0: 1,428 frames over
  23.8685 seconds, inspected at second 22 (`paul-head-final-mednafen/`). Both
  animated Paul instances and readable HUD are visible. This recording is a
  neutral boot check, not a new full-encounter recording.
- `paul-head-final-profile/observation.json` reports 127 ms work, 72 ms scene/HUD,
  12 ms skin, 774/2048 commands, 7.0 work loops/s and 280 logic ticks per 600
  emulator frames. Rejected faces, draw timeouts and runtime errors are zero.
  The 30 Hz render / 60 Hz logic gate still fails. Head correctness and the
  larger High WRAM margin do not establish usable final performance.

## Per-actor skeleton alignment (2026-10-16)

- The owned match now requires a parts table for each actor. Private generation
  accepts `T3_LOCAL_ACTOR0_KMD` for P1's original skeleton, records its source
  SHA, checks the common scale and preserves the loader's original Z fixup.
  The current `build/paul-blend` uses record 75 for P1's skeleton and record 71
  for P2's. Both rendered meshes/textures still use record 71; the override is
  explicitly skeletal and does not claim a second-costume mesh import.
- `paul-peractor-replay/frame-replay-audit.json` advances the unchanged owned
  match from the same single original seed for all 2,396 requested ticks.
  **Every checked ground, joint-origin, body-sphere/push, move/clock/queue,
  hit/HP and blend/local-matrix field matches throughout.** This resolves the
  first separation discrepancy and the later accumulated root/angle drift.
  The only remaining differences in this interval are world rotations of
  head slot 2 (37,337 matrix-element differences). Overall replay remains
  failed; procedural head tracking and the original clear/scripted input
  boundary at source frame 2704 still need implementation. No snapshot reseeds
  the simulation and no comparison tolerance was added.
- The diagnostic record now lives in Low WRAM and receives its complete
  zero/default initialization in `main` before the first publish. This permits
  the additional private skeleton within the existing linker limits. The
  initialized image is 949,040 bytes; High WRAM BSS ends at 0x060FBF78, leaving
  the reserved 16 KiB stack plus 136 bytes. Low WRAM ends at 0x002A4C2C.
  The fit is still tight; no larger memory map or reduced stack reserve is used.
- All 36 CTest and 83 Python tests pass, including actual LibSaturn exporter
  integration and three new synthetic skeleton import checks. All 18 original
  combat/round Ymir cases pass with ELF SHA256
  `5af01b16599dfb2803712949e7e23bf356e78a49c143ffbe73898c75db489f1d`:
  guards, hits, held/repressed input, two pads, simultaneous hits, KO, next round,
  first-to-two completion and Start reset, without decode/runtime/command errors.
  The procedural baseline also passes all 23 emulator input scenarios after the
  shared diagnostic storage change (`paul-peractor-baseline-input/suite.json`).
- The same ELF/app/disc is recorded through actual Mednafen CD boot in
  `paul-peractor-mednafen/`: 1,427 video frames, 23.8518 seconds, exit 0. Inspected
  second 22 shows both recognizable animated Paul instances and readable HUD.
  This is a neutral boot recording; the earlier full encounter CD recording
  remains historical evidence for its own build.
- `paul-peractor-profile/observation.json` still measures 126 ms work, 73 ms
  scene/HUD, 12 ms skin, 781/2048 commands, 7.0 work loops/s and 280 logic ticks
  in 600 emulator frames, with zero rejected faces, draw timeouts or runtime
  errors. The 30 Hz render/60 Hz logic gate remains failed.

## Body replay diagnosis and library publication (2026-10-16)

- The continuous evaluator now compares all eight body-sphere XYZ/radius fields
  and three push components for each actor, in addition to joints and placement.
  Reports include this category only when the evaluator declares it checked.
  The new private `paul-body-replay/frame-replay-audit.json` still executes all
  2,396 ticks from one seed. Move, hit and blend coverage remains matched; overall
  replay still fails. P1 sphere 4/6 positions differ from the first tick, whereas
  the first push-X discrepancy occurs at tick 136 with the placement discrepancy.
- At tick 136/source 663, the deepest overlapping pair is sphere 4 against
  sphere 4. Original centers differ by `(90,67,-193)` and native centers by
  `(90,67,-194)`. Using the verified original SquareRoot0 table gives penetration
  18 versus 16, respectively; native push-X is -9 versus original -10.
  Re-extracting record 75 from the fingerprinted disc confirms that among all
  22 skeleton parts, only part 6/slot 15 differs from record 71: file offset Z
  -99 versus -100 (runtime +99 versus +100). Its full SHA matches the earlier
  live-model identity audit. This localizes the first separation discrepancy
  to unequal model geometry; no tolerance or hardcoded coordinate correction
  has been added. Per-actor model alignment and a new replay remain required.
- LibSaturn commits `342afd8` and `d377d28` publish the HUD pixel advance fix,
  native Mach-O/standard-header portability, synchronous animation test linking,
  explicit unavailable-test reporting, and deterministic GCC/Clang bullet RNG
  order. Available native tests and Python tool tests pass; the fixed-address
  parallel ABI test is explicitly unavailable on native macOS, not a pass.
  Header/manifest CTest passes 3/3 and the SH-2 installed package builds.
  Rebuilding the port retains the exact previously exercised ELF SHA256
  `8d08baf600412efba1ee0b1926e56d76028056b2d02dcca4c6945945a03f8b42`.
- GitHub validates the library's host tests (including the parallel ABI test on
  Linux), package, skeletal, physics, transform and scene gates. Its broader
  Phase 3 cross-example gate remains failed: the bullet-hell example references
  absent `assets/palette_bgr555_be.bin`. This is a separate unresolved example
  asset dependency; the library publication is not claimed wholly green.
  The port's published `32cb4fb` host gate and preceding firmware/disc gate pass.

## Live transition blending and spline endpoint correction (2026-10-16)

- `build/paul-blend` integrates the original normal-match DECAY, BRANCH and
  STANCE blends into the authoritative match transaction: arbitrary-row root
  and FK/IK sampling, retained displayed local matrices, signed 16-bit deltas,
  original counters/weights, truncated root-Y adjustment and orthonormalized
  slots 1–17. World placement still uses the root stream; FK/IK correctly keeps
  the pose stream's root channels when those streams differ. Global replay
  playback/hold is outside this bounded live path.
- The reciprocal-sqrt coefficient window is extracted from the fingerprinted
  USA EXE with mapped-address and static-reference checks. Its 768 bytes include
  preceding data addressed by wrapped-negative squared vector lengths. No
  generated coefficients or retail fixtures enter public tests or Git.
  Native normalization/scaled-matrix arithmetic agrees with pinned research in
  10,008 synthetic cases, including saturation and signed overflow; the same
  comparison passes with private coefficients and ASan/UBSan.
- Continuous replay exposed a separate motion-decoder error: the final spline
  segment's coefficient sum was narrowed to 16 bits. At the endpoint its value
  can be 0x10000, so narrowing erased the last authored increment (walk's first
  displayed rootY was -986 instead of -1034). The sum now stays 32 bits; a public
  synthetic endpoint regression reproduces the failure without game data.
- The same one-seed, 2,396-tick PS1 comparison now matches **all checked blend
  scalar fields, previous local matrices and delta matrices for both actors**,
  along with all previously matched move/clock/queue and hit/HP fields. This
  also validates sampling endpoints across repeated transitions. No expected
  snapshot reseeds the running state. Ground matching improves from 107 to 135
  consecutive ticks; the first remaining position discrepancy is one unit at
  tick 136/source 663. Head rotation still differs at tick 1, and P1 still uses
  reference costume 75 while the native configuration shares 71. Those world
  geometry differences and the clear/scripted input boundary remain open;
  **overall replay is still failed**, not relabeled as a blend-only success.
- The interactive image is 949,156 initialized bytes; both WRAM link bounds and
  the 16 KiB Master stack reserve pass. The new blend routines use size
  optimization only within that component. Font storage and match config use
  Low WRAM, with complete initialization before first read. High WRAM BSS ends
  at 0x060FBFF8 (only 8 bytes beyond the reserved 16 KiB stack); Low WRAM ends at
  0x002A49DC. Compiler stack-usage reports 7,084 bytes for match-step, 704 for blend
  preparation and 764 for its sampling callback, excluding deeper callees.
  This is a tight residency fit, not proof of a measured maximum stack depth.
- All 36 CTest checks and 80 Python tests pass, including three blend checks and
  the spline regression. All 18 interactive Ymir combat/round cases pass with
  this exact ELF: guard/hit, KO, full-HP next round, 2–0 match end and Start reset.
  The procedural baseline also passes its 23 emulator input scenarios after
  the shared HUD storage change.
  Neutral profiling observes no decode/command/face overflow or draw timeout:
  126 ms work, 73 ms scene/HUD, 12 ms skin, 781/2048 commands, 280 logic ticks per 600
  emulator frames and 7.0 work loops/s. **30 Hz rendering/60 Hz logic remains unmet.**
- The same interactive CUE boots in Mednafen and records 1,426 video frames/
  23.835 seconds with clean exit. Inspected second 22 shows two recognizable
  animated Paul actors, health bars and readable controls. This recording is
  neutral CD-boot validation; it does not replace the earlier full encounter
  evidence or claim a new-blend CD encounter with attacks/KO.
- Private evidence: `paul-blend-replay/frame-replay-audit.json`,
  `paul-blend-tables/manifest.json`, `paul-blend-full-suite/suite.json`,
  `paul-blend-baseline-input/suite.json`,
  `paul-blend-profile/observation.json` and `paul-blend-mednafen/` under
  `assets/imported/`. Previous checkpoint evidence is retained separately.

## Continuous PS1 replay and joint-order correction (2026-10-16)

- A new read-only original capture retains one full neutral FightFrame entry
  seed and 2,690 consecutive exit snapshots. The paused final RAM independently
  agrees with the phase observer. The native evaluator copies state **once**;
  subsequent snapshots compare results only. It advances the same owned match
  core used by Saturn with each observed InputSource result. Missing frame
  snapshots, wrong seed/entry data and broken native tick continuity fail closed.
- This exposed a real integration bug: KMD mesh order was being passed to
  collision routines that index the original joint slots. For example KMD part
  2 is slot 11 (hips), while part 17 is slot 2 (head). Component audits seeded
  with original joint arrays could not reveal this caller-side mistake.
- `t3_psx_parts_to_slots` now remaps composed poses before body/hurt/attack
  queries. The renderer maps back through the model's slot table when copying
  skin matrices, preserving mesh ownership and coordinates. The optional old
  preview/shadow observer also converts explicitly at that boundary. Missing
  or duplicate mappings fail atomically; in-place permutation is tested with
  ASan/UBSan. The original contact table's zone zero is a foot, despite the
  legacy diagnostics field name `psx_hurt_head_local_y`.
- With the correction, all checked movement/clock/queued-branch fields and hit
  fields (including HP, guard/clean flags and per-opponent hitDone) match for
  **2,396 continuously advanced ticks**, covering high/mid/low moves, standing
  and crouching guard reactions, and eight actual HP changes. The earlier
  incorrect ordering diverged in HP at tick 157. This is stronger than the
  earlier independently seeded transactions, but **overall replay still fails**.
- The replay stops explicitly before original InputSource's unrepresented
  clear/scripted direction at source frame 2704. The remaining 294 captured
  frames are not silently skipped or labeled as verified. Ground fields match
  only the initial 107 ticks: root Y differs at tick 108 when the fighter starts
  walking, and later roots/angles/separation also diverge. Original transition
  blending remains unimplemented.
- Head rotation differs at tick 1 because procedural head tracking is missing.
  There is also a one-unit initial P1 leg-origin discrepancy. Its data cause is
  identified: the original reference loads KMD record 75 for P1 (left hip Z 99)
  and record 71 for P2 (Z 100); our current rendering configuration shares record
  71 for both. All 243 non-pointer KMD row fields per actor agree with their
  respective loaded original headers. Model/costume correspondence must be
  aligned before treating that coordinate as a solver arithmetic error.
- All 18 interactive combat/round cases pass with the corrected order, including
  next-round restoration, a 2–0 match end and Start reset. The round-case control
  generator now follows the observed side instead of an old fixed direction
  sequence. Its full pad prefix is rerun from reset at every observation; HP,
  collapse, score and reset assertions remain strict. A new
  pad-only controlled encounter also passes two guards and a mid hit followed
  by lows through KO: 12 hits, two blocks and P1's point at tick 1340. Its private
  replay matches all 67 observed fields and the input trace at the same tick.
- The corrected demonstration CUE boots in Mednafen 1.32.1 and was recorded for
  6,274 frames / 104.87 seconds, exiting cleanly. Inspected frame 80 shows P2 HP
  zero, collapse and P1's point; frame 90 shows round 2 with both HP restored and
  score 1–0. ELF/app hashes match its validated Ymir replay. It is visibly labeled
  RECORDED INPUT DEMO. Earlier recordings remain historical checkpoints, not
  validation of the corrected collision integration.
- All 33 CTest checks and 79 Python checks pass. Owned and preview/shadow SH-2
  syntax checks also pass. The interactive image is 945,412 initialized bytes,
  with 67,944 High WRAM BSS bytes and 671,632 Low WRAM bytes; native/CD checks and
  the 16 KiB stack reserve still pass. No command/face overflow, decode error or
  draw timeout is observed in the bounded cases. The profile measures 126 ms
  work / 73 ms scene-HUD / 12 ms skin, 292 logic ticks in 600 emulator frames
  and 7.5 work loops/s. **The performance gate remains unmet.**
- Evidence: `ps1-paul-frame-replay/frame-replay-audit.json`, its before/after
  summaries and `model-identity-audit.json`; `paul-slot-feedback-full-suite/`,
  `paul-slot-encounter/`, `paul-slot-replay-aligned/`, `paul-slot-profile/` and
  `paul-slot-mednafen/encounter.mp4`. Seeds, source RAM, assets and movies stay
  private. The optional native evaluator is configured only through explicit
  `T3_PRIVATE_REPLAY_ASSET_DIR`; public CI does not compile retail fixtures.

Next align the reference model data, implement and audit transition/root blending
and head tracking, represent original clear/scripted input modes, then repeat the
continuous host/Saturn/PS1 comparison. Preserve the verified bounded CD encounter
while addressing the separate 30 Hz render / 60 Hz logic shortfall.

## Tick-indexed encounter and measured optimization checkpoint (2026-10-16)

- The fixed-point pose helpers now use arithmetic right shift on compilers that
  define signed shift that way, with a portable division fallback. This removes
  redundant signed-floor corrections while preserving the audited PS1 rounding.
- Owned rendering applies its exact diagonal scale-six/arena-center transform
  directly to the freshly skinned vertices. It bypasses the generic 4x4 transform
  per vertex without changing skinning order or rounding. A 1,001-vertex test
  compares the former Q16 result, including negative components and atomic
  rejection of unsafe buffers; it passes ASan/UBSan.
- The final interactive profile measures 123 ms work, 72 ms scene/HUD and 11 ms
  skin, versus the previous 128/76/12 ms. It still advances 292 ticks in 600
  emulator frames at 7.5 work loops/s: **the performance gate still fails**.
  Current initialized image is 945,012 bytes, High WRAM BSS 67,944 bytes and Low
  WRAM 671,632 bytes. No rejected face, command overflow or draw timeout occurs.
- An `-Os` experiment reduced image size but worsened the observed rate to
  5.5 work loops/s and 178 ms work. It was rejected. A generic small-face culling
  experiment also failed to improve measured work and was reverted; the sibling
  library retains only the earlier HUD fix. No new library optimization is claimed.
- All 18 final interactive combat/round cases pass. The KO case now locates the
  score checkpoint when a cadence change makes the fixed emulator-frame sample
  occur after the next round; this remains a pad-only run from reset, without
  guest-state writes. All 33 CTest checks and 79 Python checks pass.
- New opt-in input recording/replay captures the exact relative input pairs at
  the controller-to-simulation boundary, indexed by logic tick. Only explicitly
  configured private builds include it. The 512-record Low WRAM buffer reports
  overflow; the generator rejects malformed/unordered input fixtures. Replay
  changes inputs only and is visibly labeled RECORDED INPUT DEMO.
- A controlled encounter records standing high guard, crouching low guard, a mid
  hit and lows through KO. Both guards preserve native HP. At tick 1812 there are
  12 hits, two blocks, P2 HP zero and one point for P1. The 95-event replay reaches
  that same tick from reset with all 67 checked game/pose fields and its input
  stream matching exactly. This is **free-running Saturn-to-Saturn** evidence at
  an aligned checkpoint, not an audit of every intermediate tick or PS1 parity.
- Input collection, replay configuration and actual-CD recording commands are
  documented in [SATURN_INPUT_REPLAY.md](SATURN_INPUT_REPLAY.md). Private evidence
  includes `paul-encounter-inputs/`, `paul-encounter-replay-aligned/`,
  `paul-perf-final-combat-suite/`, `paul-perf-current-profile/` and the shorter preliminary
  `paul-demo-mednafen/` recording. The preliminary 78-second recording stops before
  KO and cannot satisfy the complete CD encounter gate.

- The final labeled demonstration CUE was recorded in Mednafen 1.32.1 for
  7,471 frames / 124.878 seconds, with clean exit. Its ELF/app hashes match the
  validated Ymir replay exactly. Inspected frames show P2 HP zero, P1's round
  point and collapse at 105 seconds; at 115 seconds round 2 has full HP and
  score 1–0. The recording includes the preceding movement, high/low guards,
  mid hit and subsequent lows. This closes the actual-CD **bounded encounter**
  demonstration, using recorded inputs rather than live manual input. It does
  not claim original PS1 round-flow or replay parity, or hardware performance.
- The complete CD evidence is `paul-encounter-mednafen/`: command/build hashes,
  MOV, ffprobe metadata, inspected PNG checkpoints and a compressed MP4. The
  normal interactive build has input recording/replay disabled. Further work
  must preserve that separation and rerun the appropriate final-build checks.

## Live combat and bounded round checkpoint (2026-10-16)

- `psx_hit_transaction.c` applies the original pair of hit transactions before
  committing either actor. It uses sparse original reaction rows, native HP,
  original classification/guard and per-move hitDone continuity. Only attack
  slots 295 (high), 357 (mid) and 355 (low) are enabled. Unsupported attacking
  branches increment diagnostics and are rejected without prototype damage.
- Queued reactions own the same VM used by movement and rendering. USA VS
  FighterCollapse starts slot 3430 immediately in the lethal tick. The regional
  collapse flag is independently observed in the USA game's RAM. Air reactions,
  reversals, throw pairing and the alternate regional KO path remain unsupported.
- A new original capture contains 22 HitApply RAM pairs, including the lethal
  mid at source frame 5007/call 27312. The actual owned transaction matches all
  22 through MoveStepAll: HP, damage/guard/counter flags, current or pending slot,
  pose/root/move clocks, entry, branch kind and transition. An older capture's
  eight pairs also match with its missing trace fields explicitly reported.
  These are independently seeded transactions, **not full input replay parity**.
- The arithmetic-only audit reports 22 matching cases and explicitly excludes
  the queued normal reaction at KO: MoveStepAll replaces it with immediate
  collapse. The owned transaction audit covers that immediate collapse directly.
- Private reactions are three indexed original records (132 bytes), rather than
  embedding the full 26,586-byte table. The extractor fingerprints the full
  original table; the asset builder validates and selects the exact used rows.
- A MoveVM stream-identity bug was corrected: the bank pointer alone cannot
  identify an animation. Comparing its validated stream key offset distinguishes
  different streams while retaining move age for a shared stream.
- Diagnostics v19 exposes registered hits/blocks, unsupported moves, damage,
  reaction selection, hitDone and persistent KO-collapse state. Saved decoder
  versions 12–18 remain supported. Query contact counts remain separate.
- Fifteen real Ymir combat cases pass: high/mid/low hits, standing/crouching
  guards, a visible reaction, miss, held/repressed input, independent P2 damage,
  simultaneous contact, unsupported command reporting, reset and pad-only KO.
  The KO replay registers 12 hits and reaches native HP zero/slot 3430 without
  guest-memory mutation or runtime/decode/draw error. The selected original
  moves have zero hit-freeze bytes; no visible hitstop is claimed for them.
- `psx_round.c` now connects KO/time-out to a HUD result, score and fighter reset.
  **Temporary slice policy:** start directly in FIGHT with 3,600 ticks; first to
  two wins; draws award neither side and have no maximum-round cap; hold a KO
  result 240 logic ticks or a time-out 90, then restore both fighters for the next
  round. Match end stays latched until Start. Result input and new damage are
  disabled while authored collapse/recovery continues. Intro, replay, win poses
  and original draw/tie-break handling are omitted. Result health normalization
  and KO/perfect/great flags follow the pinned reference; this is not original
  round-flow parity.
- The extended 18-case Ymir suite now passes the complete bounded round sequence:
  KO gives P1 one point; at 4,000 emulator frames round 2 has both HP restored,
  original spawn anchors, no retained hits/KO and score 1–0. At 7,460 frames a
  second pad-only KO produces match end with score 2–0. Start resets round,
  score and native HP. No rejected face, command overflow, runtime/decode error
  or draw timeout occurs. The final
  screenshot visibly shows PLAYER 1 WINS, round 2 and the 2–0 score.
- All 33 CTest checks pass, including result classification, a single score award,
  result hold, match-end latch, time-out/draw, and owned fighter-state restoration.
  All 77 Python checks also pass. The final procedural 23 and owned movement 12
  regressions pass; the preceding checkpoint also passed nine owned pose cases.
  Owned match/restart and hit-transaction tests pass with ASan/UBSan, and the
  SH-2 frontend passes strict warning/syntax checks.
- The presentation-only `g_game` buffer moved to Low WRAM and is fully initialized
  by `t3_match_reset`; this preserves the 16 KiB High WRAM stack reserve after
  round integration. Initialized bytes are never placed in NOLOAD Low WRAM.
  The current private image is 947,292 bytes, with 67,944 High WRAM BSS bytes
  and 671,632 Low WRAM bytes, and passes native/CD image checks.
- The final round build profile still measures 7.5 work loops/s,
  292 logic ticks in 600 emulator frames, 128 ms work / 76 ms scene-HUD / 12 ms
  skin, 767 commands and 678 faces, with no draw timeout/runtime error. The
  performance gate remains unmet.
- The matching CUE boots in Mednafen 1.32.1 with both textured actors and clear
  HUD/controls. Its neutral recording contains 1,188 frames / 19.8569 seconds
  and exits cleanly. ELF/app/disc hashes match the Ymir build. This is actual
  CD boot evidence, **not** the required complete encounter recording.
- Private evidence includes `ps1-paul-ko-hit-phases/owned-hit-audit.json`,
  `ps1-paul-hit-phases/owned-hit-audit.json`, `paul-combat-v19-suite/`,
  `paul-combat-v19-profile/`, `paul-combat-feedback-ko/`, `paul-round-suite/`,
  `paul-round-complete-suite/`, `paul-round-profile/` and `paul-round-mednafen/`.
  Earlier neutral CD boot recordings do not demonstrate the complete encounter.

Next close matched free-running PS1 replay and pose differences, and improve
logic/render cadence. The newer checkpoint above records the bounded CD encounter.

## Earlier exact attack/contact integration checkpoint (2026-10-16)

- `psx_exact_contact.c` ports the original baked attack placement and
  SegmentHitsCylinder arithmetic. It preserves wrapped perpendicular-foot
  products, slab clipping and truncation towards zero. The legacy generic
  Q16 collision helper remains for its existing callers; original match
  queries now use the exact routine. A synthetic long segment explicitly
  demonstrates a PS1 overflow decision differing from ideal geometry.
- Baked placement uses current signed heading/facing for both current and
  previous anchors, compensates heading-minus-facing around the current root,
  and copies previous WORLD endpoints verbatim on later swept frames. It
  does not require pose time to advance, so a frozen pose can retain a sweep.
- A new original capture contains 1,943 FightFrame calls, 3,886 shape updates
  and 3,886 HitTest calls. All 7,772 independently seeded native cases match,
  including both segment endpoints, 14 hurt-zone centers/radii, slot flags,
  accepted zone, contact point/direction, cooldown, freeze input and whiff.
  Eight contacts were accepted by the original. The final checkpoint agrees
  with independently decoded paused RAM. These counts do not establish our
  free-running animation/HP parity.
- The owned match now builds attack segments after arena correction and before
  body separation, uses its existing world joints directly for hurt geometry,
  and queries the original HitTest eligibility rules. There is no second
  transform of world-space joints. No external root injection is needed.
  The old local-pose/injected-root shadow path is excluded from owned mode.
- Diagnostics v17/v18 expose geometry status, zone, gate status and accumulated
  contact/eligible-frame counts. These count queries, not registered hits:
  hit-slot reservation and per-move hitDone state are not active yet. Decoder
  support for saved versions 12–17 remains intact in v18.
- Six real Ymir contact cases pass: far high misses after retreat; near high,
  mid and low contact; a crouched opponent avoids the high but is reached by
  the low. Twelve owned movement and nine owned pose cases still pass, plus
  all 23 procedural cases. Native HP remains 140 Q16 for both actors in these
  query scenarios, as required until the full transaction is activated.
- All 31 CTest checks and 76 Python checks pass. New C/Python differential
  coverage compares 10,001 synthetic cylinders/segments with pinned original
  arithmetic, including cases differing from its optional non-wrapping fix.
  Placement/atomic-failure tests also pass ASan/UBSan.
- Initialization-only texture metadata and the debugger's fully rewritten pose
  trace buffer now reside in Low WRAM, resolving a linker stack-reserve failure
  when the new geometry became reachable. No LibSaturn source change was needed.
  Current image: 939,588 initialized bytes, 68,360 High WRAM BSS bytes and
  671,004 Low WRAM bytes; both link budgets and native image checks pass.
- At 600 emulator frames, the integrated query build advances 292 logic ticks
  and publishes 73 work loops. Sampled work is 126 ms, scene/HUD 75 ms, skin
  12 ms and loop rate 7.5 Hz. There are 761/2,048 commands, 678 flushed faces,
  no draw timeout or runtime error. The performance gate remains unmet.
- The matching v18 CUE boots in Mednafen with both textured actors and the
  revised original-mode control legend visible. The recording has 1,188 frames
  / 19.8569 seconds and a clean SIGINT exit. ELF/app/disc hashes are retained;
  this is neutral CD boot evidence, not a completed fight.
- Evidence stays private under `ps1-paul-contact-phases/`,
  `paul-contact-v18-suite/`, `paul-contact-v18-inputs/`,
  `paul-contact-v18-poses/`, `paul-contact-v18-profile/`,
  `paul-contact-baseline-regression/` and `paul-contact-v18-mednafen/`.

Next activate the complete hit transaction: original hit-slot/per-move
continuity, damage/guard classification, event effects, hit freeze and queued
reaction selection/starts. Keep head tracking/blending, original free-running
replay, KO/round completion and 60 Hz logic/performance gates open. Contact
queries must not be presented as completed playable combat.

## Earlier grounded body/arena integration checkpoint (2026-10-16)

- `psx_body.c` ports normal-mode ArenaBounds, BodyOverlapSolve, BodySeparate
  and BodySphereProfile with explicit integer wrapping, original table sqrt,
  raw trig, movement-weighted overlap, charging and push/contact flags.
  Ball, Force and throw-linked behavior are outside this grounded path.
- A new read-only PCSX-Redux capture observes 1,943 FightFrame calls and
  5,829 component calls: 1,943 separation and 3,886 arena-bounds calls.
  All match the native C adapter, including all eight body spheres' centers
  and radii from original joint observations. There are 776 calls with actual
  root corrections. The final instruction-boundary checkpoint independently
  matches paused RAM. This proves seeded component behavior, not free-running
  original match parity or the correctness of our animated joint inputs.
- The owning portable match now evaluates authored pose/FK/IK/world parts.
  Saturn rendering reuses that result instead of independently evaluating it.
  After root placement/composition, normal arena correction and body separation
  execute before input queuing. Shapes/joints retain their pre-separation
  coordinates for the source frame, matching the reference's ordering.
- The private extractor emits `psx_body.bin`: 384 bytes of original SquareRoot0
  mantissas, 32 bytes of body-joint indices and 16 bytes of Paul radii. These
  are embedded only through the explicit `T3_LOCAL_PSX_BODY_TABLES` input.
  Provenance records their hash and the radii pointer from the verified EXE.
- Diagnostics v16 adds body activation, accumulated contact ticks and last
  per-actor push vectors. The decoder retains saved v12–15 compatibility.
  Twelve owned movement cases pass, including three sustained approach
  checkpoints: contacts accumulate, the opponent is pushed and actors do not
  cross. Nine owned authored-pose cases and 23 procedural cases also pass.
- All 29 CTest checks and 73 Python checks pass. New synthetic regressions
  cover normal overlap, charge minimum, invulnerability, atomic table failure,
  arena correction clamping and body-radius profiles. CI's owned-mode compile
  fixture now supplies synthetic body tables too.
- The integrated image has 930,700 initialized bytes, 81,064 High WRAM BSS
  bytes and 658,100 Low WRAM bytes. The first link rejected insufficient
  Master stack reserve; moving the uninitialized owned match/world-pose buffers
  to Low WRAM resolved it without changing that reserve or requiring a RAM cart.
  Compiler-reported stack for the match-step function itself is 5,272 bytes;
  this excludes nested callees and is not a measured maximum stack depth.
- The 600-frame profile advances 292 logic ticks and publishes 73 work loops;
  sampled work is 125 ms, scene/HUD 75 ms, skin 12 ms, loop rate 7.5 Hz.
  There are 770/2,048 commands, 678 flushed faces, zero rejected faces, no
  draw timeout or runtime error. The fixed-60-Hz/performance gate still fails.
- The v16 build also boots from its actual CUE in Mednafen: 1,188 recorded
  frames / 19.8569 seconds, textured actors and HUD visible, clean SIGINT exit.
  Its ELF/app/disc hashes match the corresponding Ymir profile and suites.
  This remains a neutral boot recording, not a fight through round completion.
- Private evidence: `ps1-paul-body-phases/`, `paul-body-v16-inputs/`,
  `paul-body-v16-poses/`, `paul-body-v16-profile/`,
  `paul-body-baseline-regression/` and `paul-body-v16-mednafen/`.

The full objective remains open. Head tracking, transition/root-Y blending and
free-running placement/contact comparisons remain unverified. Next integrate
and verify attack geometry/gates, damage and queued reactions against original
instruction observations, then KO/round flow and sustained timing.

## Earlier owned movement integration checkpoint (2026-10-16)

- `T3_ENABLE_PSX_OWNED_MATCH=ON` now routes the frontend through
  `t3_psx_match_step`, without advancing `t3_match_step`. The HUD/camera receive
  a presentation snapshot; the renderer consumes the owning state's world pose
  and root with one shared centering transform. Disconnected P2 stays neutral.
- Original grounded start effects match 145 instruction-boundary RAM pairs;
  ground physics matches all 3,886 observed calls in the same capture. The
  1,943-call capture's final FightFrame return independently matches paused
  RAM. These remain independently seeded component audits, not match replay.
  The paused emulator sessions were collected and terminated after validation.
- Diagnostics v15 adds mode activation, Q16 HP and both original root/anchor
  coordinates; saved versions 12–14 remain readable. Idle animation can displace
  a root relative to its stable anchor, so movement tests distinguish them.
- Nine owned movement scenarios pass in Ymir: neutral/repeat, approach, retreat,
  P2 control, disconnected P2, held punch versus release/repress and reset.
  Nine authored-pose scenarios also pass with both lifecycle and ownership
  required. All 23 procedural scenarios pass separately after rebuilding.
- A caller-owned motion-stream cache avoids repeatedly parsing/validating the
  immutable active/root stream. The same 360-emulator-frame profile improves
  from 152 to 172 simulation ticks and 38 to 43 published work loops. Sampled
  work falls from 134 to 124 ms; loop rate rises from 6.7 to 7.5 Hz. Skin work
  remains 11 ms and scene/HUD work 76 ms. This still drops simulation time and
  fails the 30 Hz render / 60 Hz logic target. PC samples additionally identify
  projection, mesh transformation, skinning, face submission and branch work;
  one PC sample per emulator frame is coarse evidence, not cycle attribution.
- The cached build contains 923,096 initialized image bytes, 85,240 High WRAM
  BSS bytes and 652,372 Low WRAM bytes. It links and passes the native CD image
  checks without a RAM cartridge. Texture residency remains 92,672 bytes for
  both actors. At the profile checkpoint there are 784/2,048 commands, no
  rejected faces, no draw timeout and no runtime/animation decode error.
- Host CTest passes 28/28 and Python passes 72 tests with native runners
  initialized. The cache regression covers local/shared selection, cache hits,
  failed opens and explicit invalidation. CI now compile-checks owned mode
  using synthetic ground tables; no retail bytes are required for that check.
- The matching cached build boots from its CUE in Mednafen with `softfb`:
  1,190 recorded frames over 19.8903 seconds, both textured actors and HUD
  visible. The run exits cleanly after SIGINT; build hashes and video metadata
  are retained beside the recording. This is neutral CD boot evidence, not a
  supported encounter through round completion.
- Private evidence: `paul-owned-neutral/` (before cache), `paul-owned-cached/`
  (after), `paul-owned-cached-inputs/`, `paul-owned-cached-poses/`,
  `paul-owned-baseline-regression/`, `ps1-paul-ground-starts/` and
  `paul-owned-cached-mednafen/`, all under ignored `assets/imported/`.

Next: body separation/arena constraints, required blending, contact against
owned world poses, live HitApply/reaction integration, KO/round reset and a
free-running original replay. The presentation timer currently counts down
without enforcing a round result. Input-buffer mutation and unsupported
transition/branch semantics still require audit. Movement integration does not
prove collision, damage, KO, original round policy or final performance.
The [continuation plan](CONTINUATION_PLAN.md) retains those gates.

## Earlier phased-preview checkpoint

- The MoveVM now owns independent signed 16-bit pose, root and move clocks,
  with the original no-pending-move advance path for freeze, held poses,
  reverse playback and ballistic holds. Rendering/events/diagnostics read the
  owning VM's pose clock. The private build now enables
  `T3_ENABLE_PSX_MOVE_LIFECYCLE`: one-based counters, a separate pending move,
  begin-of-step starts/advancement and end-of-step input decisions. Pose stream
  sampling explicitly subtracts one. The legacy immediate-branch API remains
  available for comparison, but rejects accidental use on a phased VM.
- The phased VM keeps root and pose move ownership separate for held
  transitions 4/5, and rebinds the root stream when the hold releases. It
  implements delayed forward/reverse entry tests, clock effects of the bounded
  ground starts, continuation and input look-ahead. No original health or
  physics is inferred from the provisional match.
- The newer original 547..2300 capture contains 1,943 FightFrame calls.
  All 145 queued ground starts match native clock/readiness results. All 3,760
  audited input decisions match target slot, transition, branch kind, entry
  frame and branch window. Each decision is independently seeded with observed
  original move/clock/history state; this is not a free-running match replay.
  The audit excludes 118 history warmup observations and eight non-input hit
  reactions. It exposed two corrected bugs: END continuations retain window
  -1, and recovery can look ahead into the next stance's input branches.
- 3,498 original no-pending-move observations match the native clock exactly
  at MoveStartAll entry/return. All observed cases are normal forward play;
  138 pending starts are explicitly excluded. The final capture checkpoint
  matches independent paused RAM. An additional 10,000 synthetic states match
  the pinned original research across freeze/hold/reverse/ballistic and signed
  wrapping cases. This exposed and corrected the held-pose release rule:
  root time resynchronizes to pose time instead of advancing its old counter.
- Post-hit MoveVM projection now uses the same animation-length normalization
  as the running VM. Previously a real raw row's length zero disagreed with
  the loaded authored length, so original-data projections were rejected even
  when the target move existed. A synthetic stream exercises real loading,
  projection and rejection of an out-of-range target frame.
- `src/game/move_clock.c` and `src/game/move_lifecycle.c` are attributed C adaptations of the
  pinned research under PolyForm Noncommercial 1.0.0. Its required notice,
  license URL and changes are recorded in the file and `NOTICE.md`; it is not
  silently covered by the repository's MIT license.
- After the phased integration, nine original-pose and 23 procedural input
  Ymir scenarios still pass. The matching current CD records 1,135 Mednafen
  frames (18.971 seconds), showing both textured actors and the HUD. Current
  evidence lives in `saturn-phased-pose-suite/`, `saturn-phased-baseline-suite/`,
  `paul-phased-profile/` and `paul-phased-mednafen/` under ignored imported assets.
  The extra authored cases cover df+LP mid (357), d+LK low (355) and its
  observed continuation into 3564. At 600 emulator frames the latest profile
  reports 292 logic ticks, 668 flushed faces, 760/2,048 commands and no draw
  timeout/error. Work-loop rate remains 7.5 Hz with 127 ms total and 75 ms scene/HUD work: no performance
  improvement is claimed for the correctness change.
- Diagnostics version 14 exposes lifecycle activation, pending slots and each
  fighter's independent root/move/freeze/hold counters. The big-endian decoder
  retains support for saved version-12/13 records.
- Original Paul textures now render on both models in Ymir and through real
  Mednafen CD boot. The private converter uploads 31 TIM records from ARC 73
  into a virtual PS1 VRAM, resolves KMD materials/CLUTs and UVs, bakes 8x8
  patches, and quantizes a shared 256-color palette through LibSaturn's host
  tools. There are 724 unique patches, 46,336 bytes per actor and an 81,885-byte
  SKA1 archive. Both actors currently share the same original costume.
- The model's original two-sided flags are preserved. PS1 quad strip ordering
  is converted to VDP1 perimeter ordering; enabling backface culling without
  that conversion exposed holes. The corrected textured scene uses about 780
  commands instead of the earlier unculled 1,698 in the measured idle pose.
- Textures use the existing public `sat_model_upload_textures` API with static
  application lifetime. The individually managed texture registry has only 64
  slots, so the original managed-character uploader cannot hold 724 patches.
  Two independent native uploads and CRAM banks fit without changing that
  registry's capacity. Textured actor storage uses existing Low WRAM.
- Diagnostics version 13 additionally measures model upload bytes, skinning
  time, rendering time and successful frame submissions. The decoder still
  accepts saved version-12 records. At 600 Ymir frames, the textured build
  reports 92,672 uploaded model texture bytes, 12 ms pose/skin preparation,
  83 ms scene/HUD work (including that preparation), and 131 ms total work for
  the sampled loop. Millisecond timers are quantized; these are emulator
  measurements and submission counts, not hardware/presented FPS.
- EXE common branches are extracted through the fingerprinted layout and
  attached to both DIVMOT banks. The bounded walker implements `0xC00C` CALL
  and `0xC00D` RETURN rather than treating RETURN as input. Synthetic tests
  cover call matching, return/continuation, missing tables and invalid nesting.
- `scripts/saturn_original_pose_suite.py` passes six actual-emulator cases:
  idle, LP/RP/LK/RK and independent LP/RK on both actors. These confirm full
  authored pose application and observed move slots 295/296/300/301 for the
  basic limb buttons; they do not validate live original HP or reactions.
- Pad-only PCSX-Redux automation now reaches an original USA Paul mirror
  encounter in VS mode. The recorder uses running `curSlot +0xA0` and rejects
  unfinished zero-state fighters; using pending `moveSlot +0x1A0` previously
  discarded real gameplay. Listeners are retained explicitly across Lua GC.
- `assets/imported/ps1-paul-levels/` contains 4,000 consecutive paired source
  frames 397..4396, original pad observations, a screenshot and 20 observed HP
  changes (including a round reset). It includes high/mid/low moves and two
  guarded-hit edges. LP is slot 295 (high); the selected mid is slot 357 and
  low is slot 355, with other crouch/transition slots retained as observed.
  No KO was observed in that bounded clip. A subsequent longer run reproduced
  its first 4,000 records exactly before continuing towards a KO.
- `scripts/collect_psx_session.py` validates mounted disc identity and the
  fingerprinted executable, requires paused execution, independently decodes
  RAM, and requires exact agreement with both the final Lua observation and
  the trace's last paired frame. Artifacts, hashes, inputs and logs remain
  private. This gate passed on both captured 4,000-frame clips.
- A paused original P2 idle pose (slot 3, pose frame 37, blend mode 0) agrees
  with the C solver in 17 of 18 local matrices when sampled at `poseFrame-1`.
  All nine remaining differences are in slot 2, modified by the original
  procedural HeadLookAt. The saved source audit reports full parity as false;
  head tracking and blending are still required.
- The KMD loader's Z inversion is now checked against paused original RAM.
  After loading file offsets as `(x,y,-z)`, the C-computed root and all 18
  world joint origins match the sampled original idle pose exactly. Only the
  head rotation still differs. Saturn rest translations use `(x,-y,+z)`;
  vertex anchors use `(x,-y,-z)`. A nonzero-Z synthetic fixture protects this
  distinction.
- `ps1-paul-round-end/` retains 4,732 original paired Vsync observations,
  source counters 397..5128, ending after HP depletion and an observation
  window. The paused final RAM agrees exactly with Lua, including original
  round wins/result (`round_won=1/-1`). Its sampled `ko` byte stays zero;
  HP depletion/result are proven, instruction-level KO flag timing is not.
- Instruction-phase capture now uses read-only entry/return Exec hooks for
  FightFrame, MoveStartAll, HitTest, HitApply, InputSource and MoveStepAll.
  The 650..2300 source-counter window contains 1,818 FightFrame calls and
  167 repeated guest counters. Vsync occurs inside multiple functions, so the
  earlier Vsync clips cannot be treated as completed combat ticks. The final
  FightFrame-return checkpoint independently matches paused RAM.
- Eight actual pre/post HitApply RAM pairs pass the native portable C core:
  high/mid/low damage, resulting Q16 HP, all used slot flags and next reaction
  slots agree with the original. Hits cause 6/9/12 HP damage for these contact
  zones; standing/crouching guards cause zero. Reaction slots are
  2250/2328/2352 for those hits and 2693/2725/2739 for the guards. This is
  observed-contact arithmetic/selection parity, not live Saturn fight parity.
- Immutable mesh/material bindings are validated once using the existing
  public LibSaturn API. The measured scene/HUD cost falls from 83 to 76 ms,
  sampled loop work from 131 to 124 ms, and work-loop rate from 7.3 to 7.5 Hz.
  The current textured CD boots in Mednafen and records 1,135 frames / 18.971 s;
  both original actors and the HUD are visible after the offset correction.

## Executed and verified

- `scripts/saturn_probe.py` invokes sibling LibSaturn's external Ymir probe,
  resolves `_g_t3_diagnostics` and its size from the matching SH-2 ELF, dumps
  target RAM, decodes big-endian signed/unsigned fields, captures the screen,
  and records image/emulator/BIOS hashes and repository revisions.
- Diagnostics exposes both pad samples, position, health, attacks,
  defense, rounds and the existing original-pose observations. An odd/even
  publication epoch rejects interrupted snapshots. Simulation ticks, work-loop
  serials and emulator frames are reported separately.
- `scripts/saturn_input_suite.py` passed 23 emulator cases on the procedural
  build: neutral/repeat, approach/retreat, both sidesteps, guard/crouch, P2,
  CPU on disconnect, four attacks, four release/repress cases, three guards
  and reset. Neutral diagnostics reproduced exactly. Held attacks produced
  one hit; release/repress produced a second. These damage assertions apply
  only to the explicitly provisional moveset.
- The HUD overlap was fixed in sibling LibSaturn's
  `src/graphics/2d/hud.cpp`: the font API expects an absolute glyph advance,
  whereas the default HUD supplied 1 rather than 8 pixels. New captures show
  legible labels and counters. The sibling change is local and uncommitted.
- Skinning/evaluation now runs once per submitted scene, with animation clocks
  still advanced by simulation ticks. In the procedural neutral observation,
  work-loop rate improved from approximately 15 to 30 Hz; this does not prove
  visible frame cadence or real-hardware performance.
- Paul KMD record 71 compiles: 556 compiled vertices, 773 faces, 22 skeleton
  joints, 103 blend vertices and a 24,045-byte bind archive. The original-data
  importer incorrectly demanded a texmap trailer; the real rows instead end
  with zero to three alignment bytes. That parser and its synthetic fixture
  are corrected. Hand variants remain pending; original texture support is
  described in the current checkpoint above.
- Record identity uses the pinned research's original `paul3.kmd` filename,
  repeated costume-group layout and cross-region model correspondence; motion
  record 74 has bank type 0. Shared bank 99 is record 279. Private input hashes
  are retained in the imported inventory and generated model manifest.
- `scripts/extract_psx_pose_tables.py` checks the USA EXE fingerprint and the
  headerless-body hash against the pinned release layout, requires full mapped
  segments, cross-checks explicit address points and records static MIPS
  references to the tables. Limb offsets immediately follow the skeleton
  offsets in the same checked segment. It extracts sine, sqrt, basis, skeleton,
  limb and normalized spline data. This verifies provenance, not PS1 pose parity.
- Twelve actual local/shared animation streams, sampled for twelve frames each,
  passed the existing C/Python FK/IK differential audit: 23,328 matrix components,
  zero mismatches and zero incomplete frames. This is reference-code agreement;
  original-emulator joint observations are still required.
- The private build uses matching axis conversion for anchors, rotations and
  translations (`D=diag(1,-1,-1)`) and a presentation-only scale of 6. Original
  solver offsets use PS1 loader coordinates `(x,y,-z)`. Both actors have full 18-part validity
  and apply authored poses. This visual scale is not original world calibration.
- MoveVM now performs the loader's animation-length fixup. Raw retail move rows
  store zero duration; before this fix idle restarted every tick. The current
  neutral run reaches pose frame 46 with two transitions in 156 ticks. A synthetic
  regression verifies both initial loading and destination-move fixup.
- The private model/pose build boots from CD in Mednafen with `softfb`, showing
  both original models and the repaired HUD. Ymir runs it through injection.

## Reproduction

Keep BIOS and retail data under ignored paths. Configure/build the existing
Saturn toolchain and sibling installed package as in
[the macOS execution guide](MACOS_EXECUTION_PLAN.md). Then:

```sh
export LIBSATURN_BIOS="$PWD/assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin"
export PATH="$HOME/dev/saturn-tools/bin:$PATH"
python3 -m venv build/asset-venv
build/asset-venv/bin/python -m pip install pillow numpy
python3 scripts/saturn_input_suite.py --bios "$LIBSATURN_BIOS"
python3 scripts/extract_psx_pose_tables.py \
  --exe assets/imported/tekken3-usa/SLUS_004.02 \
  --output assets/imported/tekken3-usa/pose-tables

cmake -S . -B build/paul -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$HOME/dev/saturn-prefix/share/libsaturn/toolchains/sh2eb-elf.cmake" \
  -DCMAKE_PREFIX_PATH="$HOME/dev/saturn-prefix" \
  -DLIBSATURN_SOURCE_DIR="$HOME/dev/lib-saturn" \
  -DPython3_EXECUTABLE="$PWD/build/asset-venv/bin/python" \
  -DT3_LOCAL_KMD="$PWD/assets/imported/tekken3-usa/raw/071.bin" \
  -DT3_LOCAL_CHARACTER_ARC="$PWD/assets/imported/tekken3-usa/raw/073.bin" \
  -DT3_LOCAL_DIVMOT="$PWD/assets/imported/tekken3-usa/divmot/074/bank_0.t3logic" \
  -DT3_LOCAL_COMMON_DIVMOT="$PWD/assets/imported/tekken3-usa/divmot/279/bank_99.t3logic" \
  -DT3_LOCAL_PSX_SEQUENCES="$PWD/assets/imported/tekken3-usa/sequences.json" \
  -DT3_LOCAL_PSX_COMMON_BRANCHES="$PWD/assets/imported/tekken3-usa/pose-tables/psx_common_branches.bin" \
  -DT3_LOCAL_PSX_SPLINE_TABLES="$PWD/assets/imported/tekken3-usa/pose-tables/psx_spline.bin" \
  -DT3_LOCAL_PSX_TRIG_TABLE="$PWD/assets/imported/tekken3-usa/pose-tables/psx_trig.bin" \
  -DT3_LOCAL_PSX_IK_TABLES="$PWD/assets/imported/tekken3-usa/pose-tables/psx_ik.bin" \
  -DT3_ENABLE_PSX_SKIN_PREVIEW=ON \
  -DT3_ENABLE_PSX_MOVE_LIFECYCLE=ON
cmake --build build/paul -j4
python3 scripts/saturn_original_pose_suite.py --require-phased --bios "$LIBSATURN_BIOS"
python3 scripts/saturn_probe.py --build build/paul --bios "$LIBSATURN_BIOS" \
  --frames 360 --profile --out assets/imported/paul-profile
mednafen -sound 0 -video.driver softfb -ss.region_default na \
  -qtrecord /tmp/paul.mov build/paul/disc/tekken3_saturn.cue
```

For the owned movement configuration, use the same private inputs in a separate
`build/paul-owned` directory, additionally set `T3_ENABLE_PSX_OWNED_MATCH=ON` and
`T3_LOCAL_PSX_GROUND_TABLES` to the extractor's `psx_ground.bin`, plus
`T3_LOCAL_PSX_BODY_TABLES` to `psx_body.bin`. Run
`scripts/saturn_owned_input_suite.py` against that build; pass `--require-owned`
and `--require-phased` to the authored-pose suite. The owned suite deliberately
asserts unchanged native HP until live contact/damage are integrated.

Interrupt Mednafen with SIGINT to finalize its recording. The harness emits
PC, cycle, instruction and transfer profiles when `--profile` is selected.

Local evidence is under `assets/imported/saturn-input-suite/`,
`saturn-baseline-profile/`, `paul-profile/`, `paul-pose/`,
`paul-mednafen-animated/` and `tekken3-usa/pose-tables/`. All are ignored.

## Measurements and unresolved gates

The current textured model build has 911,052 initialized image bytes,
82,744 High WRAM BSS bytes and 652,372 Low WRAM bytes. The linker accepts
both memory regions and the native image validator accepts CD loading. No RAM
cartridge is required. The private-bank input cap was raised from 450,000 to
600,000 bytes for the measured 543,988-byte Paul/shared pair; full link-time
RAM and image limits still apply.

At the 600-frame Ymir checkpoint before binding validation, the corrected
textured Paul scene flushes 681 faces and uses 773 of 2,048 commands, with no
rejected faces or draw timeouts. The binding optimization runs at about
7.5 work loops per second. The four-tick recovery cap drops simulation
time under this load (292 simulation ticks versus 600 emulator frames), so
this build does **not** satisfy fixed-60-Hz runtime behavior or the performance
gate. Profiling and substantial reduction of repeated pose/geometry work are
required before choosing a 30/60-Hz rendering target for the original models.

Tests executed at this checkpoint: 27/27 project CTest checks, 71 Python checks
with native integration runners assigned, the standalone skin basis/atomic-failure
regression, 23 procedural input scenarios and nine original-pose scenarios in
Ymir. The eight private original HitApply cases also pass the compiled native
adapter. Sibling LibSaturn previously passed 3/3 host checks; its only local
source correction remains the HUD advance. Native integration Python modules
require their runner arguments; plain blanket `unittest discover` leaves those
runner globals uninitialized (the new RAM adapter explicitly skips without one).

Still required to finish the original objective:

- Free-running calibration of original movement, root placement and body
  inputs. Grounded start/physics/separation components and runtime integration
  now exist, but do not establish match parity. Complete end-turn orientation
  refresh, input-buffer mutation and required command/condition semantics.
- Higher-resolution texture/UV/lighting calibration, hand variants, head
  tracking, blending and visual checks against the original encounter.
- Replay original FightFrame-return observations through the same host/Saturn
  simulation. Timing boundaries are now measured; VM transition/hold/freeze
  timing and KO-collapse timing still need integration. The original
  health-zero/round-result capture exists, but native KO arithmetic still
  needs a pre/post HitApply round-ending sample.
- A single authoritative original-combat path with calibrated movement, collision,
  live damage/reactions and supported round completion verified against those clips.
- Full private-input regression scenarios, measured visible cadence, optimization,
  and a recorded supported encounter from CD boot through round end.
