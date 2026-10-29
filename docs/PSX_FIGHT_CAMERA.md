# Original fight camera on Saturn

`T3_PSX_CAMERA` (default OFF; requires `T3_ENABLE_PSX_COMBAT`) drives the
Saturn view from the original Tekken 3 fight camera instead of the fixed
framing camera. Historical comparisons used the remake harness's patched
presentation policy. The current retail-code audit and corrected input phase
are in [RETAIL_CAMERA_PARITY.md](RETAIL_CAMERA_PARITY.md): 1,560 fully matching
fresh ticks. [RETAIL_CAMERA_EDGE_CASES.md](RETAIL_CAMERA_EDGE_CASES.md) adds
faithful bugs 53/54/55 and direct MIPS phase proofs, with natural trajectory
coverage still open. This report
also preserves the historical results and covers how the camera is wired
into the owned match and the renderer, the LibSaturn floor change it needed,
and what is still missing.

The rules come from Tekken3Remake (PolyForm Noncommercial; see `NOTICE.md`).
The camera tables are retail data. `scripts/build_local_camera_assets.py`
converts them into the git-ignored `generated/private_camera` at build time.

## Place in the fight frame

The original FightFrame runs RoundFlow, then CameraDirector, CameraShakeStep
and CameraFrame (the view), then the fighters.
`src/game/psx_match_camera.c` follows that order inside `psx_match.c`:

- **Round start.** `t3_psx_match_camera_round` runs on the first round and on
  every following one. The legacy neutral initializer seeds all three
  generators with 1, starts in FIGHT and has no intro timer. Production
  `start_intro` instead preserves the placement RNG, initializes the camera
  generator from the configured seed and starts in RESET. Later rounds also
  start in RESET. Each round sets `screen_x` to ∓100, clears the
  shake and the overhead flag, and builds the first view.
- **Each frame.** `t3_psx_match_camera_frame` runs before the round-flow 3/4 →
  5 conversion. It syncs the camera phase from the flow value:
  - a change to 0 resets the camera;
  - 3/4 is copied while the phase is below 3;
  - 6/7, when it differs from the phase, starts the loser camera.

  It then steps the director, advances `camera_rand`, applies the shake and
  builds the view.
- **After the fighters.** `t3_psx_match_camera_after` sets `screen_x` from the
  GTE projection of joint 11 with this frame's view. When the camera is
  overhead it uses depth `t[2] − 0xA3A`. On live ticks only, it then starts
  shakes: a landing (`landed_b`) starts script 0, and a move event with high
  byte 1–3 starts script `hi − 1`.
- **Replays.** `capture` / `replay` patch the roots on the first replay frame
  and step the replay camera, without starting shakes.

The fighter inputs come from the owned state:
- the position is `ground.anchor`;
- the head height is `world[2].origin.y`;
- the move id is the address of the slot-table entry;
- the event words are the last decoded move events;
- the hit point is the opponent's strike segment end, when a hit was accepted.

## Mapping to the Saturn camera

`t3_move_probe_camera` (`src/saturn/move_probe.c`) turns the PS1 view into a
LibSaturn camera:
- **World.** A Saturn coordinate is 96 × (x − cx, −y, −(z − cz)) in 16.16,
  where (cx, cz) is the fighters' midpoint.
- **Eye and axes.** The eye is the mapped view position. The forward axis is
  row 2 and the up axis is −row 1.
- **Projection.** The PS1 picture is 368 × 480 around (192, 240), and row 1 is
  scaled by 0x1BD2/4096. The perspective therefore takes its focal terms
  directly from H: `m[0] = H/184` and `m[5] = 0x1BD2·H/(240·4096)`. The
  `fov_y` and aspect (≈ 1.333) are set to the matching values, so cache keys
  still compare.

The private host tool `tools/psx_match_camera_eval.c` checks this mapping. It
runs a scripted two-round owned match (5,554 frames, every camera phase
except LOSER). It compares a float look-at projection with the GTE
projection:
- the worst error is 1.36 px;
- right-axis roll is always 0;
- the lowest eye is 737 units above the floor.

