# Reaching the 1,800-image Mednafen gate

Goal: 60 committed logic ticks/s and 30 images/s. The measurable gate is the
HUD `FRAME` counter in a native Mednafen CD run (`two-paul-release`, neutral
input). Over the 60 s window it must advance by at least 1,800. Mednafen is
deterministic for an identical binary. Unrelated layout changes still move the
count by about ±15, so only steps well beyond that are kept.

| Build | Change | FRAME delta / 60 s |
| --- | --- | ---: |
| t30-q | starting point (cold Low WRAM, overlap, DMA command list) | 1,755 |
| t30-t | one SDK call per HUD string | 1,759 |
| t30-u | placement fused into skinning | 1,776 |
| t30-y | consume only actor 1's geometry after the parallel join | 1,778 |
| **t30-z** | **Slave runs actor 1 preparation and the deferred ticks as one task** | **1,844** |

The game timer advances one second per real second in every recording, so the
tick rate stays at 60/s. `FRAME` counts submitted images. Displayed-image
cadence still needs a physical-hardware check (see
[COLD_LOW_WRAM_EXPERIMENT](COLD_LOW_WRAM_EXPERIMENT.md#costs-and-limits)).

## Where the time went

`T3_OVERLAP_VDP1` removes the vblank wait, so an image costs whichever CPU
finishes last. One image budget is 7,465 FRT (two fields).

- **Master path:** input and leading ticks, scene preparation (camera, floor,
  the Master's actor, then the merge), flush, HUD.
- **Slave path:** its share of the actor preparation, then the ticks deferred
  to it, which the Master joins before presenting.

The wait probe showed both CPUs close to critical. The Slave chain was longer
by about 220 FRT on average. Before this change it was:

1. the Slave finishes actor 1;
2. it signals completion;
3. the Master finishes actor 0 and merges;
4. the Master captures the image state;
5. the Master submits the simulation job;
6. the Slave wakes and starts ticking.

Every step after step 1 was pure latency on the critical path. That explains
why the Master-only savings (HUD +4, invalidation cut +2) barely moved the
count, while shared preparation savings (fused placement +21) did.

## The change

`characters.c` accepts an optional worker-start hook
(`t3_characters_set_worker_start`). With a hook installed, the fused
preparation no longer calls `sat_parallel_for`:

- The hook submits one Slave task.
- The Master prepares actor 0.
- The Master spins on an uncached `worker_ready` flag that the task raises as
  soon as actor 1 is prepared.
- The Master then pulls only actor 1's geometry into its cache.

In `main.c` the hook is installed only for images with deferred ticks:

- **Hook:** `start_prepare_simulation` captures the image state and submits
  `PREPARE_SIMULATION_TASK` with the pads, first tick and count the old
  `simulation_submit` call used.
- **Task:** `prepare_simulation_entry` purges the Slave cache (new
  `sat_parallel_purge_cache` export), prepares actor 1, then runs the unchanged
  `t3_simulation_task_entry`.
- **Fallback:** images without deferred ticks keep `sat_parallel_for`.
  `simulation_join` is unchanged.

The Slave's ticks now start right after actor 1, overlapping the Master's
actor 0 and the merge. Nothing on the Master reads `g_game` or move-probe state
after the task starts. Poses and world centers are copied in `prepare_actors`
and in the fused context before dispatch, and the HUD reads the captured
`g_image_match`.

## Supporting SDK changes (private clone, not yet upstream)

- `sat_skin_deform_range_placed` (`skin3d.h`) stores `x*scale + offset` within
  the deform loop. It rejects any placement whose arithmetic could overflow,
  and it reports any coordinate outside `limit` as `SAT_ERR_INVALID_ARG`. The
  game's `t3_psx_skin_placement` supplies the same ×6 / −cx·96 / cz·96
  transform that `t3_psx_skin_place` applied as a separate pass.
- `push_ascii_text` (VDP1 HAL) emits a string's glyph sprites in one call,
  with the same checks and order as one `push_sprite` per glyph.
- `sat_parallel_purge_cache()` exposes the executor's Slave cache purge to
  custom tasks.
- In the scene compiled-program emitter, `command_pair` packs two command
  words in memory order on either byte order. The SH-2 output is byte-identical
  (same ELF SHA). The two little-endian host tests that link `faces.cpp`
  (`test_scene3d_faces_api`, `test_skin_scene`) now pass. They use
  `tests/host_stubs/projected_program_spy.hpp`, which replays committed HAL
  slots through the tests' existing draw spy.

## Verification (t30-z)

- Frozen tick-120 frame: RGB SHA
  `dbd1dcf895998266165aed2f9e3a3cfbe9626638b3a864f1f2a3678a800a2c2a` over 97
  fields, which matches the reference.
- Continuous reference replays at `--sh2-cache on`: p1 5,481/5,481 and draw
  5,221/5,221 ticks, and p2 5,632/5,632. The default 48,000-frame checkpoint
  stopped p2 while diagnostics were being written, so the probe could not
  decode it. p2 passes at a 48,100-frame checkpoint.
- All 24 live original-combat cases pass (KO and timeout included).
- The game host ctest passes 50/50 against the private SDK headers. The SDK's
  `make test` passes in full; its Python tests need the asset venv for PIL.
- Mednafen: FRAME 360 → 2,204, a delta of 1,844. Screenshots at 30, 40 and
  45 s show both fighters, the stage and the HUD intact.

## Paul vs Law (2026-10-23)

The 1,800-image result above holds for two Pauls. Paul vs Law fell far short of
it in Mednafen while Ymir reported about 30 img/s. Ymir's harness turns SH-2 cache
emulation off by default. With `--sh2-cache on`, Ymir's relative gaps track
Mednafen's, but its absolute figures still run about a quarter high. Read
Mednafen's HUD FRAME counter at two times in round 1 for the real figure.

Ymir cache-on profiles, diffing the Law build against two Pauls, found two
problems:

- **Poses missed every frame.** Paul's idle loop is 54 frames and Law's is 98.
  Together they need 152 local poses:
  - The 128-entry FIFO local FK/IK cache evicted each pose before reuse.
  - The direct-mapped sample cache lost a third of the samples to collisions.

  One cache now keys a stream frame by its first key's address. It holds both
  the sampled pose and its FK/IK result. It is two-way set-associative, with 256
  entries, and consecutive frames go to consecutive sets
  (`src/game/psx_local_pose.c`). The idle loop then hits on every frame. A
  fight hits about 73%. The replaced sample cache's memory pays for it.
- **The stage tables cost the Master about 50k cycles per vblank.** The
  panorama lines, the floor coefficients and their encoding are now claimed with
  TAS.B by whichever CPU gets there first: the Slave after its ticks, or the
  Master after flush and HUD, just before the join. Whichever CPU did not build
  the tables drops its cached lines (`t3_stage_vdp2_sync`).

Results:

| Run | Before | After |
|---|---|---|
| Mednafen, round 1, neutral | 17.5 | 22.3 |
| Mednafen, round 1, CPU P2 | about 15 | 17.0 |
| Ymir cache-on, 1100→2000, neutral | 26.9 | 30.3 |
| Ymir cache-on, 1100→2000, CPU P2 | — | 27.5 |

Both runs hold 1.00 ticks/vblank. Two Pauls on the same tree run at 26.7 in
Mednafen. The AI pad hash over 1,000 updates is unchanged (0x949844c0).

What remains, from `T3_NATIVE_WAIT_PROBE` in Mednafen, in FRC/128 ticks of
about 4.8 µs for a 2-tick image:

| Phase | Paul vs Law | Two Pauls |
|---|---|---|
| Actor preparation (PREP) | 5,123 | 4,918 |
| Flush | 1,928 | 1,815 |
| HUD | 1,286 | 807 |
| Join | 642 | 187 |
| Total | 9,343 | 8,092 |

Paul vs Law's HUD figure includes the stage, which the Master claimed. Thirty
images need about 6,990 in total. In a fight, the Slave spends about 210k of its
477k cycles per vblank on the SDK's submission, projection and skinning for
actor 1. The AI takes about 20k, and pose and blend misses about 50k.
The next cut is renderer work on both CPUs.

## Geometry on both CPUs (2026-10-24)

Mednafen has no profiler, so the Slave is measured from inside the game.
`T3_SLAVE_FRT_PROBE` adds Slave FRT totals in Low WRAM, and
`scripts/mednafen_state.py` reads them from the save state that
`record_mednafen.py --save-state` keeps. In round 1 with CPU P2, each
deferred tick costs about 1,990 FRC/128, of which the AI's update is about 280.
Actor 1's preparation costs about 3,800. So both CPUs were over the 7,465 that
a 2-vblank image allows, not only the Master. A native AI would save at most
about 550 of the Slave's 7,800 per image.

`T3_NATIVE_PHASE_PROBE` in Mednafen splits each actor's preparation:

| Phase, FRC/128 | Before | Pipelined projection | Plus screen culling |
|---|---|---|---|
| P1 skin | 832 | 833 | 832 |
| P1 geometry (projection and faces) | 3,186 | 2,988 | 2,255 |
| P2 skin | 857 | 858 | 857 |
| P2 geometry | 3,229 | 3,039 | 2,160 |
| Master, dispatch to merge | 4,171 | 3,982 | 3,171 |

Two LibSaturn changes made the difference:

- **Projection with a rotated camera** now overlaps each divide with the next
  rows. The clip-Y row is formed during the X divide, and the next vertex's
  clip-W and clip-X rows during the Y divide. Clip Z is never formed. The
  products and rounding are unchanged, and a host test checks bit-exact output.
  On top of screen culling, the old loop gives 19.9 (CPU P2) and 25.4 (neutral)
  in Mednafen, against 20.9 and 26.4. Ymir's cache-on profile instead shows the
  new loop slower (51k to 65k cycles a vblank), so trust Mednafen for divider
  scheduling.
- **`SAT_SCENE3D_CULL_SCREEN`** culls back faces by projected area alone. The
  default also tests each surviving face's world normal, which reads all four
  world corners again. With two actors' projected and world vertices (about
  12 KB) against a 4 KB cache, those reads missed. The original culls with NCLIP
  on the screen corners only (`3dmk-models.md`), so the fighters use the screen
  test. A face with a corner behind the camera still takes the world test.

In a matched frozen pair at tick 120 (two Pauls, Ymir with cache on), 8 more
faces out of 713 are drawn. The model differs by about six isolated edge pixels.
Every other diagnostic field is equal, and Ymir's loop rate goes from 36.0 to 40.3.

Mednafen, round 1, Paul vs Law:

| Input | Before | After |
|---|---|---|
| Neutral | 22.3 | 26.4 |
| CPU P2 | 17.0 | 20.9 |

The round timer still advances 15 s in 15 s. The AI pad hash is unchanged
(0x949844c0), and host ctest and LibSaturn `make test` pass.

Other results from this round:

- **Giving the Master half of actor 1's skin** (`T3_MASTER_SKIN_PERCENT`)
  gave 17.0 at both 50% and 0%. The default is now 0. Inlining the lead path no
  longer fits in High WRAM, which has 32 bytes free.
- **`DEFERRED_TICKS=3`** gave 18.3 against 17.0 with CPU P2 (before the
  geometry change). It stays at 2, because each Slave tick adds a tick of
  pose lag.

What remains is memory as much as time. High WRAM's largest items are:

- the 256-entry local pose cache (110 KB)
- the two decoded-in-place character archives (166 KB, of which about 94 KB
  of texture bytes are dead once uploaded)
- `g_actors` (75 KB)
- the SDK's runtime state (64 KB)

## Skinning straight to screen (2026-10-24)

A Ymir cache-on profile showed each CPU spending, per image, about 110k cycles
skinning its actor to world space and 148k projecting the result: two
transforms per vertex, with the world vertices written out and read back. The
original concatenates each joint's matrix with the camera's and runs RTPT on
the model vertex once. `T3_SKIN_TO_SCREEN` (on by default with fused actors)
does the same through two new LibSaturn calls:

- `sat_skin_clip_joints` multiplies each joint by the ×6 placement and the
  camera's clip X, Y and W rows.
- `sat_skin_project_range` transforms each vertex once and divides as the
  projection did. Single-joint vertices take a loop whose rows are four
  `mac.l` products each, with translation and rounding in the accumulator,
  and a 32-bit clamp test that settles every on-screen point.

The actor is then submitted with `pre_projected` set, so the face pass skips
projection. A pose or camera off the compiled fast path (a corner behind the
camera, a coefficient that could overflow) falls back to skinning to world;
`g_t3_skin_screen_fallbacks` counts it.

Rounding once instead of twice moves a clip coordinate by at most 8 units of
16.16 in the host test (1 vertex in 3,840 moved a pixel). In a matched frozen
pair at tick 120 (Paul vs Law, Ymir with cache on), the faces drawn and culled
are identical (721 and 829). The model differs by one pixel; the only other
difference is the HUD FRAME counter, 637 against 711.

Mednafen, round 1, Paul vs Law:

| Input | Before | After |
|---|---|---|
| Neutral | 26.7 | 29.2 |
| CPU P2 | 20.6 | 22.7 |

The AI pad hash is unchanged (0x949844c0). Host ctest (55 of 55) and LibSaturn
`make test` pass, and all 24 live original-combat cases pass in Ymir, KO and timeout
included. High WRAM ends at 0x060FA160, 7.8 KB below the 0x060FC000 limit.

Two smaller changes came first, to free memory:

- `psx_ai.c` is compiled with `-Os`. That frees 17 KB of High WRAM against
  `-O3`. Whole-game `-O2` would free 32 KB but drops CPU P2 from 20.9 to 19.3.
- The control hints under the HUD are drawn only while a round is introduced.
  The original has none, and each glyph is a VDP1 command the Master writes.
  Together: 20.6 with CPU P2 (20.9 before, within noise) and 26.7 neutral (26.4).

## Late images and the third tick (2026-10-24)

A follow-up to skinning straight to screen removed about 9k cycles a vblank
from each CPU in Ymir (a variable shift in the clamp test called libgcc twice
per vertex), yet CPU P2 stayed at 22.8 in Mednafen. So the rate was not set by
either CPU's total work. `T3_NATIVE_WAIT_PROBE`, with its HUD
rows hidden and its totals read from a Mednafen save state, splits the Master's
image by the number of ticks it carried (FRC/128, round 1, CPU P2, after tick
120). Its HUD phase includes JOIN, so HUD below is the probe's figure minus
JOIN; a build that drew only the FRAME counter measured the same rate.

| Phase | 2 ticks | 3 ticks |
|---|---|---|
| PRE (leading tick) | 151 | 2,674 |
| PREP | 3,183 | 3,305 |
| FLSH | 2,133 | 1,966 |
| HUD | 206 | 208 |
| JOIN | 1,586 | 2,134 |
| POST | 181 | 181 |
| Total | 7,440 | 10,468 |

A two-tick image sat on the 7,465 of two vblanks. A three-tick image ran its
leading tick on the Slave before the capture while the Master waited, and took
three vblanks, so the next image carried three ticks again. 216 of 402 images
were three-tick images.

Two changes break that loop:

- **`DEFERRED_TICKS` is 3.** All of a late image's ticks run after its capture,
  alongside the Master's work. An image is now at most three ticks (50 ms)
  behind the simulation instead of two, and only late images are; the logic
  still steps 60 times a second. The release workloads accept a pose lag of 3.
- **`T3_MASTER_WORKER_FACES`** (on with the Slave simulation). The Slave skins
  actor 1 to screen and goes straight on to the deferred ticks. The Master,
  which waited for actor 1 anyway, submits its faces after its own, with no
  prepared batch to reserve or merge.

| Change, Mednafen round 1 | CPU P2 | Neutral |
|---|---|---|
| Before | 22.8 | 29.2 |
| `DEFERRED_TICKS=3` alone | 23.2 | |
| Master submits actor 1's faces alone | 23.5 | |
| Both | 25.9 | 29.9 |

With both, 319 of 454 images carry two ticks:

| Phase | 2 ticks | 3 ticks |
|---|---|---|
| PRE | 152 | 279 |
| PREP | 4,337 | 4,408 |
| FLSH | 2,157 | 1,971 |
| HUD | 210 | 211 |
| JOIN | 511 | 2,209 |
| POST | 183 | 183 |
| Total | 7,550 | 9,261 |

The Master's PREP grows by about 1,150: actor 1's face pass (1,102 on the
Slave) and the cache syncs before it. Both CPUs are now near 7,400 for a two-tick image, so the
next gains have to come from less work on both, not from moving it.
`scripts/profile_by_object.py` groups a Ymir profile by object file; a Slave
tick is about 190k cycles there, of which the AI is about 38k, pose
composition 33k, blend math 23k, motion 14k and block copies 23k.

The combat suite first panicked at a round-2 start: the Master's submit of
actor 1 was refused as pre-projected (`SAT_ERR_UNSUPPORTED`, the documented
fallback to world vertices), but `sat_scene_submit_instance` recorded the
refusal as the frame's error and `sat_scene_flush` returned it. LibSaturn no
longer records that refusal (host test in `test_scene_api.cpp`); all 24 cases
then passed.

Most of the motion cost was `t3_motion_open` summing every knot span of a
stream, about 9k cycles a call. The branch look-ahead in
`t3_move_vm_queue_input` resolved the same target each frame of a window. It
now peeks (`t3_move_vm_peek`, header and key bounds only), as does
`same_animation`. A target is still resolved in full by `t3_move_vm_queue`
before it is used, and a stream is still validated before it is sampled.
CPU P2 26.0, neutral 29.9; the AI pad hash is unchanged.

### Where a tick goes in Mednafen

`T3_SIM_PROFILE` now runs in a serial build in Mednafen:
`-DT3_SLAVE_SIMULATION=OFF -DT3_SIM_PROFILE=ON -DT3_SIM_PROFILE_CAPACITY=2`.
A capacity of 2 retained rows keeps the capture out of Low Work RAM; the totals
cover every tick. Record with `--save-state --neutral-input` and the
diagnostic workload. Then:

    mednafen_state.py RUN --elf ELF --symbol _g_t3_sim_profile --dump DIR
    analyze_simulation_profile.py --raw DIR/_g_t3_sim_profile.bin --out report.json

New stages split the match step: the AI, the camera, local pose, the
full-pose sample, world root, slot order and body shapes. Round 1, CPU P2,
ms per tick (instrumented, so about 5% high):

| Stage | Before the cursor | With it |
|---|---|---|
| Whole tick | 9.96 | 9.78 |
| compose (pose, blend, parts) | 4.64 | 4.45 |
| AI | 1.77 | 1.76 |
| parts (`t3_psx_compose_parts`) | 1.22 | 1.21 |
| full-pose sample | 1.07 | 0.71 |
| blend | 0.73 | 0.74 |
| camera | 0.67 | 0.67 |
| IK | 0.66 | 0.66 |

`T3_SLAVE_FRT_PROBE` gives the Slave's side in the parallel build: a tick is
about 1,989 FRC/128, actor 1's skin 1,487 per image, and the stage tables
about 1,259 when the Slave claims them (322 of 504 images). The Master's
two-tick image is about 7,040 without its wait, so the Slave sets the rate.

The sample walked every spline channel from its first knot on each call,
and a local pose cache miss happens about once a tick per fighter. A knot
cursor (`t3_motion_sample_cursor`) keeps where each channel's walk stopped,
for the four streams the local pose cache sampled last. It steps forward or
back from there, so a frame near the last costs a step or two. A step back is
the forward step undone: a knot's `p1` is its `p0` plus the key before the
last read, and `prev` is `cur` less the last key's span. That also covers the
odd channels, which run backwards. `test_psx_motion` checks 17,158 samples of
random streams (forward, backward, jumps, switching streams) against
`t3_motion_sample`. CPU P2 went from 26.0 to 26.7; the AI pad hash is
unchanged. High Work RAM now ends at 0x060FB3B0.

## Current Paul vs Law checkpoint (2026-10-24)

The later `t30-nxt` build advances `FRAME` from 73 at 25 s to 515 at 40 s
with P1 neutral and the CPU controlling Law: **442 images in 15 s, or 29.47
images/s**. The `t30-exp` build, which only enables `T3_SLAVE_FRT_PROBE` in its
CMake configuration, advances from 72 to 514: the same 442 images. The earlier
`t30-bis` recording advances from 66 to 473: 407 images, or 27.13 images/s.
These are round-1 Mednafen CD screenshots, not Ymir estimates. The 30 images/s
gate needs 450 images in this window, so the current candidate is eight short.
The source changed between `t30-bis` and `t30-nxt`; this pair does not isolate
which change accounts for the difference.

The `t30-exp` save state confirms the CPU path ran (`g_t3_ai_calls=1160`). Its
Slave FRT totals are 2,056,040 for ticks, 783,309 for actor preparation and
482,894 for stage builds; the Slave claimed 486 stage builds and the Master
82. These totals are diagnostic, not a per-image critical-path measurement.
The release candidate's ELF SHA-256 is
`f340541abdae4457e52dd9fc005afbcd2f7656b1232936418361e2c41dc49188`;
the probe ELF is
`75caa24426ce1628474df9b2377c208ec8a84c48aec4280dcc9be33a01d68321`.
The current host CTest suite passes 55/55 and `mwf-combat` passes 24/24 live
cases. Next, profile the release candidate's two CPU paths in the same round-1
window and cut a measured critical-path cost before repeating the gate.

## One-pass screen culling (2026-10-24)

The release candidate's `T3_NATIVE_WAIT_PROBE` counters were first recorded
with their HUD rows visible. That build submitted 414 images from 25 to 40 s,
so its rate did not represent the release build. The new
`T3_NATIVE_WAIT_PROBE_SILENT` option keeps the counters but leaves the HUD
hidden. The silent build submitted 441 images in the same window, against 442
for the uninstrumented control. Its saved state confirmed 1,163 AI calls and
reported these average Master phases in FRT/128 ticks:

| Image | Count | Pre | Prepare | Flush | HUD plus join | Join | Post |
|---|---:|---:|---:|---:|---:|---:|---:|
| Two ticks | 490 | 282 | 4,210 | 1,730 | 662 | 460 | 183 |
| Three or more | 20 | 309 | 4,235 | 1,704 | 2,348 | 2,143 | 182 |

The Slave totals over this run are 2,062,701 FRT for 1,163 ticks, 783,803
for actor preparation and 481,856 for 485 claimed stage builds. A two-tick
image puts most pressure on the Master's preparation and flush. A three-tick
image adds a long wait for the Slave. These are phase totals, so the next
optimization must still be checked by the Mednafen FRAME counter.

LibSaturn's compiled face path used a 128-face survivor queue even when
`SAT_SCENE3D_CULL_SCREEN` needs only a projected area test. That mode now
tests and appends each face in one pass, keeping its indices and projected
corners hot. The pre-projected fallback and the other culling modes still use
their established paths. `t30-face` differs from the retained `t30-nxt`
control by this SDK change (the game source also adds a missing time header
and an unused silent-probe option). The CMake `T3_` settings match.

In Mednafen, CPU Law and neutral P1, `t30-face` advances FRAME from **75 at
25 s to 527 at 40 s: 452 images / 15 s, or 30.13 images/s**. The control
advances 73 to 515: 442, or 29.47 images/s. Both screenshots show round 1
with the fighters, stage and HUD intact. The new ELF SHA-256 is
`1717afe0bddd1aec13a19a131e7525b061ec9e11e1fd5be13e7291a6e6f49e7e`.
LibSaturn's full `make test` passes with the asset Python environment, the
game's host CTest passes 55/55, and `t30-face-combat` passes 24/24 live cases,
including KO, replay, next round, terminal match, reset and two neutral
timeouts. This 15 s round-1 window crosses 30 images/s; the historical 60 s
two-Paul gate is a separate measurement.

## Longer CPU Law encounter (2026-10-24)

The same uninstrumented `t30-face` CD image was recorded for 70 real-time
seconds in Mednafen with neutral P1 and CPU Law. Its ELF SHA-256 matches the
one-pass candidate above. Screenshots at 25, 40, 55 and 65 seconds show the
HUD `FRAME` counter at 75, 527, 995 and 1297. Thus the active round-1 fight
submits 920 images between 25 and 55 seconds, **30.67 images/s** over 30
seconds. The 40–55-second interval submits 468 images, **31.2 images/s**.
At 65 seconds the timeout result is on screen; the 55–65-second interval
submits 302 images, **30.2 images/s**. The 25–65-second aggregate is 1222
images in 40 seconds, **30.55 images/s**. These counters count submitted
scenes, not independently measured display changes or logic ticks.

`assets/imported/t30-sustain-cd` retains the screenshots, recording metadata
and hashes. Its `run.mov` was deleted after inspection.

The same ELF was also run for 4,200 emulator fields in headless Ymir with
SH-2 cache emulation and port 2 disconnected. After boot it reached 3,799
scheduled and committed simulation ticks, **zero discarded fields**, round 2
with Law ahead 1–0, and 2,329 submitted images. It reported no runtime
error, draw timeout, attack decode error or unsupported move. This is
injected execution, not a second CD boot or a Mednafen image-rate
measurement. Its private observation is in `assets/imported/t30-sustain-probe`.

## Retained evidence (private, git-ignored)

- Builds: current candidate `build/t30-face` and exact preceding control
  `build/t30-nxt`. Superseded `t30-bis`, `t30-exp`, `t30-law`, `mprobe`,
  `simprof`, `frtp` and `sts-prof` build directories were deleted. Their
  measured results remain above. Earlier `t30-ctl`, `t30-z` and `w3-z`
  builds were also deleted.
- Mednafen evidence: `assets/imported/t30-face-cd`,
  `assets/imported/t30-sustain-cd`,
  `assets/imported/t30-nxt-cd`, and silent-probe saved state plus matching
  ELF in `assets/imported/t30-silent-cd`. Their `run.mov` files were deleted
  after reading the screenshots and counters. The noisy visible-HUD probe,
  older `t30-exp` and `t30-bis` recordings were deleted.
- Live combat: `assets/imported/t30-face-combat/suite.json` (24/24 cases).
- Long CPU Law probe: `assets/imported/t30-sustain-probe/observation.json`.
- Recordings: `assets/imported/t30-z-cd` and `assets/imported/w3-z-cd`.
- Combat suite: `assets/imported/t30-z-combat/suite.json`.
- Screen-culling frozen pair (tick 120, Ymir cache on):
  `assets/imported/cull-frozen` (with `diff.png`) and `assets/imported/cull-control-frozen`.
- Earlier Paul vs Law combat suite: `assets/imported/t30-law-ai-combat/suite.json`.
- Skinning-to-screen frozen pair (tick 120, Ymir cache on): `assets/imported/sts-frozen`
  (with `diff.png`) and `assets/imported/sts-control-frozen`. Its combat suite output
  was deleted by mistake; the result is recorded above.
- The earlier `mwf-combat` output was deleted after the current suite passed
  the same 24 cases.
