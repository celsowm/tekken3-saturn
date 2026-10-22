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

## Retained evidence (private, git-ignored)

- Builds: `build/t30-z`, `build/t30-z-frozen`, `build/t30-z-ref-{p1,p2,draw}`,
  and the probe build `build/w3-z`.
- Recordings: `assets/imported/t30-z-cd` and `assets/imported/w3-z-cd`.
- Combat suite: `assets/imported/t30-z-combat/suite.json`.
