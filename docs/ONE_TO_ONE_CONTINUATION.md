# One-to-one port continuation plan

Planning checkpoint: 2026-10-25, repository HEAD `a5aa52f`.
This is the next execution order. It supersedes the priorities in
[CONTINUATION_PLAN.md](CONTINUATION_PLAN.md); those earlier experiments remain
historical evidence. This update changes documentation only and does not close
any implementation or validation gate.

Execution is now tracked in [FIRST_ROUND_START.md](FIRST_ROUND_START.md):
original-font messages, production round-one initialization, per-tick HUD
phase and the first configuration-initialized SH-2 comparison have landed.
Use its artifact-specific results for current status; the inventory below
records the planning baseline.
The next combat result is tracked in [THROW_ESCAPE.md](THROW_ESCAPE.md):
original tap retention and a continuously compared throw-escape path.
Reversal integration is tracked in [REVERSAL_PROGRESS.md](REVERSAL_PROGRESS.md):
original conditions/lists/walker and a 600-tick fresh original/host/SH-2
comparison through counter-throw damage and recovery. Its final release gate
passes all 26 live cases and 3,000 reference ticks. Additional airborne/juggle
and recovery input comparisons are in [AIR_RECOVERY_PARITY.md](AIR_RECOVERY_PARITY.md);
their limits remain explicit.
The camera audit is now tracked in [RETAIL_CAMERA_PARITY.md](RETAIL_CAMERA_PARITY.md).
It uncovered always-on presentation patches in the remake's original-code
harness and corrected the exporter to restore retail instructions before
setup. Older captures retain their recorded patched-presentation policy.
The second human side update and physical mapping now run after drawing.
Three fresh retail fixtures (escape, reversal and held-right side crossing)
match all compared fields for **1,560 ticks**, including the full 60-entry
input histories. Host suites pass **72/72 Paul–Law** and **63/63 Paul**.
The preceding RNG/eye/sound work is in
[PSX_FIGHTER_SOUNDS.md](PSX_FIGHTER_SOUNDS.md); its isolated sound tests compare
2,878 original MIPS cases, and blink compares 624. Current native neutral
and high-hit smoke tests pass; the historical full release gate does not
qualify the latest ELF. The next camera step is recorded in
[RETAIL_CAMERA_EDGE_CASES.md](RETAIL_CAMERA_EDGE_CASES.md): bugs 53/54/55 are
now preserved in faithful mode and match 1,216 unpatched MIPS phase cases;
the corrected mode matches another 1,216 patched-code cases. All 1,560 fresh
retail ticks still match, and host suites now pass 73/73 Paul–Law and 64/64
Paul. Independent SH-2 camera/input comparison is now tracked in
[OWNED_CAMERA_SH2_PARITY.md](OWNED_CAMERA_SH2_PARITY.md): fresh configuration,
physical controls, full input histories and camera/sound/eye/HUD/tap/RNG state
are hashed without original view/side feedback. The three native cases match
4,100 ticks, including a 3,000-tick retail timeout and next intro. That longer
comparison exposed and corrected main-loop camera RNG advancement between
rounds. Natural throw-chain edges, native audio/replay events and complete
image/cadence coverage remain open; the report binds each result to its ELF.
The audio continuation is in [REPLAY_SOUND_RING.md](REPLAY_SOUND_RING.md):
the portable 300-entry sound ring matches 12,647 original MIPS operations,
with complete host suites at 74/74 Paul–Law and 65/65 Paul. It is not yet
connected to match/replay/native playback. The complete-bank audio size audit
shows why the current fixed sample subset cannot establish full coverage.
The subsequent full native gate passed 26 live cases and 3,000 independent
reference ticks for the current playable ELF. A new varied-input retail
fixture exposes missing effect-object RNG at source frame 222. The spark
phase port and next integration requirements are in
[HIT_SPARK_PROGRESS.md](HIT_SPARK_PROGRESS.md): 1,384 MIPS cases / 18,048
updates match, but the pool is not yet connected and the fight still fails.
Current host suites pass 75/75 Paul–Law and 66/66 Paul. Port the complete
allocation/update lifecycle before inserting sound/replay consumption; do not
replace the missing particle state with a fixed RNG adjustment.
The pool is now connected to the owned fight:
[EFFECT_POOL_INTEGRATION.md](EFFECT_POOL_INTEGRATION.md). All 3,000 varied-input
ticks and 190,576 semantic particle fields match on the host. Full host suites
pass 76/76 Paul–Law and 66/66 Paul; stock-RAM native smokes pass with a 96-entry
derived pose cache and the full 80-slot effect capacity. Particle drawing,
active-glow/projectile/replay cases and the native audio bridge remain open.
The same varied-input reference also matches all 3,000 ticks on SH-2 with
the runtime-owned pool/camera markers and Low WRAM prefilled `0xA5`. The
new playable candidate's complete gate also passes: 142 host tests, 26 live
combat cases and all 3,000 reference ticks. It qualifies only the finite
coverage recorded for ELF `8faa85fa…`, not the complete port.
Particle rendering preparation is tracked in
[PARTICLE_RENDER_PROGRESS.md](PARTICLE_RENDER_PROGRESS.md): USA/Rev.1-identical
sprite/palette/model extraction, an independent complete asset audit, and
234 original projected reference quads. Native drawing and additive blending
are still pending; a VDP1 half-transparent approximation is not pixel parity.
The portable C billboard transform now matches **1,514 unpatched original
draw cases**, including actual OT insertion and all 234 captured fight quads.
Host suites pass 67/67 and 77/77. The report also records VDP2's available add
mode and the remaining actor occlusion, VRAM and viewport integration issues.

