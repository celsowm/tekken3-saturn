# Tekken Force: plan

Status (2026-11-06): phases 0-2 done, phase 3 partly, phase 4 and 5 started; **the mode does not play yet**.
In the shipping build the menu still lists TEKKEN FORCE and does nothing (`docs/ROSTER.md`, `docs/ROADMAP.md`);
with the private levels configured it starts a Force demo program (the level script, camera, items and the
tally on a scrolling panorama, enemies and the player as boxes). Done and checked: the research harness, the
enemy packs and level files, the level runner, target choice, roles, walls, camera and hand-off in C (golden
cases; the runner and camera also against a recorded whole game), the match core's fighter count for three
fighters (duel binary unchanged). Open: the three-fighter match comparator, the AI state for Force, mode-8
contact and sound facts, real fighters and the third actor's cost, the level 4 staircase, the original HUD, the
screens' text, a full game disc played from the menu, and the frame-rate A/B (phase 6). The sections below are
the plan; the dated notes inside them are the results.

## What the mode is

Mode 8 of the original (`third_party/Tekken3Remake/docs/research/code/modes.md`,
section "Tekken Force"): the player walks through four side-scrolling levels
and fights up to two CPU enemies at a time (character 21 in the costumes Crow,
Falcon, Hawk and Owl, motion bank 20). A level script of 20-byte records
spawns enemies, locks the screen for fights, drops items (roast chicken, +50
health) and ends with a real fighter as boss (level 4 is only the boss). Each
defeat adds 2 to 8 seconds to a timer; the level ends with a tally (stage
bonus, clear bonus, bonus time). Three keys unlock Doctor B.'s final stage.

The rules are already researched, ported and verified against the original
code: `tools/research/force_sim.py` (`level_frame`, `items`, `fighter_roles`,
`choose_target`, `background_setup`), `verify_force_sim.py`,
`check_force_bosses.py`, `check_force_manual_target.py`, and the flow traces
`force`, `force_run`, `force_final`, `force_bosses`. We adapt rules into C with
attribution in the file and `NOTICE.md` (as `src/game/psx_head.c`), never copy
GDScript, never commit converted data.

## What differs from what we have

| Need | Today | Consequence |
|---|---|---|
| 3 fighter records (player and 2 enemies) | `t3_psx_match_t.fighter[2]`, `T3_DRAW_ACTORS==2`, Master and Slave each prepare one actor | Generalise the match; a third actor is the biggest frame-rate risk |
| Character 21 (not in the 21-fighter roster) | Packs `Fnn.BIN` for ids 0-20 | New enemy pack (mesh, bank 20, costumes) |
| Boss is a roster fighter, swapped in mid-level | One pack pair per run, restart per pairing | Boss as its own run (below) |
| Scrolling levels, tile panorama, walls, scroll modes | Fixed arena, VDP2 cylinder and floor | New stage layer and camera |
| Items, `+n SEC`, progress bar, key icons, tally, STAGE CLEAR, NOW LOADING | Fight HUD only | New HUD and result screens |
| Keys, hi-score, Doctor B. flag persisted | none | Needs the Saturn save path |

## Review (2026-11-06)

Checked against the code, the docs and the local tools before any work:

