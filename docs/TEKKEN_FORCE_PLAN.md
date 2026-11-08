# Tekken Force: plan

Status (2026-11-07): phases 0-2 done, phase 3 partly, phase 4 and 5 started; **the mode does not play yet**.
The plan to finish it is [Plan to finish](#plan-to-finish-2026-11-07); it supersedes the old resume list.
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
  enemies close and within 400 units. **Resolved 2026-11-07:** the check's RAM lacked the overlay's quadrant-order
  table (`0x800B0F04`); with it loaded the roles are exact over the whole trace (see Plan to finish). (The submodule fixes, `force_sim.py`'s `ds._s32` and this range, are in
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
- **Sound facts for three fighters (2026-11-06).** `psx_fighter_sounds.c` paired each fighter with `1u-p`; at
  `T3_PSX_FIGHTERS>2` it now takes the opponent from the match's `opponent[]` (a context field that exists only
  in that build) and accepts three actors. Versus `app.bin` stays 491,228 bytes (a first version with a run-time
  lookup grew it by 104 bytes and was replaced by the compile-time one); 103 host tests pass; both files compile
  at three fighters. Unverified at 3, like the rest of the match core.
- **Contact facts, mode 8 (2026-11-06).** At three fighters the hit gate gets `game_mode = 8` (a thrown defender
  cannot be hit, the gate's THROW_LOCK rule for modes 7 and 8); the duel keeps 1. `free_slots_mask` stays 3 (the
  match applies one hit per defender per frame). Versus binary unchanged (491,228 bytes).
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

## Plan to finish (2026-11-07)

This replaces the 2026-11-06 resume list. It is in dependency order. Every step has its own exit test, and
the mode ships only when the last step passes.

### What changed since the last plan

- **The remake already plays Tekken Force exactly.** Tekken3Remake's milestone M4 is done. Its `GameFlow`
  matches the original game run from boot, frame for frame, in four flow scenarios:
  - `force`: 7,000 frames;
  - `force_run` and `force_bosses`: 40,000 frames each, the levels with their real boss fights;
  - `force_final`: 57,729 frames, all four levels, the Doctor B. level, the Force result and the ranking.

  It compares all three records field by field, plus the level script, enemy slots, items, score, timer,
  targeting and walls. So every three-fighter rule already has a verified, readable reading:
  - `remake/core/force/tekken_force.gd`: 1,063 lines;
  - `remake/core/combat/combat.gd`: walls, throw link, forced hits, Force score, the three-record body phase;
  - `remake/core/ai/ai_decision.gd`: the Force AI timers and words;
  - `remake/core/fight/fight_simulation.gd`: the record count, the enemy and boss model swaps, `_force_fighters`;
  - `round_flow.gd`, `round_hud.gd`, `vibration.gd`, `result_screens.gd`, `vs_screen.gd`.

  "My reading" is no longer needed anywhere: each rule is adapted into C from these files and the research
  docs, with attribution and never copying GDScript, and is checked against the same traces.
- **The 77 role frames are explained (2026-11-07): exact.** They were a flaw in the check, not in the rules:
  the facing-quadrant order table (`0x800B0F04`, 2, 0, 1, 0) is overlay data that the trace does not record, so
  the test RAM held zeros and every tie between two close enemies fell to the second one.
  `scripts/check_force_roles_trace.py` (new, replaces the earlier ad-hoc run) loads the table and steps
  `fighter_roles` from the previous frame's records and pair table (the roles run first in `FightFrame`):
  **5,994 frames, 0 differ**. Game bug #47 (the harness that recorded the traces patches it; `force_sim.py`
  reproduces it) does not change the target on this trace, with or without `--fixed`; it only matters for the
  opponent-side angle fields, which the comparator (step 2) will show. Policy: the C port follows the traces.
- **Memory is measured.** The versus program has 672 bytes free in Low WRAM (`.wram_l` 1,047,904 of 1 MiB)
  and about 40 KB in High WRAM (`__bss_end` 0x060F60A0). The probe uses 13 KB and 68 KB. A third actor still
  cannot be added to the versus program (decision 0 stands).
- **The frame rate is higher.** Versus is now 29.46 marked / 27.45 worst segment, against 26 when phase 0
  ran, so there is more room for the third actor.

### Rules inventory (step 0, done 2026-11-07)

Every mode-8 rule outside the level runner, with the remake's verified reading (under
`third_party/Tekken3Remake/remake/core/`) and the state of our C. "Unverified" means written in C at
`T3_PSX_FIGHTERS>2` but not yet compared with the original at three fighters (step 2 does that).

