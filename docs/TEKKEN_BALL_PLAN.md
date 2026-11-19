# Tekken Ball

Tekken Ball (mode 7, `volley.ovl`) plays on the Saturn: TEKKEN BALL MODE in the menu, the single-player select with the ball type
after the fighter, the VS screen, then `BALL.BIN` on the beach court against the CPU. The rules are researched and verified in the pinned
Tekken3Remake (`tools/research/ball_sim.py`, `verify_ball_sim.py`, `docs/research/code/modes.md`, section Tekken Ball). This page
tracks the port; "Not done" at the end is the honest list.

## Decisions

1. **A second program, `BALL.BIN`**, built from `src/saturn/main.c` with `T3_BALL_PROGRAM` and without Practice. Phase 0 measured the
   fight program's memory: its high Work RAM image had about 10.8 KB of slack and low Work RAM 96 bytes; the ball's code alone is
   about 10 KB at `-Os`, which first made the link fail by 208 bytes. Practice's code and data are about 42 KB, so `BALL.BIN` has
   34,320 bytes of slack. The court's strip, the ball and the HUD would not have fitted otherwise. `CMakeLists.txt` clones the fight
   target's sources and settings (`tekken3_saturn`) into `tekken3_ball` and drops the practice sources; the menu starts it with
   `sat_exec_keep_display` after the VS screen. The ball hooks exist only in `BALL.BIN`: `move_probe.c` under `T3_BALL_PROGRAM`, and the shared match, camera, AI and hit code under
   `T3_PSX_BALL`, which only the library variant `tekken3_fight_ball` (cloned from `tekken3_fight` in `CMakeLists.txt`) and `BALL.BIN`
   define (the host library defines it too, for the golden tests). The fight program links the stock library.
   **Frame-rate check** (the rule in AGENTS: shared code must not slow the fight): the same arcade pad script on the Ymir probe with
   `--debug-profile`, counting `draw_ends` over fields 4,800-6,900 (seven 300-field slices). HEAD (also `build/fin-cand`): 1,804. First
   version with the hooks compiled into the shared code: 1,752 (-2.9%, so they were moved behind `T3_PSX_BALL`). Final: 1,825 (the
   dips fall in different slices; no systematic loss). The check is Ymir cycles, not the Mednafen recording the older results use.
2. **The ball is not part of the match.** `move_probe.c` keeps `t3_ball_game_t`, ticks it after `t3_psx_match_step_original_in_place`
   (the original runs it inside the frame, after `MoveBranchAll`, so hits and scoring are one tick later than the original's) and
   snapshots a `t3_ball_view_t` per image in `t3_move_probe_present`.
3. **Verification is trace-based**: C ports are compared against cases exported from the verified Python port; the golden files are
   retail-derived and live under `build/ball-cases/` (git-ignored); tests skip (77) without them.
4. No pair-specific names or tests; fighters vary.

## Verified pieces (host tests in `ctest -R ball_`, 6 suites)

| Piece | Where | Verified by |
|---|---|---|
| The ball's rules: serve, flight, touches, attack and charged hits, floor, scoring, render kind, move tuning, round set-up | `src/game/ball_rules.c` | `tests/test_ball_rules.c` against `scripts/export_ball_cases.py`: 6,000 random fight states, every int32 of the ball, the engine words and both fighters, plus the events; 100% of the lines run, 0 mismatches |
| The same over real games | `scripts/check_ball_flow_trace.py` (with `tests/ball_rules_shim.c`) | the `ball` and `ball_versus` flow traces: 3,777 of 3,783 and 7,540 of 7,571 frames equal (99.8%, 99.6%); the rest are single hit frames, where the trace lacks the moves' projectile flag and segment source |
| The power gauges' state | `src/game/ball_hud.c` | the same traces: 7,618 of 7,620 gauge frames equal |
| The single-player opponent choice | `src/game/ball_flow.c` | `tests/test_ball_flow.c`, 4,000 cases |
| The court's bounds (mode 7 ArenaBounds) | `t3_psx_arena_bounds_ball` in `src/game/psx_body.c`, flag `ball_court` in the match config | `tests/test_ball_court.c`, 4,000 cases |
| The backdrop's scroll and horizon | `src/game/ball_backdrop.c` | `tests/test_ball_backdrop.c`, 3,000 cameras against `background_draw` |
| The fight camera's ball target (CameraFrameFighters in mode 7) | `t3_psx_camera_frame_fighters` in `src/game/psx_camera.c` (`ball`, `ball_on`) | `tests/test_ball_camera.c`, 4,000 cases against `camera_sim.camera_frame_fighters`, with and without the ball |
| The CPU's Ball hooks (the scene projected on the players' line, distance to the ball, back-dash, attack filter) | `src/game/ball_ai.c`, used by `psx_ai.c` through `bus.ball_line` | `tests/test_ball_ai.c`, 4,000 cases of `ai_update.ball_*`; the full AI is not re-verified in mode 7 |

