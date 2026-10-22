# Whole-actor preparation and the remaining 30 FPS work

The target remains 60 committed simulation ticks and 30 newly completed,
displayed images per second with both original Paul models and the VDP2 Paul
stage. It is not achieved. Whole-actor preparation passes its three paired CD
measurements and complete combat qualification. It now defaults ON for the
original textured VDP2 stage when parallel skin/geometry and direct spans are
selected. Explicit OFF remains the control; other configurations keep their
previous defaults.

## Implemented candidate

`T3_FUSED_ACTOR_PREPARATION=ON` packs both original joint poses, then assigns
one complete actor to each CPU through the existing range executor. Each CPU
deforms, places, projects and prepares its actor's faces before the join.
Master merges the reserved second-actor span and performs the existing global
painter flush, HUD and hardware submission. This removes the intermediate
skin-job join and second worker dispatch. There is no new SDK API, renderer
queue, simulation snapshot or change to authoritative arithmetic.

The range executor publishes its inputs and invalidates stale Slave cache
state. The actors own disjoint mutable vertex/projected storage. Slave writes
its face/key span through uncached aliases; Master consumes the actual written
projected/key prefix after the join. Cold storage is consumed when clipping or
uncompiled materials require it. No reservation is reclaimed on a live-worker
timeout. The executor's stopped-worker fallback reruns deformation before
placement, so it cannot apply the sixfold placement twice to reused vertices.

The option requires the owned encounter, parallel skin/geometry and direct
spans. Compiled/native renderer prototypes and geometry-verification mode are
excluded. Ordinary configurations retain their existing path. The workload
guard additionally requires a completed `fused_actor_frames` counter.

Final qualification build: `build/paul-stage-fused-qualification`, using the
unchanged installed SDK `6302bec`. Initialized image: 873,776 bytes; High BSS
ends at `0x060f9ef8`, Low WRAM at `0x00281908`. The checked 16 KiB Master stack
reserve remains intact. ELF SHA-256:
`a29bc995b15d04ef81d3946d46d67efa46079f47f54ad560c7cf54c5859bf628`.

## Evidence and qualification status

- All 47 host CTests and 39 Saturn tool tests pass.
- The final frozen tick-120 candidate matches all 97 stable diagnostic fields
  and every pixel in the existing model/stage crop. RGB SHA-256:
  `dbd1dcf895998266165aed2f9e3a3cfbe9626638b3a864f1f2a3678a800a2c2a`.
- The final qualification binary passes all 24 combat cases, including KO,
  replay, authored victory, next round, second KO, terminal match/reset and
  both neutral timeout cases. Every case records ELF `a29bc995…bf628`.
  Adaptive approach/search observations are not additional completed cases.
- The final 5,400-field cache-on run commits 5,100 ticks and 1,325 submissions,
  with 5,292 observed and 192 discarded fields, no guest error or draw timeout,
  and 1,326 completed whole-actor preparations. The preceding qualified stage
  control records 4,878 ticks and 1,221 submissions. This does not meet 60 ticks/s.
- The warmed 3,600-field model/stage crop changes RGB 904 times, or 15.07/s,
  versus the control's 833/13.88. RGB changes are neither unique pose epochs nor
  physical Saturn FPS. The FRAME HUD is outside this crop.

An initial actual Mednafen CD comparison uses neutral connected pads and movie
seconds 30/50/70/90, after stage loading:

| Variant | FRAME endpoints | 60-second increments |
| --- | --- | ---: |
| Paul-stage control | 158 / 368 / 579 / 791 | 633 |
| Whole actor, screening binary | 167 / 389 / 612 / 837 | 670 |

This is 5.85% more submission increments, approximately 11.17/s versus 10.55/s.
The interval includes round transitions. The faster candidate has already
finished round one at second 70, so its 30-to-70 interval is not relabeled as
the control's same-round interval. It is one screening pair, not three-pair
qualification or a displayed-image count. Three alternating pairs on the final
qualification binary and a fresh control have now completed. Every pair gives
the same endpoints:

