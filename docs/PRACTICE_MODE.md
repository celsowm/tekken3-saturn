# Practice mode

Practice follows Tekken3Remake's `practice_mode.gd` and `modes.md#practice` (PolyForm Noncommercial; the C is an
adaptation with attribution in `NOTICE.md`). Files: `src/game/practice_data.c` (attack data, hit markers),
`practice_dummy.c` (training dummy), `practice_menu.c` (pause menu), `practice_keys.c` (key ring),
`practice_signal.c` (freeze signal); the Saturn side is in `src/saturn/move_probe.c` (per-tick work) and `main.c`.

## What it does

The Saturn game is the USA release, so practice follows the USA overlay (`SLUS_004.02`'s practice.ovl, BNS record 6) and not Japan
Rev.1's (the remake's docs/research/code/usa-version.md lists the differences). It was compared with the original USA game running in
PCSX-Redux (`scripts/pcsx_redux_practice_capture.lua` with `scripts/capture_ps1_menus.py --lua ... --names ...`).

- No damage (`HitApply` suppressed), 200 health (`FUN_800519B4`), no round timer, the caption PRACTICE MODE, no winner message.
- START opens a pause menu; the fight stands still while it is open. The first pause shows MODE SELECT: 1P FREESTYLE, VS CPU,
  COMBO TRAINING, PLAYER SELECT, RESET. A page starts with OK/CANCEL and COMMAND LIST and ends with RETURN TO MODE SELECT. The USA
  pages: 1P FREESTYLE has TRAINING DUMMY (STAND, CROUCH, STAND GUARD, CROUCH GUARD, GUARD ALL, AUTO GUARD, UKEMI MAE, UKEMI OKU,
  CONTROLLER; the two rolls print QUICK ROLL with an arrow), COUNTER ATTACKS, ATTACK DATA, HIT ANALYSIS and COMBO REPLAY; VS.CPU has
  CPU DIFFICULTY and CPU LEVEL in place of the first two; COMBO TRAINING has COMBO PLAYER and COMBO TYPE. There is no KEY DISPLAY
  row in the USA tables (`0 7 13 2 1 3 4 14` and so on).
- Attack data per side: TOTAL DMG, DMG of the last hit, its share (PCT), COMBO; a hit marker per clean hit (text level: HIGH, MID,
  LOW, UNBLOCKABLE); the key display (the last inputs as pictures) shows only with the guide, that is in COMBO TRAINING.
- HIT ANALYSIS (Japan's FREEZE SIGNAL): `t3_practice_can_act` (a port of `FUN_800B3520`) and a per-actor colour: both fighters
  turn green while they can act and red while they cannot (see "FREEZE SIGNAL's tint").

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
  dummy; full health bars; and no TIME UP through tick 3301, round 1. The freeze signal's colour needs fighter lighting
  (see "FREEZE SIGNAL's tint").

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
its HIGH label appeared on the dummy at the hit. The segment's end is HitTest's slot point (`HitTestSegments` stores the segment's
end, `combat.gd` `_hit_segments`), which the C match does not keep. For attacks whose descriptor joint is 0x18 or more (fire,
breath, gas) the segment is the one the effect object published, which the C match keeps as `projectile` (`projectile_hit`), so the
marker takes `projectile.end` then. An earlier note here said the original uses the defender's zone joint for those attacks; it
does not: the zone only selects the cylinder that was hit.

## The menu's structure and font

`practice_menu.c` follows the original's pages (FUN_800B49C4, FUN_800B4C8C, FUN_800B4E4C): the first pause opens MODE SELECT
(1P FREESTYLE, VS CPU, COMBO TRAINING, PLAYER SELECT, RESET; moving the cursor onto a sub-mode resets it as in the original, a face
button chooses). A page starts with OK/CANCEL (it reads CANCEL until a value changes, then OK; a face button resumes the fight)
and COMMAND LIST, then the sub-mode's items, and ends with RETURN TO MODE SELECT. Each page has its own cursor, a face button on
a value row sends the cursor back to the first row, and left and right change the value. The menu comes back on the page it left.
`t3_practice_menu_draw` (`practice_hud.c`) draws them in the original font on the original panels (dark blue (0, 16, 48) in a
light grey frame; MODE SELECT 172 x 192 at (8, 40), a page 240 x 316 at (64, 40); title font 1 at (72, 56); rows font 0 from y
110, the cursor row colour 10 and the others 11; values right-aligned to column 25; the dummy's setting on the line under its
label; COMBO PLAYER n- with the character's name at x 207; RETURN TO MODE SELECT at y 328). The roll settings print QUICK ROLL (the USA word; Japan's is TECH ROLL) with the original's down (UKEMI MAE) or up (UKEMI OKU) arrow picture
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
plays Paul against Law). The title is the USA list's own: STR_TITLE (0x80027C4C) is the atlas word 0x01, "When Facing Right", drawn at (x0 + 0x27, 0x61) in the font's capitals. On Mednafen the list of the player's
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

