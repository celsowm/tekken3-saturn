# Continuation plan: original Paul combat on Saturn

This plan continues [the playable milestone](NEXT_PLAYABLE_MILESTONE.md).
It replaces its historical execution order for the remaining work; the final
completion criteria still apply. This document plans work, not completed gates. Runtime movement and grounded
body/arena integration have since landed; current execution evidence and the
remaining pose/contact/timing gaps are in the progress record.

## Target

Deliver a private Paul mirror encounter in which original movement, three
verified attacks (high 295, mid 357, low 355), standing/crouching defense,
damage, reactions, KO and restart share one authoritative state. Both pads
must work. Demonstrate the encounter from CD boot in Mednafen and reproduce
its behavioral checkpoints in headless Ymir.

The implementation reference remains the pinned `third_party/Tekken3Remake`
submodule. PCSX-Redux observes the original; sibling `../lib-saturn` supplies
the Saturn runtime and external Ymir harness. Retail data and BIOS stay private.

## Resume execution plan (2026-10-17)

This section is the next execution order. The older numbered sections below
retain design rationale, not a list of work to repeat. Baseline commit is
`579815d`: 38 CTest checks, 91 Python checks and 20 Ymir combat cases passed;
the current image boots from CD. Scoring is verified, but round sequencing,
continuous comparison past tick 2,487 and the performance gate remain open.
No gameplay implementation is part of this planning update.

### A. Establish the round-transition contract

Start with the corrected `ps1-paul-score-verified/` and
`ps1-paul-draw-verified/` captures. Check which transition edges their valid
compact snapshots actually cover before launching another original session.
Use the fingerprinted USA executable and pinned `round_flow.gd`,
`round_state.gd` and `replay_state.gd` to build a transition table: entry
condition, call order, counter/timer changes, input ownership, fighter clock
policy, pose start and exit condition. Capture missing edges read-only.
Do not reuse the invalid round ranges from the historical captures.

Cover timeout, ordinary/perfect KO, a drawn round, next-round initialization
and terminal match result within normal two-player VS. Observe P2 as winner
too. Verify the replay mode/request and skip paths rather than assuming a KO
and timeout take the same path. The initial divergence is victory slot 3432
at native tick 2,488/source 2795; this is an investigation anchor, never a
hardcoded trigger. Round camera controls need observation where they gate
progress, even if exact camera rendering is outside the current comparison.

Deliverable: a documented transition table and independently checked RAM/JSON
evidence for each supported branch. Identify missing evidence explicitly.

### B. Implement original normal-VS round flow in the shared core

Replace the bounded hold/reset path with explicit observed states. Keep the
already verified scorer. Implement input lockout/scripted input, winner pose
selection, loser clock behavior, victory completion, next-round fighter reset,
intro and timer activation in source call order. Reset the required input,
blend, contact and reaction state; preserve match scores and configuration.
Use the same implementation in host replay and the Saturn application.

Budget replay storage before adding it: the reference fighter rings alone
contain `300 * 2 * 0x34 = 31,200` bytes, before bookkeeping. The current image
has 961,780 initialized bytes against a 983,040-byte loader limit. Decide
placement from the linker map and measured stack/Low WRAM headroom; preserve
the existing 16 KiB stack reserve and image assertions. `t3_psx_match_step`
currently copies the complete match for atomic updates. Keep a large history
buffer outside that per-tick copy and commit ring mutations only after a
successful tick. Do not add a RAM-cartridge requirement by default.

Implement the observed replay playback/restore path when required by this
encounter, including the half-speed pose/root behavior. A user-accessible skip
branch may be validated separately; it must not stand in for an unimplemented
default path. Do not require unrelated modes, audio or effects to close this
bounded gameplay comparison; report their coverage separately.

Deliverable: host state-transition regressions, paired original component
audits and an initial Saturn victory/next-round checkpoint. Synthetic cases
must exercise ordering, off-by-one boundaries and state restoration rather
than repeat constants from the implementation.