| Finding | Evidence | Change to the plan |
|---|---|---|
| Work RAM is full | `build/tekken3_saturn.elf`: `.wram_l` 0xFFD60 of 1 MiB (160 bytes free); `.bss` ends at 0x060E64E0 (about 104 KB of High WRAM left) | A third actor's buffers cannot be added to the versus program. Tekken Force becomes its own program (decision 0) |
| The mode-8 camera is not ported | `camera.md` names `FUN_800B545C` and `FUN_800B5504`; `force_sim.py` has no camera function; `psx_camera.c` has about 96 two-fighter references | New research port (phase 2), verified in the harness like the others |
| The force verifier does not run here | `work/jp_rev1/bns` has placeholder names; BNS record 7 is `force.ovl` (`tools/remake_import/common.py`). Linked as `007_force.ovl` (git-ignored); `verify_force_sim.py` then faults at PC 0 on the first `FUN_800B1778` call | Fix the harness set-up first (phase 0); decision 4 depends on it |
| Force data is not imported | Packed stages are 0-14 (`STGnn.BIN`), fighters 0-20 (`Fnn.BIN`); stages 15-18 use tile panoramas, not the cylinder `build_stage_packs.py` bakes | New stage builder; extend the pack builder to character 21 and its four costumes |
| Preparation is wired for two actors | `characters.c`: `fused=… T3_DRAW_ACTORS==2`, actor 0 on the Master, actor 1 on the Slave | A three-actor split to design and measure (phase 0) |
| The hand-off is 16 bytes | LibSaturn `exec.h`: `SAT_EXEC_ARG_MAX 16u` | Force state between runs (level, player, score, hi-score, timer, health, keys, flags) is about 28 bytes: LibSaturn change (below) |
| Saving exists but is unused | LibSaturn `save.h` (`sat_save_*`, internal memory and cartridge); no `sat_save_` call in `src/` | Game-side work only; test on Mednafen |
| Level 4's staircase fits the VDP2 | LibSaturn `vdp2_layers.h`: `vertical_cell_scroll` for NBG0/NBG1 | The raised 64-dot columns become a vertical cell scroll table |
| VDP1 texture room is limited | the attract fight reports `vdp1_vram_free` 145,824 to 148,384 bytes | Measure the enemy costumes and item pictures against it (phase 0) |

### LibSaturn changes

- **Needed: a larger hand-off.** Raise `SAT_EXEC_ARG_MAX` (for example to
  64 bytes) where `sat_exec` keeps the argument, with its host test. Without it
  the state must go through backup memory between runs, which is slower and
  writes the user's save on every level.
- **Useful, not required:**
  - a helper that writes an NBG pattern-name map from a list of 64 x 64 tiles
    (texture and palette word per cell), to keep the Force stage loader short;
  - a frame-time probe for N actors in the parallel preparation, if the
    three-actor split needs SDK support (decide after phase 0).
- **Not needed:** saving (`sat_save_*`), vertical cell scroll, parallel tasks
  (`sat_parallel_*`), CD streaming and the RAM cartridge (`ram_cart.h`) exist.
  The mode must not depend on a RAM cartridge.

## Phase 0 results (2026-11-06)

- **Harness fixed.** `verify_force_sim.py` fails for two reasons: (1) `force.ovl` has no
  GTE site list, so every COP2 instruction is found lazily and the CPU stops at PC 0.
  `scripts/overlay_cop2_sites.py` writes the list by recursive descent (Ghidra 12.2 cannot
  analyse here: the PSX loader extension was built for 12.1.4 and crashes in analysis, and it
  needs Gradle 9.1 to rebuild). (2) `force_sim.py` calls `projection_sim._s32`, which does not
  exist (`draw_sim._s32` does): `scripts/patches/tekken3remake-force-sim-s32.patch`, to apply to
  the submodule checkout. Then `verify_force_sim.py` has 0 mismatches in all 11 checks,
  `check_force_bosses.py` 0 differences in 352 pairs, `check_force_manual_target.py` passes.
