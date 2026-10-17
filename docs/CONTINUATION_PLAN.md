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
Its ELF/app hashes match the aligned Ymir replay. The earlier first-to-two round
policy remains explicitly provisional; original intro/replay/win poses and draw
handling are omitted.

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
