# Tekken Force integration review — 2026-11-10

Original review verdict (before the repairs below): the Saturn frontend is an incomplete bring-up program and is not a
playable implementation of Tekken Force. The documented host simulation results
do not establish that the device executable works.

Scope: current working tree, including uncommitted `force_main.c`; the pinned
remake's `CLAUDE.md`, `docs/research/code/modes.md`; the Force plan; existing
Mednafen screenshots. No source fixes, fresh emulator run, recording or new
golden comparison was performed. Disk usage: build 117 MB, imported 4.6 GB.

## Blocking findings

1. **P0 — Saturn input bits are passed as game input bits.**
   `src/saturn/force_main.c:482` directly constructs `t3_input_t` from
   `pad0.held/pressed`, while line 517 disables physical-input conversion.
   These bit layouts are different (`fight.h:26` and LibSaturn `input.h`).
   Saturn directions occupy bits 12–15; game directions occupy bits 0–3.
   Saturn X (bit 6) becomes RP instead of LP; A (bit 2) becomes a lateral
   direction instead of LK; Start (bit 3) becomes another lateral direction.
   Normal walking and attacks cannot work with this boundary. Implement and
   validate the Force-specific physical/screen-side mapping; merely copying
   the duel's relative mapping also needs checks when targets change sides.

2. **P0 — No demonstrated visible fighters in the current device build.**
   The latest available capture, `build/force-work/shots-prio/e.png`
   (2026-11-09 08:38), shows the panorama without fighters or diagnostics.
   The executable and frontend source were timestamped immediately before it.
   An earlier capture, `shots-dbg1/s38.png`, shows a fatal error in FORCE_MAIN.C
   (line 445, code 12 in that build; do not map that historical line onto the
   current source). `characters.c:773–780` requires a complete bridged pose;
   line 2200 suppresses any actor that fails it. This is a relevant diagnostic
   gate, not a proven cause: camera, geometry and submission need observation
   before attributing the blank scene to a particular subsystem. A static
   background capture proves neither live simulation nor working rendering.

3. **P1 — Boss support exists in the core but is never configured by the frontend.**
   `init_match_config` zeroes the config and never supplies `force_boss_bank`
   or `force_boss_like`. `psx_match.c:1594` consequently skips boss setup.
   `force_main.c` does not read BOSS.BIN or load the selected boss's bank,
   body/model data or renderer assets. Packaging BOSS.BIN does not connect it.
   The end-of-area fight cannot become the original's roster boss encounter.

4. **P1 — A stage clear exits to the menu instead of continuing the run.**
   `force_main.c:493–510` constructs next-level state but executes MENU.BIN.
   The block is packed as 40 bytes but sent with `SAT_EXEC_ARG_MAX` (64),
   whereas `t3_force_handoff_unpack` requires exactly 40 bytes. The menu
   accepts its own BOOT_ARG_BYTES protocol (`menu_main.c:894`), not a Force
   continuation. There is no complete path through four stages, the earned
   final challenge and results. No explicit game-over frontend is present.

5. **P1 — Essential game information and pickups have no presentation.**
   The frame draws actors and optional diagnostic text (`force_main.c:552–573`).
   It does not draw health, remaining time, score, progress, chicken pickups,
   key icons, defeat-time popups or the stage tally. The runner can heal the
   player (`psx_match.c:1707`) while the pickup itself remains invisible.
   Sound, model-request and save events emitted by the runner are also not
   connected to device presentation/persistence; MODEL_BIND only restarts
   blink state (`psx_match.c:1719–1727`). Both enemy actors use the same F21
   pack regardless of requested costume.

6. **P1 — The menu bypasses character selection and saved Force progress.**
   `menu_main.c:902–910` launches Force before the normal selection flow,
   with a zeroed handoff: character 0, no keys and no saved high score.
   The frontend ignores carried costume, kills and seed, and has no save or
   results integration. This is observable product behavior, not just missing
   cosmetic polish.

## Additional gaps and limits

- Done (2026-11-12): the floor descriptor, the panorama at the original's scale and top edge, and level 4's
  staircase (stepped panorama and rolled camera); see the plan's "Panorama scale and level 4's staircase".
  The Doctor B. level's black-tile-only panorama (`0x800B70F4`) is done too.
- The loop advances one simulation tick for each rendered iteration. It has
  no elapsed-field catch-up. Any render iteration spanning multiple video
  fields slows gameplay; actual Force timing requires a measurement. The
  documented versus A/B cannot establish the three-actor frontend's rate.
