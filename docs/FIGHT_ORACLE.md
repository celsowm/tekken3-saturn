# Original-code fight oracle

The owned match core (`src/game/psx_match.c`) is checked against fights run
by the original FightMain (Japan Rev.1 code). Those fights run in the
Tekken3Remake Unicorn harness (`third_party/Tekken3Remake/tools/research/fight_harness.py`,
PolyForm Noncommercial). The harness plays both sides with its random bots, so
each seed is a fresh, long fight with every kind of move. Before comparison,
the fighter records and globals are mapped to the USA layout through
`tools/remake_import/layouts/usa.json`.

Every case is derived from the game. Cases stay under `build/`. Do not commit
them.

## Tools

Run the scripts marked *venv* with
`third_party/Tekken3Remake/work/venv/bin/python`. Run the rest with
`python3`.

| Script | Purpose |
|---|---|
| `scripts/fight_oracle_export.py` (venv) | Export a case: `seed.bin`, `frame-N.ranges`, `cases.tsv`, `seed.json`. |
| `scripts/fight_oracle_eval.py` | Run `t3_match_replay_eval` on a case. Prints the matching prefix, the first divergence and the moves dropped as unsupported. |
| `scripts/build_fail_trace_eval.py` | Build an evaluator that prints the refusal chain. Defaults to `build/fail-trace/<target>`. `--target` selects the binary. |
| `scripts/fight_oracle_show.py` | Print fighter fields and captured globals per source frame. Takes named fields, `--offset name=0xOFF:fmt` or `--address name=0xADDR:fmt`. |
| `scripts/fight_oracle_watch.py` (venv) | Print every write to watched addresses (`F:0xOFF:size` or `ADDR:size`) during frames, with the PC and return address. `--at PC` prints the values when a PC runs, which is how to observe software-GTE stores. Takes about 3.5 minutes per run. |
| `scripts/dump_branch_rows.py` | Print a move slot's branch rows. |
| `scripts/move_record_words.py` | Print a move slot's index entry, damage, attack descriptor, active window and reactions. |

## Loop

```
third_party/Tekken3Remake/work/venv/bin/python scripts/fight_oracle_export.py \
  --out build/oracle-paul-sN --seed N --costumes 1 0 --round-time 2 --rounds 1 --ticks 3000
python3 scripts/build_fail_trace_eval.py
python3 scripts/fight_oracle_eval.py --trace build/oracle-paul-sN
```

To find the cause of a divergence:

1. Read the first failing row in `build/oracle-paul-sN/native.jsonl`.
2. Inspect the frames around it with `fight_oracle_show.py`.
3. If no rule explains the value, find the writer with `fight_oracle_watch.py`.
4. Look up the function in `third_party/Tekken3Remake/docs/research/code/function-index.md`.
5. Read the remake's port and the decompilation (`work/decomp/SLPS_013.00.c`).

## Rules ported through this loop

All addresses are Japan Rev.1.

- **PairwiseDistances (0x8004391C).**
  - A fighter is off screen when `(u16)screen_x >= 0x191`.
  - The fallback applies when the fighter has `fixed_facing` or the opponent is off screen. It uses distance `|180 − |180 − screen_x|| × 10`. The target is 0xC000 or 0x4000, depending on the fixed side.
  - Round placement (`FUN_8002BFCC`) sets screen x to ±100. The first frame therefore takes the fallback (1000).
  - MoveStep (0x800454E0) then restores the pair table distance.
- **SituationClassify (0x800313B0).** Throw situations come from:
  - the folded relative angle;
  - the opponent's state bits 0x80, 0x40, 0x20 and 0x2;
  - the opponent's quadrant.

  Its preconditions are contact, an opponent not in the air, and an opponent not about to hit.
- **Throws.** These rules come from `MoveStartAll`, `ThrowVictimStart`, the transitions 0x21 and 0x22 in `GroundStart`, the throw link (`FUN_80043260`) and the arena partner correction:
  - The victim starts first.
  - The thrower's pending move +0x32 picks the victim slot, through `g_throwVictimSlots` (0x800174C4) or the first reaction slot.
  - The thrower turns over 16 frames toward the quadrant heading. The victim takes heading −0x8000.
  - `throw_state` counts up for the thrower and down for the victim.
  - `no_push` is set while in a throw.
  - An arena correction moves both partners.
  - The blend's previous event frame is the pose before ThrowVictimStart restarts the victim at pose 1.
  - Round placement clears `in_throw` but keeps `throw_state`. The next move start clears it.
