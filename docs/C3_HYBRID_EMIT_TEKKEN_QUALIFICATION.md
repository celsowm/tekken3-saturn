# C3 hybrid VDP1 emitter: Tekken 3 Saturn qualification (2026-11-19)

LibSaturn's opt-in C3 emitter (`-DLIBSATURN_VDP1_HYBRID_EMIT=ON`, `SAT_VDP1_EMIT_EXPERIMENT=3`) writes
`push_distorted_sprite`, `push_projected_sprite` and `push_projected_program` with packed 32-bit stores. This report
asks whether it helps the real game. It does not hurt it, and it cannot help it: Tekken almost never calls those
three emitters.

**Recommendation: keep C3 opt-in. Do not promote it on Tekken evidence.** Physical Saturn validation remains outstanding.

## Method

- **Matched SDKs.** One LibSaturn revision (`42950709`) built twice from the same tree, with the same compiler
  (sh2eb-elf-gcc 13.2.0) and flags. The two archives differ in exactly one member, `vdp1.cpp.obj`, out of 183. Only
  hybrid carries `-DSAT_VDP1_EMIT_EXPERIMENT=3`. Each game's linker map pulls `vdp1.cpp.obj` from its own
  archive. Checked by `lib-saturn/harness/scripts/inspect_vdp1_emit_sdk.py`.
- **Matched game.** Tekken `55d0764`, built in an isolated worktree, once against each SDK, with original assets.
- **Matched input.** A tick fixture (`T3_LOCAL_INPUT_TAPE`, written by `scripts/build_c3_combat_tape.py --ticks`)
  feeds both pads identical buttons on identical simulation ticks. The pad device script is idle.
- **Matched states.** Live runs compare equal fields, which are not equal states. So builds frozen at a simulation
  tick (`T3_RENDER_BENCH_TICK`) compare the state, the bank-normalised VDP1 command list
  (`scripts/compare_vdp1_vram.py --banked-control`), the raw framebuffer and the screenshot at the same tick.
- **Separate runs.** Cadence runs never profile. Profiles are separate diagnostic runs.
- Emulator: the Ymir probe, `--sh2-cache on`.

## Emitter code

| Emitter | Control: instructions / `mov.w` / `mov.l` | Hybrid: instructions / `mov.w` / `mov.l` |
|---|---|---|
| `push_projected_program` | 86 / 7 / 6 | 90 / 1 / 8 |
| `push_projected_sprite` | 197 / 7 / 10 | 193 / 1 / 11 |
| `push_distorted_sprite` | 210 / 8 / 12 | 208 / 2 / 13 |

## Coverage: where Tekken reaches C3

- **Fight geometry: never.** Fighter and stage faces go through `sat_scene3d_program_run_sh2`, the assembly
  program-run kernel. They never call the three C emitters.
  - In the 48-frame profile window (fields 2000–2047), the three emitters ran 0 cycles and 0 calls on both CPUs, in
    both builds.
  - The generic emitters that do run there (`push_scaled_sprite` 0.65%, `push_polygon`) are not touched by C3.
- **Hit sparks: rarely.** `flipbook_burst.c` draws its sprites with `sat_draw_sprite_distorted`, which is C3's
  `push_distorted_sprite`.
  - Over fight ticks 0–3600 (13 hits), they drew 267 sprites in control and 271 in hybrid.
  - That spans about 3,377 presented images, so about 0.08 C3 calls per image, against about 790 VDP1 commands per
    image.
- **Tekken Force: never.**
  - The floor uses `sat_sprite_batch_emit_distorted`, which writes commands itself: 99 calls in the window, 0.03%
    of Master time.
  - Projected materials use `program_run_sh2`.
  - Over fields 1500–1547 of the Force input probe, the three emitters ran 0 cycles.
- **Tekken Ball HUD: not C3.** It uses `push_distorted_sprite_gouraud`, which C3 does not change (232 bytes in
  both builds).
- **Menus: not C3.** `menu_main.c` has no C3 call site.

Expected effect on Tekken: zero by construction. The A/B below is therefore a non-regression and correctness check.

## Results

### Paul and Law (pair `0;1`): matched states, frozen at a tick