## FREEZE SIGNAL's tint (HIT ANALYSIS in the USA game)

The original tints each fighter's back light (FUN_8003AA6C) green while it can act and red while it cannot. The port does the
same through the fighter lighting (docs/FIGHTER_FIDELITY.md, "Lighting as the default"): `t3_practice_signal_colour` gives the
back colour, `t3_fighter_light_back` rebuilds the actor's shade table with it and the next relight rewrites every Gouraud table. It
is a few hundred words written only when a fighter's state flips. The fighters turn green and red as they can and cannot act
(Ymir, `scripts/ymir_practice_flow.py`, on the shipped disc). The earlier CRAM-bank tint was removed: the lit fighters' textures are
LUT4 and no palette bank carries the colour, and a build without lighting has no freeze signal.

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
practice fight with its attack data, START for MODE SELECT, the 1P FREESTYLE page, QUICK ROLL with its arrow, HIT ANALYSIS on and the fighters
tinted green and red, the key pictures, and the command list (WHEN FACING RIGHT, cards with pictures, the easing) and back.
Key injection into Mednafen is not used: it takes the keyboard.

## Checked against the USA game on a PS1 emulator

PCSX-Redux ran the USA disc with a Lua pad script (no keyboard) into practice mode and saved stills: MODE SELECT, the 1P FREESTYLE
page, the QUICK ROLL line and HIT ANALYSIS on. They showed that the USA page has no KEY DISPLAY row, calls Japan's FREE "1P
FREESTYLE", FREEZE SIGNAL "HIT ANALYSIS" and REPLAY SETTINGS "COMBO REPLAY", and that HIT ANALYSIS turns both fighters strongly green.
The port was changed to match. The rows of the 1P FREESTYLE page, measured as fractions of the displayed picture, agree with the PS1
still to within 0.002 (OK 0.219 against 0.219, COMMAND LIST 0.268/0.268, TRAINING DUMMY 0.333/0.333, COUNTER ATTACKS 0.433/0.431, ATTACK
DATA 0.478/0.480, HIT ANALYSIS 0.526/0.529, COMBO REPLAY 0.580/0.578, RETURN TO MODE SELECT 0.752/0.757; the PS1 still's 480 lines
are mapped to the Saturn's 224 as (y - 20) / 448).

The command list was measured the same way (PS1 still `list`, Saturn field 8520, the first card at the top): the six card frame lines
sit at 0.211, 0.356, 0.499, 0.642, 0.785 and 0.927 of the picture on the PS1 (as (y - 20) / 448) against 0.212, 0.353, 0.496, 0.638,
0.781 and 0.926 here (within 0.004, one Saturn pixel), and the cards span x 0.005 to 0.492 of the width against 0.006 to 0.494. The text
there is mixed case (the PS1 list uses the move-text atlas's 6 x 12 glyphs), so the port bakes those glyphs (PKEYS.BIN pieces 80 to 173,
area-averaged to 5 x 6 for the Saturn's 224 lines, from the character ARC's atlas) and draws "When Facing Right", the names and the
commands with them; "PUSH BUTTON TO EXIT" has the original's colours 6, 5, 6, 5 (BUTTON and EXIT yellow). The 1P FREESTYLE page's columns were measured too (PS1 still `page`, Saturn field 7735, fractions of the picture width): the labels start at
0.198 on the PS1 and 0.200 here, OK ends at 0.239/0.237, COMMAND LIST at 0.484/0.484, TRAINING DUMMY at 0.533/0.531, the right-aligned values
end at 0.802/0.800 (the STAND value starts at 0.688 on both), RETURN TO MODE SELECT ends at 0.704/0.703 and the panel frame spans
0.179 to 0.815 against 0.178 to 0.816: all within 0.002, under a Saturn pixel.

The shipped disc was run on the headless Ymir probe (a Saturn emulator) with a pad script, and on Mednafen: the shipped configuration
(`build/saturn`, `music.cue`, with the opening movie, the VS screen, the endings, Tekken Force and the stage music) was built once with
`-DT3_MENU_SELFTEST=ON -DT3_PRACTICE_SELFTEST=ON`, so the disc drove itself and no key was sent to the emulator (key injection takes the
keyboard, and the user asked for Ymir or a Mednafen that does not). Mednafen's own movie recorder showed, from the real title: PRACTICE
MODE, the select, the practice fight with its attack data and HIGH marker, the green and red tint, 1P FREESTYLE with QUICK ROLL and its
arrow, the CONTROLLER dummy, and the command list in two columns (When Facing Right, mixed-case names, arrows and buttons, PUSH BUTTON TO
EXIT with BUTTON and EXIT yellow). The flags were then turned off and the shipped disc rebuilt (`play_mednafen.py` finds nothing missing).