Package 4's per-tick event ring and its live/reference result are in
[COMBAT_EVENT_RING.md](COMBAT_EVENT_RING.md).

## Target and reference policy

First complete a faithful Paul-versus-Law match on stock Saturn RAM, from
first-round initialization through terminal result. Then extend the same
implementation to the roster, stages and game modes. A successful bounded
match is a milestone toward the full port, not proof that all Tekken 3 works.

Use the pinned readable remake to understand the rules, its verified Python
ports and original-code harness to prove them, and the original USA disc to
check release-specific behavior and final audiovisual output. Godot's core is
a valuable implementation reference; its modernized presentation is not a
pixel reference for the original PlayStation game.

For each missing behavior, follow this order:

1. Search the remake's `CLAUDE.md`, research docs, Python port and typed
   GDScript implementation. Record the function, source revision and evidence.
2. Adapt the rule into portable C, preserving original call order, integer
   widths, wrapping, rounding, RNG draws and relevant original bugs. Attribute
   adaptations in their source and `NOTICE.md` under the existing license.
3. Differentially compare C with original-code execution. Diagnose the first
   mismatch; retain the complete failing tail. Never copy expected state into
   the simulation, reseed between ticks or filter out unsupported moves to pass.
4. Consult decompilation/disassembly or capture original RAM only for a gap
   that existing research and harnesses do not resolve. Japan Rev.1 addresses
   require the pinned `tools/remake_import/layouts/usa.json` mapping for USA.
5. Integrate the same C state machine on Saturn, then check presentation and
   cadence with a matched input schedule and artifact identities.

Gameplay and timing require exact compared state per simulation tick.
Presentation requires original assets, placement, camera rules and event
timing, with each Saturn-specific visual approximation measured and recorded.
The current 320 x 224 output and VDP2 panorama approximation do not establish
literal PlayStation pixel identity. Visual differences must remain explicit.

## What already exists

The latest committed reports establish the following, without rerunning them
for this planning update:

| Area | Recorded evidence | Limit of that evidence |
| --- | --- | --- |
| Shared combat | Movement, branches, damage, guard, reactions, round flow, KO replay, victory and next round; original-code fights in [FIGHT_ORACLE.md](FIGHT_ORACLE.md) | Case coverage is finite; all move families are not certified |
| Live release | Both HUD configurations pass 56 + 55 host CTests, 26 live combat cases and a 3,000-tick Paul-Law reference | Applies to the recorded ELFs, not uncommitted edits |
| Earlier continuous comparison | 16,334 original/host/Saturn ticks across three Paul-mirror fixtures | Historical fixture/build coverage; not a new Paul-Law result |
| Original HUD | Sprite bars, 60 Hz bar state, timer, marks, names and mapped placement | Whole-frame HUD and all text states remain unqualified |
| Runtime cadence | Recent HUD CD windows around 31-32 submitted images/s; Ymir intervals at 60 logic ticks/s | Submissions do not prove distinct displayed images or real hardware speed |