| Tick | Logical state | Command list | Framebuffer bytes differing | Screenshot pixels differing |
|---|---|---|---|---|
| 900 | identical | MATCH, 799 | 0 | 0 |
| 1800 | identical | MATCH, 785 | 0 | 0 |
| 2700 | identical | MATCH, 765 | 0 | 0 |
| 3600 | identical | MATCH, 783 | 0 | 0 |

Raw VRAM is not byte-identical, for two reasons:

- Hybrid has presented one more image by the freeze, so its current bank is the other one: the root link is 0x20
  against 0x14020.
- The other differing bytes are stale commands after END in both banks.

The normalised lists are kept in `diff/frozen-*-commands.json`.

### Determinism

Rerunning the tick-3600 frozen capture reproduced, in both builds:

- the same VRAM, framebuffer and screenshot SHA-256
- the same diagnostic state

### Paul and Law: live checkpoints

| Fields | Sim ticks (control / hybrid) | Images (control / hybrid) | Late swaps | Draw waits | Timeouts | Simulation state |
|---|---|---|---|---|---|---|
| 3,000 | 2175 / 2175 | 1615 / 1617 | 125 / 126 | 2 / 2 | 0 / 0 | identical |
| 8,000 | 7175 / 7175 | 5815 / 5817 | 307 / 303 | 1770 / 1786 | 0 / 0 | identical |
| 30,000 | 29175 / 29175 | 27815 / 27817 | 307 / 303 | 23770 / 23786 | 0 / 0 | identical |
| 60,000 | 59175 / 59175 | 57815 / 57817 | 307 / 303 | 53770 / 53786 | 0 / 0 | identical |

- **Fight cadence.** Up to field 4,800, the fight gives 3,594 images in control and 3,596 in hybrid: 44.9 against
  45.0 images/s at 60 simulation ticks/s. No fields were discarded.
- **Code layout.** Hybrid's `.text` is 8 bytes smaller (537,528 against 537,536 bytes). That shifts everything
  linked after `vdp1.cpp.obj`. With the SH-2 cache modelled, this layout shift alone moves image counts by one or two
  (`docs/TEKKEN_FORCE_PLAN.md`, "Force sound on"). The +2 images is that noise, not an emitter gain: the emitters
  barely run.
- **Field 3,000 screenshot (982 pixels differ).** The two builds are showing different ticks at that field. The
  frozen comparisons above show the render is identical at equal ticks.
- **Faces and commands.** `faces_submitted` and `commands_used` also differ at 3,000 for the same reason. They
  describe the presented image, not the simulation.
- **Match end and the long run.** The match ends 2–0 at about tick 7,175. The 30k and 60k checkpoints therefore
  cover a stable result screen, not further combat.
- **Faults.** No timeouts, no rejected faces, no errors.

### Lei and King (pair `2;3`, another stage)

- **Frozen at tick 1800:**
  - logical state identical
  - command list MATCH (779 commands)
  - framebuffer and screenshot identical
- **Live, 8,000 fields:**
  - logical state identical through the 2–0 match end: 10 hits, 4 blocks, health [300, 0]
  - final screenshot identical
  - images 3,191 against 3,192 up to field 4,801
  - late swaps 269 against 275, draw waits 258 against 256, no timeouts

### Tekken Force (input-probe replay, `scripts/ymir_force_rate.py`)

| | Control | Hybrid |
|---|---|---|
| Input hash at tick 600 | 1714359845 | 1714359845 |
| State hash at tick 600 | 2983578197 | 2983578197 |
| Images by tick 600 (603 fields) | 233 | 233 |
| Worst gap (fields) | 5 | 5 |
| Pose cache hits / misses / evictions | 126 / 905 / 873 | 126 / 905 / 873 |
| Screenshot and framebuffer at field 3000 | identical | identical |

### Memory and stack

| Measure | Control | Hybrid |
|---|---|---|
| `.text` (bytes) | 537,536 | 537,528 |
| `.data` (bytes) | 516 | 516 |
| `.bss` (bytes) | 449,312 | 449,312 |
| WRAM-H free above `__bss_end` | 44,768 | 44,784 |
| Slave simulation stack used (`__stack_reserve` 16,384) | 5,352 | 5,352 |
| VDP1 VRAM free | 64,288 | 64,288 |
| `BALL.BIN` size | unchanged | unchanged |