| Rule | Original | Remake reading | Our C |
|---|---|---|---|
| Pair distances for every pair; off-screen stand-in; a free enemy slot counts as off screen | `PairwiseDistances`, `FUN_800B2EEC` | `combat.gd` 70 | **verified** (5,996 frames, `--pairs`) |
| Roles: player, target, the other; opponent indexes; the hidden manual switch | `FUN_800B5EC0`, `FUN_800B598C`, `FUN_800B5910` | `tekken_force.gd` `roles` | **verified** (`t3_force_roles`: 30,000 random cases and 5,994 recorded frames) |
| MoveStartAll: pending throw picks the thrower by the frame counter, order victim / thrower / third; otherwise 0, 1, 2 | `FUN_80043F40` and its caller | `move_system.gd` 1121 and around, 1433 | unverified (`psx_match.c`) |
| MoveStepAll: 0 and 1 by frame parity, the third after them, only while the level runs (`0x800B70E8` clear) | `MoveStepAll` | `move_system.gd` 828, 1433 | unverified |
| Throw skip: in Force a throw transition row is skipped when the fighter is not in a throw and its opponent is | `MoveBranchStep` | `move_system.gd` 1121 | **missing** |
| Body phase on pairs (0,1), (0,2), (1,2); throw doubling when two or more are in a throw (`0x800958C4 > 1`) | `BodySeparate` | `combat.gd` 518-548 | unverified (`t3_psx_body_separate_force`) |
| Walls: z limits, the player's x lines, an enemy within ±0x1068 of the camera, level 4 lower line | `ArenaBounds` mode 8 | `combat.gd` 155 | **verified** (30,000 cases) plus an invariant on the recorded game; the correction values are unverified |
| Throw link in Force: the victim's anchor follows the thrower; the link drops when the pair drifts more than a limit after move frame 0x12 | `FUN_800432B0` | `combat.gd` 193-205 | **missing** (`psx_match.c` handles fighters 0 and 1 only) |
| Hit gate: a thrown defender cannot be hit in modes 7 and 8 | `HitTest` gate | `combat.gd` 684 | unverified (`game_mode = 8`) |
| Forced hits: a CPU body thrown faster than `0x59` from frame 11, and the player's power moves (flag 16, cost 10 health), hit whoever they touch; contact at the defender's root; sound `0x4951`; forced reaction; `+0xC9` | `FighterMovePhysics`, `HitTest` 0x80044304 | `fighter_physics.gd` 209 (`_forced_hit`), `combat.gd` 671-707, 842 | **missing** |
| Hit application: which attacker counts for a defender | `HitApply` | `combat.gd` 520-548 | unverified (my reading) |
| Force score in `HitApply`: ten points per damage (the hit's base damage for the player or a forced hit, twice the extra damage) and no KO replay | `HitApply` | `combat.gd` 865-887 | **missing** |
| `no_look_at`: 0 for the human, else 1 | `FighterMovePhysics` | `fighter_physics.gd` 247 | **missing** |
| Round over: the player's health while the level runs; the first or second record's in the boss fight; then the time | `FUN_8003E608` | `round_flow.gd` 275 | **missing** (Force has no round flow in C) |
| The human's pad `0x800B70D8` (0 or 1): input, Start, vibration (bug #16 fixed or not), COMMAND page, music pick | resident | `fighter_input.gd` 122, `vibration.gd` 35, `round_flow.gd` 606 | **missing** |
| Scripted input for the third record | `CpuOrPadInput` | `round_flow.gd` 117 | **missing** |
| Enemy variant and boss model swaps during the fight; the area change (round state 7, sub-states 21-23) | `FUN_800363FC`, `FUN_800B2B3C`, slot states 3-4 | `fight_simulation.gd` (`_force_fighters`), `tekken_force.gd` | **missing** (the runner emits the events; nothing loads the model) |
| Force AI: targets, words per group and level, `AI_MULTI`, the layout, the leader return, the attack hold-off timers | `0x8009F6A8`, `0x80023210`, `0x8009F6A4`, `0x800AE3D8`, `FUN_800B2E60` | `ai_decision.gd` 157, 1160-1167, 1681-1685 | **missing** (step 3) |
| Camera: the side-scrolling camera | `FUN_800B545C`, `FUN_800B5504` | `camera_director.gd` 202, 252 | **verified** (20,000 cases and 5,996 recorded frames) |
| Level runner, slots, items, tally, boss set-up | `FUN_800B1778` and friends | `tekken_force.gd` | **verified** (30,000 cases; 6,671 recorded frames) |

Nine rows outside the AI are missing (throw skip, throw link, forced hits, the score, `no_look_at`, round
over, the human's pad, scripted input, the model swaps) and five are unverified (MoveStartAll, MoveStepAll,
the body phase, the hit gate, hit application). The AI is step 3. Step 2 takes the rest in the original's order, each
adapted from its remake reading with attribution (never copying GDScript).

### Step 1: traces and the role frames

- Record the long flow traces with the `pair_table` range, in the background, one at a time:
  - `force_run`: 40,000 frames, about 90 minutes;
  - `force_bosses`: 40,000 frames;
  - `force_final`: 57,729 frames, about 2 hours.

  Each is about 130-200 MB in `work/traces/` (git-ignored). Check `du` first. Keep only these four traces.
- Roles against the recorded game are done (exact, above). Run `check_force_roles_trace.py` over each new
  trace too; expect 0 differences. Any frame that differs gets an inventory row for its cause before going on.

### Step 2: three-fighter comparator (the correctness gate)

- **Fixture.** A Force exporter (venv, `scripts/`) turns a flow trace's Force section into the duel oracle's
  self-driven fixture shape:
  - the level start state: three records placed, bank type 20, walls, the level runner's start;
  - the seed;
  - the human's pads;
  - the enemies' AI output as an `ai.bin` tape, as `--cpu-mask` does for the duel;
  - the bank epochs and the model-swap frames.

  An importer and host test are in `tests/`. Exit: the fixture loads and frame 0 equals the recorded records.
- **Design findings (2026-11-07), from reading the duel evaluator and the trace.**
  - The `flow_force` trace's player is character 4 (bank type 4, 130 health); both enemies are character 21,
    bank type 20, costumes 84 and 85 (`0x54 + costume`). The first running Force frame is 412.
  - The duel evaluator (`tools/psx_match_replay_eval.c`, built by `T3_PRIVATE_REPLAY_ASSET_DIR` against
    `generated/private_logic` and `generated/characters`) already picks each fighter's bank by its seed bank
    type from a main bank plus an optional second (`build_local_divmot_assets.py --bank … --second-bank …`).
    Force needs the player's bank as main and type 20 as second; a boss run needs a third. That is the only
    bank change.
  - What the evaluator hard-codes to two and must take from `T3_PSX_FIGHTERS` for a `tools/psx_force_replay_eval.c`
    sibling: the seed loops (`i<2`, the linked-bank pairing), the per-fighter comparisons, the
    `0x800A9228` USA fighter base and its stride, the pad and AI tape (`psx_ai_tape.h`) per fighter, and the
    Force globals the match reads (`force_view_x`, `force_stage`, `level_ended`, `opponent[]`, the walls).
  - The trace is Rev.1 layout and the evaluator is USA layout: reuse `fight_oracle_export.py`'s mapping through
    `tools/remake_import/layouts/usa.json`. A Force exporter takes the seed from the trace frame before the
    first running frame (the records, the globals, the pair table) and the later frames as `frame-N.ranges`.
  - The enemies' pads come from the original AI (the `ai.bin` tape). `flow_trace.py` does not record
    `AiUpdate` calls; add that to the Force scenarios (as `fight_harness` does with `--cpu`), or read the pads
    from the records' own pad fields after each frame, which is simpler and enough before step 3.
- **Comparator.** A host program steps `t3_psx_match_step` at `T3_PSX_FIGHTERS=3`, with `psx_force_level.c`,
  roles, walls and the Force camera in the loop. It compares the three records and the Force globals after
  every frame, and stops at the first difference with the field list (as `fight_oracle_eval.py` does).
- **Close the differences in the original's order.** That order is the pair table (done), MoveStartAll,
  MoveStepAll, the body phase, the throw link, hits and forced hits, the score, then round over. (The roles are exact already.) Each
  missing rule from step 0 goes in as it is reached, adapted from the remake's reading.
- After every change at 2 fighters: the versus `app.bin` stays byte-identical, or an attract-fight A/B shows
  img/s and worst segment no worse.
- Exit, in three parts:
  - 0 differences over `force`'s level-1 section (about 6,000 frames);
  - then over `force_run`'s levels 1-3;
  - then over `force_bosses`' boss fights: slot 0 as a type-4 boss, mode-8 round over, no KO replay.

### Step 3: Force AI in C

- On the AI bus (`psx_ai_game.c`), map:
  - `FORCE_TARGETS`, `FORCE_WORDS` (per group and level), `AI_MULTI` = 1, `FORCE_LAYOUT` and the leader
    return;
  - three fighter images;
  - two CPU fighters (`cpu_mask` beyond 3);
  - the Force bank placement: the enemies share bank type 20, read from the remake's `_force_fighters` and
    `force.ovl`.
- Exit, in two parts:
  - the step 2 comparator with the `ai.bin` tape replaced by the C AI: still 0 differences over the same
    sections;
  - the duel AI's oracle cases unchanged, and the versus binary unchanged or A/B no worse.

### Step 4: the real `FORCE.BIN`, measured early

Do this in parallel with steps 2-3 as soon as step 0 is done, because it is the go/no-go.

- A new target `tekken3_force`, built with `T3_PSX_FIGHTERS=3` and `T3_DRAW_ACTORS=3`, replaces the probe
  as `FORCE.BIN`. It drops the versus-only code and data (arcade ladder, VS records, endings, replay
  tables), keeps the probe's stage layer, and loads the player's pack and one enemy pack. All four
  costumes share bank 20 (107 KB); the texture set is per variant.
- **Memory map.** Record `.wram_l`, `.bss` and VDP1 texture room with the player's largest pack against
  the measured `vdp1_vram_free` (about 146 KB).
- **Boss decision.** If the largest roster pack also fits beside them, load the boss in the same run, as
  the original does with its model reload (sub-state 23): the flow and timing stay the original's. Only if
  it does not fit, keep decision 1 (two runs) and record the extra load time.
- **The third actor's preparation split.** Measure three ways with the wait probe on both CPUs:
  - (a) the Slave prepares both enemies (342 faces each);
  - (b) the Master prepares the player and one enemy;
  - (c) the enemies are split by vertex range.

  R8's cache sync covers only actor 1's span (`note_worker_write`). Generalise it to every actor the worker
  writes, with the same exact span.
- **Level loading.** One sector-aligned read per level file (the probe took about 20 s with many reads),
  done while NOW LOADING shows.
- Exit:
  - level 1 runs with the real player and two enemies (scripted pads at first) on Mednafen;
  - a deterministic pad tape gives the same pad hash twice;
  - the frame rate with two enemies on screen is measured at the attract-fight workload.

  **Bar (recommended):** two-enemy segments at or above the versus fight's current worst segment (27.45),
  the same 30-field cadence, and no fidelity trade. If it misses, the 30 img/s tracker's tools (profile,
  divider, cache-sync cuts) are used on `FORCE.BIN` before any other option.

