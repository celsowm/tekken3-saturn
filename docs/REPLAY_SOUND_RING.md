# Original sound replay ring

Checkpoint: 2026-10-27, HEAD `a5aa52f-dirty`.

`src/game/psx_replay_sounds.c` now implements the original 300-entry sound
ring as a portable component. It is not yet connected to `psx_match.c` or
native playback. The completed release gate tests the unchanged playable
ELF `79c0324fd4d85d25f9eefae00acbd38a63178bfcd6ebd5e04bfda3413eb54dbe`.

## Rules and proof

Sources: pinned Tekken3Remake `remake/core/fight/replay_state.gd`,
`docs/research/code/sound.md`, and original Japan Rev.1 functions
`0x80033780` (record), `0x80033838` (advance/clear) and `0x800338E0`
(playback). The adaptation carries the upstream notice in the file and
`NOTICE.md`; no GDScript or game data is copied into the repository.

Only recording mode 7 stores requests. Each request occupies
`(record + sound_delay) % 300`, then increments the delay. Multiple requests
in one tick therefore spread across later frame slots. At the start of each
recording tick, after the frame cursor advances, positive delay decreases by
one and the next free slot's kind byte is cleared. Code/player bytes remain
unchanged. A round reset resets delay but preserves the allocated ring.
Playback dispatches kinds 1/2/3 on even half-frames only; unknown/zero kinds
and odd half-frames do nothing. System dispatch ignores the stored player.
Voice-off notifications are not recorded by FighterSounds' consumer.

The component accepts caller-owned numeric ring words and explicit cursors.
The caller selects the bank; the proof currently covers normal VS bank 0.
Playback returns raw logical sound/voice codes. Audio resolution, echo effects,
channel stopping, volume and sample playback remain consumer work.

`tests/test_psx_replay_sounds.py` executes all three original MIPS functions
without FightHarness presentation patches. Only their audio entry points are
stubbed to observe dispatch. It compares the delay, dispatch and **all 300
ring words after each of 12,647 operations** (3,794,100 word comparisons):
every frame and half-frame cursor, all replay modes, known/unknown kinds,
byte/word truncation, stale bytes, delays around ring boundaries, bursts,
backlog draining and continuous wrapping. All operations match.

Both complete host suites pass after adding the test: **74/74 Paul–Law**,
**65/65 Paul**. Logs:
`build/parity-replay-sounds-differential.log`,
`build/parity-replay-sounds-host-law-tests.log` and
`build/parity-replay-sounds-host-tests.log`.

```sh
cmake --build build/host-law
ctest --test-dir build/host-law -V -R '^ps1_original_replay_sounds$'
```

## Audio resource measurement

The private bank inventory in `build/parity-replay-audio-budget.json` measures
complete combat/system/Paul/Law banks using the pinned verified ADPCM decoder.
It preserves the original zero lead-in, writes no audio and reports payload
size before driver/allocator overhead.

| Bank | VAGs | ADPCM bytes | PCM16 bytes | PCM8 bytes |
| --- | ---: | ---: | ---: | ---: |
| Combat | 46 | 248,240 | 866,264 | 433,132 |
| System | 25 | 99,936 | 348,376 | 174,188 |
| Paul | 18 | 70,320 | 245,112 | 122,556 |
| Law | 18 | 62,640 | 218,232 | 109,116 |
| Total | 107 | 481,136 | 1,677,984 | 838,992 |

No decoded PCM16 waveform is duplicated across these banks, and none loops.
These are complete-bank numbers, not a reachable-sample census. Both PCM16
and PCM8 exceed the SCSP's entire 524,288-byte RAM, before its driver and
streams. The existing fixed subset is not full audio coverage. Expanding
that subset to a few more samples would not establish original playback.

```sh
python3 scripts/audit_psx_audio_budget.py --build build/parity-reversal
```

The inventory fingerprints the USA executable against the pinned layout,
reads the same voice resources as the candidate and records bank hashes.
It is a size audit, not a decoded-sample equality or audible-fidelity test.
The installed SDK offers caller-backed PCM streams, but their documented
buffered startup is not evidence of correctly timed combat SFX.

## Integration still required

1. Stage the original ordered requests inside the simulation tick and commit
   them only after a successful step. Store the ring outside per-tick match
   copies, alongside replay history; advance/clear and delayed writes must
   commit atomically. Preserve round-reset and half-frame call order.
2. Extend original-code traces through KO/replay and compare sound delay,
   changed ring entries and ordered playback requests continuously. Keep the
   existing camera/combat/input comparisons and forbid state feedback.
3. Resolve system/effect/voice codes with original volume, voice allocation,
   stop/retrigger and echo rules. Replace the current per-image heuristic
   `combat_events.c`/`sfx.c` dispatch once that path is proven.
4. Measure the reachable working set and implement a loading/decoding strategy
   that fits stock RAM without dropping original requests. Preserve waveform
   timing and account explicitly for any unavoidable Saturn audio differences.
5. Test the committed native queue across multiple simulation ticks per
   displayed frame and slave/master publication, then validate actual audio.

The new component adds no runtime state to the current playable match and
makes no native playback, full release, CD boot or cadence claim. The full
26-case/3,000-tick release gate completed successfully under
`assets/imported/parity-owned-camera-gate/`. The subsequent varied-input
retail fight reveals missing effect-object RNG; its failure and the spark
phase port are recorded in [HIT_SPARK_PROGRESS.md](HIT_SPARK_PROGRESS.md).
No SDK copy or recording was created. Disk budget: 1.4 GB build / 4.4 GB imports.