### LibSaturn host suite

`gmake test` at `42950709` exits 0. This includes `test_vdp1_upload`, `test_texture_api` and
`test_texture_registry`, which failed earlier in the campaign from unrelated texture-API drift. The earlier host C3
A/B (`run_vdp1_emit_host_v4.py`) gave identical command hashes for modes 1 and 3.

### Not run

- **The 704×448 menu:** it has no C3 call site.
- **An audio capture:** the SDKs differ only in `vdp1.cpp.obj`, and the audio state is unchanged.
- **Tekken Ball:** its HUD sprite is outside C3, and its bursts are the same flipbook path measured above.
- **Mednafen:** with no coverage there is nothing for a second emulator to separate.
- **Physical Saturn.**

## Where the frames go (control profiles)

- **Fight Master** (fields 2000–2047, 21.56 M cycles):

  | Function | Share | Code |
  |---|---|---|
  | `sat_skin_rigid_project_sh2` | 22.7% | assembly |
  | `screen_cull_sh2` | 17.5% | assembly |
  | `program_run_sh2` | 8.2% | assembly |
  | `skin_blend_project_sh2` | 5.1% | assembly |
  | `projected_bounds_sh2` | 3.6% | |
  | `skin_clip_joints_sh2` | 3.4% | |
  | `paint_order_grouped_buckets` | 2.7% | C++ |
  | `paint_keys_quantise`, `paint_keys_spread`, `paint_keys_range` | 6.5% together | |
  | `t3_stage_backdrop_lines_offset` | 2.7% | |
  | `cache::invalidate_range` | 2.4% | |

  Draw waits are near zero during the fight (2 by field 3,000), and frame work is about 20 ms. **The fight is
  CPU-bound on the Master's render preparation, not VDP1-bound.**
- **Fight Slave:**
  - About 30% is waiting or polling: `frt::counter` 12.5%, `mailbox::slave_wait_request` 8.9%, `capture_pending`
    5.7%, `signal::pending` 2.6%.
  - The rest is the PSX simulation: `t3_psx_mul_rot` 5.4%, `pose_ik`, `pose_fk`, `compose_parts`.
  - **The Slave is sync-bound and off the critical path.**
- **After the match** (result screen): draw waits rise by about one per field. That is presentation-bound idling and
  needs no work.
- **Tekken Force Master** (the Slave does not run in Force):

  | Function | Share | Code |
  |---|---|---|
  | `project_vertices` | 9.1% | generic C++ |
  | `deform_vertices<0, sat_vec3, PlainStore>` | 6.6% | generic C++ skinning |
  | `append_screen_culled<false,false>` | 6.0% | generic cull |
  | `t3_psx_mul_rot` | 5.7% | |
  | `submit_instance<true>` | 4.7% | |
  | `paint_order_grouped_buckets` | 1.0% | |

  233 images in 603 fields is 23 images/s. **Force is CPU-bound on one SH-2.**

## Compatibility and opportunity matrix

| Candidate | Tekken path today | Coverage | Opportunity | Compatibility notes |
|---|---|---|---|---|
| B1 general projection | Fight: fused into `skin_*_project_sh2` (assembly); `project_vertices_general` 0.7%. Force: `project_vertices` 9.1% | Force high, fight low | **High for Force** | Must keep exact rounding; Force's static and skinned instances are the gate scenes |
| B2 skeleton | Tekken uses its own PSX pose code (`t3_psx_*`), not LibSaturn's skeleton | none | none in LibSaturn | Game-side `t3_psx_mul_rot` (Slave 5.4%) is off the fight's critical path |
| B3 skinning | Fight: rigid and blend assembly already (about 28% Master). Force: generic `deform_vertices` 6.6% | Force | **Medium**: first check why Force misses the assembly kernels before writing new ones | Per-influence rounding must hold |
| C1 final buckets | `paint_order_grouped_buckets` C++: 2.7% fight, 1.0% Force | both | **Medium** | Stable tie order; exact queue bytes |
| C2 clipping and cull | Fight: `screen_cull_sh2` already assembly (17.5%). Force: generic `append_screen_culled` 6.0% | Force | **High for Force** | Exact visibility, double-sided rules |
| C3 emission | Program faces bypass it; only hit sparks (about 0.08 calls per image) | ≈0 | none for Tekken | Correct and deterministic here; retain opt-in |

