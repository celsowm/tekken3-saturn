# Original fighter sound decisions and shared RNG

Checkpoint: 2026-10-26. This ports the simulation decisions of `FighterSounds`
(`0x80041138`), not the complete Saturn audio playback/replay path.
The following audit in [RETAIL_CAMERA_PARITY.md](RETAIL_CAMERA_PARITY.md)
corrects the oracle policy: FightHarness had presentation patches enabled.
The frame-320 gap below is historical; new retail escape/reversal/crossing
fixtures now pass completely after the InputSource side-order correction.
The isolated 2,878-case sound proof uses unpatched `load_release()` and
remains valid.
The next component proof is [REPLAY_SOUND_RING.md](REPLAY_SOUND_RING.md):
12,647 original MIPS operations match the portable sound recorder, clearing
and half-frame dispatch. Match integration and native playback remain open.

Sources are pinned Tekken3Remake `docs/research/code/sound.md`,
`remake/core/fighter/fighter_sounds.gd` and
`tools/remake_import/tables.py`, checked against original Japan Rev.1 MIPS.
Adaptations are attributed in the C source and `NOTICE.md`.

## Implementation

`src/game/psx_fighter_sounds.c` owns voice cooldowns, damage-voice timers,
sound script id/last id/position/frame, hold flags and the original shared
`rand()` draws. It executes KO voices, the move's first three sound commands,
impact/guard choices, Gun Jack/Mokujin handlers, specials, scripts and ambient
damage/attack voices, jump/landing and swing requests. Its callback emits the
original ordered replay event types and sound/voice ids without playing audio.

`psx_match.c` calls it for active fighters after HitApply and before
InputSource. It retains cooldowns across round placement, stops scripts at
placement and the normal result/replay transitions, and stops the throw
victim's old script when the original thrown-start rule does. Fresh match
configuration supplies each fighter's voice set explicitly.

The original frame-271 discrepancy was the attack-shout probability test
(return address `0x80041A0C`) and selected shout (`0x80041A3C`). These now
execute through the sound rules, rather than a fixed RNG adjustment.

The native match currently discards the new sound requests; the existing
`combat_events.c`/SFX playback remains in use. A committed event queue and
replay/audio bridge are still required. The production binding currently
targets Paul/Law: other characters still need voice metadata, wooden flags
and the actual VBlank input for Gun Jack's impact selection. The independent
sound routine accepts these inputs and tests their rules; using the fight
frame as VBlank in the present pair does not qualify Gun Jack playback.

## Private data and memory

`scripts/extract_psx_sound_tables.py` fingerprints the USA executable and
normalizes the 23 voice sets, 470 sound scripts and auxiliary sound choices
to an offset-based `T3SF` blob. With `--rev1`, the complete **15,378-byte**
normalized result is byte-identical to Japan Rev.1:
SHA-256 `360d53c1b2ab8af472d5352129b1c8eb5b3beeb8ef5b9fc7cf8de388c5333a1b`.
The imported blob stays at
`assets/imported/tekken3-usa/pose-tables/psx_sounds.bin`.

`divmot_to_logic.py --with-sounds` preserves section-3 command semantics,
interning identical executed lists and relocating each move's sound index.
Paul/Law/common command storage shrinks from **1,380/1,700/288** bytes to
**230/182/50** bytes. The local asset generator keeps scripts reachable from
those banks, preserving original script ids in the directory. Missing scripts
have invalid offsets and fail on accidental use. No sound script starts
another script. The candidate's embedded tables take **5,062 bytes**.

The first build with uncompressed commands/tables exceeded Low WRAM and the
High WRAM stack margin. List interning, retaining reachable scripts and moving
the master-only CDFS mount record to High WRAM resolved both linker failures.
Final map: `__wram_l_end=0x002FF794` (below the `0x002FFE00` reserved boundary),
`__bss_end=0x060FB6F0`, with the original **16 KiB** master stack reservation.
No cache capacities or stack reservation were reduced. No extra SDK was made.