- **Round gap.** FightMain states 5 and 6 run no FightFrame. Placement happens on the next frame, at round frame 0.
- **Branch window and kind.**
  - The branch window (+0x6C) is one persistent field. MoveBranchStep (0x8002DC3C), which runs only when nothing is pending, resets it to −1, and a found branch sets it.
  - The branch kind (+0x8B) is one field too.
  - A reaction, a throw victim start and a round or intro start keep both fields, even after the branch's move has started.
- **Merged move index** (FighterSetupParts, `_link_banks`). An empty slot of a fighter's bank takes the opponent bank's entry, else the fallback slot 0xDC5. A throw victim's move therefore comes from the thrower's bank (`src/game/move_index.c`).
- **MoveTotalDamage.** A move's damage is its damage word plus the argument of every 0x0Bxx event.
- **Attack segment count (+0xB4).** At a move's start: 2 when the descriptor's second joint pair is set, 1 for the first, 0 without an active window. Throws use the 13 built-in descriptors (0x80017C10). Their four joint bytes, and the bytes after them that the game reads as points, come from a 0x400-byte EXE window at the end of `psx_reactions.bin`.
- **Win-pose draw.** RoundFlow's replay-done state animates and draws both fighters before CameraFrame, with the previous frame's fighter view. The draw moves their screen x, which the same frame's PairwiseDistances reads. Without a camera, the evaluator projects through the exported fighter view (0x800AE2E8) and H (0x800A919C).

## Results

All attacks enabled (the default; `T3_QUALIFIED_ATTACKS=1` restricts them), Paul mirror (costumes 1 and 0), with every compared field
equal on every frame:

