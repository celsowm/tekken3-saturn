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
