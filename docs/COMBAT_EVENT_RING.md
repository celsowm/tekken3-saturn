# Per-tick combat event ring (2026-10-29)

Package 4 of [ONE_TO_ONE_CONTINUATION.md](ONE_TO_ONE_CONTINUATION.md).
Sound and spark events were derived from counters once per submitted image
(about 30 Hz), so two hits or a short swing between images merged or vanished.

## Implementation

`src/saturn/combat_events.[ch]` keeps a 32-entry ring. `t3_combat_events_tick`
runs after every simulation tick on the CPU that ran it: the master loop and
`t3_simulation_task_body` on the Slave. Each event records the tick, fighter,
kind (HIT, BLOCK, SWING; a block outranks a hit on one tick), the attacker's
descriptor joint and the hitting segment end. Ring state and the edge-detector
state live in `T3_HOT_WRAM`, like the owned match. A full ring drops the newest
event and counts it. The displaying CPU pops the ring once per image while no
simulation job is in flight.

`sfx.c` plays one sound per event; the hurt-voice cooldown is the original's
40 ticks (it was 20 images). Sparks take the position captured on the tick of
the hit, not the state at the next image. The old poll function is removed.
Voice picks still cycle: the owned match does not yet carry the original's
sound RNG, and the portable `psx_fighter_sounds` decisions are not connected.

## Evidence

Probe counters `event_overflow/hits/blocks/swings/popped` are dumped by
`scripts/saturn_probe.py` (`event-*.bin`).

- Candidate ELF `3d80fd31…47928`, `build/evring-cand`, with
  `T3_LOCAL_POSE_CACHE_ENTRIES=64`: the working tree no longer links at 128
  (High WRAM exceeds the 16 KB stack reserve) or 96 (2.2 KB over). The ring
  itself costs under 1 KB.
- 26 live Ymir cases pass (`assets/imported/evring-suite`). In all 68 captures
  overflow is 0 and popped == hits + blocks + swings. Counts match the
  scenarios: high/mid/low hit 1 swing + 1 hit; miss 1 swing; standing and
  crouching guard 1 block; simultaneous 2 + 2; the 12-hit KO run 12 hits;
  reset clears the ring.
- 3,000/3,000 independent reference ticks pass on diagnostic ELF
  `44ccb8a2…91eeeb` (`assets/imported/evring-gate`), 177.5 s.

## Original sound requests (2026-10-29)

The portable `psx_fighter_sounds` decisions already ran inside the match (they
consume the shared RNG) but their output only fed voice-echo effects. The match
config now has an optional `sound_sink`; `t3_psx_match_effects_sound` forwards
every request in call order (voice ids already resolved to the character's
sound code). On Saturn the sink queues `T3_EVENT_SOUND` events in the same ring,
stamped with the tick that produced them. `sfx.c` then plays the sample
extracted for that exact code: effect codes (VAB 0), system codes (VAB 1) and
the firing player's voice codes. With the sink connected the old swing/hit/guard
and KO-voice heuristics are off; requests without a sample are counted
(`sfx_unmapped`, first distinct ones dumped as kind:player:code), never
approximated. Announcer calls still come from the round flow.

`scripts/extract_psx_sfx.py` now also extracts the effect tones the fight
requests (0x7041/43/44/45/48/59/64, 0x6049, 0x1053/54) and every distinct code
in each fighter's four voice lists, and writes a (bank, slot, code) key table.
Two LibSaturn limits had to rise (sibling repo, one prefix rebuilt in place):
`kSoundCapacity` 24 -> 64 and `kAllocationCapacity` 40 -> 64 (53 samples,
353,640 bytes of the ~436 KB resident sound RAM). These lib-saturn edits are
uncommitted in that repository.

Final gate (`assets/imported/evring-gate2`): 101 host tests, 26 live cases
(718 s) and 3,000/3,000 reference ticks (179 s). Playable ELF `748076b7…ac5`,
reference ELF `3caea326…7b62`, still `T3_LOCAL_POSE_CACHE_ENTRIES=64`.
In the live runs the event ring never overflowed and the only unmapped requests
were 0x104F (`throw-damage`, an empty tone in VAB 0, so no sample exists) and
0x7059 before it was added; the last full capture of the ko-victory path ran
before that addition, so its coverage is unchecked.

## Limits

Requests and sample choice are checked, not audible output or timing against
the original; the match's request order is not yet compared with the MIPS
trace at the Saturn boundary (the portable component is, 2,878 cases).
The host tick-by-tick comparison of event timelines, native replay-log
consumption, CD listening and the hardware cadence remain open. Uses a smaller
pose cache than earlier gates, so their cadence numbers do not carry over.
