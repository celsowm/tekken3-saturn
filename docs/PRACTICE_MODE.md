# Practice mode

Practice follows Tekken3Remake's `practice_mode.gd` and `modes.md#practice` (PolyForm Noncommercial; the C is an
adaptation with attribution in `NOTICE.md`). Files: `src/game/practice_data.c` (attack data, hit markers),
`practice_dummy.c` (training dummy), `practice_menu.c` (pause menu), `practice_keys.c` (key ring),
`practice_signal.c` (freeze signal); the Saturn side is in `src/saturn/move_probe.c` (per-tick work) and `main.c`.

## What it does

- No damage (`HitApply` suppressed), 200 health (`FUN_800519B4`), no round timer, the caption PRACTICE MODE, no winner
  message. After a KO-less round the fight simply goes on.
- START opens a pause menu; the fight stands still while it is open. Rows: MODE (FREE, VS CPU), TRAINING DUMMY (STAND,
  CROUCH, STAND GUARD, CROUCH GUARD, GUARD ALL, AUTO GUARD, UKEMI MAE, UKEMI OKU, CONTROLLER) and COUNTER ATTACKS on
  FREE, CPU DIFFICULTY and CPU LEVEL on VS CPU, then ATTACK DATA, FREEZE SIGNAL, KEY DISPLAY, PLAYER SELECT, EXIT.
- Attack data per side: TOTAL DMG, DMG of the last hit, its share (PCT), COMBO; a hit marker per clean hit (text level:
  HIGH, MID, LOW, UNBLOCKABLE); the key display (last inputs as text, e.g. `R+1`).
- FREEZE SIGNAL: `t3_practice_can_act` (a port of `FUN_800B3520`) and a per-actor back colour in `fighter_light.c`
  (green when the fighter can act, red when it cannot). It needs `T3_FIGHTER_LIGHTING`.

## The Slave CPU

The simulation runs on the Slave (`t3_simulation_task_body`), so the practice work runs there too. The menu's settings
travel in `simulation_job_t` (both job builders: `simulation_submit` and the prepare job), and the results (attack data,
markers, key ring, can-act flags) come back in `t3_practice_state_t`, which is in `t3_move_probe_simulation_sync`'s
query and reset lists.

## Verification

- Host: `test_practice_data`, `_dummy`, `_menu`, `_keys`, `_signal`; the match step with a stopped timer and a practice
  hit through the real transaction (`test_psx_match`, `test_psx_hit_transaction`).
- Original code: `scripts/export_practice_trace.py` turns `flow_trace.py practice` (the original practice overlay in the
  CPU harness) into cases; `tests/test_practice_golden.c` replays them: attack data 1564 frames (16 clean hits, 140 inside a
  combo), key ring 1000 frames, hit markers 1564 frames, 0 mismatches. The trace is derived from the game and not in git.
  The throw flag comes from the original's stored value (the records do not hold the move row).
- Emulator: a test disc (`-DT3_BOOT_FIGHTERS=0;1 -DT3_BOOT_PRACTICE=ON -DT3_PRACTICE_SELFTEST=ON`, built with
  `scripts/build_game_disc.py --boot-fighters 0 1` from `prepare_game_assets.py` output) boots straight into practice;
  with `T3_PRACTICE_SELFTEST` a scripted pad 1 opens the menu, sets the dummy to CROUCH, COUNTER ATTACKS and FREEZE
  SIGNAL on, closes it, walks up and punches, then sets STAND and punches again. Mednafen screenshots
  (`scripts/mednafen_shots.py`) showed: the menu and its values; the dummy crouching; the key display logging the
  scripted inputs; hit sparks, TOTAL DMG 14, 49, 84, 119, 140 (DMG 6, PCT 7, COMBO 1) and HIGH markers against the standing
  dummy; full health bars; and no TIME UP through tick 3301, round 1. The freeze signal's colour was not seen (the test
  disc has no fighter lighting).

## Not ported

COMBO TRAINING (needs the executable's combo tables), COMMAND LIST (move-list text), REPLAY SETTINGS (a replay
recorder driven from practice), the key display's and hit markers' sprites, and the original-font layout of the menu and
readouts (debug font for now).
