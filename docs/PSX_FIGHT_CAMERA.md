# Original fight camera on Saturn

`T3_PSX_CAMERA` (default OFF; requires `T3_ENABLE_PSX_COMBAT`) drives the
Saturn view from the original Tekken 3 fight camera instead of the fixed
framing camera. The camera port (`src/game/psx_camera.c`) matches the
original's golden traces on every frame. This report covers how it is wired
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
  every following one. The first round zeroes the state, seeds all three
  generators with 1, starts in phase FIGHT and has no intro timer. Later
  rounds start from RESET. Each round sets `screen_x` to ∓100, clears the
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

- **Backdrop.** The VDP2 backdrop is still placed for the old camera. With the
  lower original eye, a black line shows between its bottom edge and the
  floor horizon. The original fills that gap with the panorama box: the floor
  grid reaches only about ±9,000 units around the fighters' tile. Fixing it
  means wiring the backdrop turn and the box (Tekken3Remake `stages.md`),
  which is the next stage task.
- **Overhead KO depth.** The renderer does not apply the overhead
  `t[2] − 0xA3A` fighter depth. Only `screen_x` uses it.
- **Bank streams.** Throw camera streams are not embedded, because the
  simulation does not support throws yet.
- **Shake coverage.** The scripted host match rarely launches anyone, so it
  started no shakes. The shake itself is covered by the camera trace tests.

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