## Evidence

| Check | Result | Scope |
| --- | --- | --- |
| `ps1_original_fighter_sounds` | **2,878** original MIPS cases | Sound state, RNG and ordered replay events; all 23 voice sets and all 470 scripts, cooldowns, signed timers, impacts, specials and mixed commands |
| Original/compacted banks | **1,163** move lists identical | Every move in the three private raw/compact banks, including list relocation |
| `ps1_original_owned_camera_rng` | **600/600** ticks | Fresh neutral Paul/Law, owned camera, all captured camera/RNG/eye/sound/combat/round/HUD/input fields; sources 2–601 |
| `ps1_original_owned_sound_escape` | **460/460** component ticks | Sound script/timer/hold state, eyes and all three RNGs during attack/throw escape; complete camera audit still fails |
| Host suites | **71/71** Paul–Law, **63/63** Paul | Current CTests, including the older 6,600-tick camera air/timeout matrix coverage |
| Native Ymir neutral | **600 fields / 196 simulation ticks** | FIGHT reached, HP 140/140, zero runtime errors, draw timeouts, decode errors or rejected faces; 750/2048 commands |
| Native Ymir high hit | **920 fields / 516 simulation ticks** | Paul hits Law once: HP 140/134, no blocks, runtime/draw/decode/rejected-face counters zero; fixed physical input schedule retained |

The isolated MIPS sound test stubs playback and VOICE_OFF; it compares the
ordered replay requests and resulting sound/RNG state, not rendered samples.
The fresh escape case retains its entire failing camera tail. First overall
difference is source **320**, source-3 Z `-5114` versus `-5113`, and view
translation Z `5477` versus `5476`. Later view/side differences remain.
All 460 sound/eye/RNG comparisons pass without filtering source ticks or
dropping unsupported moves. This is component coverage, not a passing full
escape/cinematic comparison.

Current production candidate: `build/parity-reversal`, ELF SHA-256
`a071c5d4f68bdfc2554c78ec41ae27dbb78bfbbd1f86cded13597e06f9b8a5b4`;
initialized payload **588,508 bytes**. The old 26-case/3,000-tick full release
gate qualifies its historical ELF, not this one. The present Ymir test uses
injected execution; no new CD boot, audio fidelity, cadence or hardware
certification is claimed.

Private evidence: `build/parity-sounds-{neutral,escape}/`,
`build/parity-sounds-differential.log`, `build/parity-sounds-host-tests.log`,
`build/parity-sounds-host-paul-tests.log`,
`build/parity-sounds-saturn-build.log`,
`assets/imported/parity-sounds-neutral-saturn/observation.json`,
`assets/imported/parity-sounds-high-hit-saturn/observation.json` and its
`input.devices`, plus `build/parity-sounds-summary.json`.
`native-camera.jsonl` retains all comparisons, including failures.

## Reproduce

```sh
python3 scripts/extract_psx_sound_tables.py \
  --rev1 third_party/Tekken3Remake/work/jp_rev1/exe.bin
# Import the local raw banks with --with-sounds as well as events/motion/hitboxes;
# regenerate the private logic bindings with --sound-tables psx_sounds.bin.
cmake --build build/host-law
ctest --test-dir build/host-law --output-on-failure
ctest --test-dir build/host-law -V \
  -R '^ps1_original_(fighter_sounds|owned_sound_escape|owned_camera_rng)$'
python3 scripts/fight_oracle_eval.py build/parity-sounds-escape \
  --evaluator build/host-law/t3_match_replay_camera_eval \
  --initialize-match --independent-camera
```

The last command intentionally exits nonzero for the remaining camera
difference. Resolve that first divergence, rerun the throw/reversal camera
fixtures, then expand the original sound audit through KO and replay while
connecting committed requests to native audio.