### C. Close uninterrupted original/host/Saturn comparison

Extend the existing replay seed to initialize round globals and configuration
once. The current adapter does not enable original round flow. Add comparisons
for round state, counter, timer, score, result and relevant replay state, while
retaining every existing fighter comparison. Extend capture ranges only from
verified USA addresses and instruction widths.

Execute all 2,690 existing captured ticks and retain all divergences. Require
the previously matching 2,487-tick prefix to remain intact, then pass the
victory and next-round boundary. Extend the trace through the later intro's
return to FIGHT and an accepted player command. Separately cover KO and the
two-draw terminal result. Original InputSource outputs can help diagnose a
component; end-to-end validation must distinguish external pad input from
internally generated/cleared input and let native round flow own the latter.

Compare Saturn at the same simulation checkpoints with the same initialization
and external pad schedule. Never reseed fighters, copy expected return state
into the simulation or truncate a failing tail to claim parity. Archive exact
comparison coverage, first mismatch and artifact hashes.

Deliverable: passing continuous comparisons for the supported branches, plus
the complete Ymir combat suite updated for original transition timings. Keep
emulator frame numbers separate from simulation ticks when updating schedules.

### D. Reach the measured 30 Hz render / 60 Hz logic budget

Take a fresh live baseline after round integration. Measure intervals across
multiple checkpoints, including moving/hitting poses and round transitions;
separate logic, pose/skin, scene/HUD, waits, submissions and presentation where
observable. The previous live interval was 8.1 submissions/s and 32.4 logic
steps/s. Rendering/skin samples overlap and cannot be added as separate costs.

Optimize the largest measured costs first, with one attributable change at a
time. Inspect repeated pose/matrix work, state copying and dense face/HUD work
before considering Slave SH-2 scheduling. Generic fixes belong in sibling
LibSaturn, with library checks and a rebuilt installed SDK. Use the labeled
fixed-pose benchmark only for controlled render comparisons; final evidence
must use `T3_RENDER_BENCH_TICK=0`, live inputs and both animated actors.

Require sustained 60 logic steps/s and at least 30 scene submissions/s over
measured intervals at nominal 60 Hz VBlank, with bounded input-to-state delay,
no discarded accumulated ticks, and presentation cadence checked separately.
Increasing the four-tick catch-up cap alone does not pass. Preserve recognizable
authored models, memory limits, command capacity and zero draw timeouts. If
this remains unmet, publish the measured shortfall and leave the gate open.
Emulator measurements do not certify real hardware performance.

### E. Validate and publish the complete encounter

On the final live build, run the relevant host/Python checks, full Ymir combat
suite, image/memory validation and affected library checks. Record actual
Mednafen CD boot through movement, high/mid/low, standing/crouching defense,
KO, victory and a controllable next round. Retain a terminal match/draw check
and reset/two-pad evidence. A neutral boot alone does not satisfy this gate.

Bind video, screenshots, diagnostics, input schedule and performance results
to the same final ELF/app/disc hashes and repository/SDK/emulator revisions.
Commit and push each coherent passing implementation increment, checking its
CI result. Keep retail assets, BIOS and private captures out of Git. Update
the progress record with passed gates and unresolved mismatches; mark the
milestone complete only after its behavior, continuous comparison, performance
and final CD encounter gates pass. Do not expand the roster or install Godot
unless a concrete remaining dependency makes it useful.

## Current checkpoint and immediate gap

Owned movement, clocks, world pose, body/arena and exact contact are integrated.
`build/paul-combat` now also applies bounded original damage, guard, reactions and
immediate USA VS KO collapse. The normal frontend routes it through the owned
core and only copies presentation data to the old match structure. It does not
call provisional `t3_match_step` in this mode.