| The ball's and its shadow's matrices (RotMatrix of the spin, the squash along its axis, the shadow's yaw and squash) | `src/game/ball_pose.c`, used by `src/saturn/ball_render.c` | `tests/test_ball_pose.c`, 4,000 cases of `ball_sim.ball_draw` and `_shadow` on `projection_sim`'s matrix routines, 0 mismatches. The first version composed the spin in the wrong order (z * y * x; the original's RotMatrix is not), and the shadow had an extra transposed-axis factor; both were caught by this test |

How the trace check steps a frame: the ball and the ball-owned words come from frame k-1, the fighters' records and the engine's
per-frame words (`lob_base`, the round state, `no_score`, the round clock) from frame k, because the original runs the ball after the
fighters moved. Frames of a deciding point's replay (the ball is played back from the replay ring) and round restarts are skipped.

`ball_rules.c` takes the fighters' facts (attack segments, hurt cylinders, move level, damage, position) as inputs and returns the
ball plus events (sound, vibration, camera shake, replay event, popup, burn, glow, the stand-in attacker copy). `ball_game.c` builds
the facts from the match (`ground.root`, `world[1]` for the head, `t3_psx_hurt_from_parts`, `strike`, `vm.move.attack_word`) and writes
health and the per-move lock back.

## Data and assets

- `scripts/extract_ball_data.py`: the three ball types, the hit points, the 42-vertex mesh, normals, faces and colours from the Rev.1
  `volley.ovl`, as a private blob and C source (`assets/imported/ball-probe/`). The USA `volley.ovl`, rebased to the Rev.1 layout by
  the converter, has the same bytes in every region used (checked), so one blob serves both.
- `scripts/export_ball_stage_pictures.py` (the converter's stage 19: tile-map picture, floor, RULES picture) and
  `scripts/build_ball_stage.py` (`STG19.BIN`, the layout of `src/game/stage_format.h`): the panorama's bottom 240 rows quantized to 255
  colours (528 colours in the original; worst error 7 in 5-bit steps) and the 16-colour floor. The backdrop is a flat strip, not a
  cylinder: `t3_stage_vdp2_set_strip` fills the line-scroll table (screen dot = 23/20 strip dots, screen line = original line, as
  `psx_viewport.h` maps the display); the floor has no walls.
- CMake: `-DT3_LOCAL_BALL_DATA=<ball-data.c> -DT3_LOCAL_BALL_PICTURES=<stages/v>`; `T3_BALL_BOOT_FIGHTERS="0;17;0"` builds a
  `BALL.BIN` that starts straight into a match (run it with `--bin` on the probe); `T3_BALL_TEST_DISC=ON` also builds a disc that boots it.
  `scripts/play_mednafen.py` refuses a disc without `BALL.BIN` or `STG19.BIN`.

## Running it

`build/asset-venv/bin/python scripts/ymir_ball_flow.py --build build/saturn --out build/ball-ymir` replays a pad script on the headless
Ymir probe (START through the movie and title, five DOWN to TEKKEN BALL, the select's fighter, LEFT per `--ball-type` step, START) and
keeps screenshots; the field
numbers depend on disc speed. A fatal error shows as a black screen and the program in `_sat_panic`: the panic record
(`g_sat_last_panic`: code, line, file) can be read with `probe --dump-mem <address>:64:<file>`.

The first runs found, and fixed: the VS screen must be composed and kept up by the menu (`t3_vs_screen_finish` panics otherwise); the
floor's far-limit step must stay 1 (INVALID_ARG otherwise); `SAT_RGB555` takes 5-bit components.

## Not done

- **Flow trace (checked):** `scripts/check_ball_flow_trace.py` matches the original's ball on every frame of the versus trace
  (7,571 of 7,571) and the single-player trace (3,783 of 3,783). The 31 frames that differed were the checker's, not the port's: it
  gave the touch test the fighter's flags after the hit that sets them (frame 447, a serve hit) and now takes them from the frame
  before. The gauge differs on one frame only (frame 68 of the versus trace: the ball's struct is just valid, and the power it reads is not yet set up; 7,990 of 7,992 gauge frames match).
- **The charged ball's hit** is queued on the match (`ball_hit`: victim, cylinder, charge, and `stand_in`, a copy of the hitter's whole
  record taken when the ball was struck, as the original copies it into `0x800AC808`). The next HitApply (`psx_hit_transaction.c`)
  takes it as a hit from that stand-in with the charge as damage and attack word `0xFFFF`: guarding, damage, reaction, hit direction and
  knockdown are the ordinary hit code's, with the stand-in's move. Approximations: the copy is taken after the tick's step rather than
  at the touch inside the frame. Compared with the PS1 probe (`scripts/pcsx_redux_ball_hit.lua`, `T3_HIT_BODY=0`: the ball touches
  fighter 0, fighter 1 charged it; fighter 0 is ID 7 and fighter 1 is Gon on both sides, the stand-in is a copy of Gon's record with the
  attack word `0xFFFF`): the victim is guarding at the touch (`guard=16`, slot 3) on both, so the charged hit is blocked: no damage,
  the ball keeps flying with fighter 0 as owner, and the victim's slot goes 3, 3, 0 with the guard cleared on the third row on both
  probes. The victim's slide matches: with the stand-in's first attack segment zeroed (as the PS1 charger's record reads at that tick,
  so the hit direction is zero on both), the victim's z follows the PS1 frame for frame (-130 at the guard tick, -126 at tick 6, -120 at
  tick 12, -114 at tick 20). Its x differs only because PS1's victim is pinned at the court wall: the Tekken Ball court is |x| <= 0x1400
  (fight-frame.md, arena bounds), PS1 moves it 12 units to x -5120 and stops, while Ymir's victim (at x -2146) has room to slide. The
  forced Ymir touch with the charger's stale segment left in the stand-in gave the same x path. The victim is the touched fighter alone on
  both sides: the charger keeps its slot (PS1 38 then 39, Ymir 4006) and takes no hit (`CHG` lines of the PS1 probe; `charged.bin` rows
  16-19 on Ymir). Ball fighters start at 130 health on both sides now: `FUN_800519B4` gives mode 7 the switch's default, 130, where the
  port had 140 (`fight_max_health_q16` in `move_probe.c`). Unguarded charged hit (Nov 2026, `T3_BALL_TEST_UNGUARDED` on Ymir, `T3_HIT_UNGUARD=1` on PS1): the touched
  fighter's `powerTimer` (+0x122, 120 frames; combat.md "cleared while powerTimer runs") is set before the touch's frame, so its guard
  is 0 from that frame's update. Clearing `+0x66` directly failed because the next update rewrites the guard; powerTimer holds. The
  reaction matches: slot 3 -> 0 two frames after the touch, slide -130 in z on both sides (PS1 z 35 -> -95, Ymir 248 -> 118). The damage
  matches too: charge 50 takes 78 health at frame 1 on both sides (8519680 -> 3407872), and PS1's ball then pops up (state 5, owner and
  target the victim), as `_charged_touch` says. Earlier PS1 runs showed no damage because the probe's stand-in kept the charger's index:
  the strike (ball_sim.py `_attack_hit`) copies the charger's record into the stand-in and then writes its index `+0x1E` back to 2,
  and `HitApply` keeps a Ball hit's damage only when the attacker is index 2 (combat.md). The probe now does the same (plus the stand-in's
  `+0x122` = 1, `+0xDF` = 1 and the striker's `+0x85/+0x88/+0xD1/+0x23`); no charged-hit flag write is needed (`T3_HIT_FLAGS` stays
  off; its USA address `0x800B688C` is unverified). PS1's later 30-health loss at frame 97 (victim airborne, slot 3414, ball state 2 -> 7)
  is not part of the charged hit: it is the point penalty (`hp -= 0x1E0000`, ball_sim.py) after the rally that follows, which the Ymir
  test's AI does not replay. Earlier "65 on both" runs had a stale attack word, so they were unguarded by accident and are not evidence. The burn event drives
  `t3_psx_effect_burn` (`move_probe.c`). Lethal charged hit (charge 100, unguarded, Nov 2026): the victim's health goes from 130 to 0
  on the touch's first frame on both sides, the ball pops up (state 5) for six game frames on both (PS1 12 vsyncs, Ymir 6 ticks), and
  the victim is launched the frame after (`charged.bin` column 10 is the ball's state). The original's charged touch raises no damage
  popup (`_charged_touch`: burn, replay event 0x82, result 6, sounds); popups come from charging strikes and the set. The iron ball's
  result flag (6 replays) is done (`ball_rules.c`); the event 0x82's dust ring is not replayed, because the port's replay ring keeps
  no effect log. Fighters' own hits do no damage in the court
  (`suppress_health_change` with `ball_court`, lifted for the ball's hit).
- Sound: the original plays the ball's sounds as raw sound codes (`SoundPlayFighter(0, code, 0)`: bank, voice, volume, program and
  tone in the code's bits), not as sound-table ids. The port used to send them as table ids, so `0x704E` played table entry 0x4E
  (`0x60C3`) and `0x7043` entry 0x43 (`0x6151`): the wrong samples. They now go to the sink as `T3_SOUND_CODE` requests, which
  `sfx.c` looks up by exact code. `scripts/extract_psx_sfx.py --with-ball` extracts all eight from VAB 0 (serve `0x4CE8`, reset
  `0x4CE9`, bounce `0x4E6E`, touch `0x704E`, set `0x7082`, spike `0x4DAF`, guarded `0x7043`, weak `0x4E6D`) into the Ball program's
  own `BSFX.BIN` (244,832 bytes; versus keeps `SFX.BIN`). Identical samples in different key banks are now stored once (touch and
  guarded are versus samples; serve and reset share one). With both voice packs this needs 462,252 bytes of Sound RAM, 16 KB past
  the resident area (446,464), so the Ball program lends the four streaming rings to it (`sat_sound_ram_borrow_stream_rings`, new in
  LibSaturn; the Ball's music is CD-DA and opens no stream): capacity 512,000. A 9,000-field Ymir rally: no panic, 0 unmapped
  requests, serve, reset, bounce and touch played. Cross-correlation against Ymir's mixed stereo output found two clear serve
  matches (0.82 and 0.80) and a weaker bounce match (0.43) under the CD-DA music. The glow (effect type 15) is the original's art
  (see below and PARTICLE_INTEGRATION_PLAN.md section 61). Vibration is not done (the Saturn standard pad has no motor); the
  camera shake (`BALL_EV_SHAKE`, scripts 0 and 1) is.
- Done on the Ymir probe, not yet compared with the original: the ball's squash (axis * diag * axis^T * spin) and the ring-based shadow (two quads, not the original's four: Work RAM low has room for no more faces), the point flash's four streaks (`main.c`; seen on the probe with the test option `T3_BALL_TEST_POINT`, which shows kind 2 for a second every five seconds: white wedges from the ball to the top and bottom of the screen). Lit: the original's ColorMatCol per vertex (`src/game/ball_light.c`: the stage's main light, doubled, and the fill from below, SQR run max(power,1) times, the colour step with the type's back colour; 6,000 random cases against the verified GTE port, 0 mismatches, `ball_light_golden`), carried by one Gouraud table per face (`ball_render.c`; the scene material's `vertex_gouraud`, no extra faces). The stage's rig comes from `fighter_light.c` (`t3_fighter_light_rig`), the specular power and back colour from the type's option row. Kinds: 0 and 1 fade (-8k, 8k-1), 2, 5 and 8 are white, 4 pulses through the back colour. Seen on the probe: the kind 0/1 fade (the ball goes grey, then black, on a reset), the flat white of kinds 2, 5 and 8, and the kind 4 pulse (with `T3_BALL_TEST_POINT` holding the ball beside fighter 1 for a second, sixteen frames 7 fields apart show it washed out pale, then dark and shaded, then bright again: the 0 - 255 - 0 back colour); the COMMAND pages of two fighters were paged on screen. Not modelled: the effects' fire light (the point light that replaces the fill row while its power is above 0). The streaks' 16 sparks (effect type 1, `ball_game.c`, spawned once when kind 2 starts; the positions follow ball_sim.sparks, the random numbers come from the tick count) are in the code and the host suite, but are now seen on the probe's frames (small yellow sparks around the ball and a fighter, with `T3_BALL_TEST_POINT`). They had never drawn because of a bug of the fighter packs' attachment change, not of the Ball: the original projection (which the particle compositing needs) refuses a part outside the joint mask, and Gon's tail (parts 18 and 19) had no joint, so every image with a live effect and Gon on the field fell back and no particle was composed (his jaw and tail still drew). `publish_owned_pose` in `move_probe.c` now gives each present attachment part its parent's rotation and the placed offset (a rigid tail; the original sways it with attachment dynamics, which the port has not got) and sets its mask bit; the projected count matches, the overlay gets tiles, the fighters' lighting reads the part. The same applies to every fighter's attachments. The forced burn is seen too (side by side over thirty fields): bright glow dots along the burning fighter's joints, with pink and orange spark dots and small rings flying around. The fire, flame, smoke, cloud and gas objects now draw the original's flipbook art (see PARTICLE_INTEGRATION_PLAN.md section 61), and the charged ball's glow is the original's (kinds 6 and 7: the hitter's character flipbook, frames 9 to 20; kind 9: the fireball's grow and loop) in place of the octagons. The type choice's spinning ball is done: `scripts/pack_ball_spin.py` renders each type's 8 frames from the private mesh with the fight's lighting (`BALLSPIN.BIN`, 40 x 70 texels per frame, 59 colours, drawn three times as wide), and `ball_spin_draw` in `menu_main.c` loads the shown type's block into the second portrait's VRAM and the first 60 codes of its palette slot, and before the fighter is chosen a small version of it (`BALLSML.BIN`: 24 x 42 texels, 15 colours, in the palette codes 240 to 255 the select screen leaves free, its own VRAM region) turns between the two fighter slots above the timer, as the original's does. The COMMAND page's decoder handles every input byte the 21 fighters' lists use (403 lines checked). The original's
  HOW TO page is done as a page of its own: START in the fight shows the
  retail picture (`HOWTO.BIN`, packed by `scripts/pack_ball_howto.py` from the exported how_to.png, shrunk to 192 x 102) on the light frame
  with its black shadow (positions by the HUD's x * 20 / 23, y / 2 mapping), and a button returns to the fight (`how_to_page` in `main.c`;
  the page runs at the top of the next loop iteration, outside a frame, and keeps colour RAM bank 7, which the stage's backdrop shares,
  to put it back). START opens the pause menu around it (`pause_menu` in `main.c`: CANCEL, COMMAND, RESET, HOW TO, in the debug font; the fight stands still; RESET is the old START restart). COMMAND is the first player's command list (`CMDLIST.BIN`, a copy of the practice blob, read from the disc into a 9 KB buffer when the page opens; UP and DOWN scroll), in the debug font: the moves' names show, and many inputs come out empty or as text because the extracted lines keep the original's button icons as plain text. The HOW TO picture is streamed into VDP1 VRAM a kilobyte at a time (High Work RAM has no room for it, and the scene's face array belongs to the Slave outside a frame). `ymir_ball_flow.py --pause-item` drives them; its punch taps stop once paused, because X is also the menu's OK. The VS
  screen's caption reads TEKKEN BALL (the red lightning background, as the original's: the arcade picture, not the VS one).
- Opponent rotation: the menu picks the first game's opponent as Gon and later games with `ball_flow.c`. The first-game flag and
  the met counts now live in an internal-backup record (`T3BALL`, 56 bytes: `ball_progress.c` packs it, `ball_save.c` reads and
  writes it, as Force's record). The flag is kept for good, as the original saves `0x800982FE` with its progress. The counts
  last one Tekken Ball session, as the original keeps them in the mode's context: the menu clears them when Tekken Ball is chosen
  from the title. `ARG_BALL_PLAYED` still covers a missing or full backup memory within a session. The original's "within 4 of the
  most met" filter keeps every character (the list is sorted from the most met down), so the counts never change the pick.
  Host test `ball_progress_save`. On Ymir, two power-ons sharing one backup image (`--backup-ram`): Gon first, then Bryan, with
  the record holding the flag and Bryan met once. Gon's unlock, the records and the menu entry's unlock (story step 10) are not
  saved (the entry is always available).
- Damage popups ("!" and the charge) are made from the ball's events, kept for 30 frames and drawn in the debug font over the projected
  ball; compiled and run, but no frame of the emulator runs so far shows one, so they are unverified on screen.
- Round end (Nov 2026, `psx_round_flow.c` `ball_replays`/the Ball result, `psx_replay.c` `t3_psx_replay_start_ball`/`_update_ball`,
  from round_flow.gd `_ball_replays`/`_ball_result` and replay_state.gd): the deciding point is replayed `result` times from result
  frame 16 (2, or 6 after a lethal iron-ball hit), each from the last 30 frames of the ball's own ring (`ball_game.c`; the ball does not
  run while a replay plays, and a replayed point flash plays its bounce sound), at double speed but for the last, the third of six ending
  16 frames early; the result clock stands while each plays, effects are cleared between replays (`t3_psx_effect_pool_round`), the bars
  hide until the last ends and the result message shows only with it. Then the win poses start straight from the result (a fighter
  beaten by the CPU keeps a sliver of health; the loser camera holds 0xF0 frames), and the next round serves from the loser's side
  (`SERVE_SIDE`), with the ball restarted (`t3_ball_fight_start`, first_round only for round 1). Seen on Ymir with the lethal iron-ball
  test: six REPLAY passes (fields 5773 - 5916, the last at normal speed), no bars during them, PERFECT! after the last, YOU LOSE on the
  winner's pose, ROUND 2 served from the loser's side. The match camera now restarts the loser camera and takes a winner phase whenever
  the flow picks one again (`psx_match_camera.c`), as `_result_camera` does: Ball picks them once for the replays and again for the
  poses. The menu's first-game flag is now a flag bit (`ARG_BALL_PLAYED`), not the carried-rounds byte: every Ball match used to open
  on ROUND 2. Compared with the PS1 (beach ball, `T3_HIT_SHOTS` of `pcsx_redux_ball_hit.lua`, `build/ballfin-ps1-lethal`): 25 frames
  after the lethal touch the PS1 shows the first replay straight down from above the KO'd fighter with no HUD at the top. Two fixes
  followed: (1) HudRoundUpdate returns at once while `hud_shown` (0x800AE6C8) is 0, so the names, marks, timer and mode line now hide
  with the bars (`main.c` `ball_hidden`); (2) Ball's replay frame runs the director in the round end's phase, not the replay orbit
  (fight_simulation.gd `_replay_frame`; `t3_psx_match_camera_replay`), and the KO'd loser's straight-down camera does not set the
  overhead flag in Ball (`psx_camera.c`, as its comment already said). Ymir now shows the first replay from straight above the loser
  and the later ones from the winner camera. The ordinary versus replays keep the port's HUD (the original hides it there too while
  `replay_playing`; not changed here).
- The one-tick lag of the ball after the fighters (decision 2) and the stand-in's move byte are approximations of the original's order.
- The mixed-mode image passed `play_mednafen.py`'s full-game feature check and booted through the opening movie to the Nina–Lei
  attract fight in Mednafen. Tekken Ball itself was exercised in Ymir. Real hardware remains untested.

## Compared with the PS1 (USA disc, PCSX-Redux)

`scripts/capture_ps1_menus.py --lua scripts/pcsx_redux_ball_capture.lua --names menu-4 f1 f2 f3 f4 f5 f6 f7 f8 --out build/ball-ps1c`
walks the title and main menu into TEKKEN BALL MODE, then saves the original's select, VS screen and court (retail frames: they stay
under build/). The menu entry is locked until story step 10, so the script writes 1 to the USA byte `0x80097F26` (Rev.1 `0x80098306`
through the converter's layout; checked: the unlocked-characters mask `0x3FF` sits at `0x80097EF0`) while the title is up. States:
4 menu, 10 quick select (`TEKKEN BALL SELECT`), 11 VS screen, 8 fight.

What differs from the Saturn port (frames side by side, `build/ball-ps1c/sheet.png` against `build/ball-ymir21/sheet.png`):

- **Timer (done):** the PS1 shows the infinity glyphs for the whole fight. The flag `0x800958D4` is the round-time option's sixth value (modes.md: `FUN_800DAF2C`; the resident code only reads it), i.e. the match's `timer_stopped`. `move_probe.c` sets it for Tekken Ball and `main.c` draws HUD digits 10 and 11 (already in HUD.BIN) in `BALL.BIN`. In the probe's long run Paul lies knocked down for thousands of fields: not a bug, the pad script sends no input after field 9,700, his health bar still shows 40% and an infinite round has no time-out to end it (checked on a full-size HUD crop at field 12,500).
- **VS screen (done):** the caption bar reads TEKKEN BALL (no number) over a red lightning background, now on both.
- **Select (type choice done):** after the player's fighter the select runs the original's type choice (select_sim.py BallHooks
  `side_select` / `draw`, volley.ovl FUN_800B569C / FUN_800B593C): the ball's x eases an eighth a field toward its target, LEFT rolls it
  off to -0x1000 and brings the next type in from the right once it passes -0x800 (RIGHT the previous one, from the left); at rest the
  yellow arrows blink (field & 0x20) and the original-font texts show (name colour 11, description font 1 colour 4, BALL DAMAGE colour
  5, `%3d%f%H%V%%` 60/80/100 in colour 6), a face button confirms and B (the PS1's SELECT) backs out to the fighter. The ball, texts and
  arrows sit at the PS1's places (resting centre (185, 348); texts at y 0x144 ... 0x1B6) mapped onto the 704 x 448 screen and moved 18
  lines down to clear the name plates, over a black panel in place of the grid; the CPU's silhouette, the timer and the cursor are
  hidden while it shows. The palettes go over codes the choice leaves idle (TEXT.BIN group 3: the timer's 208, slot B after the
  59-colour ball at 159 and 175, white at 240); the menu's text page is gone. Not the original: the port keeps its arcade select
  layout (not the 3 x 7 quick select, no TEKKEN BALL SELECT title) and the ball is pre-rendered frames, not the 3D renderer. Seen on
  the probe (`build/ballfin-sel`): the three types in turn, back-out and re-pick, and the fight with the iron ball.
- **HUD (done):** the mode line (FUN_8004EE3C, volley.ovl FUN_800B5210) puts TEKKEN BALL (font 0, colour 6) over the human's bar
  at (19, 24) and the CPU side's blinking coin prompt (FUN_8004F1D0: INSERT COIN, or PUSH P2 START with a pad in port 2; every other
  64 fields FREE PLAY / CREDIT 0; colour 5) centred on x 284. The power gauges (FUN_800B4E6C) are the health bar's caps and strip
  with the gauge's own CLUTs: player 1's fill 0x7EDE (red), player 2's 0x7EDD (yellow, from the right end), the trail 0x7EDC, empty
  0x7EDF (grey); the charging side pulses (the PS1's 0..2 texel scale as a VDP1 Gouraud offset). Both come from `BHUD.BIN`
  (`scripts/extract_ball_hud.py`: the texts composed from print_text's font-0 glyphs; `src/saturn/ball_hud_art.c`). Seen on the
  probe (`build/ballfin-hud`); the port's gauges had been the health bar's colours (red empty).
- **Court (done):** the centre band is a thin white line that fades in and out along its length (additive Gouraud quads, brightness 0 to
  192 and back). Tried: two additive Gouraud scene faces (the scene refuses `SAT_VDP2_SPRITE_ADD` on an RGB face: black screen), then
  half-transparent HUD quads (VDP1 half transparency does not blend with a VDP2-only floor: a dark line). Now `main.c` draws eight
  opaque quads in the HUD pass, each mixed from the sand colour towards white by its level (24, 72, 120, 168, 168, 120, 72, 24 of
  255), over the fighters (a thin line); the scene keeps the two white side lines.
- **Start of the fight:** the shared round placement was overriding the Ball spawn positions with the normal fight's ±1000.
  Ball now places the fighters at ±0x1400 at the start of every round, as in `FUN_800B0C90`. The host match test checks the
  opening round and the next round. Ymir fields 5100–5200 show both fighters at the court ends with the wide camera through
  READY and the first serve, as in the PS1 capture (`build/ballfin-roundstart/comparison.png`).

**The ball itself** (PS1 frame `build/ball-ps1c/f8.png` against the port's): solid, eight pole-to-pole gores (the type's 8 face colours)
shaded smoothly and stretched by the squash. Three causes had broken the port's: (1) the scene's Z mirror turns the winding over, so
the corners are submitted in reverse; (2) the **rim gaps** were the quads: the original's corners are in strip order (a POLY_G4's
0 1 / 2 3), so submitted as they came they were bow-ties, sheared diamonds with gaps between them; `ball_render.c` takes them around
(reversed: 2 3 1 0); (3) the scene paints in sorted order and shows the far side through the front, so faces turned away are skipped
as the original's NormalClip does (the face's mean normal against the camera's x, y, z). Now the Ymir frames show a beach ball with
the original's gores.

Gouraud precision: a VDP1 table moves a corner at most -16..+15 steps of 8 around the polygon colour (the corners' mean). Measured on
the real vertex normals, the default rig and the types' real face colours (300 random spins each): 0.0% of the corners clamp for the
beach and gum balls, 0.3% for the iron ball (worst 19 steps). Only a white face (colour 255) would clip at about 20%.

**Frame rate** (the same pad script, `--debug-profile`, draw_ends over fields 4,800-6,900, the Ball program): 1,118 before the light,
1,160 after it (the faces turned away are no longer drawn).

The full-disc check after the HUD, sound and replay changes counted 1,030 draw ends, and the build with the corrected court
placement counted 1,000 over those same fields. The VS transition took up most of fields 4,800–5,100 in both runs. Over fields
5,100–6,900 the counts were 967 and 936, respectively. The court placement also changes camera distance and rally positions, so
these counts include the cost of the different scenes. The probe's event log reached its capacity on both runs, but all 2,101
field counter rows were present (`build/ballfin-verify/debug.frames.csv`, `build/ballfin-roundstart/debug.frames.csv`).