- **The Force enemy is light.** Character 21 uses costume slots 47-50 (key `0x54 + costume`
  in the table at `0x80095CE8`), disc records 259-262, 263-266, 267-270, 271-274 (the voice
  record, the second of each, is empty). KMD 11-12 KB with 342 faces (Paul: 26.5 KB, 773),
  ARC 24 KB, motion bank 107 KB (Paul's 313 KB; one bank for all four costumes). Two enemies
  are 684 faces, less than one more fighter: three actors are 1,457 faces against 1,546
  for two full fighters, so the geometry load does not grow. Third-actor go/no-go for
  geometry: **go**; the real cost (preparation split, texture patches, shadows) still has to
  be measured in the `FORCE.BIN` skeleton.
- **The mode-8 camera is small.** `FUN_800B545C` is 42 instructions and `FUN_800B5504` 231.
- **LibSaturn hand-off:** `SAT_EXEC_ARG_MAX` is 64 bytes (LibSaturn `b52ec400`, host test
  passes).
- **Phase 1 started (2026-11-06).**
  - `scripts/build_fighter_packs.py --force-enemies` writes F21-F24.BIN (Crow, Falcon, Hawk, Owl; 163-173 KB
    each, 247 vertices, 342 faces; the nested four-costume ARC is handled in `psx_character_textures.py`;
    a normal roster pack is byte-identical to before).
  - `scripts/build_force_levels.py` writes LEV1-4.BIN from stage archives 51-54 (map, deduplicated 64 x 64
    tiles and palettes, floor descriptor, level script): 170, 160, 139 and 35 KB. Level 1's 16 x 5 panorama
    renders correctly from the baked file.
  - `scripts/build_force_boss_table.py` writes BOSS.BIN (92 keys x 4 levels x character, costume; run with the
    Tekken3Remake venv). `scripts/build_force_icons.py` writes KEYS.BIN (the three key sprites, decoded from
    force.ovl's ARC at `0x800B0FE0`).
  - The other pictures (roast chicken, progress bar, `+n SEC`, STAGE CLEAR, NOW LOADING) are system textures
    and strings of the executable, so they come from the existing HUD extraction (`extract_psx_hud.py`), not
    from force.ovl. The music of the boss fight is the boss character's record (`FUN_800B2B3C`); the
    level's own track is still to find (phase 5).
- **Phase 2 done for the rules (2026-11-06).** Each piece is C with attribution, a host test over golden cases
  and no game data in the repository (cases are generated into `build/force-cases/`, skipped when missing):
  - `psx_force_level.c`: the level runner (script commands, wait, end-of-level, tally, boss set-up, spawn
    patterns, the two enemy slots, the items and pickup, the caps) as an event-emitting state machine. 30,000
    random states against the verified Python port (`export_force_level_cases.py`): 0 mismatches.
  - `psx_force_target.c`: who the player faces (hidden, close, nearer, in front, 120-frame wait, the manual
    switch), 30,000 cases (`export_force_target_cases.py`): 0 mismatches. The relative angles and pair
    distances are inputs.
  - `psx_force_walls.c`: the z and x limits, 30,000 cases (`export_force_walls_cases.py`): 0 mismatches.
  - `psx_force_camera.c`: the side-scrolling camera (FUN_800B545C, FUN_800B5504), checked against the
    **original code** in the CPU harness (`export_force_camera_cases.py`), 20,000 cases: 0 mismatches.
  - `psx_force_handoff.c`: the 40-byte run hand-off block (round trip, range and checksum test).
  - Constant tables: `build_force_tables.py` (TABLES.BIN); boss table BOSS.BIN.
  - Not ported: the per-frame draw code (progress bar, key icons, text, panorama sprites: phase 4) and
    `fighter_roles`' record bookkeeping (phase 3, it needs the match's fighter records).
- **Phase 3 sizing (2026-11-06).** Findings before any integration code:
  - The versus program is byte-identical (`app.bin` 491,228 bytes, same `__bss_end` and `.wram_l` end) with all
    the Force C files linked into `tekken3_fight`: the linker drops what nothing calls. (The Saturn toolchain
    has no `string.h`: the Force modules use none.)
  - `T3_DRAW_ACTORS = 3` **compiles** (the actor preparation and render loops in `characters.c` are generic),
    so the third actor's drawing is a data and budget question, not a rewrite.
  - The simulation is the hard part: `t3_psx_match_t` is a duel (`fighter[2]`, `round_actor[2]`,
    `hud_bars[2]`, `sound[2]`, one `pair_distance`, "pair-atomic" `HitApply`, `last_attacker` and
    `last_hit_target` indices, a camera and replay that follow two). Force's rules run the same engine on
    three fighter records (the player and two enemy slots, with `fighter_roles` choosing the target), so
    the match core needs an N-fighter generalisation (pairwise distances, body separation, hit application
    for each attacker and defender, the round state for Force) checked against **three-fighter traces from
    the original code**. `fight_harness.py` exports two-fighter traces today; a Force trace (level 1 section
    with two enemies) has to be exported first (`flow_trace.py` has the `force` scenarios).
  - Order for the next work: (1) export a three-fighter golden trace of a level-1 section with the harness;
    (2) generalise the match core behind a fighter count and prove the two-fighter oracle tests unchanged;
    (3) run the three-fighter trace through it; (4) only then the `FORCE.BIN` program, memory map and the
    third actor's frame cost.
- **Three-fighter golden trace exported (2026-11-06).** `flow_trace.py force --jobs 1` (10 minutes) writes
  `third_party/Tekken3Remake/work/traces/flow_force.bin`: 7,000 frames of the whole game from boot into a Force
  level-1 section, with three fighter records per fight frame (git-ignored, private). It needs every overlay
  linked as `NNN_<name>.ovl` in `work/jp_rev1/bns` (records 000 enbu, 005 arcade, 006 practice, 007 force,
  008 volley, 009 select, 010 title, 011 ranking, 301 ending, 302 result: symlinks to `NNN_rec`, git-ignored).
- **Phase 3, slices 1-4 done (2026-11-06), duel path untouched.** `T3_PSX_FIGHTERS` (default 2) sizes the match's
  per-fighter arrays, fighter loops and pad array; `T3_PSX_OPP(match, i)` replaces `1u-i` in `psx_match.c`,
  `psx_match_camera.c` and `psx_match_effects.c`. At 3 (the `FORCE.BIN` build) the match also gets `opponent[]`,
  `off_screen_extra[]`, `pair_dist[]`, `pair_all[]`, `level_ended` and the Force walls. Taken from the original
  code (disassembly of `FUN_8004391C`, `FUN_80045450`, `FUN_80045834`, `FUN_80043F40` and its caller):
  - pairwise distances for every pair, each fighter against its own opponent, the stand-in for an off-screen
    opponent including the Force enemy whose slot is free (`FUN_800B2EEC`);
  - MoveStartAll: with a pending throw the frame counter picks the thrower among all of them, order victim,
    thrower, the third; otherwise 0, 1, 2. MoveStepAll: fighters 0 and 1 alternate by frame parity, the third
    follows and only while the level runs (`0x800B70E8` = 0);
  - the body phase: BodySeparate on the pairs (0,1), (0,2), (1,2) with the pair table's distances, and the
    Force walls (`psx_force_walls.c`) in place of the arena limits.
  - the hit application: each defender takes the attacker among the fighters that face it and whose gate
    accepted (else its own opponent); the reversal pass uses the opponent helper and MoveBranchAll's order.
  Checked at 2 after every step: the 94 running host tests pass and the versus `app.bin` is byte-identical
  (491,228 bytes), so the duel's code and frame rate are unchanged. **Not checked at 3**: there is no
  three-fighter comparator yet, and the hit application's attacker choice is my reading, not the original's
  HitApply read for three fighters. Left for phase 3:
  - the AI bus (`psx_ai_game.c`) runs the original AI code over a memory bus; Force's AI has its own state it
    reads and writes (`ai_sim.py`: `FORCE_TARGETS` 0x8009F6A8, one s32 per enemy and the AI record's +0x32C
    words from `FORCE_WORDS` 0x80023210 per group and level, `AI_MULTI` 0x8009F6A4 = 1 in Force,
    `FORCE_LAYOUT` 0x800AE3D8, the leader return `FUN_800B2E60`), none of which the bus maps yet. Its address map already holds three fighter slots, but its fighter images,
    picks, collections and pads are two, `cpu_mask` is limited to 3, and its bank placement
    (`place_banks`: fighter 0, fighter 1, divmot 99) is the duel's. Force's enemies share bank type 20 and
    the original's Force bank placement has to be read first; verify with the AI harness
    (`verify_ai_sim.py`);
  - the contact phase's mode-8 facts (`free_slots_mask`, `game_mode`) and the sound facts' opponent
    (`psx_fighter_sounds.c` pairs `1u-p`). Done since: mode 8's throw doubling in BodySeparate
    (`t3_psx_body_separate_force`, built only at three fighters, unverified at 3) and `fighter_roles`
    (`t3_force_roles`, 30,000 golden cases, 0 mismatches), whose result is written into `opponent[]` each frame;
  - the round flow (Force has none), the Force camera and
    replay instead of the fight camera's, and the duel round logic's two-element snapshots (`hp`, `pose`,
    `old_active`);
  - the three-fighter trace comparator against `flow_force.bin`.
- **Phase 4 started: the stage layer works on the Saturn (2026-11-06).** `src/saturn/force_stage_probe.c`
  (CMake target `tekken3_force_probe`, `-DT3_LOCAL_FORCE_LEVELS=assets/imported/force-packs`, disc
  `build/disc/tekken3_force_probe.cue`, 41 KB) reads `LEV1.BIN` from the disc and shows its 16 x 5 panorama as
  an NBG0 cell layer scrolling sideways; checked with `scripts/mednafen_shots.py` (screenshots only, no video).
  Design that came out of it:
  - 64 x 64 texel tiles become 64 cells each; 2-word pattern names (the 1-word 10-bit character numbers cannot
    address 5,120 cells), 16-colour palettes at CRAM colour `p * 16` (80 palettes, CRAM mode 1), the character
    data at `0x40000` (character number `0x2000 + n`), names in a 2 x 1 page plane at 0;
  - the disc reads matter: one read per palette or per tile made the load take over 25 s in Mednafen; with the
    palettes in one read and four tiles per read it takes about 20 s. A game build must stream or pack this
    better (one read for the whole level, or load while NOW LOADING shows);
  - texels of colour 0 are transparent, as on the PlayStation, and show the back colour.
  The probe also runs the C side-scrolling camera (`psx_force_camera.c`, with `TRIG.BIN` from
  `scripts/build_force_trig.py`) on the Saturn: a player walking right at 30 units a frame, the layer scrolling
  `camera.x / 20` dots; two screenshots ten seconds apart show the level moving.
  Not done in phase 4: the real player and enemies behind that camera, level 4's staircase
  (vertical cell scroll), the third actor, the HUD, items and the enemies' drawing.
- **First comparator against the whole original game (2026-11-06).** `scripts/check_force_flow_trace.py` replays
  `psx_force_level.c` over `flow_force.bin` (7,000 frames from boot through a Force level-1 section): each frame
  is stepped from the recorded state before it, with the fight's inputs (camera, health, positions) taken from
  after it, and every field the level runner writes is compared with the recorded state after it: **6,671
  frames, 0 differences** (script position, state, wait, patterns, both enemy slots, items, kill counter,
  scroll mode, bounds). Not compared, because the trace does not record them or the projection does: the fight
  timer and what depends on it (clear time, bonus time, score), and the popups' screen positions. The inputs
  that must come from after the frame are the camera x, the fighters' health, `fce`, `fe7`, `fc4` and the text
  flag; only the freeze flag comes from before it. With `--trig` the same script also steps
  `psx_force_camera.c` over the recorded game (the record from the previous frame, the player's position from
  the previous frame, since the camera runs before the fighters move): **5,996 frames, 0 differences** in
  x, y, z, pitch, yaw, h, the target x and the pitch target (the scroll mode and bounds are the level runner's,
  written in the same frame). This is the template for the match comparator: same trace,
  same frame-by-frame rule, the fighter records as the thing to compare.
- **Roles against the recorded game (2026-11-06), not exact.** Stepping the verified Python `fighter_roles` (the C
  `t3_force_roles` is equal to it on 30,000 random cases) over the same trace, from the previous frame's
  targets, wait, hidden and screen flags and the current positions: 6,678 of 6,776 frames equal the original's
  targets (98.6%), 98 differ, all of them in frames where both enemies are close and within 400 units of each
  other, so the pick rests on the facing-quadrant and relative-angle fields (`+0x2A..+0x34`) whose values before
  the frame are not in the trace. **Update:** with the pair-distance table recorded too (`flow_trace.py` now has
  a `pair_table` range at `0x8009EA08`; the trace is re-recorded, 14 minutes) and everything taken from the
  previous frame's records, 6,699 of 6,776 frames (98.9%) equal the original; 77 still differ, again both
  enemies close and within 400 units. The cause is not found; the roles stay verified on random states and on
  98.9% of a real game. (The submodule fixes, `force_sim.py`'s `ds._s32` and this range, are in
  `scripts/patches/tekken3remake-research-fixes.patch`.)
- **The level runner runs on the Saturn (2026-11-06).** The probe now also runs `psx_force_level.c` with the real
  level script read from `LEV1.BIN` and the tables from `TABLES.BIN`: the player walks right and waits while the
  script holds the screen, the camera follows it, the runner spawns enemies at the script's triggers, and each
  enemy is "killed" 150 frames after it starts fighting. Enemies, the player, the script position (green bar)
  and the runner's state (cyan block) are plain boxes. Mednafen screenshots show the script advancing, the camera
  locking and released, and defeated enemies (white) beside the player. No fighters, models or combat yet: those
  need the three-fighter match core and `FORCE.BIN`.
- **Menu entry and hand-off wired (2026-11-06), not run end to end.** With the private levels configured
  (`-DT3_LOCAL_FORCE_LEVELS=assets/imported/force-packs`, which now needs `LEV1-4.BIN`, `TRIG.BIN` and
  `TABLES.BIN`), `MENU.BIN` builds with `T3_FORCE_PROGRAM`: TEKKEN FORCE becomes selectable and starts `FORCE.BIN`
  (for now the probe program) through `sat_exec` with the 40-byte hand-off block (`psx_force_handoff.c`: level 0,
  seed from the menu clock); the program reads it and loads `LEVn.BIN` for the level (level 1 when started on its
  own, as the probe disc does, checked in Mednafen). The game disc's file list gains `FORCE.BIN`, `LEV1-4.BIN`,
  `TRIG.BIN` and `TABLES.BIN`. Not done: a full game disc built and played from the menu into Force (the disc
  build needs the whole private data set), NOW LOADING and the tally *screens* (the numbers). The demo does run the
  whole end of a level on the device: the script ends, the runner passes NOW LOADING's states, freezes on the stage
  clear, computes the bonuses and runs the tally (the score bar jumps, the runner state reaches the tally state),
  then asks `sat_exec` for `MENU.BIN` to return to the title (not found on the probe's own disc, so it stays; the
  return is untested).
- **Items and health on the device (2026-11-06).** The demo draws the runner's item slots (green boxes: the script's
  roast chickens, which scroll away behind the camera if missed) and the player's health bar. The player starts at
  half health; in Mednafen screenshots an item is on the ground at the screen's edge and a later shot has the
  health bar 50 points longer (the +50 pickup, `0x4E6A` sound and LIFE UP! state in the runner). Pictures,
  text, the progress bar and the key icons are still the original HUD's to draw (phase 4).