## LibSaturn: general VDP2 floor

The old `stage_vdp2.c` homography handled pitch only. It returned
`SAT_ERR_UNSUPPORTED` as soon as the camera yawed, which the original camera
does all the time. The candidate panicked at the floor call for that reason.

LibSaturn now provides `sat_vdp2_floor_from_view`
(`include/saturn/vdp2_floor.h`, `src/graphics/vdp2/floor.cpp`). It builds the
RBG0 rotation parameters and per-line coefficients for any camera that does
not roll, using only `view_proj` and the eye:
- **Axes.** The rows of `view_proj` give fx · right, fy · up and the forward
  axis f. The horizontal forward is g = (0, 1, 0) × right.
- **Per line.** Screen line v shows the floor at depth
  w = h / −(f.y + v·u.y/fy). Its coefficient is k = w·density/focal.
- **Constant terms.** `Xs` follows the right axis. `Ys` is linear in the line
  and follows g. The map offset is origin + density · eye, wrapped by an
  optional period.

`tests/host/test_vdp2_floor.cpp` projects five cameras (looking along +z, along
−z, yawed, steep and nearly level). It compares every sampled dot with the
exact floor intersection: the worst error is 0.022–0.111 map dots. It also
covers the horizon cut-off, the period wrap, rejection of a rolled camera
and of an eye below the plane, and null arguments. `make test` in LibSaturn
passes.

`t3_stage_vdp2_prepare` now calls it with the stage's 1,800-unit tiles
(density 64 dots per tile, period 64 dots). The single SDK prefix
`~/dev/saturn-prefix` was rebuilt in place. LibSaturn's earlier uncommitted
changes were kept.

## Results

| Check | Build | Result |
|---|---|---|
| Host `ctest` | `build/host` | 54/54 |
| Combat suite (`--with-ko --with-timeout`, Ymir) | `t30-cam` | 24 live original-combat cases passed |
| Frozen tick 120 vs `t30-z-frozen` | `t30-cam-control` (camera OFF) | all stable guest fields match; pixels differ only in the floor (see below) |
| Mednafen CD boot, 30 s neutral input | `t30-cam` | boots and fights; screenshots at 22 s and 28 s |

The control's frozen comparison no longer matches bit for bit. The stable
diagnostic fields all match. `scripts/diff_saturn_screens.py` shows that the
differing pixels are:
- the `FRAME 580/581` HUD counter, a timing field, in rows 47–53;
- about 2,000 isolated floor dots in single-row bands. These are texels that
  flip at sub-dot boundaries, because the new floor rounds differently from
  the old pitch-only formula.

The fighters are unchanged. The new floor is closer to the exact projection,
so this is accepted as the new frozen baseline.

On the CD, the candidate shows the original low, close framing. The control
places the eye higher and looks down.

## Known gaps

- **Backdrop.** Resolved: the panorama box is now a VDP2 cylinder scrolled
  per line from this camera and its backdrop turn, and the floor ends at the
  box walls (`PAUL_STAGE_VDP2.md`, "Moving panorama"). A line can only
  scroll, so box corners and steep views bend the picture by a few texels.
  Floor distance shading, stage lighting and shadows are still missing.
- **Overhead KO depth.** The renderer does not apply the overhead
  `t[2] − 0xA3A` fighter depth. Only `screen_x` uses it.
- **Bank streams.** The earlier checkpoint omitted throw streams while the
  simulation lacked throws. The later throw
  implementation and its camera handoff pass live regressions; see
  [NEXT_PLAYABLE_PROGRESS.md](NEXT_PLAYABLE_PROGRESS.md). Full bank-stream
  coverage and independent camera feedback parity remain separate gates.
- **Shake coverage.** The scripted host match rarely launches anyone, so it
  started no shakes. The shake itself is covered by the camera trace tests.

## Independent feedback audit (2026-10-26)

