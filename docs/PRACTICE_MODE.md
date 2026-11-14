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
- COMBO TRAINING: a third MODE (COMBO PLAYER, COMBO TYPE); Select plays the guide.
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

## The menu-built disc

A disc built the shipped way (`build_game_disc.py` without `--boot-fighters`, with `-DT3_MENU_SELFTEST=ON
-DT3_PRACTICE_SELFTEST=ON`) was recorded with Mednafen's own movie recorder (`-video.driver softfb -qtrecord`, no window
or keys needed; the movie was deleted after its frames were read). The scripted pad went through the title (START), two UP
from ARCADE MODE to PRACTICE MODE, A, the player select (Paul against Law), the loading screens, and into the practice
fight: the practice HUD (TOTAL DMG, DMG, PCT, COMBO, the key display `R`, `R+1`), the green freeze-signal bars, the pause
menu with its rows, and the attack data after punches (TOTAL DMG 14, DMG 6, PCT 7). So the arg-based entry
(`ARG_FLAGS` bit 1) and the menu's hand-over to the fight program work in the shipped flow.

## COMBO TRAINING

`scripts/extract_practice_combos.py` reads the combo tables (62 combos for 17 bank types) from practice.ovl (Japan Rev.1's
overlay under `third_party/Tekken3Remake/work/jp_rev1/bns/`) into a private blob and a C source;
`prepare_game_assets.py` runs it, `build_game_disc.py` passes `-DT3_LOCAL_PRACTICE_COMBOS`, and `move_probe.c` reads the
weak `t3_practice_combo_blob` symbols (no tables: no combos). `practice_guide.c` is the guide and recorder state machine
(`_guide_step`, `_guide_ready`, `_guide_play`, `load_guide`). While the guide plays, its scripted word replaces the guided
fighter's pad for that frame only (the original resets the input mode every frame). Select is the Saturn pad's Z.
The golden test replays 362 guide frames of the original trace with 0 mismatches; the trace only covers READY, STAND and
PLAY, so recording and replay are covered by unit tests only, and the stance slot and lying state, which the records do
not hold, are taken from what the original did.

In the emulator the test disc's scripted pad went FREE, VS CPU, COMBO TRAINING, pressed Select, and Paul played the combo
(COMBO 3, HIGH markers, TOTAL DMG 84) against the standing dummy; the prompt returned to `PLAY = SELECT` afterwards.
The test disc has no arena wall, so a scripted walk of several hundred frames pushes both fighters past the renderer's
±30000 range and ends in `INVALID ARG` (`t3_psx_skin_placement`); the scripted walks are short for that reason.

## The FREE page's input recorder

As in the original, the recorder (S+0x87) is switched on by a chord held as the menu closes (the original's
L2+R2+L1+R1+circle; here L+R+Z). Select (the Saturn pad's Z) replays the recorded input, Down+Select records a new one, and
Select ends the recording; `practice_guide.c` is the same state machine as COMBO TRAINING's. Recorded on Mednafen from a
scripted pad: `PLAY(SEL) REC(DW+SEL)`, `START REC = ANY KEY`, a recorded walk and punch (TOTAL DMG 13), and the prompt back.
The replay's own frames were not isolated in the stills.

## REPLAY SETTINGS

`practice_replay.c` is `_replay_control`: OFF, after a combo of 4, 5 or 6 hits (plus 30 frames), or MANUAL (Select), counted
in REPLAY_FRAMES (S+0x10, at most 300) and COMBO_FRAMES (S+0x12); a replay is at least 90 frames. The counters and the
dummy's dropped combo match the original trace on 1363 frames (the trace has the setting OFF throughout, so the start rules
are covered by unit tests only). Playback reuses the KO replay's ring: `t3_psx_replay_start_back` ports
`start(fight, back)`, `t3_psx_match.practice_replay_back` starts it at the next step, and the fight stands still while the
poses play back and `REPLAY` blinks; when it ends the ring records again. On Mednafen, Select with MANUAL played back
earlier positions with the blinking `REPLAY` text and the fight resumed. Sounds, effects and the replay camera are not
replayed separately from the KO replay's own handling.

## COMMAND LIST