- **Pair distances against the recorded game (2026-11-06): exact.** With the `pair_table` range in the trace,
  `scripts/check_force_flow_trace.py --pairs` checks `PairwiseDistances`' table: after every Force frame (5,996
  of them) each pair's distance equals `isqrt(dx^2 + dz^2)` of the fighters' roots **as they were when the
  frame began** (the previous frame's final roots), 0 differences over consecutive frames of a running round. This is the rule the three-fighter
  `pair_distance` in `psx_match.c` implements (it runs after the move start and before the ground step, on the
  roots the previous frame left), and the order of the whole pair table by `fold(i) + fold(j)` is confirmed.
- **Walls against the recorded game (2026-11-06), invariant only.** At the end of each running Force frame the
  fighters should already be inside the walls: `_force_walls` (equal to `psx_force_walls.c` on 30,000 random
  cases) gives a zero correction in 17,300 of 17,424 fighter-frames (99.3%). The 124 exceptions that were looked
  at are enemies at `view + 5000`, the spawn x, 800 units beyond their 0x1068 limit (`dx -800`): a spawned enemy
  is pulled in by the ramp over the next frames, as the original does. Fighter 0 has 48 outside frames that
  were not looked at. The check does not prove the correction values; that needs the roots at the moment of the
  body phase, which a flow trace does not hold.
- Private data for the enemy is extracted to `assets/imported/force-import/` (records
  259-274 and 9, 10).

## Decisions

0. **Tekken Force is its own program.** `FORCE.BIN` runs from the menu like
   `MENU.BIN` and the fight, with its own memory layout: three actors, two
   enemy slots and the Force HUD and stage, without the versus-only data. The
   shared game code is compiled with a fighter count (`T3_PSX_FIGHTERS`, 2 for
   the versus program, 3 for Force), so the versus fight keeps its memory and
   frame rate.
1. **A level is two runs.** The beat-'em-up section (player, two enemy slots,
   enemy pack) is one program run; the boss is an ordinary two-fighter duel in a
   second run, using the existing versus path. The run hand-off carries score,
   timer, health, level, keys and the hi-score, as the pairing hand-off already
   carries the picks (`sat_exec`, `PHASE_*`). This keeps the loaded pack pair
   fixed within a run and reuses the duel as it is.