The new `t3_match_replay_camera_eval` target starts from configuration and
uses the production camera bindings, including both character camera banks.
Run it with `fight_oracle_eval.py --initialize-match --independent-camera`.
It rejects seeded mid-fight initialization, CPU tapes and reference exports.
The ordinary replay executable rejects the independent-camera request.
Camera audit output uses `native-camera.jsonl`/`.stderr`, preserving ordinary
combat comparison evidence.

Only physical controls and the source frame counter drive this mode. It
recomputes relative directions from the owned previous screen positions;
taped mapped directions, original side flags, screen positions and previous
views do not feed the simulation. Each tick compares all nine view rotation
entries, three translations, projection H, both joint-11 screen X values and
the director phase, in addition to the existing fighter/round/HUD/input audit.
New original exports also capture camera sources, director weights/state,
RNG (including the auxiliary AI generator), targets and panorama RAM. Available source/weight/state/RNG fields are
compared; the target and panorama state still need dedicated comparisons.
Older captures lack these added spans and cannot prove internal RNG parity.

`ps1_original_owned_camera_air_timeout` passes **6,600 consecutive ticks**:
the six 600-tick air/recovery fixtures and the 3,000-tick timeout fixture
through the second intro. No original state is copied into the fresh match,
no source frames are filtered, and no unsupported moves are dropped. See
[AIR_RECOVERY_PARITY.md](AIR_RECOVERY_PARITY.md) for attack/landing coverage.
The host-law suite now passes **69/69** tests, including the existing scripted
projection check, the blink differential and the full-state camera test below;
log: `build/parity-camera-rng-host-tests.log`. The Paul-mirror host passes
**62/62**, log `build/parity-camera-rng-host-paul-tests.log`. The two
Paul/Law camera fixture tests are registered only when both the second logic
bank and its camera bank are configured; they cannot run on mirror-Paul data.
These are host comparisons;
the separate Saturn smoke test below does not establish SH-2 camera parity.

**Open differences, not passing coverage:**

- The 600-tick reversal fixture first differs at source frame **411**, in
  both fighters' `side_flag`, despite matching the view/screen comparisons at
  that frame. Its 460-tick control and 360-tick first-contact negative pass.
- Both 460-tick throw/escape fixtures first differ in the view at source
  frame **318**. The escape case changes yaw and screen X; its control first
  differs in the view translations. These ordinary combat fixtures continue
  to pass when supplied original screen/view feedback; that is a narrower
  claim than independent camera parity.
- `build/parity-camera-escape` captures 340 original ticks with internal
  camera state. After the lifecycle fixes below, **269 consecutive ticks**
  (source frames 2–270) match every available comparison. The first difference
  is now source frame **271**, shared `rand`: owned `2633739833`, original
  `267834847`. The complete failing tail remains in `native-camera.jsonl`;
  this is not a passing escape/cinematic test. Earlier pre-fix evidence is
  retained as `native-camera-before-rng.jsonl`. The reversal and throw/control
  results above describe the pre-fix audit and need fresh reruns before any
  stronger claim.

### Shared RNG lifecycle correction

An original-code hook on `rand` during round placement identified two draws
from stance setup (return address `0x8002D5F0`), then two from `AiReset`
(`0x800596B0`, return address `0x800598C4`). `AiInitRound` calls this even with
human players. Production placement now reproduces both resets' random
advances; the existing AI-bus path still performs its own resets once. Fresh
intro initialization starts the auxiliary AI generator at 0, producing 18
after these two resets. The next missing draws were the eye timer at source
frame 6 (`0x8003459C`).

`src/game/psx_blink.c` adapts the remake's documented `FUN_80034354` rules:
forced closure, the four-tick closed interval, and random open duration from
the high bits of the shared generator. It runs at fighter composition time,
including replay/round-flow composition. Eye texture uploads and Gon jaw
commands remain unimplemented presentation work. `ps1_original_blink_rng`
compares **624 cases** directly with the original MIPS function, covering
flags, move states, signed timer boundaries, closed states and seeds.