### Step 5: rendering

- The tile panorama behind the real camera, using the original's rules:
  - one dot per 20 units of view-space x;
  - the top edge at the projection of (camera x, 0, 2,500) minus 64 × the map height;
  - transparent colour 0.
- The Force floor and lighting:
  - the levels' floor descriptor;
  - the spotlight floors if a level uses them;
  - the pick-up back-colour flash;
  - the Doctor B. level lit by stage 12's record (`0x800B70F4`).
- Items as two camera-facing VDP1 quads at the item's depth, with the shadow semi-transparent.
- Shadows for three fighters.
- The four enemy texture sets.
- Level 4's staircase as a vertical cell scroll table, with its black fill tiles.
- Exit, in two parts:
  - frozen-pixel screenshots at fixed ticks for levels 1-4 and the Doctor B. level, kept as the new baseline;
  - each compared by eye with the original's ordering-table pictures (`gpu_raster.py`) of the same trace
    frames.

### Step 6: HUD, screens and flow

- **The Force HUD:**
  - name plates and bars (`FUN_800B34B0`);
  - the progress bar with its CLUT colours and blend modes;
  - the key icons (`KEYS.BIN`);
  - the timer;
  - `LIFE UP!` and `+n SEC`;
  - the 5…1 announcer.
- **The Force screens:**
  - the Force VS screen: the player's portrait, the enemy names, `TEKKEN FORCE` with `STAGE n` / `FINAL STAGE`,
    the caption colour (144, 112, 248) at width 232;
  - NOW LOADING and the remaining time over the player;
  - the tally, one line every 30 frames with its sounds, and its bonus-time rule;
  - STAGE CLEAR;
  - game over, with no continue;
  - the Force result screen (state 15) with its picture (overlay archive `0x800D8A20`) and its keys, saved and
    boss-list branches;
  - then the ranking.