2. **Vertical slice first.** Level 1 with one player character: grunts, an item,
   the tally, the boss duel. Then levels 2-4, then keys and Doctor B.
3. **The existing fight must not slow down.** The rule in AGENTS and memory
   applies: any change to shared code (match, preparation, stage) ships only
   with an A/B on the attract fight (`scripts/mednafen_fight.py`) showing the
   marked img/s and worst segment no worse. Prefer a build option or a
   separate path over making the versus fight pay for the third actor.
4. **Verification is trace-based**, as for the match: C ports are compared
   against traces exported privately from `force_sim.py` and the flow traces
   (git-ignored), by host tests that skip when the traces are missing.
5. No new name or test scoped to one pair of fighters; the player character
   varies in tests.

## Phases and exit criteria

**0. Spikes (answer before designing further).**
- Make `verify_force_sim.py` and `check_force_bosses.py` pass on the local
  `work/jp_rev1` tree (the record 7 link is done; find what faults at PC 0).
- A `FORCE.BIN` skeleton linked with `T3_PSX_FIGHTERS=3`: its memory map, and
  the versus program unchanged (same ELF size and attract-fight A/B).
- The LibSaturn hand-off size.
- Size of character 21's models (vertices, faces, parts) and the cost of a
  third actor in the Master/Slave split: measure two enemies at the
  attract-fight workload before committing. If a third full actor does not fit
  the 30-field budget, options are a lower-detail enemy path or one enemy at a
  time on screen.