| Case | Frames |
|---|---|
| `oracle-paul-1` | 600 / 600 |
| `oracle-paul-s2` | 1383 / 1383 (end of tape) |
| `oracle-paul-s3` | 3000 / 3000 |
| `oracle-paul-s4` | 3000 / 3000 (includes a KO and the next round's placement) |
| `oracle-paul-s5` | 2258 / 2258 (end of the match) |
| `oracle-paul-s6` | 3000 / 3000 (a KO during a throw) |
| `oracle-paul-s7` | 1391 / 1391 (end of the match) |
| `oracle-law-s1` | 3000 / 3000 (Paul costume 0 against Law costume 0; throws and a win-pose draw) |

### Paul against Law

A second bank needs its own asset build and host evaluator:

```
cmake -S . -B build/law-assets <Saturn options> -DT3_LOCAL_SECOND_DIVMOT=assets/imported/tekken3-usa/divmot/082/bank_1.t3logic \
  -DT3_LOCAL_ACTOR1_KMD=assets/imported/law-import/raw/<Law costume 0 KMD>
(cd build/law-assets && ninja generated/private_logic/t3_local_divmot.h)
cmake -S . -B build/host-law -DT3_PRIVATE_REPLAY_ASSET_DIR=build/law-assets <host options>
third_party/Tekken3Remake/work/venv/bin/python scripts/fight_oracle_export.py \
  --out build/oracle-law-sN --seed N --chars 0 1 --name paul_law --costumes 0 0 \
  --round-time 2 --rounds 1 --ticks 3000
python3 scripts/fight_oracle_eval.py build/oracle-law-sN --evaluator build/host-law/t3_match_replay_eval
```

The evaluator takes each fighter's bank from the seed's bank type (+0x16) and
character (+0x18), and links the two banks for the merged move index.

The Saturn combat suite (`scripts/saturn_combat_suite.py --with-ko
--with-timeout --sh2-cache on`, headless Ymir) passes all 24 cases with all
attacks enabled on `build/t30-law` (2026-10-23, including KO, replay, the
second round, match reset and two neutral timeouts). Its output is
`assets/imported/t30-law-combat/suite.json`.

### Original CPU opponent

`fight_oracle_export.py --cpu-mask` records each original `AiUpdate` call in
`ai.bin`. The host evaluator can substitute the C AI for the recorded pads:

```
T3_AI_TAPE=build/oracle-cpu-law-s1/ai.bin \
T3_AI_TABLES=assets/imported/tekken3-usa/pose-tables/psx_ai.bin \
python3 scripts/fight_oracle_eval.py build/oracle-cpu-law-s1 \
  --evaluator build/host-law/t3_match_replay_eval
```

The private table path must be the one selected by the build; it is not a
committed asset. The AI comparison checks generated held/pressed pads, the AI
memory carried to the next call, and the original fighter/global fields read at
entry. The evaluator takes the original camera random state at each call
because its host replay does not run CameraFrame. This verifies AI behavior at
the recorded calls and end-to-end match state, but does not establish that the
host independently reproduces camera RNG between calls.

| Case | Match ticks | AI calls | AI mismatches |
|---|---:|---:|---:|
| `oracle-cpu-law-s1` | 1500 / 1500 | 1500 | 0 |
| `oracle-cpu-law-s2` | 1500 / 1500 | 1500 | 0 |
| `oracle-cpu-both-s3` | 1500 / 1500 | 2124 | 0 |
| `oracle-cpu-paul-s2` | 859 / 859 | 230 | 0 |

The last case ends with the original tape. These results use the correct
character banks: the Law cases use `build/host-law`, and the Paul mirror uses
`build/host`.

The `build/t30-law` Saturn image was also run in headless Ymir for 1,800
emulator fields with port 2 disconnected and SH-2 cache emulation enabled.
Its `t30-law-ai-probe/observation.json` reports 50 P2 move transitions,
two registered P2 hits, four P2 blocks and P1 health at 93/140, so the CPU
opponent acts and makes contact in the live game. The run reports no runtime
error, draw timeout, rejected face, unsupported move or attack decode error.
It reaches only 1,076 simulation ticks, discards 382 elapsed fields and
submits 336 images. This is a mid-round runtime check, not a round-completion
or performance pass. The observation binds the app SHA-256 to
`522a462558631a567e73a91c5e45eb17232f31effa531b47c2a10e472033d31f`.

A longer 4,800-field run of the same image reached round 2 with the CPU ahead
1–0 and both fighters reset to full health. It then hit a guest panic from
`simulation_join` at `main.c:1004` (`SAT_ERR_INVALID_ARG`). The last published
state was tick 3,137, with 797 discarded fields. The private failing dump is
`assets/imported/t30-law-ai-round/`; inspect it with
`scripts/inspect_saturn_dump.py`. The 24-case suite uses connected pads and
does not cover this disconnected-port CPU failure.

An opt-in `T3_MATCH_FAILURE_TRACE` build narrows this failure to
`t3_psx_camera_step`: the director is in cinematic stream state 6 for Law,
with script `0x80000604`, and returns 0. The current generated camera asset
has **zero** streams and assigns Paul's camera choice table to both fighters.
The private Law raw bank (BNS record 082) contains the selected stream at
offset `0x604`. `scripts/inspect_camera_bank.py --summary` reports 30 Paul
streams in 7,290 encoded bytes and 36 Law streams in 10,304 encoded bytes;
expanding both to per-frame samples would take 134,400 bytes before metadata.
The camera now binds each fighter's own choice table and compact section-8
streams. The 17,594 stream bytes are loaded from `CAMERA.BIN` into Low Work RAM
and checked with FNV-1a before play. `scripts/verify_camera_streams.py` compiles
the production C decoder on the host and compares every sample against the
pinned original-code Python port: **66 streams, 9,600 frames, seven channels
per frame, all equal**. The build reserves 576 cold face records and moves
small camera and bank descriptors to Low Work RAM to fit stock hardware.

The uninstrumented `build/t30-law` release image passed a 5,000-field Ymir
run with port 2 disconnected and SH-2 cache emulation enabled. Evidence is in
`assets/imported/t30-law-ai-release/`; its app SHA-256 is
`3147ecde7dfdeb46cda04fcf63c861f67a759c182cf93345c2dc4f6b217014c6`.
It reached 3,709 simulation ticks and round 2, beyond the original panic at
tick 3,137. There was no panic, rejected face, attack decode error or
unsupported move. This is an injected execution test, not a CD boot test.
It discarded 927 elapsed fields, so the timing target is still unmet.

A bounded PC profile of the same image (`t30-law-ai-profile`, fields
1,250–1,264; five image submissions) places the largest Master SH-2 costs in
face submission (17.4%), skin deform (11.2%) and face flush (7.9%). On the
Slave, face submission (15.2%), motion spline evaluation (10.3%) and vertex
projection (9.3%) lead. The Master also spends time polling the dual-SH-2
mailbox; its 8.3% receive share includes waiting and is not all removable
work. Profile before changing the renderer: optimize face submission and
motion sampling on a controlled candidate, then remeasure both submitted
image cadence and discarded fields with the same CPU encounter.

Three match-core mismatches exposed by these CPU fights were fixed against the
original code and the pinned remake's research: HitApply clears the previous
frame's guard/counter flags before applying new contacts; MoveBranchStep's
lookahead uses the heading after the current move's end turn; and normal VS
round flow suppresses the match-deciding loser camera in a team round. The
first two changed combat and branch state, and the last changed the camera
phase and the actor selected during round completion.

### AI view rules found through the CPU fights

The original runs `AiUpdate` (InputSource, FightFrame step 29) after HitApply and
before MoveStep, but this match core applies the hit transaction in one step.
`t3_psx_ai_game` therefore rebuilds the view the AI saw:

- When a hit selects a reaction, `+0x1A4` holds the row of the *pre-hit* pending
  slot (`hit.prior_pending_*`), or 0 when none was pending. MoveStep queues the
  reaction afterwards.
- On a KO collapse the slot, pose frame and attack word are the pre-collapse
  values (`hit.prior_*`); MoveStep runs the collapse after the AI.
- The guarded (`+0xCF`) and counter (`+0xD3`) flags exist only on the frame of
  the hit: `FUN_80045260` clears them every frame before HitApply. Branch
  conditions such as `0x39` read them through `t3_psx_match_branch_facts`, which
  gates both on the opponent's `got_hit`.
- `FighterUpdateSide` (`FUN_800464CC`) runs for both fighters every frame with no
  CPU check, so the AI's side flag follows the same rule as a human's.
- The Saturn build selects the CPU opponent when port 2 has no pad.

With these, `oracle-cpu-law-s1`, `-law-s2`, `-both-s3` and `-paul-s2` match the
original for every recorded tick and AI call. Paul-only oracle cases must use
`build/host`; `build/host-law` links the second bank and does not reproduce
Paul-against-Paul.

### Arcade sequencing and current evidence (2026-10-23)

With port 2 empty the Saturn build plays the CPU Law as P2 and raises the CPU
level 4→7 for each match the player wins (`arcade_advance` in `main.c`); a loss
or a draw repeats the stage, and a connected port-2 pad resets to level 4. Only
two fighters exist, so every stage faces Law.

`build/t30-law` with the AI: 1,800-field Ymir run, port 2 empty
(`assets/imported/t30-law-ai-probe2`), `first_error` 0, `loop_rate_x10` 290,
P2 contact registered. The 24-case combat suite passes on the same image
(`assets/imported/t30-law-ai-combat/suite.json`), so a port-2 pad still takes
over P2.

### Sound effects (first slice)

`scripts/extract_psx_sfx.py` reads the USA executable's VAB 0 (VH at `0x16050`,
body at `0xC3E6C`), decodes four samples and writes `SFX.BIN`: swing
(`0x604A`), hand and foot guard (`0x704D`, `0x704E`) and the clean-hit impact
(`0x61A0`, sound id `0x81`). Each plays at its tone's rate (16,042 Hz here), as
8-bit PCM, 32,172 bytes in all. `-DT3_LOCAL_SFX=<private import>` puts the file
on the disc and builds `src/saturn/sfx.c`.