The native owned core now also runs from one original neutral seed without
replacing fighter state between ticks. Its checked move/clock/queued-branch and
hit fields match for 2,487 ticks, including eight HP changes. This exposed and
fixed a KMD mesh-order versus original joint-slot mismatch in body/contact
integration. Overall replay is still incomplete at the original victory-pose boundary.
The newer `build/paul-blend` ports
all three normal transition blends and fixes a 32-bit spline endpoint coefficient
that had been truncated to 16 bits. All checked blend scalars, previous local
matrices and matrix deltas now match for both actors across all 2,396 ticks.
Per-actor skeleton selection now eliminates the subsequent placement discrepancy:
ground, all joint origins and body-sphere/push fields match all 2,396 ticks.
Original head tracking subsequently resolves all world-matrix differences too.
InputClear now preserves its raw direction value 5 separately from a normal
neutral pad. All 2,690 captured ticks execute, with all checked categories
matching through tick 2,487. At tick 2,488/source 2795 the original starts
victory pose 3432 and freezes the loser; later it restores HP for a new round.
The bounded native round policy does not reproduce those original transitions,
so the full replay remains failed rather than truncating that evidence.

The corrected order passes the short interactive combat cases and a controlled
pad encounter through guards, a mid hit, lows and KO. A matching, visibly labeled
input replay boots from CD in Mednafen through result and next-round restoration.
Its ELF/app hashes match the aligned Ymir replay. Normal VS scoring now follows
the pinned RoundWins/MatchResult implementation, including points for both
players on draws, the decreasing round cap and configured tie selection.
The original's two neutral draws were independently captured and agree with
the native scorer. The live default timer is now the observed 2,400 ticks.
Intro/replay/win poses and round-transition sequencing remain provisional.

Remaining execution order: investigate original round-flow transitions;
repeat continuous host/Saturn/PS1 comparisons; optimize scene/HUD and tick pacing.
Preserve the verified corrected CD encounter. The 30 Hz render / 60 Hz logic gate
still fails at approximately 8.4 reported work loops/s and 316 logic ticks per 600 emulator
frames. The [progress record](NEXT_PLAYABLE_PROGRESS.md) holds scoped evidence.
The newest private `build/paul-memory` uses aligned RAM-copy/fill and dense
projected-face preparation improvements in LibSaturn. Its 600/1200-frame
interval yields 8.1 submissions/s and 32.4 logic steps/s. It retains the same
authored models/tables and original match core. `render_work_ms` includes skin
work; do not add the skin counter again. The fixed-pose `T3_RENDER_BENCH_TICK`
configuration is explicitly labeled and must remain 0 in playable validation.

`T3_LOCAL_ACTOR0_KMD` optionally imports P1's original skeleton independently
from the shared render archive. The current private configuration uses record
75 for P1's skeleton and record 71 for P2's, matching the captured mirror match.
The displayed meshes/textures still share record 71; this is not a complete
second-costume renderer. Both skeletons must have the same authored scale.

The numbered sections below retain the implementation rationale; use the
[progress record](NEXT_PLAYABLE_PROGRESS.md) for what has actually passed.

## 1. Close the owned-state integration

Create a separate `build/paul-owned` configuration with
`T3_ENABLE_PSX_OWNED_MATCH=ON`, preserving the existing comparison build.
Regenerate and embed the verified ground tables. Route this mode through
`t3_psx_match_step`; expose a presentation snapshot for HUD/camera instead of
calling provisional movement or damage. Use neutral P2 when disconnected until
an explicit original-mode training/CPU policy exists.

Make rendering consume the same root, heading and pose as simulation. Remove
the extra per-actor translation/rotation currently applied to model-local
preview poses. Check reanchoring, root-clock ownership, scale and Z conversion;
diagnostics must expose activation, original root/anchor, clocks and Q16 HP.
Unsupported transitions must produce an explicit diagnostic rather than silently
substituting prototype behavior. Update adaptation notices for the new files.

Gate: neutral, approach, retreat, release/repress, two pads and reset execute
in Ymir without a second simulation advancing underneath. HP remains unchanged
at this checkpoint. Build the CD and verify it boots in Mednafen. Rerun host
checks and the separate baseline suite after completing this wiring.