The new fresh neutral Paul/Law fixture `build/parity-camera-neutral` matches
**600 consecutive ticks**, source frames 2–601: view, projection, screen X,
phase, five sources, director state/weights, all three RNG states, eye state,
and the existing combat/round/HUD/input comparisons. It uses owned camera
feedback throughout, with no original screen/view injected. The test
`ps1_original_owned_camera_rng` requires captured internal fields on every
tick; an older matrix-only fixture cannot silently satisfy this test.
Host private camera configuration must enable `T3_COMPACT_CAMERA_STREAMS`:
the production candidate already does, but the old host configuration omitted
its streams. Both character banks are bound.

The original hook extended through frame 271 identified the next draws
in `FighterSounds`, return addresses `0x80041A0C` and `0x80041A3C` (attack
shout test and selection). The subsequent sound lifecycle port now reproduces
these draws; see [PSX_FIGHTER_SOUNDS.md](PSX_FIGHTER_SOUNDS.md). The new
460-tick escape capture matches sound/eye/all RNG fields throughout, but
its full independent audit still fails first at source **320**, source 3 Z
owned `-5114` versus original `-5113` (view translation Z `5477`/`5476`).
This replaces the frame-271 RNG discrepancy as the next known difference.
The failing tail remains intact. Original hook evidence:
`build/parity-camera-rand-sound-calls.json` and its `-probe.py`/log.

The production candidate `build/parity-reversal` was rebuilt in place:
ELF SHA-256 `3f640a650498300cd44c757a66a4412fc98642a6ee4d50318c67ff8f071653b2`,
initialized payload 578,636 bytes. A **600-field Ymir smoke test** reached
195 simulation ticks and FIGHT, preserved both HP at 140, and reported zero
runtime errors, draw timeouts, attack decode errors and rejected faces
(750/2048 commands). Evidence:
`assets/imported/parity-camera-rng-neutral-saturn/observation.json` and
`build/parity-camera-rng-saturn-build.log`. This injected execution does not
prove CD boot, performance or original/SH-2 camera equality. The earlier
26-case/3000-tick gate belongs to its recorded historical ELF, not this hash.

A machine-readable result inventory, including the rejected wrong evaluator,
is retained at `build/parity-camera-summary.json`.

The subsequent retail-oracle/input-phase correction resolves these three
fixture gaps; see [RETAIL_CAMERA_PARITY.md](RETAIL_CAMERA_PARITY.md). Next
extend the phase proofs of camera bugs 53/54/55 to natural throw chains and
expand the retail captures through KO/replay. Separately
verify the visible display rectangle and GTE viewport origin in the rendered
image. The HUD-origin correction alone does not establish absolute 3D
viewport alignment.

```sh
cmake -S . -B build/host-law \
  -DT3_COMPACT_CAMERA_STREAMS=ON \
  -DT3_LOCAL_CAMERA_BANK="$PWD/assets/imported/tekken3-usa/raw/074.bin" \
  -DT3_LOCAL_SECOND_CAMERA_BANK="$PWD/assets/imported/law-camera/raw/082.bin"
cmake --build build/host-law --target t3_match_replay_camera_eval
ctest --test-dir build/host-law -R ps1_original_owned_camera_air_timeout --output-on-failure
python3 scripts/fight_oracle_eval.py build/parity-camera-escape \
  --evaluator build/host-law/t3_match_replay_camera_eval \
  --initialize-match --independent-camera
```

## Reproduce

```sh
cmake --build build/host && ctest --test-dir build/host
export PATH=$HOME/dev/saturn-tools/bin:$PATH
python3 scripts/build_saturn_variant.py --baseline build/t30-z --out build/t30-cam \
  --cmake-option=-DT3_PSX_CAMERA=ON --sdk-prefix $HOME/dev/saturn-prefix
python3 scripts/saturn_combat_suite.py --build build/t30-cam --workload two-paul-release \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/t30-cam-combat --with-ko --with-timeout
build/asset-venv/bin/python scripts/diff_saturn_screens.py \
  assets/imported/t30-z-frozen assets/imported/t30-cam-control-frozen
```

Frozen probes need `--sh2-cache on` to match the existing baselines.