The rejected world-cull and full-packed emitter were not revisited. No two candidates were enabled together.

## Ranked next priorities

1. **Force C2 + B1.** In Force, the generic cull (6.0%) and projection (9.1%) make up about 15% of a CPU-bound,
   single-SH-2 loop running at 23 images/s. These are generic LibSaturn paths, so the gain carries to other
   consumers.
2. **Force B3.** Find why Force skins with the generic `deform_vertices` (6.6%) while the fight uses the rigid and
   blend assembly. Routing it to the existing kernels may beat writing new ones.
3. **C1 final placement.** `paint_order_grouped_buckets`, the remaining C++ stage of the painter (2.7% of the
   fight Master).
4. **Fight Master work reduction.** Skinning and culling are already assembly (about 40%). More images need less
   work (visibility, pose caching), not new kernels.
5. **Slave speedups only after the Master stops being the limit:** the Slave already waits about 30%.
6. **C3:** no Tekken action.

## Reproduce

Paths are relative to `~/dev`. The worktree `tekken3-saturn-c3` is a checkout of `55d0764`, with `roms` and
`assets/imported` symlinked from the main tree. Both symlinks are excluded locally (`.git/info/exclude`).

```sh
python3 tekken3-saturn/scripts/build_c3_combat_tape.py --ticks --length 12000 \
    tekken3-saturn/build/c3q/combat.ticks.json
cd lib-saturn/harness/scripts
T=~/dev/tekken3-saturn
python3 run_vdp1_emit_tekken_v4.py --tekken ~/dev/tekken3-saturn-c3 --baseline $T/build/saturn \
    --bios $T/assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
    --device-script $T/build/c3q/idle.devices --game-build-root ~/dev/tekken3-saturn-c3/build \
    --game-define "T3_BOOT_FIGHTERS=0;1" --game-define T3_TEST_SKIP_INTRO=ON \
    --game-define T3_LOCAL_INPUT_TAPE=$T/build/c3q/combat.ticks.json \
    --frames 3000 8000 30000 60000 --freeze-ticks 900 1800 2700 3600 \
    --profile-frames 2600 2000 48 --timeout 7200 --jobs 8
python3 inspect_vdp1_emit_sdk.py ../build/vdp1-tekken-v4-<id> --game-build-root ~/dev/tekken3-saturn-c3/build
# Force: reconfigure each game build with -DT3_FORCE_INPUT_PROBE=ON -DT3_RENDER_BENCH_TICK=0, then
python3 $T/scripts/ymir_force_rate.py --build <game build> --out <dir> --frames 3000 --dump-vdp1
python3 $T/scripts/ymir_force_rate.py --build <game build> --out <dir> --frames 1600 --profile 1500 48
# Second pair: -DT3_FORCE_INPUT_PROBE=OFF "-DT3_BOOT_FIGHTERS=2;3" with T3_RENDER_BENCH_TICK=1800 (frozen),
# then 0 (live), and saturn_probe.py --frames 3300 --dump-vdp1 and --frames 8000.
# Burst coverage: saturn_probe.py --dump-symbol g_t3_flipbook_drawn --dump-symbol g_t3_flipbook_seen.
python3 $T/scripts/pack_c3_qualification.py --run ../build/vdp1-tekken-v4-<id> --extra <follow-up folder> \
    --out $T/build/c3_v4_tekken_qualification-return.zip
```

`build/c3_v4_tekken_qualification.json` is the machine-readable summary.
`build/c3_v4_tekken_qualification-return.zip` holds:

- the run's summaries, provenance, emitter disassembly, linker maps, logs, screenshots and normalised command lists
- the follow-up results

It holds no ROM, BIOS, VRAM dump or binary.