- USA wording from `usa-version.md`. The menu entry is no longer a demo.
- Exit:
  - the flow from the menu through level 1, the boss, the tally and level 2 on Mednafen;
  - screens checked against `gpu_raster.py` pictures of the flow trace's frames.

### Step 7: progress, secrets and the final level

- **Saving.** The game has no save code yet (`sat_save_*` is unused). Add one save block with:
  - the keys (save `+0x28`, saturating at 255);
  - the hi-score;
  - the Doctor B. flag (`0x800982D0` bit 19).

  Use internal memory, then the cartridge, with no RAM cartridge required.
- **Keys and the final level.**
  - The key award at the end of level 4 sets the result flags;
  - three keys give `CHALLENGE FINAL STAGE!!` and the Doctor B. level;
  - beating him unlocks him in the select screen.
- **The secret.** The manual target switch (the four held buttons at the start) is read on the physical
  buttons, before the key configuration.
- Exit: `force_final`'s path (four levels, Doctor B., the result) is played on Mednafen. Then a cold boot shows
  the saved keys and the unlock, and the save survives a power cycle in Mednafen.

### Step 8: disc, gate and docs

- The game disc carries `FORCE.BIN`, `F21-F24.BIN`, `LEV1-4.BIN`, `TRIG.BIN`, `TABLES.BIN`, `BOSS.BIN` and
  `KEYS.BIN`.
- `scripts/play_mednafen.py` `missing_features` requires them, and the release configure
  (`scripts/print_release_configure.py`) sets `T3_LOCAL_FORCE_LEVELS`.
- A whole run, levels 1-4, recorded with the shortest windows that answer each question. Delete the videos
  after reading them.
- The versus attract-fight A/B against the last promoted control: marked and worst segment no worse.
- Update `docs/THIRTY_IMAGE_GATE.md` (with the Force rate), `ROADMAP.md`, `ROSTER.md` and this file's status
  header.
- Exit: `play_mednafen.py` accepts the disc, and the menu's TEKKEN FORCE plays the mode.

### Order and parallel tracks

1. Step 0, then step 1, which mostly waits on the background trace exports.
2. Steps 2-3 (correctness, host only) and step 4 (device, the go/no-go) run alternately. Never benchmark
   while a build or export runs.
3. Steps 5-7 after step 4 passes its bar.
4. Step 8 last.

The 30 img/s tracker's open items (R2, R7, the small cache-sync calls) continue between steps. Any win in
the shared preparation also lowers the third actor's cost.

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

### Step 2 progress (2026-11-07)

- The replay evaluator now builds at three fighters. `T3_PSX_FIGHTERS_COUNT` (CMake) sets the owned
  match's record count; `scripts/build_oracle_pair_evaluator.py --fighters 4 21 22` builds a player and
  two Force enemies (pack ids 21-24 are `FORCE_ENEMIES`). The seed and compare loops follow
  `T3_PSX_FIGHTERS`; the two-fighter evaluators still build. Inputs: the player's raw records from
  `tekken3-usa-full`, the enemies' from `force-import`, banks `04` and `21` in one logic directory.
- **A finding that cost a re-record.** The flow trace does not hold what the evaluator seeds from (the
  ground step accumulators, the body's previous roots, the arena ramp, the HUD bars, the sound timers).
  `scripts/force_flow_record.py` runs the pinned `flow_trace.py` with its range list widened by the
  spans `fight_oracle_export.py` reads (mapped USA to Rev.1) and the regions around them, so the
  recordings carry them. The first `force_run` was discarded 13 minutes in and restarted with it.
  The older `flow_force.bin` stays valid for the roles, the camera and the level runner.
- Still to write: the exporter from a wide trace (seed from the frame before the first running frame,
  `frame-N.ranges` after), a Force round flow in the evaluator, and the rule fixes themselves.
- **The comparator cannot seed from a neutral frame.** In the recorded game the player is standing
  neutral (slot 3, blend idle) on 8 frames out of 7,000, and never with an enemy on screen except
  frame 2106; the enemies spawn mid-fight. The versus evaluator's neutral seed (it rebuilds the move
  and blend state from a standing pose) therefore does not apply. The Force comparator instead
  follows the evaluator's `T3_ORIGINAL_MATCH_START` mode: no original fighter state is copied, the
  owned code initializes the player at level start and spawns each enemy itself, and every frame is
  compared. That makes the spawn path (level runner to fighter init, model swap, costume key) part of
  what step 2 proves, and puts Force's own start-of-level init ahead of the rules in the inventory.
  `scripts/force_oracle_export.py --seed-at FRAME` writes the globals and frames for such a run.