See [NEXT_PLAYABLE_PROGRESS.md](NEXT_PLAYABLE_PROGRESS.md),
[ORIGINAL_HUD.md](ORIGINAL_HUD.md) and
[THIRTY_IMAGE_GATE.md](THIRTY_IMAGE_GATE.md) for hashes and measurements.
The old 8.49 submissions/s checkpoint is not the present release baseline.

At planning time, local edits already exist in `scripts/extract_psx_hud.py`,
`src/saturn/main.c`, `move_probe.c` and `original_hud.[ch]`. They implement
part of original-font extraction and round/result text. Preserve and review
this work first; do not overwrite it or call it validated from older reports.

## Ordered work packages

### 0. Establish the actual candidate and its coverage

- Record the current diff, private CMake configuration, submodule/SDK revisions
  and candidate hashes. Keep the last qualified HUD-off release as control.
- Review the in-progress HUD text changes against `hud.md`,
  `tools/remake_import/hud.py`, `core/fight/round_hud.gd` and
  `presentation/screens/fight_canvas.gd` in the remake.
- Start a coverage table with separate entries for implemented behavior,
  original-code comparison, Saturn live test, original-image comparison and
  cadence. Include unsupported branches and remaining approximations.
- Reconcile stale status in `RELEASE.md`, `SATURN_VALIDATION_SCRIPTS.md` and
  the older camera/HUD reports when recording the next qualified result.

Exit: a reproducible candidate/control pair and an honest list of evidence
for each, including the validation still needed for local text changes.

### 1. Finish original HUD text and first-round entry

Finish and validate the existing text slice first. Cover ROUND/FINAL ROUND,
READY, KO/double KO/perfect/time up, win/lose/draw and the blinking REPLAY
label, with original fonts, palette, placement and frame boundaries. Verify
each state's entry and exit, P1/P2 winners and human-versus-CPU wording.
Avoid hardcoded fighter names in the eventual reusable path; derive them
from private character metadata. Mode lines can follow in the same HUD track.

Then close the explicit first-round gap: `psx_match.c` currently initializes
`flow.state` to FIGHT. Follow the original round-start order in `fight-frame.md`,
`core/fight/fight_simulation.gd`, `round_flow.gd` and `round_state.gd` to cover
placement, intro camera, input suppression, timer activation, HUD and announcer.
Use the original counters and observed transition boundaries; documentation
descriptions such as "150 frames" are not a substitute for an off-by-one test.

Keep mid-fight oracle seeding distinct from production match initialization.
Existing fixtures beginning in FIGHT should remain valid; add an original
trace beginning before first-round setup rather than shifting every tape to
conceal an initialization difference. Test held input across intro exit and
confirm that the first legal command is accepted on the same source tick.

Exit: matched first and later intros, result messages and replay blink;
continuous original/host comparison across setup-to-control and Saturn
checkpoints using the same contract, plus a native CD boot.

### 2. Expand combat proof for Paul and Law

Use `moves.md`, `combat.md`, `fighter.md`, `game-bugs.md`, the corresponding
`core/fighter/` and hit-system implementations, and `fight_harness.py` before
adding any inferred behavior. Begin with the open families recorded in the
progress report: other command throws and follow-ups, throw escapes,
reversals, stance transitions, knockdown/get-ups, airborne/juggle reactions,
posture/evades and trades. Inventory what C already supports before porting it.

For each family, select a deterministic original scenario that actually
executes it, compare every tick through recovery, and inspect transitions,
pose/blend, roots/headings, contact flags, HP, cooldowns, input history and
relevant RNG. Exercise both sides, guard/counter conditions and a nearby
window boundary where applicable. Add seeds for coverage of specific gaps;
random-bot seed count alone does not prove a mechanic.

Exit: each declared supported family has original-code evidence and a live
Saturn regression. Keep unsupported/refused paths visible. Preserve existing
KO, timeout, replay, next-round, terminal result and two-pad checks.

### 3. Close the camera and geometry feedback loop

The current reference workflow supplies original camera screen positions and
sparse previous views. Its combat-state pass does not establish independent
camera parity. Implement a comparison that drives the native camera from the
same initial camera state, fighter state and RNG and checks its outputs and
draw-derived fighter screen coordinates each tick without feeding expected
camera output back into C.

