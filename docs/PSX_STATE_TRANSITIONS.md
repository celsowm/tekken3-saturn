# Read-only PS1 post-hit transition harness

The new `psx_state_transition.[ch]` stage follows
`psx_combat_pipeline` without changing the playable demo.

## What this actually proves

* HitTest, HitClassify, HitDamage, HitApply and available reaction records
  are evaluated using the same starting snapshot for both fighters.
* An explicit oracle-supplied policy starts hitstun, blockstun or knockdown
  at reaction frame 0 using the genuine selected reaction move slot.
* Existing stun/knockdown phases count down deterministically and hand off
  to an explicitly supplied recovery move, then to the specified neutral
  move. A new reaction takes priority over countdown.
* Damage/KO and combo-count policy are tracked separately, and input states
  are never modified. Failed evaluation does not publish a partial output.
* Native synthetic regression cases exercise trades, knockdown, repeated
  ticks, recovery, missing timings/tables and mismatched health snapshots.

## Deliberately unknown

The original PS1 per-reaction frame durations, cancel windows, tech-roll,
ukemi, wake-up direction, real juggle mechanics, airborne reaction tables
and exact combo reset conditions have **not** been measured. The fields
`reaction_timing_known`, `recovery_known`, `neutral_known` and
`combo_known` require observed facts; they are not auto-filled.

`recovery_move_slot` and `neutral_move_slot` are supplied by the caller
only when their source transitions have been verified. There is no PS1
behavior parity claim. This harness does not commit changes to MoveVM or
to the playable Saturn fight loop.

## Next oracle capture

For each of standing hit, guarded hit, counterhit, grounded hit and
double-KO trade: capture both players' tick-indexed original move slot,
pose frame, HP (16.16), state flags, hit slots, combo count and KO bit.
Identify the earliest reaction tick, the final reaction tick and both
recovery/neutral destinations. Feed the verified durations/slots to this
harness, then diff each tick against the original emulator trace.

Run local native tests with:

```sh
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Private retail-derived records and traces must remain outside Git.

## Original DIVMOT MoveVM projection (read-only)

`src/game/psx_movevm_bridge.[ch]` composes the same-tick two-player
hit/reaction preview with two existing `t3_move_vm_t` snapshots. For every
fighter, it requires an **independently observed next pose frame** through
`t3_psx_pose_frame_fact_t`. The phase's `phase_frame` is NOT used as the
animation clock; stun timers and PS1 pose frames can diverge due to freeze,
cancels, or character-specific rules.

`t3_psx_movevm_project` resolves the selected move through the character
bank and, where indicated by the 0x40000000 slot namespace, the shared
`divmot99` bank. It refuses missing slots, stale source VM rows, frames
beyond the authored move length, and a saturated transition counter.
The paired bridge publishes neither fighter if EITHER fails. It refuses
KO transitions because the actual original KO victim move slot has not
yet been decoded.

The output contains *copies* of both projected `MoveVM` states, never
live game mutation. It does not run original branches, move events,
animation physics or hitstop; real in-game application remains gated
on PS1 oracle validation.

## Frame-by-frame PS1 versus Saturn comparator

`scripts/compare_psx_combat_traces.py` consumes two user-supplied private
JSONL captures. Each tick must contain exactly one fighter 0 and fighter 1
record. Use original PS1 emulator observations independently of the Saturn
projection: comparing the projection to itself is not parity evidence.

Required integer fields per fighter/frame:

```json
{"tick":0,"fighter":0,"move_slot":3,"pose_frame":0,"health_q16":32768000}
{"tick":0,"fighter":1,"move_slot":3,"pose_frame":0,"health_q16":32768000}
```

The tool also compares `phase`, `phase_frame`, `frames_left`,
`combo_hits_received`, `hit_slot` and `ko` when supplied. Optional
fields must be present on both sides for a given fighter/frame. A
mismatched value, missing fighter, duplicate frame, invalid integer or
noncontiguous tick interval is an error, not a silent skip.

```sh
python3 scripts/compare_psx_combat_traces.py \
    --ps1 /private/oracles/tekken3-usa-ps1.jsonl \
    --saturn /private/traces/tekken3-saturn.jsonl
```

Exit codes: 0 exact match, 1 mismatches, 2 invalid input. Unit tests
use synthetic frames only and are run in host CI. No real Tekken 3
trace has yet been captured or proven 1:1. Do not commit imported
original retail table bytes or private traces.