## 2. Establish usable timing before expanding combat

Profile the owned build with the matching ELF: simulation, pose/skin work,
scene submission, VDP1 waits, command peak and memory residency. Attribute the
largest costs before changing architecture. Avoid repeated evaluation and
validation within one simulation/render step; evaluate reduced draw detail or
texture/face work only against measured costs and recognizable-model captures.
Put generic library fixes in sibling LibSaturn, verify them there, rebuild the
installed package, then rerun this project.

Target 30 Hz rendering first with 60 Hz logic. Require sustained simulation
progress without discarding ticks, measured submissions/cadence and bounded
input latency. A larger catch-up cap alone is not a performance fix. If this
target remains unmet, retain the measured shortfall and continue correctness
work, but do not call the playable milestone complete. Recheck timing after
contact and reactions are integrated; emulator timing is not hardware proof.

## 3. Close world geometry and original contact

Verify the frame order of root placement, pose composition, separation, arena
constraints and hit processing against the pinned implementation and original
entry/return observations. Port the required grounded body-separation and arena
rules. Feed hit/hurt geometry from the authoritative world pose, including any
blending needed by these moves. Remove reliance on externally injected roots
for this path. Audit initial placement and every tick of short approach clips.

Gate: actors stop/separate correctly, do not drift through each other, and the
high/mid/low contact decisions agree with original observations. Cover misses,
both sides and standing/crouching defense; matching damage arithmetic alone
cannot pass this gate.

## 4. Activate a complete combat transaction

Connect verified contact gates, attack continuity, damage arithmetic, guard
selection, reaction selection and queued reaction starts to owned HP/physics.
Implement the required recovery, pushback and freeze lifecycle. Preserve source
ordering for simultaneous attacks and prevent repeated damage from one contact.
List the supported commands and fail/report uncovered transitions explicitly.

Gate: scripted high/mid/low hits and standing/crouching guards produce the
expected Q16 HP, reaction slot and recovery. Held attacks cannot manufacture
extra hits; a valid release/repress can attack again. Host and Saturn consume
the same inputs and initial state. Neither uses provisional combat constants.

## 5. Complete KO, round flow and replay validation

Capture an original pre/post round-ending hit at instruction boundaries; the
existing Vsync HP-zero/result clip does not establish exact KO timing. Integrate
the necessary KO reaction, input lockout, round result and reset. Any temporary
round policy must be explicit and cannot be described as original parity.

Build a short free-running replay from verified initialization and pad inputs.
Compare at aligned FightFrame-return checkpoints; report the first divergence
in move, pose/root clocks, placement, HP or reaction. Fix divergences instead
of reseeding each tick. Keep component audits as separate evidence.

Gate: one supported encounter reaches KO/result and restarts; both pad roles
work. The bounded replay agrees for its declared coverage, with unsupported
mechanics and any timing differences listed.

## 6. Final executable evidence

Run a dedicated original-combat Ymir suite for movement, misses, three attacks,
defenses, repeated input, simultaneous contact, two pads, KO and reset. Retain
the procedural suite separately. Record a Mednafen CD session covering the
supported encounter through round completion, using the exact validated build.

Deliver build/data/emulator hashes, decoded observations, private screenshots
and video, memory/texture/command budgets, timing measurements and remaining
mismatches. No draw timeout or memory/command overflow is acceptable. Compilation,
injected execution and real CD boot remain distinct checks.

## Scope and first delivery

The first reviewable delivery is gates 1 and 2: authoritative original movement
visible on Saturn, reproducible two-pad/reset scenarios, a fresh CD boot and an
attributed performance profile with measured improvements. It is a checkpoint
toward the full encounter, not completion of the milestone.

Do not expand roster, menus, stages, audio, throws or juggles during this slice.
Implement additional mechanics only when required by the supported encounter.
No calendar estimate is assigned before the runtime and profiling gates expose
the remaining integration cost.