- **Step 1 done: four recordings, roles exact on all of them.** `check_force_roles_trace.py --fixed`:
  `force` 5,994 frames, `force_run` 37,757, `force_bosses` 38,367, `force_final` 49,528; 0 differ in
  each (131,646 frames). The last three carry the comparator's seed ranges (`force_flow_record.py`);
  `force_run` and `force_bosses` are 40,000 frames, `force_final` 57,729; together 1.4 GB under
  `third_party/Tekken3Remake/work/traces` (git-ignored).
- **The start looks like the versus start (frames 261-412 of `force_run`).** The player's record goes
  live in fight sub-state 8 at frame 261 (health 130 in Q16, pose frame 1), plays a pose that advances
  one frame per frame for 150 frames (pose word 9,830,550 = frame 150 at 411), then enters slot 3 (the
  stance) at frame 412 and moves on at 413 (slot 21). That is the shape of `place_original_round(first)`
  (`pose_frame = move.length - 149`, the first-round intro), so the player's start can reuse the owned
  start before Force-specific placement (positions from the level, enemies inactive with health 0 and
  invulnerable until their slots spawn them) is added on top. The enemies' records stay zero until
  spawned.

### Step 2 result: the level start and the first player moves are exact (2026-11-08)

`scripts/force_oracle_export.py --seed-at 261` + the three-fighter evaluator (`scripts/build_oracle_pair_evaluator.py
--fighters 4 21 22`, `T3_FORCE_START=1 T3_ORIGINAL_MATCH_START=<rand at the seed> T3_ORIGINAL_START_PADS="0 0"`)
compare every field of all three records against `force_run`. **Rows 262-463 (202 frames) pass with zero
differences**: the 150-frame intro, the stand, and the player's first 50 frames of play on the human pad. Rules
this closed (all behind `config.start_force`, three fighters only):

- the start: the player alone at the origin, no intro camera, the Force clock (3600), one round to win, one
  human; the enemies wait inactive (stance queued for the first, the first move for the third, no random draw),
  health 0, invulnerable, with their body spheres from the unposed load, and nothing strikes or is struck;
- the roles fix the player's facing while both enemies are invulnerable (`fixed_facing`); `no_look_at` follows
  mode 8 (`FighterPhysics`); the round flow validates only the records on the field and ends the round on the
  player alone until the level has ended; no replay KO mark; the HUD record is not compared (Force draws its own).
- a latent three-fighter bug: the match core indexed the round flow's two-entry pad arrays and the KO-mark
  health array with the third fighter, which clobbered the flow facts.

First failing row: **202 (frame 464), the first enemy spawn.** Next: wire the verified level runner
(`psx_force_level.c`) into the match so its model and spawn events initialize the enemy records, then compare on.

### Step 2 result, continued: 427 frames, through the first kill (2026-11-08)