| Variant | FRAME at 30 / 50 / 70 / 90 | Increments, each of three pairs |
| --- | --- | ---: |
| Fresh Paul-stage control | 158 / 368 / 579 / 791 | 633 |
| Final whole-actor candidate | 166 / 388 / 610 / 834 | 668 |

The median gain is **5.53%**, approximately 11.13 versus 10.55 submission
increments/s. The orders are control/candidate, candidate/control and
control/candidate. Each recording contains more than 104 movie seconds. The
separate 30-to-50 first-round interval gives 222 versus 210 increments, a 5.71%
difference; both images at second 50 still show round one. It is a 20-second
same-round check, not the older control's 40-second interval. Full combat
qualification also passes; no new displayed-image or hardware claim follows.

The screening binary reported only pose-packing time in `skin_work_ms`; that
value must not be interpreted as eliminated skinning. The qualification binary
also measures Master's actual deformation. Its geometry timing includes the
remaining worker join. Complete-render measurements and call profiles are the
appropriate comparisons across these schedules.

Private evidence and source snapshots are under
`assets/imported/fused-actor-source`; captures use `paul-stage-fused-*` names.

## Rejected reciprocal-table projection

A separate local projection adapter estimated both screen quotients using a
16 KiB Low-WRAM reciprocal table and corrected the integer remainder. It removed
DIVU transactions while preserving exact integer coordinates, including negative
truncation. Tests covered every admitted depth, quotient boundaries and signed
extrema, plus random coordinates. Frozen pixels and 97 stable fields matched.

Actual Mednafen screening gives only 636 increments versus 633 for the control.
Combined with whole-actor preparation it gives 671 versus 670. Neither result
justifies the additional table and adapter. The code, option and dedicated test
were removed; their exact source, binaries, proofs and captures are archived in
`fused-actor-source/reciprocal-rejected`. The temporary 48-test result belongs to
that rejected source, not the retained 47-test tree. This is a tested, rejected
experiment, not an untried route to 30 FPS.

## Executed simulation/flush pipeline — rejected

The next experiment moved serial authoritative ticks to Slave and overlapped
at most two of them with Master's ordinary painter flush and HUD. Both CPUs
still prepared separate actors first. Master owned a 436-byte match/HUD
snapshot for the image, joined before diagnostics/reset/reuse, and recorded
its separate pose tick and two-tick lag. The original simulation arithmetic
and fighter ordering remained unchanged. No SDK source was modified.

The stock 4 KiB Slave stack could not run the simulation: the top-level match
step alone reserves 6,784 bytes. An initial caller-owned High-WRAM transaction
copy reduced that frame, but nested calls still exceeded 4 KiB. The completed
candidate therefore used an application-owned 16 KiB stack, with an SH-2 ABI
trampoline preserving the executor's original stack and PR. Moving texture
descriptors to Low WRAM made the High-WRAM stack fit while preserving the
Master's checked 16 KiB reserve. The transient workspace API and core edits
were removed; the final candidate ran the unchanged original core.

The 16 KiB Low-stack variant, final High-stack variant and High-stack variant
with 16-FRT-tick completion polling were screened from actual Mednafen CD boot,
with the same SDK, assets, neutral pads and movie endpoints:

| Variant | FRAME at 30 / 50 / 70 / 90 | Increments |
| --- | --- | ---: |
| Fresh whole-actor control | 166 / 388 / 610 / 834 | 668 |
| Low stack / High transactional workspace | 152 / 356 / 559 / 766 | 614 |
| High stack / unchanged original core | 180 / 420 / 663 / 903 | 723 |
| High stack / local completion polling | 181 / 422 / 666 / 908 | 727 |

The best screening gain is **8.83%**, about 12.12 submission increments/s. It
fails the selected 1.5x architecture continuation gate of 1,002 increments.
Local polling adds only 0.55% over the ordinary wait. These are single screening
pairs, not three-pair qualification or hardware/displayed-image rates. Both
control and candidates remain in round one at second 50; the 30-to-50 intervals
are 222, 204, 240 and 241 increments respectively.