`t3_sfx_poll` plays the hand-guard sound when a fighter's registered-block count
rises and the hit sound when its registered-hit count rises. The swing, the foot
guard (the original picks by the attack's joint), character voices, the other hit
variants and music are not played yet.

LibSaturn's audio init linked the stream registry (33 KB of Low Work RAM) even
for programs that only play sounds. It is now a weak reference, like music
(`audio_streams_reset` and `audio_streams_service` in `src/audio/streaming/api.cpp`),
so only a program that opens a stream pays for it. To fit the extra code in High
Work RAM, the branch-filter table shrank from 3,072 to 1,792 words (Paul and Law
need 1,680).

Evidence: `assets/imported/t30-law-sfx`, 1,800 fields with port 2 empty:
`first_error` 0, `loop_rate_x10` 295 and 6 sounds played. The headless harness
cannot show that the audio is audible, only that the SCSP calls succeed.

### HUD and title (2026-10-23)

Health bars follow `FUN_8004E55C`: player 1's is anchored at its inner end and
shrinks outwards, player 2's mirrors it, and a yellow "recent damage" length
closes on the health by 1/8 per displayed image (1/16 per 60 Hz frame in the
original). The original's sprite art, name plates, round marks, round messages
and the 16 × 46 timer digits are not reproduced; the HUD is still LibSaturn's
8×8 text.

`-DT3_TITLE_SCREEN=ON` adds a title before the stage loads: it waits for START on
pad 1 and then re-reads port 2 (pad: 2 player VS; empty: 1 player ARCADE against
the CPU). It is off by default because the headless probes and the combat suite
start fights without pressing START. Checked with a device script that presses
START at field 300 (`assets/imported/t30-law-title`): the simulation ran 813
ticks afterwards and `first_error` was 0.

### Hit and guard sparks, swing sound and block variants

`src/saturn/combat_events.c` turns the owned match's registered-hit and
registered-block counters, and the start of each attack window, into
per-fighter events once per displayed image. `sfx.c` plays the swing (`0x604A`),
the hit impact, and the hand or foot block sound: the original picks by the
attack descriptor's first joint (below 12 is a hand), which the match now keeps
as `strike_joint`.

The spark is a cross drawn with the HUD at the attacker's hitting segment end
(the point `psx_match_camera.c` gives the camera), projected through the image's
camera, yellow for a hit and blue for a block, six images long. Submitting the
same flash as two scene quads in front of the fighters cost about 2.5 images/s
whatever its size (28.5 to 25.0 in the 1,800-field probe; no clipping), so it is a
HUD overlay; a temporary build that pinned it on confirmed it lands on the chest.
The original's effect sprites (`effects.md`) are not ported.

Probe with the sparks, swing and block variants: `loop_rate_x10` 275,
`first_error` 0, 14 sounds in 1,800 fields.

### Character voices

`extract_psx_sfx.py --voice SLOT:CHAR:VH:ARC` adds a fighter's own sounds from its
voice bank (BNS `72+4n` header, member 2 of ARC `73+4n`): two attack shouts and
up to three hurt voices, resolved through the original's `g_charVoices` table
(USA file offset `0xB554`, 24 bytes per character) as `f<slot>_shout0/1` and
`f<slot>_hurt0..2`. Paul (BNS 72/73) and Law (80/81) are wired in CMake from
`T3_LOCAL_SFX` and `T3_LOCAL_ACTOR1_ARC`; the voice headers come from
`psx_disc_import.py --extract-ids 72,80`. All 14 samples are 63,840 bytes of
8-bit PCM at 16,042 Hz (effects) or 7,956 Hz (voices); each is read from `SFX.BIN`
into a 5,488-byte scratch before `sat_sound_create`.

A hit plays the receiver's hurt voice (cycling, 20-image cooldown, standing in for
the original's 40 frames and random choice) and one swing in four plays a shout.
The original's KO voices, round-result voices, light/heavy damage choice and
voice cooldown rules are not implemented.

Cost: sound calls take under a millisecond each (`sfx_update_ms` 18 ms over a
1,300-field scripted fight). The same scripted fight with and without the sound
build reads `loop_rate_x10` 320 both ways; the sound build submits fewer images
in the window (434 against 476) because reading `SFX.BIN` adds boot time on the
emulated disc. Fight-dependent rate differences between unscripted runs (23.8 to
29.5) come from the fight each run happens to play, not from the sounds.

### Round announcer

The system bank (VAB 1: VH `0x17270`, body `0x10081C` in the USA executable)
supplies the announcer, `ann_round`/`ann_final` (ids `0x3B`/`0x3A`), `ann_one` to
`ann_three` (ids `0x34`–`0x36`) and `ann_fight` (`0x3C`), 6 samples at 7,956 Hz.
`t3_sfx_round_update` follows `FUN_8003DCE0` and `hud.md`: "round" (or "final
round" in the deciding round, both fighters one win from the match) at intro
frame 61 of the first round and frame 1 of later ones, the round number at frame
100 or 40 unless final, and "fight" when the intro ends. The result voices (KO,
time up, win and lose calls) are not played.

`SFX.BIN` is now 102 KB; `t3_sfx_init` reads it in large sequential chunks into
the idle face store. Twenty separate reads had taken the 1,800-field probe's
boot from a fraction of the window to most of it (`loop_rate_x10` 98).

Round messages follow `hud.md`: `ROUND n` (or `FINAL ROUND`) from the start of a
later round's intro, `READY?` from intro frame 106 (46 after round 1), `K.O.`,
`DOUBLE K.O.` or `TIME UP` over the winner line when a round ends. The owned match
starts round 1 directly in the fight (`psx_match.c`, which the oracle cases
compare from), so round 1 gets only `FIGHT!` and the "fight" call at its start.
Fighters are not locked during that moment, unlike the original's 150-frame
intro.

KO voice: the loser's `g_charVoices` category-2 voice (the original draws a random
`0x4nnn`; the Saturn plays the first) starts as the collapse begins. The probe dump
`sfx_ko_voices` counts it: the `ko-collapse` input tape gives exactly 1.