The level runner, the roles, the side-scrolling camera and the walls now run inside the three-fighter match
(`config.start_force`; `force_tables`/`force_script`/`force_trig`/`force_level`), and the evaluator feeds the
recorded pads of all three records (`flow_<scenario>.pads`, written by `force_flow_record.py`: every
`FighterInputPush`, the AI's included) and the camera's view x. **Rows 262-688 of `force_run` (427 frames) pass
with zero differences on all three records**: the intro, the first spawn (the level runner's health, position
and `FighterStartMove` with the enemy as its own opponent), the walk in against the Force wall, the player's
attack, the hit, the kill (the ordinary reaction, not the versus collapse) and the timer bonus.

Found on the way: a hit may only reach the defender the attacking fighter's gate was tested against (the
third record was being struck by the player's gate); the round flow's pad and KO-mark arrays were indexed with
the third fighter.

**What stops the comparison next: the shared generator.** The original draws five numbers at the round start
(the stance of records 0 and 1, `AiInitRound` for three records), which the match now reproduces, and then one
per frame from frame 465 on: the enemy AI (`AiUpdate`). Without the AI the generator diverges from the first
enemy action, and with it the shouts (`voice_cooldown`, rows 427-429). So the next step is the Force AI (step 3)
on the owned AI bus (`psx_ai_game.c`, which runs the original `AiUpdate` over typed fighters; it has two
records and `force_layout`): three records, the two enemy slots' countdowns (`0x8009F6A8`, `0x8009F6B0`), the
pattern words (`0x80023210`) and `AiInitRound`'s odd-slot flag. The comparison checks `rand` every frame.

The recordings are being redone with the pad side files (`flow_force_run`, `_bosses`, `_final`).

### Step 3 result: the Force AI runs on the owned bus; 430 frames match (2026-11-08)

`psx_ai_game.c` (which runs the original `AiUpdate` over typed fighter images) now serves three fighters in
the Force build (`T3_PSX_AI_FIGHTERS`): three images, the third record on the enemies' bank, the Force layout
(`force_layout` 1: the enemies are AI slots 0 and 1, both aiming at the player) and `AiInitRound`'s two
resets. The C `AiUpdate` (`psx_ai.c`) already carried the Force rules (the countdowns at `0x8009F6A8`, the
pattern words, the odd-slot rule). With the live AI the evaluator needs no pad tape for the enemies:
**rows 262-691 of `force_run` (430 frames) compare with zero differences on all three records, the shared
generator (`rand`, every frame), the sounds and the effects.**

Two corrections the AI check forced: the round start draws the stance of all three records (five draws with the
two AI resets, not four), and Force runs the effects (hit sparks, flipbooks) without the fight camera (they draw
the shared generator; their glows and flames cover the first two records, which is all Force's characters use).

What is verified is the first 430 frames of one level-1 game (the first 700 frames of the recording); the full recordings of the
three scenarios (about 135,000 frames: all four levels, the bosses, the final fight) are being redone with the
pad files and will extend the same check. Still open for steps 2-3: whatever the longer runs show (items, the
tally and stage clear, area changes, the bosses' own records, the Doctor B. level), and the Force-specific `rand`
users beyond these 430 frames.

### Step 2/3, the long run: 1,001 frames exact, then the hit rules (2026-11-08)

With the full `force_run` recording (40,000 frames, pads included) the same comparison now runs **1,001
frames (262-1,262) with zero differences**, up to the first of three hit-related differences. Fixed on the
way: FighterStartMove keeps the blend mode (a respawned enemy); the walls read the camera's view x as the
*previous* frame left it (the camera moves first); Tekken Force's pattern words (`0x80023210`) are in the AI
blob (`extract_psx_ai_tables.py --force` writes `psx_ai_force.bin`; the versus blob is unchanged); a KO with
`collapse_on_ko` 0 is thrown into the air by selecting the in-air reaction again (MoveStep's alternate KO).

Open, in order of the run: (1) `hit_done` is one flag per fighter; the original keeps one per attacker and
defender (`+0x83 + defender index`), and HitTest tests every pair of the three records, each defender holding
two hit slots. The owned core tests an attacker against its opponent only and picks one attacker per defender
(the first whose gate accepted), which gives `last_attacker` 1 where the original has 2 when both enemies strike
the player in one frame (frame 1,304). (2) The body push of a fighter after the pair separation (frame 1,078).
(3) The voice cooldown after a hit sequence (frame 1,290). (1) is the "unverified: hit gate and hit application
at three fighters" row of the inventory; it needs the gate and the transaction per attacker-defender pair.

### Step 2/3, the long run, continued: 2,204 frames exact (2026-11-08)

`force_run` now compares exactly for **2,204 frames (262-2,465)**; the full recording is 40,000. Fixed since the
last note: the pair table is filled for all three pairs (the third pair was zero, which cleared a pushed enemy's
body push); the sounds' state is per player slot (the third record shares the second's, as `+0x12` is `min(i,1)`);
the roles read the screen x the last frame left; a reloaded enemy restarts its eyes (the level runner's model
bind); forced hits (a CPU body thrown fast enough, or the player's power move, hit whatever the body touches;
`psx_forced_reaction.bin` holds the one reaction record they take).

The first difference now is `recover_mash` of a downed enemy hit a second time (frame 2,348, one point), which
sends the AI generator off at 2,475. Still to do from the inventory: the throw skip and throw link for the third
record, the forced hit's sound and score, `no_look_at` for the bosses, round over (`FUN_8003E608`) and the other
levels' items, tally and stage clear against `force_bosses` and `force_final`.

Update: a dead enemy that blinks out (its active flag alternating) still takes its ground step (its `recover_mash`
counts down while the pose is frozen); only a record that never started stays as the load left it. With that,
**2,213 frames (262-2,474)** match. The first difference is now the CPU opponent's generator at frame 2,475: the
second enemy (a stunned fighter, AI slot 1) draws 8 numbers where the original draws 6, with the AI's own memory
equal before and after, and not a cache effect (`T3_AI_NOCACHE=1` gives the same). It needs the original's AI
inputs at that call (an AI tape of the Force recordings, `fight_oracle_export.AiTape`), or a Python
`ai_update` run from a mid-frame state.

Update (AI ground truth): `scripts/force_ai_trace.py` records every read the original `AiUpdate` makes in one frame of a
recording (a few minutes: it runs the harness to that frame), and the evaluator writes the owned bus's reads in the same
format (`T3_AI_TRACE=FRAME T3_AI_TRACE_OUT=FILE`; `T3_FORCE_AI_BANKS` gives the recorded game's bank placement). Diffing
them at frame 2,475 found the cause: an enemy's health maximum (`+0x3F8`) is its own (the level's value, 20), not the
player's (130), so the AI's health fractions differed. Each fighter now keeps `health_max`. With the opponent index also
made dynamic as in the original (throw partner, else the last attacker when hit, else the last fighter touched, else the
roles' choice) and `last_hit_target` kept, **2,389 frames (262-2,650)** match. The first difference is the player's
`distance` at frame 2,651: it starts a throw on the enemy it is not facing, which is the throw skip / throw link of the
inventory (three-fighter `MoveStartAll`, `count_throws`, `update_throw_link`).

Update: **2,562 frames (262-2,823)** of `force_run` match. Closed since the last note: `dist` for the opponent as it is when
MoveStep runs (this frame's hits may have changed it); `+0x1F` is the opponent index the roles pass stored (the AI and the
move starts read that, not the live one); the records are hit in MoveStep's order (even frames the second record first),
so an enemy that took the alternate KO (attack class 1) gives the record stepped after it no move reaction; the hit
direction is copied whether or not a reaction is chosen; the level runner reads each enemy's throw flag, hit flag and
forced-hit flag (`+0xC4`, `+0xCE`, `+0xE7`), which decide when a defeated enemy turns invulnerable.

The first difference (frame 2,824) is a single-frame `aim_dir` of the player (it should be recomputed, fixed-facing, because
that frame's PairwiseDistances sees an off-screen enemy: the screen x it reads is the one this frame's draw writes, which
needs the owned projection of step 5; the evaluator only has the last frame's). Without the fight camera's projection the
evaluator cannot reproduce it; the long comparison continues past it (2,701 of the first 6,000 rows match).

### Update: owned Force screen x

The fighters' screen x (+0x1278) is now computed in the port: `t3_psx_camera_build_view_trig` builds the view from the Force
camera (pitch and yaw in 1/64 units, sine table only) and the pelvis joint (11) is projected at compose time. It equals the
recording in all but a rare ±1 (about 10 of 6,000 ticks). The comparison is unchanged (2,562 contiguous frames exact).
Open at call 2824: the original recomputes `aim_dir` that frame (it saw `dist > 0x200` where the stand-in gives 510 at screen
x 310); probing with the real-distance branch at that tick confirms the cause is the distance the angle pass sees, and moves the
next failure to the health at call 2964. Not yet explained; the ±1 screen rounding may be the lead (level 4 also needs the roll).

### Update: the first 6,000 frames of `force_run` are exact (all fields, three records)

`T3_FORCE_REFILL=1` mirrors the recorder's refill (`flow_trace.py refill`: the player's health back to its maximum when it
falls below 40, the timer back to 60 s below 10 s) in the evaluator; it is the recorder's doing, not the game's.
Found with `scripts/force_angle_trace.py` (the original's FighterRelativeAngles calls and `aim_dir` writes in one frame)
and the field-by-field comparison:

- MoveBranchStep's look-ahead calls FighterRelativeAngles on the real record after MoveStep set `dist`; past 0x200 it
  moves `aim_dir` and `opp_aim_dir` (the `looked` flag of the VM; the call at 0x8002DE80/0x8002DEA8).
- The level runner's health (the item's +50) is written back to the player; health above the maximum is legal until the
  next hit clamps it (hit apply, HUD bar and round-flow checks relaxed at three fighters only).
- A forced hit's damage zone is the slot's stored zone (the last contact's cylinder, kept per defender slot); its
  `hit_dir` is zero.
- The forced hit is decided after the fighter's flight step (bookkeeping), as in `FighterMovePhysics`.
- A thrown fighter follows its thrower's anchor (`throw_anchor`, 0x578 drift), the throw count covers all three
  records, a throw's partners are decided before `in_throw` redirects the opponent, and every throw partner is not pushed.
- Screen x is projected by the port itself (Force camera view, pelvis joint); the recorded values are no longer read.

Versus: the shared changes are compiled only for the three-record build (`#ifdef T3_PSX_FIGHTERS`); the two-record
`app.bin` grows from 497,944 (before the Force work) to 498,528 bytes (the transaction and a few shared loops). The
Mednafen A/B on the attract fight is still to be run.

### Update: area change, the versus A/B, and what is next

- The level runner's area change sets round state 7 (back to the fight a frame later); the exporter drops the three idle
  frames MatchFlow spends on it (display off, display on, `force_boss`). `force_run`: 6,050 contiguous frames of the
  first level are exact up to the area change; the next frame is the level's boss (record 1 becomes a roster fighter:
  here character 4, costume 16, health 130 at 4,096 units ahead of the camera), which the port does not do yet.
  Without it the round "ends" at once (fighter 1 has no health), so everything after is the missing boss.
- Versus: the two-record program is 498,528 bytes against 497,944 before the Force work (shared transaction loops and a
  few `i<T3_PSX_FIGHTERS` loops; the Force-only branches are behind `T3_PSX_FIGHTERS`). Attract fight, 280 s Mednafen,
  same AI pad hash 0xb99cc280: marked **29.44** images/s, worst segment **27.45** (R13: 29.46 and 27.45; the same build
  varies by 0.1 on single segments between runs). No loss.
- A three-record `tekken3_saturn` links (520,512 bytes, `T3_PSX_FIGHTERS=3`); it does not start a Force level yet.
- Next: `force_boss` (identity, bank, hurt radii, AI bank of the boss, music, health from the level table), then run
  `force_bosses` and `force_final`; then plan step 4 (the device program), rendering, HUD, saving, disc.

### Update: the level's boss and STAGE CLEAR are exact (7,522 frames of `force_run`)

`force_boss` (FUN_800B2B3C, adapted from the remake's `force_boss`): at the area change record 1 becomes the level's boss — its
bank (a copy of its own: the AI addresses banks by object), character, sounds, model data, a stance queued and begun at the
next MoveStartAll facing the player, boss health from the level table, placed 4,096 units ahead of the camera; the player's
root goes to its anchor and both records' eyes restart. The step runs on a private copy of the config while a three-record
match plays, so record 1's data can change. Host runs name the boss's data by an existing actor
(`config->force_boss_like`, `T3_FORCE_BOSS_LIKE`; `force_run`'s boss is the player's own character); the device program
will look it up in BOSS.BIN. STAGE CLEAR (round state 8) replaces the replay when the player wins; it ends on Start after
the tally, or after 300 frames. All 7,522 contiguous frames of `force_run` — the first level, its boss, the KO and the
stage-clear — pass on every field. `force_bosses` starts with another character (19, costume 1): the evaluator needs that
character's banks and costume model (`build_oracle_pair_evaluator.py --fighters 19 21 22` builds, but the model is the default
costume's: sphere centres differ by the costume's scale), so it is not compared yet. `scripts/force_first_running.py` finds a
recording's first live frame (261 in all three).

### Update: `force_bosses` (Doctor B, costume 1): 7,593 of 9,578 rows exact

The recording plays character 19 in costume slot 45 (the costume key table maps key 77 to slot 45, not the roster's 44):
`build_oracle_pair_evaluator.py --player-slot 45` builds that model (record 251 extracted with `psx_disc_import.py
--extract-ids 251`). Closed on the way, each found by the first differing row and, for the AI, by
`force_ai_trace.py` + `ai_first_diff.py` (the fields the original's AiUpdate read that the bus answered differently):

- `whiffed` (+0xD2) is decided at the end of every ordered HitTest call, so an attack that lands only in the second pair
  of its frame still reads as a whiff to the AI; the pairs run in the roles' order (player, its target, the other
  enemy), not by index.
- No item pickup while the player is in the air (`+0xDB`).

Open at row 7593: the level's boss is character 5 (Nina, costume 1, bank type 5) for this player, so the host shortcut
`force_boss_like` does not apply: the evaluator needs a third bank (the boss's, linked to the player's), its model, body
radii, scale and AI image. That is also what BOSS.BIN supplies on the device.

### Update: the boss from its own bank (`force_bosses`: 8,118 of 9,578 rows exact)

The level's boss now comes from its own data when the config names it (`force_boss_bank`, `_parts`, `_shapes`, `_scale`,
`_character`, `_voice`): the match keeps copies of the player's and the boss's banks, linked to each other (the merged move
index) and addressed by object by the AI bus. The host evaluator builds the boss's model, radii, scale and voice
(`build_oracle_pair_evaluator.py --boss 5 --boss-slot 11`, the costume slot from the key table) and reads its bank at run time
(`T3_FORCE_BOSS_BANK=<NN.t3logic>`); `build_local_divmot_assets.py --extra-bank` makes the combat tables cover it. With Nina
(character 5, costume 1) as Doctor B's boss, `force_bosses` runs through the boss's entrance and 530 frames of its fight.
All 9,578 rows pass (Doctor B vs the level's Nina boss, to the end of the contiguous run). The failure at row 8118 was the
player's VM still playing the original bank object while the AI and the merged move index used the linked copies: the
reaction move could not be queued. The player's VM now plays its own copy, and after an atomic step the pointers into the
staged copies follow the struct (the copies live in the match).

### Update: `force_final` (Jin, costume slot 19; boss Paul costume 1): 5,398 of 7,882 rows exact

`build_oracle_pair_evaluator.py --fighters 9 21 22 --player-slot 19 --boss 0 --boss-slot 1` (seed 15). Open at row 5398: the
player starts a throw on enemy 2 (slots 2795/2999, both at the player's new anchor) and the bystander, enemy 1, is pushed
away by the body separation of that frame: the original moves it (+52, +12), the port (+228, +52). The pair (1,2) is the one
that pushes; the overlap there depends on the thrown victim's body spheres in its first throw frame (its radii rule reads
`was_hit_this_move` and `in_air`), which is where to look. `force_bosses` passes all 9,578 rows and `force_run` all 7,522.

### Update: `force_final` exact; all three recordings pass every compared row (24,982 frames)

`scripts/force_separate_trace.py` (the original's BodySeparate calls in one frame: the three records' anchor, root and facing)
showed that in the first frame of a throw the victim's root already comes from the anchor its start set, so the throw anchor
moves the root only from the second frame on; with that the bystander's push is the original's. The wall correction of a
linked throw now moves the partner for any two of the three records. `force_run` 7,522, `force_bosses` 9,578 and
`force_final` 7,882 contiguous frames match on every field.

Versus A/B on this state (280 s Mednafen attract fight, same AI pad hash 0xb99cc280): marked **29.46** images/s, worst
segment **27.45**, 5,835 ticks, no discarded fields — equal to R13's 29.46 / 27.45. The two-record `app.bin` is 498,560 bytes.

### Step 4, first measurement: the versus program rebuilt with three records (2026-11-08)

`cmake -DT3_PSX_FIGHTERS_COUNT=3` on the versus target links (no source change): `.text` 522,972 against 498,048 bytes, `.bss`
447,520 against 442,080, `.data` 512; `app.bin` 520,512 against 498,560. High WRAM ends at 0x06100000: the versus program leaves
91,504 bytes, the three-record build **61,136** bytes (before the third actor's own buffers: `T3_DRAW_ACTORS=3` compiles but its
vertex, pose-cache and command memory are not counted, and the versus-only code and data a Force program drops are not
subtracted). The device program itself (a `tekken3_force` target that builds the three-record config from the level's
files, loads the player's and one enemy pack, runs `psx_force_level.c` and draws three actors) is the next piece of work;
`move_probe.c` (the versus config and asset loading, about 600 lines) is its model.
