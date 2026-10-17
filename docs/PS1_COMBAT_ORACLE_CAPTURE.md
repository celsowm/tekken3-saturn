# Tekken 3 USA: private PS1 combat oracle capture

Original USA Paul mirror captures now exist privately, including high/mid/low
hits, guards, health depletion and round results. This document describes
their capture and its timing limits. The repository does not bundle a PS1 disc,
BIOS, game assets, private trace data, or recompile proprietary MIPS code.

## Version and verified static address provenance

Use **Tekken 3 USA SLUS_004.02**, not Japan Rev.1 and not an arbitrary
patched build. Research source:
[n0madic/Tekken3Remake](https://github.com/n0madic/Tekken3Remake),
[Japan Rev.1 fighter offsets](https://github.com/n0madic/Tekken3Remake/blob/master/docs/research/code/fighter.md),
[EXE memory map](https://github.com/n0madic/Tekken3Remake/blob/master/docs/research/code/memory-map.md)
and the exact
[USA relocation map](https://github.com/n0madic/Tekken3Remake/blob/master/tools/remake_import/layouts/usa.json).

The research repo's `usa.json` `blocks.exe.points` contains
explicit **[canonical Japan address, original USA source address]**
pairs:

| Description | Japan Rev.1 address | USA SLUS_004.02 address |
|---|---|---|
| Fighter P1 | `0x800A96F0` | `0x800A9228` |
| Fighter P2 | `0x800AAF7C` | `0x800AAAB4` |
| Global frame counter | `0x800AFF14` | `0x800AFA4C` |
| Global game state | `0x800AE6CC` | `0x800AE204` |

Fighter stride is `0x188C`. Structure-relative member offsets are
shared across these versions according to the upstream research:

| Member | Offset | Reading |
|---|---|---|
| Running move slot (`curSlot`) | `+0xA0` | little-endian signed 16-bit; `+0x1A0` is the pending slot, often −1 |
| Current pose frame | `+0x058` | little-endian signed 16-bit |
| Health / health max | `+0x3F4 / +0x3F8` | little-endian signed 32-bit Q16 |
| Fighter state word | `+0x060` | little-endian unsigned 32-bit |
| KO flag | `+0x0DE` | unsigned byte |
| Current move frame | `+0x05C` | signed 16-bit |
| Hit freeze | `+0x076` | signed 16-bit |
| Recovery frame counter | `+0x11A` | signed 16-bit |

The upstream USA executable layout lists SHA-256
`525e689449eb135bf833ecaff26a5e2623686e5a4e2104f425978bb33a55ffec`
for its normalized extracted **EXE block** (`size=1183744` bytes).
The standalone Vsync Lua recorder does not fingerprint the disc or EXE.
`collect_psx_session.py` checks the full USA EXE fingerprint and mounted disc
identity; `prepare_psx_phase_capture.py` verifies both full EXE and body/layout
hashes before generating CPU breakpoint addresses. Live captures independently
cross-check their final paused RAM and Lua observations.

## Option A: continuous capture inside PCSX-Redux

Use [PCSX-Redux](https://pcsx-redux.consoledev.net/) with a locally
owned USA game copy. Its Lua API supports `PCSX.getMemPtr()` and
`PCSX.Events.createEventListener("GPU::Vsync", ...)`; see the
[documented memory API](https://pcsx-redux.consoledev.net/Lua/memory-and-registers/)
and [event API](https://pcsx-redux.consoledev.net/Lua/events/).

1. Boot the verified **USA** game. Do not use Japanese addresses.
2. In Debug → Lua editor or Lua console, load the script from your clone:

   ```lua
   assert(loadfile("/path/to/tekken3-saturn/scripts/pcsx_redux_capture_usa.lua"))()
   ```

3. Start a fight normally. The recorder waits for guest game state 8
   (`FightMain`), two valid fighter structs and a new guest frame count.
   It writes two fighter records for each guest frame, ignoring duplicate
   GPU VSync events without advancement.
4. The default output is
   `tekken3-usa-ps1-combat.psx-combat.jsonl` **in the emulator's
   working directory**, up to 1,200 frames. Edit the `CONFIG` table
   near the script's top for another path/limit.
5. To stop from Lua console, run:
   ```lua
   T3_REDUX_CAPTURE.stop("finished")
   ```

Reset, savestate load, leaving the fight or a skipped guest frame ends
the capture rather than quietly combining unrelated frames. The file is
closed on normal stop. The callback stores only small field snapshots,
not the complete game RAM.

**Measured sampling limitation:** `GPU::Vsync` can interrupt HitTest,
HitApply, InputSource or MoveStepAll. The game can execute multiple FightFrame
calls with the same guest counter. Vsync clips are valid observations of
events, but their rows are not necessarily completed combat ticks. Do not use
their line indices directly as original simulation tick numbers. Input schedules,
hashes and read-only CPU-phase observations are retained with the private runs.

## Option B: independent decoding of full RAM snapshots

PCSX-Redux also exposes a read-only debug web endpoint
[`/api/v1/cpu/ram/raw`](https://pcsx-redux.consoledev.net/web_server/).
Enable the web server in emulator settings (normally localhost:8080).
**Pause emulation and capture at a consistent point after each
single-frame advance**. For example, on your own machine:

```sh
mkdir -p /private/tekken3-ram
curl --fail --silent --show-error \
  http://127.0.0.1:8080/api/v1/cpu/ram/raw \
  -o /private/tekken3-ram/00000.bin
# Advance EXACTLY one emulated frame, pause, then save 00001.bin, etc.
python3 scripts/psx_ram_oracle.py \
  --output /private/tekken3-usa.psx-combat.jsonl \
  /private/tekken3-ram/*.bin
```

Each input must be exactly 2,097,152 RAM bytes, `gameState=8`,
both fighters valid, and subsequent guest counters exactly +1
(modulo unsigned-32 wrap). Files are sorted by filename.
Any invalid frame aborts without replacing an existing output.
This decoder is also useful to independently check a Lua-captured
specific frame's unsigned/signed field decoding.

## Option C: CPU phase boundaries and real-contact native audit

The interpreter with `-debugger` supports non-pausing, read-only Exec callbacks.
The observer hooks entry and the saved return PC (with matching stack pointer)
of six original functions. No guest code, RAM or register is changed. Retained
Lua objects keep callbacks alive. The final FightFrame return pauses execution.

```sh
python3 scripts/prepare_psx_phase_capture.py \
  --exe assets/imported/tekken3-usa/SLUS_004.02 \
  --output assets/imported/ps1-paul-hit-phases \
  --first 650 --last 2300 --hit-ram
SDL_AUDIODRIVER=dummy "$HOME/dev/pcsx-redux/pcsx-redux" \
  -no-ui -interpreter -debugger -softgpu \
  -bios "$HOME/dev/pcsx-redux/src/mips/openbios/openbios.bin" \
  -iso "$PWD/roms/Tekken 3 USA/Tekken 3 (USA).cue" \
  -run -webserver -webserver-port 8082 \
  -dofile "$PWD/scripts/pcsx_redux_paul_session.lua" \
  -dofile "$PWD/assets/imported/ps1-paul-hit-phases/capture.lua"
```

After the observer reports its final return, require `running=false` from
`/api/v1/execution-flow`, save `/api/v1/cpu/ram/raw` as `paused-ram.bin` and
`/api/v1/lua/t3-latest-phase` as `paused-phase.jsonl` in the capture directory.
The latter must equal the final trace row. Preserve the loaded observer,
pad-session script, screenshot, emulator log and input/output hashes with the
generated provenance manifest; do not replace scripts during an active run.

```sh
python3 scripts/analyze_psx_phases.py \
  assets/imported/ps1-paul-hit-phases/phases.jsonl \
  --ram assets/imported/ps1-paul-hit-phases/paused-ram.bin \
  --output assets/imported/ps1-paul-hit-phases/audit.json
cmake --build build/host --target t3_hit_ram_eval
python3 scripts/audit_psx_hit_ram.py \
  --capture assets/imported/ps1-paul-hit-phases \
  --evaluator build/host/t3_hit_ram_eval \
  --exe assets/imported/tekken3-usa/SLUS_004.02
```

With `--hit-ram`, each actual `gotHit` HitApply call saves complete pre/post
2 MiB RAM snapshots. The native evaluator feeds **pre-hit** facts and original
tables into the portable `t3_psx_hit_apply_preview`, rather than recomputing
damage in Python. The audit checks original resulting HP, slot classification,
damage, outcome flags and the reaction slot queued at the following MoveStepAll
return. It rejects missing snapshots and changed classification angle predicates;
it does not claim to reproduce the angle-refresh calculation itself.

The executed 650..2300 window contains 1,818 FightFrame calls, 167 repeated
guest counters, eight observed contacts and an independently verified final
RAM checkpoint. All eight native evaluations agree: high/mid/low hits and their
guards. This does not validate live collision, input buffering, freeze/hold
timing, KO collapse or a complete Saturn fight. The separate 4,732-observation
Vsync clip confirms original HP depletion and round result, while its sampled
`ko` byte remains zero. A round-ending pre/post HitApply sample is still needed.

The observer also records independent pose/root/move counters, step direction,
freeze/hold state, pending-move pointer, active state and the current move's
authored hold frame. The separate `ps1-paul-clock-phases/` capture independently
matches paused RAM. Audit the no-pending clock path with:

```sh
cmake --build build/host --target t3_clock_eval
python3 scripts/audit_psx_move_clock.py \
  --trace assets/imported/ps1-paul-clock-phases/phases.jsonl \
  --evaluator build/host/t3_clock_eval \
  --output assets/imported/ps1-paul-clock-phases/clock-audit.json
```

Its 3,498 normal forward cases agree; 138 pending starts are excluded, not
counted as passes. Freeze, holds and reverse playback did not occur in that
bounded real capture. Their 10,000-case differential test compares against
the pinned research with synthetic RAM, not against new original CPU captures.

The newer `ps1-paul-start-phases/` capture starts at source counter 547 and
records queued-entry parameters and current/root/pending move metadata as well.
Its 145 pending ground starts and 3,741 no-pending advances agree with native
clock results. The capture's final paused RAM is independently cross-checked.

```sh
python3 scripts/audit_psx_move_clock.py --pending \
  --trace assets/imported/ps1-paul-start-phases/phases.jsonl \
  --evaluator build/host/t3_clock_eval \
  --output assets/imported/ps1-paul-start-phases/start-audit.json
cc -std=c11 -O2 -Isrc/game -Ibuild/paul/generated/private_logic \
  tools/psx_move_decision_eval.c \
  build/paul/generated/private_logic/t3_local_divmot.c \
  build/host/libtekken3_fight.a -o build/host/t3_private_move_decision_eval
python3 scripts/audit_psx_move_decisions.py \
  --trace assets/imported/ps1-paul-start-phases/phases.jsonl \
  --evaluator build/host/t3_private_move_decision_eval \
  --output assets/imported/ps1-paul-start-phases/input-audit.json
```

That native input evaluator embeds the **private generated banks**, so its
binary stays under ignored `build/`. Each decision starts independently from
the observed running slot, clock, previous entry frame and reconstructed pad
history. All 3,760 compared decisions agree, including exact pending slot,
transition, kind, entry and window. The audit excludes 118 warmup observations
and eight non-input hit reactions. It proves those boundary decisions, not a
free-running simulation, physics or original HP on the Saturn.

The private runtime can enable `T3_ENABLE_PSX_MOVE_LIFECYCLE=ON` to use this
begin/queue path with one-based clocks. Its nine Ymir pose cases include mid,
low and low recovery, while provisional HP/physics remain explicitly separate.
End-turn orientation refresh, input-buffer mutation, original fighter physics,
reaction integration and complete supported-round replay remain required.

## Trace contract and what NOT to infer

Every tick contains exactly two records:

```json
{"tick":0,"fighter":0,"move_slot":3,"pose_frame":0,"health_q16":32768000,"ko":0,"source_frame":1234,"psx":{"state":2114,"move_frame":1,"hit_freeze":0,"recover_frames":0,"got_hit":0,"guarded":0,"hit_clean":0,"counter_hit":0,"in_air":0,"root_x":0,"root_y":0,"root_z":0,"facing":32768}}
```

This JSON is a **format example**, not a claimed observed frame.
`tick` is zero-based within this clip; `source_frame` preserves the
raw game counter. The `psx` object contains directly observed
struct fields, not synthesized combat state.

Crucially, the recorder **does not invent** `phase`,
`phase_frame`, `frames_left`, `combo_hits_received` or `hit_slot`:
those require separate interpretation, end-of-frame alignment and
independent oracle evidence. The existing
[`compare_psx_combat_traces.py`](../scripts/compare_psx_combat_traces.py)
compares only the known top-level fields, ignoring `source_frame` and
`psx` diagnostics. Other fields on the Saturn side must match the
same moment in a fight, with known identical starting state and inputs.

```sh
python3 scripts/compare_psx_combat_traces.py \
  --ps1 /private/tekken3-usa.psx-combat.jsonl \
  --saturn /private/tekken3-saturn.psx-combat.jsonl
```

A passing synthetic unit test demonstrates format or arithmetic only,
not authentic 1:1 parity. Keep all original retail-derived traces and
executable bytes private and gitignored.

## Report raw frame transitions before assigning original PS1 semantics

Once a **genuine** original-game trace has been captured, run:

```sh
python3 scripts/analyze_psx_combat_oracle.py \
  --ps1 /private/tekken3-usa.psx-combat.jsonl \
  --output /private/tekken3-usa-observations.json
```

This produces a JSON event timeline for both fighters: exact move-slot
changes, Q16 health deltas, KO flag edges, directly observed raw
`got_hit`/`guarded`/`hit_clean`/`counter_hit`/`in_air`/
`hit_freeze`/`recover_frames` changes, and consecutive intervals where
the same move slot and pose frame were recorded. A pose-static interval
**is not automatically hitstun** (it may be intentional holds, guard,
transition freeze, pause or a sampled blend). The script rejects frame
gaps, malformed diagnostic data and inconsistent paired snapshots.

Only after the events have been aligned to PS1 `FightFrame` stages
can frame durations and recovery/knockdown classifications be imported
as oracle-supplied facts into the Saturn MoveVM bridge.

## Grounded body separation and arena bounds

Use `prepare_psx_phase_capture.py --body-ram` for read-only entry/return
observations of BodySeparate and ArenaBounds. The compact range snapshots
include both fighter records, current/pending move rows, previous roots, pair
metrics and correction ramps. `audit_psx_body.py` checks every captured pair
through `t3_body_ram_eval`; it separately reconstructs body spheres from the
observed original joints and the fingerprinted private Paul radius table.
The final FightFrame return must agree with an independently decoded paused
2 MiB RAM snapshot using `analyze_psx_phases.py --ram`.

The 547..2300 capture has 5,829 matching calls, including 776 calls with actual
root corrections and zero body-shape discrepancies. This remains a component
audit seeded from the original each call; neither these counts nor sphere
reconstruction proves our free-running animated poses or complete combat.

## Exact world attack shapes and HitTest

`prepare_psx_phase_capture.py --contact-ram` adds CollisionShapesUpdate entry
and return observations plus compact snapshots around HitTest. It retains
current descriptor/packed points, previous anchors and segment ends, segment
mode and original joint matrices in addition to both fighter records. Body and
contact capture options can be combined; their global ranges are merged.

`audit_psx_contact.py` invokes `t3_contact_ram_eval` against every captured
function pair. Shape checks reconstruct current/previous baked world points
and the 14 hurt cylinders. Gate checks consume the pre-HitTest original shape,
compare the resulting slot/zone/point/direction and verify changed contact,
freeze/cooldown and whiff fields. The 547..2300 clip has 7,772 matching calls,
including eight accepted contacts. A paused independent RAM checkpoint also
passes. This is seeded component parity, not a free-running Saturn fight.

`saturn_contact_suite.py` separately exercises owned-state runtime geometry
with scripted pads. Its contact/gate frame counts do not reserve hit slots or
change HP; live damage and reactions require the complete hit transaction.

## Normal VS round scoring

`--round-ram` adds read-only `RoundFlow`, `RoundWins` and `WinPoseStart`
entry/return observations, typed round globals and compact RAM snapshots.
`--stop-round-results 2` pauses at the enclosing FightFrame return after two
RoundWins calls. To collect neutral draws, set
`T3_PAUL_SESSION_OPTIONS={neutral=true}` before loading
`pcsx_redux_paul_session.lua`, then load the generated capture launcher.
Both pads still navigate the original UI and select Paul; only fight input
is neutral. No guest RAM or register is edited.

```sh
python3 scripts/prepare_psx_phase_capture.py \
  --exe assets/imported/tekken3-usa/SLUS_004.02 \
  --output assets/imported/ps1-paul-draw-verified \
  --first 550 --last 6000 --round-ram --stop-round-results 2
# Run the session and capture launcher in PCSX-Redux as above, then fetch
# paused full RAM independently from /api/v1/cpu/ram/raw.
python3 scripts/analyze_psx_phases.py \
  assets/imported/ps1-paul-draw-verified/phases.jsonl \
  --ram assets/imported/ps1-paul-draw-verified/paused-ram.bin \
  --provenance assets/imported/ps1-paul-draw-verified/provenance.json \
  --output assets/imported/ps1-paul-draw-verified/audit.json
cmake --build build/host-public --target t3_round_score_eval
python3 scripts/audit_psx_round_score.py \
  --capture assets/imported/ps1-paul-draw-verified \
  --runner build/host-public/t3_round_score_eval \
  --out assets/imported/ps1-paul-draw-verified/native-score-audit.json
```

The audit checks normal VS mode 1, both fighter scores/health-left fractions,
all captured round descriptors, and both RAM edges before comparing the native
scorer's output. Missing fields, conflicting overlaps, truncated snapshots,
obsolete mapping descriptors and mismatched outputs fail the audit. The
preparer uses verified USA MatchResult struct offsets for winner/loser in
place of two incorrect isolated points in the pinned upstream layout. This
override is recorded in capture provenance. Older round captures without
this correction must be recaptured, not relabeled as verified.

The two neutral draws and the separate unequal-health timeout/KO encounter
pass four pre/post comparisons. This establishes the observed scoring
component; original intro, replay, victory pose and next-round sequencing
remain separate work. Retail observations stay ignored and private.

## Continuous owned-match replay from one original seed

Use `prepare_psx_phase_capture.py --frame-ram` for a separate private capture.
This writes one full 2 MiB RAM seed at a neutral FightFrame entry (standing slot
3, no pending row, identical pose/root rows, unpressed inputs/history), then
compact RAM ranges at every consecutive FightFrame return. Existing entry/exit
hooks retain the exact InputSource results and frame/call timing. The generated
launcher still checks the USA executable/layout fingerprints. The observer never
writes guest RAM. Keep seed, ranges, phases and screenshots private.

For the current controlled Paul session, use `--first 550 --last 3000`; retain
its launcher/observer hashes and a paused final RAM/phase cross-check with
`analyze_psx_phases.py`. Archive the actual observer used by the emulator as
`observer.lua` next to `provenance.json` before making further script edits.

Configure an explicit local-only native evaluator using the already generated
private Saturn assets:

```sh
cmake -S . -B build/host \
  -DT3_PRIVATE_REPLAY_ASSET_DIR="$PWD/build/paul-combat"
cmake --build build/host --target t3_match_replay_eval
python3 scripts/audit_psx_match_replay.py \
  --capture assets/imported/ps1-paul-frame-replay \
  --evaluator build/host/t3_match_replay_eval
```

Public CI leaves `T3_PRIVATE_REPLAY_ASSET_DIR` empty. The evaluator contains
private generated logic/character data and must not be distributed. It seeds
its own match once, then advances `t3_psx_match_step` with the observed inputs.
Expected RAM ranges are read solely to compare results, never to replace native
state. The auditor rejects missing consecutive snapshots and verifies native
tick continuity, seed/entry agreement and observer provenance. It reports
matching prefixes separately for ground, move, hit and world-joint fields;
any mismatch keeps overall parity false. Original scripted/clear input modes
that cannot be expressed by the pad adapter stop the requested replay explicitly,
rather than skipping those ticks or manufacturing neutral input.

Original world joints are indexed by animation slot. KMD mesh records have a
different order: mesh record 2 maps to joint 11, while mesh record 17 maps to
joint 2. The owned core now remaps before body/hurt/attack queries; rendering
uses the inverse model map at its boundary. Component tests supplied with
original joint arrays cannot detect a missing conversion in an integrated match.

To audit normal transition blending, generate the private Saturn assets with
`T3_LOCAL_PSX_RSQRT_TABLE` set to `psx_rsqrt.bin` from the fingerprinted pose-table
extractor, then point `T3_PRIVATE_REPLAY_ASSET_DIR` to that build (currently
`build/paul-blend`). The evaluator also seeds displayed local matrices, blend
counters, previous root displacement and retained frame bytes from the same
initial RAM. It compares mode/active/counter/frames/weight, displayed rootY,
root deltas and all previous/delta local matrix elements. Source/destination
frame bytes are compared too; row identities remain native bank indices rather
than relocated guest pointers. `blend_checked` records whether those checks
actually ran; an omitted table must not produce a claimed blend pass.

Use `--output assets/imported/paul-blend-replay` on the auditor to retain earlier
reports unchanged. The new report still fails overall when world geometry or
input-mode coverage differs, even if every checked blend field agrees. This
distinction matters: local matrices are saved before procedural head tracking,
and their agreement does not establish complete world-pose/body parity.

The mirror reference uses different Paul models: record 75 for P1 and 71 for
P2. Configure the private build with
`-DT3_LOCAL_ACTOR0_KMD="$PWD/assets/imported/tekken3-usa/raw/075.bin"` to import
P1's skeleton alongside the existing record-71 render archive. The generated
manifest retains both source hashes and states that the render mesh is shared.
The owned core requires each actor's parts explicitly; it does not patch a
coordinate or assume that mirror-match skeletons are identical. Without an
override the generator explicitly selects the primary model for both actors.
Rebuild the private generated assets and the native evaluator before auditing.
`body_checked` enables comparison of all eight sphere XYZ/radius fields and
three push components per actor; old evaluators cannot claim this coverage.

Head look-at consumes the previous world-joint frames. The evaluator therefore
seeds those frames along with the initial normal-camera head state from the
full RAM dump: USA `0x8009E578 + 20*actor` (JP `0x8009E920` in the fingerprinted
release layout). It rejects a seed outside camera phase 2. The head adjustment
runs after blend-retained matrices and before new world composition; P2 can
therefore observe P1's newly composed joints, as in the original sequential
actor order. World matrices are compared in every subsequent exit snapshot.
The older capture does not contain per-frame head globals, so this establishes
matrix-result parity rather than a separate scalar-head-state comparison.

The input adapter distinguishes an ordinary neutral direction mask (`1 << 9`)
from InputClear's raw `in_dir=5` with zero held/pressed buttons. The latter
requests `T3_PSX_INPUT_CLEAR`, an owned-core source-control flag, not a physical
button. It clears all 60 history entries at the post-physics input phase and
retains the raw-direction distinction until the next normal pad update. Thus a
neutral-direction branch cannot fire prematurely on the clear tick. The native
round-result hold uses the same path. `clear_input_frames` reports observed
clear requests per actor; other non-one-hot/button combinations still fail
closed. With this path the full 2,690-frame dataset executes, exposing original
victory/new-round transitions instead of hiding them behind an input boundary.

Profiling runs now archive `profile.elf` and an available `profile.map` beside
the PC samples. Retain those private files: rebuilding the same directory can
move functions, making later symbol attribution from a new ELF incorrect.

For a controlled render comparison, copy the private build configuration and
set `T3_RENDER_BENCH_TICK=120`. It advances the actual owned match to that tick,
then renders the frozen pose with a visible `RENDER BENCHMARK` label. Compare
command counts, decoded screenshot pixels and render/HUD work at the same pose.
The skin counter is included in render/HUD work. This benchmark deliberately
stops simulation, so it cannot establish simulation rate or playable cadence.
Keep the option at its default 0 for live input/round suites and CD encounters.