`scripts/extract_command_lists.py` decodes each roster character's USA command list (ARC member 4: names and commands) with
Tekken3Remake's `move_text.py` into a private blob and C source (403 lines for 21 characters, 10.7 KB); `prepare_game_assets.py`
runs it and `build_game_disc.py` passes `-DT3_LOCAL_PRACTICE_COMMANDS`. `practice_commands.c` parses it, `practice_menu.c` has
the COMMAND LIST row and the scrolling (up and down by one, left and right by five, wrapping; a face button or START leaves),
and `main.c` draws the list as text. On Mednafen the list of the player's character scrolled with its real names and commands.
It is plain text in the debug font, not the original's glyph atlas and button sprites.

## The key display's pictures

`scripts/build_practice_icons.py` bakes PKEYS.BIN (68 pictures, 28 KB) from the USA executable's system textures plus the pad
pictures the title overlay uploads for the fights (a plain 4-bit TIM at VRAM (384, 224)): nine arrows in four colours and the
face-button icons (16 combinations on the guide-coloured plate; the two sprites of FUN_800B721C composited, the symbols
over the plate). `practice_keys.c` ports the item rule of FUN_800B7C84 (new directions and newly held buttons, the last 15,
the guide's colours), `src/saturn/practice_icons.c` uploads and draws them, and CMake builds the pack when the title overlay
is a build input (`T3_PRACTICE_ICONS`); without it the display stays text. On Mednafen the arrow and the pink LP
icons showed along the bottom for the scripted walk and punches.

## The hit markers' pictures

PKEYS.BIN also carries the markers' burst, the HIGH, MID and LOW labels, and the exclamation mark on a red burst for
unblockable attacks (page 15; the overlay's marker table). A marker's kind comes from the attack word as in
`practice_sim.py`'s `hit_marker`, its position from the attack segment's end at contact (`fighter[attacker].hit_gate.segment`
into the attacker's `strike` segments, projected through the fight camera), and it lasts 60 frames. On Mednafen the burst with
its HIGH label appeared on the dummy at the hit. The segment's end stands in for HitTest's slot point, which the C match does
not keep; for attacks whose joint is 0x18 or more the original uses the defender's zone joint instead, which is not done.

## The menu's structure and font

`practice_menu.c` follows the original's pages (FUN_800B49C4, FUN_800B4C8C, FUN_800B4E4C): the first pause opens MODE SELECT
(FREE, VS CPU, COMBO TRAINING, PLAYER SELECT, RESET; moving the cursor onto a sub-mode resets it as in the original, a face
button chooses). A page starts with OK/CANCEL (it reads CANCEL until a value changes, then OK; a face button resumes the fight)
and COMMAND LIST, then the sub-mode's items, and ends with RETURN TO MODE SELECT. Each page has its own cursor, a face button on
a value row sends the cursor back to the first row, and left and right change the value. The menu comes back on the page it left.
`t3_practice_menu_draw` (`practice_hud.c`) draws them in the original font on the original panels (dark blue (0, 16, 48) in a
light grey frame; MODE SELECT 172 x 192 at (8, 40), a page 240 x 316 at (64, 40); title font 1 at (72, 56); rows font 0 from y
110, the cursor row colour 10 and the others 11; values right-aligned to column 25; the dummy's setting on the line under its
label; COMBO PLAYER n- with the character's name at x 207; RETURN TO MODE SELECT at y 328). The tech-roll settings print TECH ROLL with the original's down (UKEMI MAE) or up (UKEMI OKU) arrow picture
(PKEYS.BIN pieces 74-77, menu colours 10 and 11; seen on Mednafen). Start closes the menu (as it did before the port's pages
existed). Without PFONT.BIN the debug-font rows remain.

## The command list screen

`scripts/extract_command_lists.py` now keeps each command's arrow (0x80-0x90) and button (0x91-0xA0) codes in the blob next to the
ASCII. `command_screen` (`practice_hud.c`, FUN_800789E4) draws five cards 64 pixels apart in a 182-pixel column (grey frame, dark
blue body, the light top fading from it), the cursor's move as the top card and the rest following (wrapping), the title bar and
rules, the name at 6 pixels a cell (wrapping at 28 cells; the font has capitals only, so the mixed case is capitalised), and the
command with the key display's arrow and button pictures at 20 pixels each. `PUSH BUTTON TO EXIT` is at the bottom; the key display
is hidden while the list shows. The list eases between cards as FUN_800789E4 does (the offset gains 64 pixels a line and loses a
third of itself plus a pixel each frame; `command_scroll` in `practice_menu.c`, tested in `test_practice_menu`), the cards cut to
the original's window (y 116 to 436: a card's text and pictures show once they fit, its rectangles are clipped). With the dummy
on CONTROLLER and pad 2 connected a second column (x 184) shows the dummy's list, moved by pad 2. Character 6's lines 6 to 13
and 14 on get the original's green and gold cards with a flat header card at the start of each group, and character 15 shows one
card mid screen (`card_colours`, `command_column`); the colours and the second column were not seen on Mednafen (the test disc
plays Paul against Law). The title is the USA list's own: STR_TITLE (0x80027C4C) is the atlas word 0x01, "When Facing Right", drawn at (x0 + 60, 97) in the font's capitals. On Mednafen the list of the player's
character scrolled with its names and pictures.

## The replay's camera

A REPLAY (Select with MANUAL, or after a combo) ran the KO replay's camera (`t3_psx_camera_replay_start`: the fight source's weight
0, the script source's weight full, `enabled` 0). Nothing gave the fight camera back, because in the original a round follows a KO
replay. After a practice replay the camera stood still while the fighters walked, and soon both were off the screen (seen on
Mednafen, with W1 = 4096 and the eye fixed). `t3_psx_camera_replay_end` (called where the practice code resets the replay ring)
resets the director and re-seeds the sources, so the fight camera restarts on the pair. Checked on Mednafen: after the replays and a
combo the camera follows the pair to the arena wall.

## Verification of the finished port

`test_practice_menu` (pages, cursors, easing, second list), `test_practice_camera_replay` (the camera handed back after a replay),
the earlier golden tests, and Mednafen on the test disc (scripted pad: MODE SELECT, the three pages, tech roll arrow, replays,
combos, the command list) and on the menu-built disc (title, PRACTICE MODE, player select, loading, the practice fight with its
HUD, key pictures and combos). The shipped disc (`build/saturn`, `music.cue`) was rebuilt with the practice command lists and
combos and passes `play_mednafen.py`'s feature check.

## FREEZE SIGNAL's tint

The original tints each fighter's back light (FUN_8003AA6C) green while it can act and red while it cannot. Fighter lighting would
cost frame rate (docs/FIGHTER_FIDELITY.md: 21.96 img/s against 24.8) and cannot fit VDP1 VRAM beside the practice fonts, so the
port tints the fighter's CRAM palette banks instead (`t3_characters_tint` in `characters.c`): at load each actor's INDEX8 palettes
are kept, and when the signal changes the colours are rewritten a sixth of the way to green or red (bit 15 and index 0 kept).
That is a few hundred words written only when a fighter's state flips, so there is no per-frame cost and no frame-rate change.
On Mednafen the fighters turned green and red as they could and could not act, on the test disc and on the menu-built disc.

## Hit markers and menus

The hit markers are not drawn while the pause menu or the command list is open (a HIGH marker showed over the list). With
Hwoarang against Law on the test disc (pad 2 connected, the dummy on CONTROLLER) the command list showed both columns, and
scrolled to Hwoarang's later lines the green and gold cards with their flat header cards appeared.

## Checked against the original and on the shipped disc

The tech-roll line, the pause pages, the HUD, the key display, the combo counter and the menu's cursor, input and backdrop are
compared with the original overlay running in the CPU harness: `verify_practice_sim.py` (TechRoll 1000 cases, PausePage, PracticeHud,
KeyDisplay, ComboCounter, MenuCursor, MenuInput, MenuBackdrop: 0 mismatches), so the arrow's place is the original routine's. The
command list's title is the USA executable's own string.

The shipped disc (`build/saturn`, `music.cue`) was driven into practice mode on the headless Ymir probe with a pad script
(`scripts/ymir_practice_flow.py`; no window, no keyboard): the opening movie, the title, UP twice to PRACTICE MODE, the select, the
practice fight with its attack data, START for MODE SELECT, the FREE page, TECH ROLL with its arrow, FREEZE SIGNAL on and the fighters
tinted green and red, the key pictures, and the command list (WHEN FACING RIGHT, cards with pictures, the easing) and back.
Key injection into Mednafen is not used: it takes the keyboard.