- The mode-8 camera (`FUN_800B545C`, `FUN_800B5504`): read the disassembly
  and size the port.
- Whether a tile map of 16 x 5 cells (64 x 64) maps to a VDP2 scroll layer
  with one-pixel-per-20-units scrolling, and how level 4's staircase is done.
- How progress (keys, hi-score) can be saved on the Saturn (backup memory in
  LibSaturn), or whether the first release keeps it for the session only.
- Exit: a short measured note in this file; go/no-go on the third actor.

**1. Data.** A script `scripts/build_force_packs.py` (reads the private import,
writes `assets/imported/`, never Git): enemy pack for character 21 (four
costumes), the four levels' tile maps and scripts (stage ARC members 2 and 3 of
stages 15-18), enemy slot and health tables, the boss table, item pictures,
Force HUD and result pictures. Exit: files build, sizes recorded, disc budget
checked.

**2. Simulation in C** (`src/game/psx_force.c/.h`): script runner, enemy slots
with their seven states, spawn patterns, defeat counting and `+n SEC`, items and
pickup, fighter roles and targeting (and the hidden manual switch), score and
timer caps, walls (`ArenaBounds` for mode 8), end-of-level tally, the hand-off
block, and the mode-8 camera (a new port, verified in the harness). Exit: host
tests equal the exported traces frame by frame for levels 1-4.

