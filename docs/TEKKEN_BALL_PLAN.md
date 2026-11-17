# Tekken Ball

Tekken Ball (mode 7, `volley.ovl`) plays on the Saturn: TEKKEN BALL MODE in the menu, the ball type, the single-player select,
the VS screen, then `BALL.BIN` on the beach court against the CPU. The rules are researched and verified in the pinned
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
Ymir probe (START through the movie and title, five DOWN to TEKKEN BALL, the ball type, the select) and keeps screenshots; the field
numbers depend on disc speed. A fatal error shows as a black screen and the program in `_sat_panic`: the panic record
(`g_sat_last_panic`: code, line, file) can be read with `probe --dump-mem <address>:64:<file>`.

The first runs found, and fixed: the VS screen must be composed and kept up by the menu (`t3_vs_screen_finish` panics otherwise); the
floor's far-limit step must stay 1 (INVALID_ARG otherwise); `SAT_RGB555` takes 5-bit components.

## Not done

- **The charged ball's hit** is queued on the match (`ball_hit`: victim, cylinder, charge, and `stand_in`, a copy of the hitter's whole
  record taken when the ball was struck, as the original copies it into `0x800AC808`). The next HitApply (`psx_hit_transaction.c`)
  takes it as a hit from that stand-in with the charge as damage and attack word `0xFFFF`: guarding, damage, reaction, hit direction and
  knockdown are the ordinary hit code's, with the stand-in's move. Approximations: the copy is taken after the tick's step rather than
  at the touch inside the frame, and no emulator run has compared a reaction with the original's. The burn event drives
  `t3_psx_effect_burn` (`move_probe.c`; built, never seen on screen); the lethal hit's replay mark (event `BALL_EV_REPLAY`) and the iron
  ball's result flag are not done (a KO still replays through the normal round flow). Fighters' own hits do no damage in the court
  (`suppress_health_change` with `ball_court`, lifted for the ball's hit).
- Sound: the ball's sound events go to the match's sound sink as effect requests. The player plays only codes whose original sample
  was extracted into `SFX.BIN`; of the ball's, `0x704E` (touch) and `0x7043` (guard) are, the rest are counted as unmapped
  (`g_t3_sfx_unmapped`). Not heard: only the probe was run. The glow (effect type 15: the hitter's character flipbook, frames 9-20, looped on the ball, or a flame) is an approximation, a pulsing half-transparent octagon halo in the owner's colour drawn in the HUD pass (`main.c`); the original's art is not on the disc and a faithful port needs a new private flipbook asset plus VDP1 VRAM. Vibration is not done; the camera shake (`BALL_EV_SHAKE`, scripts 0 and 1) is.
- Done on the Ymir probe, not yet compared with the original: the ball's squash (axis * diag * axis^T * spin) and the ring-based shadow (two quads, not the original's four: Work RAM low has room for no more faces), the point flash's four half-transparent streaks (`main.c`, caught on no screenshot yet). Lit: the original's ColorMatCol per vertex (`src/game/ball_light.c`: the stage's main light, doubled, and the fill from below, SQR run max(power,1) times, the colour step with the type's back colour; 6,000 random cases against the verified GTE port, 0 mismatches, `ball_light_golden`), carried by one Gouraud table per face (`ball_render.c`; the scene material's `vertex_gouraud`, no extra faces). The stage's rig comes from `fighter_light.c` (`t3_fighter_light_rig`), the specular power and back colour from the type's option row. Kinds: 0 and 1 fade (-8k, 8k-1), 2, 5 and 8 are white, 4 pulses through the back colour. Not modelled: the effects' fire light (the point light that replaces the fill row while its power is above 0). Missing: the streaks' sparks and the select screen's rotating ball. The ball type is chosen
  on a text page; the original's HOW TO / RULES page is missing, and so is the VS screen's own caption
  (it says FIGHT 1).
- Opponent rotation: the menu now picks the first game's opponent as Gon and later games with `ball_flow.c` (the first-game flag rides in
  the carried-rounds byte), but the met counts cannot survive the menu's restarts, so later picks are uniform over the unlocked pool
  rather than weighted by the counts. Gon's unlock, the records and the menu entry's unlock (step 10) have no Saturn save to live in (the
  entry is always available).
- Damage popups ("!" and the charge) are made from the ball's events, kept for 30 frames and drawn in the debug font over the projected
  ball; compiled and run, but no frame of the emulator runs so far shows one, so they are unverified on screen.
- The serve side and `first_round` are set from the round number only; the deciding-point replay is not played.
- The one-tick lag of the ball after the fighters (decision 2) and the stand-in's move byte are approximations of the original's order.
- Real hardware and Mednafen were not run; only the Ymir probe (including the frame-rate check above).

## Compared with the PS1 (USA disc, PCSX-Redux)

`scripts/capture_ps1_menus.py --lua scripts/pcsx_redux_ball_capture.lua --names menu-4 f1 f2 f3 f4 f5 f6 f7 f8 --out build/ball-ps1c`
walks the title and main menu into TEKKEN BALL MODE, then saves the original's select, VS screen and court (retail frames: they stay
under build/). The menu entry is locked until story step 10, so the script writes 1 to the USA byte `0x80097F26` (Rev.1 `0x80098306`
through the converter's layout; checked: the unlocked-characters mask `0x3FF` sits at `0x80097EF0`) while the title is up. States:
4 menu, 10 quick select (`TEKKEN BALL SELECT`), 11 VS screen, 8 fight.

What differs from the Saturn port (frames side by side, `build/ball-ps1c/sheet.png` against `build/ball-ymir21/sheet.png`):

- **Timer (done):** the PS1 shows the infinity glyphs for the whole fight. The flag `0x800958D4` is the round-time option's sixth value (modes.md: `FUN_800DAF2C`; the resident code only reads it), i.e. the match's `timer_stopped`. `move_probe.c` sets it for Tekken Ball and `main.c` draws HUD digits 10 and 11 (already in HUD.BIN) in `BALL.BIN`. In the probe's long run Paul lies knocked down for thousands of fields: not a bug, the pad script sends no input after field 9,700, his health bar still shows 40% and an infinite round has no time-out to end it (checked on a full-size HUD crop at field 12,500).
- **VS screen:** the caption bar reads TEKKEN BALL (no number) over a red lightning background; the port shows FIGHT 1 over the purple one.
  (So the caption entry in "Not done" is a real difference, not a guess.)
- **Select:** the original's select screen is titled TEKKEN BALL SELECT, shows a rotating 3D ball between the two fighter slots, and the ball
  type picker is on it (BEGINNER / BEACH BALL / BALL DAMAGE 60%, the arrows to change it); the port has a text page before the select.
- **HUD:** the caption TEKKEN BALL sits at the top left over the bar; the port centres it under the timer.
- **Court:** the centre band is a thin white line that fades in and out along its length (additive Gouraud quads, brightness 0 to 192
  and back); the port draws one opaque grey quad, which shows as a wide grey wedge near the camera. The side lines match. Tried: two
  additive Gouraud quads (dark at the ends, bright at the join): the scene refuses `SAT_VDP2_SPRITE_ADD` on an RGB face (INDEX8 only)
  and the screen went black, so the band stays one opaque quad. A fade needs a translucent RGB face in LibSaturn.
- **Start of the fight:** the fighters stand at the court's ends and the camera is wide; the port's frames were taken mid-rally, so the
  framing is not yet compared at the same moment.

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
