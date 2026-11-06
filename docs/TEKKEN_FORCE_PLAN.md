# Tekken Force: plan

Status: plan, nothing implemented. The menu lists TEKKEN FORCE and does nothing
(`docs/ROSTER.md`, `docs/ROADMAP.md`).

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