**3. Match integration.** Third fighter record; mode 8 rules in the resident
paths the docs list (`MoveStepAll`, round over, `PairwiseDistances`,
`BodySeparate`, `HitApply` scoring, forced hits by thrown bodies and power
moves, no KO replay); enemy AI levels. Exit: the oracle test of a scripted
level 1 section equals the trace, and the versus A/B shows no regression.

**4. Rendering.** Third actor; tile panorama; Force camera and scroll; item
sprites and shadows; enemy costumes. Exit: frozen-pixel screenshots at fixed
ticks compared with the remake's trace frames where practical, and a frame-rate
measurement of a level 1 fight.

**5. Flow and screens.** Menu entry, NOW LOADING between runs, STAGE CLEAR and
tally, game over, the Force result screen (result states 12-15), key award and
the Doctor B. challenge, music per level. Exit: a full run level 1 to level 4
on Mednafen with `scripts/play_mednafen.py` accepting the disc.

**6. Performance gate and polish.** Meet the frame-rate bar chosen after phase 0,
document in `docs/THIRTY_IMAGE_GATE.md`, update `ROADMAP.md` and `ROSTER.md`.

## Risks

- A third animated, skinned actor on a two-CPU frame budget that is already
  tight at 26 img/s with two actors. This is the first thing to measure.
- `fighter[2]` and the transition state, replay, camera and probe code assume
  two; the generalisation touches shared structures. Compiling the count per
  program keeps the versus fight as it is, but every shared change still needs
  the versus A/B.
- Memory: the versus program has no room left, and `FORCE.BIN` must fit three
  actors' geometry where two fill Low WRAM today.
- Level 4's staircase panorama and its black fill tiles on a VDP2 layer.
- Disc and memory budget: another pack, four tile sets, HUD pictures.
- Boss loading by restart adds a visible load between the beat-'em-up section
  and the boss; the original also shows NOW LOADING between levels, but not
  inside one. Acceptable if short; otherwise preload the boss pack in the same
  run for level 1 only and measure.