- The standalone `build/force-work/disc/tekken3_force.cue` fails the existing
  `play_mednafen.py` feature check: it lacks INTRO.MOV, VSBG0.BIN, MENU.BIN,
  INTRO.BIN and END00.MOV. It is a diagnostic disc, not a whole-game deliverable.
- The plan reports 24,982 compared host frames across three initial chunks.
  Those results are valuable evidence for the core, but were not rerun in
  this review. Later carried-state chunks remain an explicit coverage gap.
  The plan's opening status and final integration note are stale relative
  to the executable and screenshots now on disk.

## Repair order and acceptance

First establish correct physical controls and one visible, animated player
with two enemies in the device build. Then prove movement, facing, targeting,
hits, throws and deaths with controlled inputs. Add the health/time HUD and
visible pickups so a fight can be assessed while playing. Connect enemy
costume requests and real boss loading; finish stage continuation, defeat,
selection, persistence and results. Implement floor/staircase presentation.
Measure simulation ticks per second and rendering cost on that runnable path.
Finally play the whole mode from the full-game menu through its ending,
including a saved-key return and the final challenge. Host trace equality
remains a separate required check whenever shared simulation code changes.

## Repair follow-up (2026-11-10)

This review's line numbers and findings describe its original snapshot.
The working tree now maps Saturn controls through the original physical-input
path and schedules simulation from elapsed video fields. A player is visibly
animated; the initial loading screen uses original PS1 glyphs. The gameplay
HUD and stage tally are connected, and the menu's Force entry now performs a
single-player character selection (compile-checked). The Force library now
explicitly enables Saturn optimization, freeing roughly 106 KB of High WRAM.

Current runtime evidence and remaining acceptance limits are recorded in
[TEKKEN_FORCE_PLAN.md](TEKKEN_FORCE_PLAN.md#force-frontend-repair-loading-art-and-memory-2026-11-10).
MacOS scripted key events failed to produce pad input; they must not be used
as movement/combat evidence. Boss loading, full progression, save/results,
pickup art, floor/staircase and whole-disc play remain open.

The first-spawn crash was reproduced with an explicit input replay and traced
to zero-filled private Force constants. The constants, boss table, keys and
trig assets were regenerated, and asset validation/regressions now reject
placeholder data. A subsequent device replay visibly walks, spawns an enemy,
exchanges hits and changes health/score. Scheduling still drops fields under
load; the plan records the measurements. Stage handoff routing/length and
carried RNG/kills are repaired in source but not yet verified through a clear.

## Repair follow-up (2026-11-11)

Labelled device probes now exercise live boss replacement (including an
alternate costume), the original fourth-stage tally, successive cold-boot
key saves, continuation into Doctor B.'s fifth stage, and the final rescue
save. The three-key and rescued-Doctor backup records have valid checksums;
BIOS write/read-back returns SAT_OK. These injected boundary/defeat cases
are not evidence of a complete played run. Detailed inputs, screenshots,
save values and limitations are recorded in
[the current plan](TEKKEN_FORCE_PLAN.md#saved-keys-and-run-end-paths-2026-11-11).

The standalone diagnostic disc now includes MENU.BIN and FORCE.BIN for menu
return and continuation, but still lacks the full-game intro, VS and ending
resources. It remains unsuitable as the user deliverable. Performance,
pickup presentation, enemy variants, sound, floor/staircase presentation,
ranking flow and full-game play remain acceptance gaps. The result screen has its original
art in MENU.BIN since 2026-11-12 (TEKKEN_FORCE_PLAN.md), now with the bosses' faces and the result music (track 6).

Chicken and shadow sprites are now extracted from the original stage TIMs
and included in Force's scene painter. A labelled device probe shows the
item, the 80-to-130 heal, LIFE UP! and its expiry; original pickup and defeat
popup coordinates are now connected. The popup fonts remain generic and
natural scripted-item play still needs acceptance. Detailed evidence and
the alpha-blending limitation are in
[the pickup follow-up](TEKKEN_FORCE_PLAN.md#chicken-pickups-and-projected-popups-2026-11-11).

Enemy model requests now bind the correct resident Crow/Falcon/Hawk/Owl
geometry and textures independently for both slots. Device probes exercise
all four models, three rebinds per enemy and the subsequent boss reload.
The shadow palette no longer overwrites an INDEX8 boss palette. Resource
lifetimes, memory bounds, screenshots and test limitations are documented
in [the variant follow-up](TEKKEN_FORCE_PLAN.md#resident-enemy-variants-and-palette-isolation-2026-11-11).
Natural progression across every stage and the remaining performance,
audio, stage, HUD/result, ranking and whole-disc checks are still open.