The final canonical High-stack candidate passes all 14 basic combat cases and
matches all 97 frozen stable fields and exact crop pixels. The sampled neutral
stack high-water bound is 10,628 bytes; all 14 combat captures retain the bottom
256-byte guard. The recorded 12-field CPU profile places all ten completed
owned simulation calls on Slave and all three completed painter flushes on
Master. This verifies that the proposed execution boundary actually ran.

In 5,400 Ymir fields that candidate commits 5,295 ticks and 1,749 submissions,
with 5,297 observed fields, two initial discarded fields and no guest error or
draw timeout. A warmed 60-second crop changes RGB 1,168 times (19.47/s), with
a 66-field unchanged interval across the round transition. These larger Ymir
gains do not override the much smaller native screening. The 24-case campaign
and 16,334-tick differential gate were not run on this rejected scheduling
candidate. Additional wall-clock presentation age was not fully qualified;
the observed two-tick lag is not relabeled as that proof.

All simulation-pipeline options, code, temporary workspace API/tests and stack
trampoline were archived and removed. The private archive is
`assets/imported/slave-simulation-source`, including immutable binaries, source
checkpoints, stack-usage reports and captures. Older binaries retain their own
source-line literals and precede the later local-polling scaffolding; rebuilding
an old directory from the current tree does not reproduce that experiment.
Do not present serial Slave simulation or Master completion polling as still
untested ways to reach 30 FPS.

## Retained default and next execution boundary

The following branch experiment has now been implemented and qualified:
[immutable current-input filter](BRANCH_INPUT_FILTER.md). It passes all 16,334
original/host/Saturn ticks and 24 live cases. Three native CD pairs give 735
versus 666 increments, a 10.36% gain. The filter now defaults ON for the optimized
original-texture VDP2-stage configuration with whole-actor preparation. The
remaining 30-image/60-tick goal is still unmet; the report supersedes the proposed
branch work below and records the next combined experiment.

1. `build/paul-stage-fused-promoted` consumes the new default. Removing its
   cached `T3_FUSED_ACTOR_PREPARATION` value and reconfiguring selects ON. Its
   rebuilt ELF is byte-identical to the fully qualified `a29bc995…bf628` binary.
   All 47 host and 39 Saturn tool tests pass after removing the rejected code.
2. Remove provably impossible movement-command candidates before walking them.
   A structural audit of the original idle branch stream follows 140 rows per
   actor; only 39 remain even under the simple necessary condition for a tick
   with no newly pressed buttons. This is an opportunity to avoid traversal,
   not a measured runtime saving. The earlier native-row experiment left the
   traversal intact and is already rejected.
3. Prototype a bounded immutable candidate index for current input classes.
   Keep CALL, RETURN and END rows reachable, preserve source order, frame/live
   restrictions and conditions, conditional skip-to-END behavior and malformed
   data bounds. Sequence-history checks must remain live; any static sequence
   filter must be a proven necessary condition only. Unsupported classes or
   unbound/changed tables retain the original walker.
4. Compare filtered and original walkers on synthetic malformed/control-flow
   cases and the actual banks. Measure whole-game neutral and combat throughput
   against the promoted default. Run all 16,334 differential ticks after any
   authoritative selection change and all 24 live combat cases before promotion.
   Keep input/class storage and boot cost inside the stock-memory budget.
5. Recompute both CPU budgets after removing useful work. Revisit an archived
   schedule only if the changed dependency/cost budget justifies it; the failed
   simulation and command-writer schedules are not new untried windows.
6. Retain tick debt and input edges as a separate correctness change. Final
   acceptance still requires independent neutral, combat and round-transition
   runs sustaining 60 committed ticks and 30 new complete displayed images/s,
   no growing debt and bounded image age in both emulators.

The valid whole-actor 36-field profile contains nine complete render/flush calls
and 34 owned simulation steps without dropped events. Mean inclusive per-CPU
model costs are 589,516 for Master actor preparation, 239,496 for Master flush,
539,309 for Slave actor preparation and 211,276 per simulation tick. Nested
costs overlap; CPU clocks are not aligned wall time. The actual executed pipeline
confirms that redistribution alone does not meet the target. Further work must
remove substantial useful work and verify the complete encounter.