Use `camera.md`, `stages.md`, `camera_sim.py`, `projection_sim.py` and
`core/camera/`. Cover movement along Z, side swaps, walls, throws, overhead KO,
replay/victory, intro streams and shake. Reconcile old "throws unsupported"
notes with the newly implemented throw path; verify bank-stream coverage
instead of assuming the throw test proves every cinematic.

For fighter presentation, compare identical source pose and view: FK/IK,
blending, head tracking, hand variants, seam anchors, UVs and clipping. Qualify
authored face-order bias only through the matched overlap cases described in
[VDP1_PRESENTATION.md](VDP1_PRESENTATION.md). Compare floor shading, lighting,
shadows and panorama separately; audit implementation before treating older
missing-feature notes as current.

Exit: a continuous independent camera comparison and matched original/Saturn
screenshots, with numeric/pixel differences and remaining hardware adaptations
documented. Keep frozen Saturn-to-Saturn checks for optimization regressions;
they do not replace original-image comparisons.

### 4. Preserve audiovisual events at 60 Hz

Current `combat_events.c` derives events from counters once per submitted
image, and documented voice/effect choices include approximations. Move
authoritative event generation to the simulation tick: type, tick, fighter,
variant, position and required random choice. Consume events once in
presentation, retaining multiple events between images. Bound queue memory
and expose overflow; replay must follow original sound/effect logging rules.

Use `effects.md`, `sound.md`, `core/fighter/move_events.gd`,
`fighter_sounds.gd` and the remake's event consumers. Port original spark
sprites and lifetimes, swing/block/hit variants, voice selection/cooldowns,
KO/result calls and stage music incrementally. Original gameplay RNG and
cosmetic RNG must preserve their respective state and call order.

Exit: original-versus-C event timelines through hit, guard, throw, KO and
replay; identical event scheduling when submitting at different cadences;
native CD listening/viewing evidence. Successful SCSP calls alone do not
prove audible output.

### 5. Qualify the complete match and expand content

Run the accumulated host/oracle/live gates on the final candidate. In
uninstrumented native CD runs, measure interval deltas during intro, movement,
combat, throws, replay, victory and next round. Require sustained 60 logic
ticks/s without discarded ticks and at least 30 submissions/s; check actual
presentation cadence separately. Retain real-hardware qualification as open
until measured there. Profile only when a measured regression requires it.

Then expand through the existing importer and shared C rules: one additional
fighter with different mechanics, one new stage, and an original selection/VS
to match/result flow. Extend special-character rules and assets according to
the coverage matrix. Replace the current repeated-Law CPU ladder with original
arcade sequencing before certifying Arcade. Follow with the remaining roster,
stages, screens and modes in bounded increments, each with its own original
trace and Saturn acceptance cases. Do not claim a full-game 1:1 port from a
two-character encounter.

## Validation and resource contract for every increment

- Use [SATURN_VALIDATION_SCRIPTS.md](SATURN_VALIDATION_SCRIPTS.md) and
  `scripts/saturn_regression.py` for the relevant host builds, all currently
  supported live cases and full reference prefix. Add focused original
  scenarios for new behavior; retain complete comparisons across boundaries.
- Rebuild reference fixtures/instrumented builds when their schema or source
  changes. Record seed, pads, source-tick definition, compared fields, first
  mismatch or full prefix, hashes, revisions, memory and command peaks.
- Validate stock-RAM image limits, stack reserve, zero draw timeouts, zero
  rejected faces and no event/tick loss. Generic renderer/audio fixes belong
  in sibling LibSaturn, with its own affected checks and one SDK prefix rebuilt
  in place. Changes outside this workspace follow its write permissions.
- Before each run batch: `du -sh build assets/imported`. Planning inventory
  was 1.0 GB and 5.3 GB respectively, below the 5/20 GB limits. Keep only one
  candidate plus one control alongside the mandatory protected baselines,
  imports and final evidence. Use a shared experiment prefix and delete
  superseded outputs by explicit list, recording what was removed.
- Prefer trace/JSON and screenshots. Record only the shortest necessary CD
  window, inspect it, retain screenshots/logs and delete `run.mov` immediately.
  Keep retail inputs, derived data and captures private. Put durable results
  and remaining mismatches in `docs/`.

The first executable task is package 0 followed by completion of the existing
HUD text slice. The next behavioral milestone is the original first-round
setup and intro, proved continuously through the first accepted command.
