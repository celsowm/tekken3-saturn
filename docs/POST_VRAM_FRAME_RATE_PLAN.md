# Post-VRAM Frame-Rate Optimization Plan

**Status:** Proposed; no runtime changes implemented by this document.  
**Date:** 2026-11-06  
**Primary repository:** `celsowm/tekken3-saturn`  
**SDK repository:** `celsowm/libsaturn`  
**Target:** Original 60 Hz combat simulation and at least 30 **newly presented, distinct** images/s in full-roster fights, including the worst sustained segment, without sacrificing visual or behavioral fidelity.

This is an **execution plan**, not evidence that any proposal below is faster. Implement one independently measurable change at a time, qualify it in both repositories when applicable, and keep an experiment only when the complete application improves.

## 1. Current, reproducible baseline

### 1.1 Source of truth

Use these existing studies before changing code:

- [30-image gate](THIRTY_IMAGE_GATE.md), especially **Shared screen projection** and **Stage tables written once** (2026-11-05).
- [Real-hardware results](REAL_HARDWARE.md), including real-device reports, cache state, AI spikes, strong-hit measurements, and the already-tested alternatives.
- [Strong-hit slowdown](STRONG_HIT_SLOWDOWN.md).
- [Direct-VRAM VDP1 bank validation](VDP1_BANK_VALIDATION.md).
- [Cold Low WRAM experiment](COLD_LOW_WRAM_EXPERIMENT.md).
- [Hot geometry in High WRAM](HIGH_GEOMETRY_MEMORY.md).
- [Material programs and descriptors in High WRAM](MATERIAL_TABLE_MEMORY.md).
- [Local pose reuse](LOCAL_POSE_REUSE_PERFORMANCE.md).
- [Compiled renderer prototype](COMPILED_RENDERER_PLAN.md).
- [Frame preparation overlap](FRAME_PREPARATION_OVERLAP.md).
- [Current harness-led performance work](SATURN_PERFORMANCE_PLAN.md).
- [Fight memory lifetimes](MEMORY_LIFETIMES.md).

Pin the exact application and SDK commits, compiler/toolchain, BIOS, emulator versions, input sequence, source-data hashes, build switches, ELF, disc image and runtime configuration **before** running any control. Compare a candidate to a clean same-source baseline, not to a historical run assembled with a different SDK or rendering mode.

### 1.2 Latest comparable full-roster result

The 2026-11-05 280-second neutral-input Kuma/Jin attract comparison in `THIRTY_IMAGE_GATE.md` records:

| Configuration | Marked submissions/s | Worst marked segment | Images in 280 s |
| --- | ---: | ---: | ---: |
| Shared screen projection | 25.57 | 23.80 | 2,401 |
| Stage-table single-write improvement | **26.41** | **24.54** | **2,491** |

The second build's seven marked segment rates are 24.90, 25.00, 28.05, 26.96, 24.54, 25.20 and 30.25 images/s. The remaining *relative* work to the 30/s target is approximately 13.6% for the marked average and 22.2% for the worst segment. These are emulator **submission** measurements, not certified distinct presentation FPS.

The earlier Paul/Law-specific 30-image gate and the 441-pair direct-VRAM smoke suite answer different questions. Do not use the easier two-Paul case, a single round, or a menu/idle test to claim the complete roster has met this target.

### 1.3 Critical-path model

The working architecture overlaps VDP1 drawing with CPU preparation. Both SH-2 processors contribute to skinning and projection; the Master emits the worker actor's faces, while the Slave prepares its share and executes deferred authoritative simulation ticks. Either processor may claim the stage-table job. The current chain is sensitive to two-versus-three-tick images: extra deferred ticks make the next image late, which feeds another slow image.

Recent measured Slave costs in `THIRTY_IMAGE_GATE.md`, expressed in its FRT/128 profiling units, are approximately:

- 2,242 per simulation tick;
- 817 per image for the Slave's share of actor-1 preparation;
- 1,388 for stage preparation when the Slave claims it, **before** the final stage single-write change;
- after that change, panorama line fitting ~628, floor coefficients ~394 and table encoding ~12, measured as separate stage components.

These are profile data from specified runs, **not additive physical-clock predictions**. Profile the exact new build again before using them to choose work.

The strong-hit path is a different bottleneck: original fighter projection, face/particle ordering and tile/mask composition cause large, episodic Master costs. The prior `STRONG_HIT_SLOWDOWN.md` measurements include 11–17 submissions/s during bursts in different historical builds; they are **not** the 26.41/s baseline and must not be mixed numerically with it.

## 2. Non-negotiable correctness and acceptance rules

1. Retain the original fighters, KMD geometry, texture identity, animation, depth ordering, clipping and color effects. No mesh decimation, animation skip, frame duplication, lower resolution or hidden fidelity toggle may count as a speedup.
2. Retain original 60 Hz tick order, AI decisions, RNG calls, hit/guard/KO/replay behavior, damage, camera and round flow. No simulation-time discard or altered input latency may be concealed as a frame-rate improvement.
3. Compare **logical state at the same tick**, not structs byte-for-byte (padding and pointers are not valid equivalence oracles). Keep the established original/host/Saturn differential tests and compare their selected state digests.
4. Compare **rendering at the same game tick and display field**. Use deterministic snapshots, VDP1 command/texture/LUT/Gouraud comparisons when the renderer changes, VDP2 registers/tables when the stage changes, and final composited pixels. A static frame without active hit particles does not qualify a particle-path change.
5. Preserve cache coherence, 16-byte cache-line granularity, ownership of shared data, Slave task synchronization, interrupt ordering, VDP1 bank safety, COPR/END handling, and fallbacks for unsupported geometry.
6. Enforce stock Saturn WRAM-H/WRAM-L, VRAM, Sound RAM, linker stack reserves, and measured Slave stack headroom. Show the ELF map for **every** release candidate. ADX, CD-DA, SFX, HUD, particles, roster selection and match restart must remain functional.
7. Distinguish **submitted lists**, completed list draws, actual swaps, independently changed displayed pixels, committed simulation ticks, discarded fields, late swaps, sound underruns and real-hardware cadence. Do not rename any one counter to FPS.
8. A measured gain must survive a same-configuration Mednafen A/B, a cache-enabled Ymir check, the appropriate gameplay/visual gates, and a second representative encounter. For very small changes, require alternating runs and improvement beyond the documented ~0.04 img/s noise/known layout effects. Any clearly worse worst segment rejects promotion even when the average improves.
9. Record failed experiments, measurements and reasons, then remove rejected runtime complexity rather than leaving speculative switches enabled by default.

## 3. P0 — Verify the real release configuration and reclaim useful WRAM-H

**Priority: first.** The newest VDP1 direct-VRAM path removed approximately **78.7 KiB of WRAM-H usage in specific paired audit/fight builds**, as recorded in `VDP1_BANK_VALIDATION.md`. That is a measured difference between particular builds, **not** a guaranteed free-space budget in the current full-roster, particles-on, ADX-enabled release.

### 3.1 Why this is the first experiment

The present `CMakeLists.txt` has an umbrella `T3_PARTICLES` block which **forces**:

- `T3_LOCAL_POSE_CACHE_ENTRIES=8`;
- `T3_HIGH_GEOMETRY_SCRATCH=OFF`;
- `T3_COLD_FACES=320`.

These values were selected for memory pressure. However, historical releases also enable individual particle options while their cached umbrella `T3_PARTICLES` is OFF. **Do not assume that any of these three forced values is active in the 26.41 img/s ELF.** Inspect the actual CMake cache, compile definitions, map and live code paths first.

A local pose-cache entry costs approximately 440 bytes. Prior controls report 3,568 bytes for 8 entries, 28,208 for 64 and 56,368 for 128; the measured larger cache improved active simulation work, but the benefit may be workload-dependent. Hot geometry placement has also improved Mednafen timings in prior experiments.

### 3.2 Implementation tasks — game

- [ ] Capture the existing final full-roster production/benchmark `CMakeCache.txt`, configuration manifest and linker maps, including all individual particle switches, `T3_COLD_LOW_WRAM`, `T3_HIGH_GEOMETRY_SCRATCH`, `T3_LOCAL_POSE_CACHE_ENTRIES`, `T3_COLD_FACES`, `T3_MASTER_SKIN_PERCENT`, `T3_FIGHTER_HAND_VARIANTS`, ADX settings and the SDK commit.
- [ ] Attribute the new VDP1 bank memory savings with **identical full-release options**, both before and after that SDK feature. Do not infer memory availability from a boot-pair audit executable.
- [ ] Create an opt-in, explicit memory-layout experiment interface. Remove unconditional `FORCE` only where a validated layout selector permits it, while preserving the safe historical default for older build configurations.
- [ ] Measure these candidates separately: (A) 8-entry cache + hot geometry where it fits; (B) 64-entry cache + existing geometry placement; (C) 64-entry cache + hot geometry; (D) 128-entry cache + existing geometry placement; optionally (E) 128-entry cache + hot geometry **only if** the complete linked image and stack reserve fit.
- [ ] Recheck `T3_COLD_FACES` against measured maximum cold-face use, original particle-path fallback and all roster pairings. Do not increase it merely because space appears to exist.
- [ ] Record local pose-cache hit/miss/eviction rates during neutral, CPU-vs-CPU, human-vs-CPU proxy, fast movement, KO and active effect bursts. Compare total Slave/Master work and end-to-end rate, not cache hit rate alone.
- [ ] Confirm the new configuration still boots a fresh disc, runs music/ADX without underruns, survives 441 roster pairings and maintains strict no-rejection/no-timeout bounds.

**Pass:** full-game marked rate **and** worst segment improve or remain nonregressive with an independently meaningful gain; map, stack and all correctness gates pass. **Fail:** a candidate needs hidden visual reductions, loses a worst segment, exceeds a memory bound or merely improves an isolated pose-cache benchmark.

**Likely cost/risk:** small-to-medium implementation; high value because prior memory constraints changed. The outcome is currently unknown.

## 4. P1 — Replace unconditional Slave cache purges only if ownership permits

**Paths:** `tekken3-saturn/src/saturn/main.c`, `libsaturn/src/core/parallel/executor.cpp`, `libsaturn/src/hal/sh2/cache.cpp`.

The game calls `sat_parallel_purge_cache()` at the start of `prepare_simulation_entry()`. The SDK currently invokes a full-cache purge preserving the active cache mode. It also exposes `sat_parallel_cache_sync_range()` for range invalidation on the calling CPU. Moving from full purge to selective invalidation is **not automatically faster** and is only correct if all cross-CPU writers and readers are accounted for.

### Implementation tasks — SDK and game

- [ ] First record the FRT duration and frequency of the full purge on the real preparation/simulation chain, including the cache miss cost immediately following the purge. Confirm whether it materially affects the end-to-end bottleneck.
- [ ] Build a field-level producer/consumer map: Master-captured pads, snapshots, camera/actor state, mutable skin, worker descriptors, stage inputs, completed actor data, result flags and any SDK task slots. Mark **which CPU writes** each range and **when**.
- [ ] Identify independently owned, 16-byte-aligned spans. Prevent false sharing where one CPU still updates another variable in the same cache line. Preserve required compiler barriers and the uncached publication protocol.
- [ ] Prototype selective **consumer-side invalidation** of data written by the other CPU. Do **not** confuse invalidation with writeback: verify the SH-2 cache policy and the actual SDK semantics.
- [ ] Keep full-purge fallback whenever ownership is ambiguous, data aliases overlap, a task restarts, or a debug verification detects unexpected state.
- [ ] Add targeted LibSaturn host tests for line alignment, overlapping ranges, task publication, restart, reset, aborted work, stale cache lines and mixed cached/uncached address aliases.
- [ ] Compare complete release builds under Mednafen and Ymir with the original binary's inputs and record CPU FRT spans, joins, cache invalidation counts, layout, hashes and image throughput.

**Pass:** exact logical/pixel equivalence, no stale actor/stage data and a reproducible whole-path win. **Fail:** a selective path depends on unverifiable coherence assumptions, increases the join, or helps only an isolated purge microbenchmark.

**Do not do:** disable the cache, remove barriers, or replace all range operations with unconditional cache-through reads.

## 5. P2 — Diagnose rare AI spikes before changing AI algorithms

**Paths:** `src/game/psx_ai.c`, `src/game/psx_ai_game.c`, `scripts/ai_cost.py`, existing trace/verification tools.

The earlier `REAL_HARDWARE.md` work already implemented per-fighter pick caches, branch filtering, constant-read memoization and several costly arithmetic removals. It nevertheless records a rare Mednafen AI-update FRT peak near 84% of one field. A nearby instruction-level profile did **not** reproduce the same magnitude. The first task is to resolve that discrepancy, **not** blindly add a cache.

### Implementation tasks — game

- [ ] Produce a low-overhead per-tick/per-update ring for CPU fighter id, match tick, start/end FRT, move slot, active window, branch count, candidate count, cache hit/miss, imported bank reads and fallback count. Keep only a small number of top-cost events, avoiding a trace that changes the deadline it is measuring.
- [ ] Align FRT samples, native PC/profile windows, AI update count, VBlank number and logical tick to establish whether the apparent spike is CPU instructions, interrupts, waiting, a measurement wrap, or actual pathological traversal.
- [ ] Exercise one-CPU and two-CPU attract configurations separately; the existing documented `-DT3_DEMO_CPU_MASK=2` option provides a one-CPU Mednafen proxy when menu controls cannot reach arcade mode.
- [ ] If the hot path is real, inspect only its measured branch/table access pattern for static, immutable, per-bank prevalidation at load time, tighter bounds or equivalent dense indexing. Avoid duplicating the already-present pick/memo caches.
- [ ] Preserve all original AI decisions, RNG consumption and update ordering. Require unchanged pad hash and the original AI differential suite (including rare branch cases).
- [ ] Re-run worst-tick, p95 and burst-time metrics before and after; verify that a reduction in AI cost translates to fewer late images or fewer discarded fields.

**Pass:** a verified reduction in the **rare worst AI tick** and improved end-to-end worst segment without pad or combat divergence. **Fail:** the peak is a measurement artifact, an optimization only moves cache misses elsewhere, or any AI decision changes.

## 6. P3 — Optimize the live particle / original-projection critical path

**Paths:** `src/saturn/characters.c`, `src/saturn/particle_prepare.c`, `src/game/psx_particle_*.c`, `src/game/psx_scene_ot.c`, `src/game/psx_face_coverage.c`, relevant LibSaturn scene preparation/emission code.

Active hit effects temporarily require the original projection and a mixed painter/particle path. Historical instrumentation identified significant work in `t3_characters_render`, `t3_psx_projected_mesh_build`, `t3_characters_mixed_ot`, `t3_particle_prepare_add` and `t3_stage_particle_multi_face`. This path should be measured **on the same current post-VRAM build**; prior burst results came from other ELFs.

### Implementation tasks — game (SDK only for genuinely reusable APIs)

- [ ] Create deterministic strong-hit traces containing at least one high-damage burst, simultaneous contact, a blocked attack, a miss and a hit/KO transition. Capture particle sprites, actual projected fighter geometry, depth buckets, tile masks, HUD occlusion, LUTs and composited RGB at **matching ticks and fields**.
- [ ] Establish per-effect-image CPU costs and counts: number of sprites, surviving faces, masks/cells, original-projection invocations, keyed sort entries, VDP1 commands, VDP2 tile updates, Master/Slave wait and simulation debt.
- [ ] Check whether the mixed compositor reprojects or reconstructs fighter faces and metadata which the original-projection stage has already produced. If so, prototype one bounded intermediate representation shared by projection, mask tests and painter ordering.
- [ ] Keep exact source order, per-face depth bias, LIFO/equal-depth behavior, double-sided rules, transparent texels, screen clipping and VDP2 additive color calculation. An A/B that merely draws fewer spark cells is a fidelity loss, not a speedup.
- [ ] Test conservative spatial bins or compact visibility masks **only** where a per-cell/per-face count proves the avoided work exceeds their construction cost. Prior generic early face culling/visibility classifiers regressed; this is a different, live-effect-specific test.
- [ ] Ensure the particle path never falls back to unbounded face queues or causes missing fighters. For unsupported geometry use the existing exact fallback.
- [ ] Compare the full burst distribution, not only the whole-fight average: minimum submissions/s across bursts, max inter-submission field gap, p95 image time, dropped fields, particle cells, exact pixels and event counts.

**Pass:** materially better worst burst with identical live effect pixels, no new rejected faces/overflows, and no regression outside bursts. **Fail:** a static snapshot passes but active particles differ, or an approximate particle mask is used to claim an exact optimization.

## 7. P4 — Revisit stage ownership as a measured scheduling decision

**Paths:** `src/saturn/main.c` (`g_stage_claim`, stage worker and Master claim), `src/saturn/stage_vdp2.c`.

Today stage preparation is claimed by whichever CPU reaches it first. The algorithm is already asynchronous and uses a TAS.B claim; **do not** re-propose moving the whole stage to the Slave or caching an unchanged camera as if those were new ideas.

### Implementation tasks — game

- [ ] Capture dispatch time, stage claimant, Slave deferred-tick count, Master's remaining work, stage duration, pending list serial, join duration and actual presentation cadence on current release builds.
- [ ] Test a **conservative** budget-aware policy: if the Slave has three or more ticks, prefer Master **only when** the Master is proved to have enough idle preparation capacity; on two-tick images avoid delaying the Slave's chain. Never let an in-flight decision depend on unsynchronized data.
- [ ] Reuse existing diagnostic `T3_STAGE_FORCE_SLAVE_PROBE` where appropriate as an attributable control; do not enable it as the production answer without a win.
- [ ] Test static policies and a small deterministic decision table before trying an adaptive heuristic. Reject extra atomics, queue overhead or oscillation if they negate the gain.
- [ ] Maintain frame/table serial ownership, VBlank publication, unchanged VDP2 coefficients, correct memory visibility and reset behavior.

**Pass:** better worst segment with unchanged committed ticks, table checksums, pixels and VBlank publication. **Fail:** a visually late table is displayed or image/tick backlog merely moves between processors.

## 8. P5 — Prevalidate immutable pose topology, without changing the math

**Paths:** `src/game/psx_pose.c` (`t3_psx_compose_parts`), KMD/part import and initialization.

The original simulation profile attributes approximately 1.21 ms/tick to the complete part-composition function in one specific Paul/Law setup. That is the entire routine's cost, **not** a predicted saving. Parent order, attachment indexes and valid transforms are repeatedly checked despite stable per-character topology.

### Implementation tasks — game

- [ ] Enumerate every condition and failure mode in `t3_psx_compose_parts`; separate immutable part topology/offset validity from mutable per-tick pose validity.
- [ ] Validate each imported roster character once at load; produce a bounded immutable execution plan with parent index, attachment slot, transform order and supported fast-path flags. Keep full fallback for invalid/unsupported data.
- [ ] In the new runtime path, remove only validation proved redundant for a previously validated immutable plan. Preserve all original Q12/Q15 intermediate rounding, matrix multiplication order, invalid-vector handling and output atomicity on failure.
- [ ] Benchmark a compact plan versus existing code, including additional memory/cache pressure and the cost of any index indirection; avoid another oversized compiled-renderer structure.
- [ ] Run per-pose bit-exact comparisons across all available recorded/representative animations, all roster topology classes, blend transitions, IK edge cases, KO/replay and both actors.
- [ ] Promote only if measured complete-tick and full-fight results improve across different character pairs.

**Pass:** bit-exact state, smaller sustained per-tick work, and no overall rendering regression. **Fail:** an operation is merely moved to initialization but creates a slower per-tick memory access, or changes the original rounding/invalid-state semantics.

## 9. P6 — Reduce the panorama's key-line fitting cost

**Paths:** `src/game/stage_backdrop.c`, `src/saturn/stage_vdp2.c`, `libsaturn/src/graphics/vdp2/floor.cpp`.

The final line-scroll encoding was already reduced dramatically by writing directly into the output table; avoid repeating it. The remaining panorama uses five rays at each 32-line fitting key, cylinder/wall intersection, division, trigonometric approximation and interpolation.

### Implementation tasks

- [ ] Profile only fitting-key computation (camera normalization, `fit`, `hit`, `picture`) after the latest SDK floor optimization; do not treat the already optimized table encoder as remaining work.
- [ ] Split terms dependent solely on static stage parameters, camera orientation and actor midpoint. Cache or precompute only mathematically valid independent terms, with explicit invalidation on all changed inputs.
- [ ] Investigate exact integer common subexpressions and reduced redundant division; compare all line-scroll words and floor depth values against the current implementation at varied camera yaw, pitch, zoom, boundaries, vertical offsets, near/far clipping and stage wraps.
- [ ] Retain the existing 32-line quality and rounded output. Do **not** silently expand the key interval or simplify texture mapping to improve the rate.
- [ ] Compare the new stage build in full matches, including the frequency with which Master or Slave claims the job.

**Pass:** exact VDP2 table contents and measurable whole-path gain. **Fail:** only an isolated line-fit microbenchmark improves, or any rendered panorama differs.

## 10. Lower-priority / conditional research

The following must **not** interrupt P0–P3 without new measurements:

- **VDP1 smaller clear area:** old no-clear Mednafen controls barely improved the game and visibly smeared pixels; reconsider only if real hardware `END MS` and drawing counters prove the VDP1 is the limiting resource. Always clear every formerly visible pixel.
- **Direct SCU-DMA for command lists:** historically slower. The new LibSaturn banks write commands directly into VDP1 VRAM; avoid adding back a staging copy or duplicate command buffer.
- **Double-buffered VDP1 command lists:** already implemented as direct VRAM banks. Do not resurrect the old proposed buffer as a new optimization.
- **Moving actor-1 face work to Slave / Master skinning both actors:** measured regressions or workload-specific tradeoffs; only revisit with a demonstrably changed scheduling balance and an explicit full-game A/B.
- **Generic early backface visibility / rigid-face classifier:** previous implementations lost 3.94% and 11.05%, respectively; do not retry without a materially cheaper proof/classification strategy.
- **Another full compiled renderer, reciprocal projection LUT or MAC skinning:** previous variants were measured and rejected. A new proposal must first explain how it removes the historical cost (extra commands, cache pollution, correction work or increased memory).
- **SH-2 cache split into 2 KiB cache + 2 KiB RAM:** possible but high risk; halving the cache might cost more than the on-chip scratch saves. Only microbenchmark with real memory traffic and hardware checks after major work is complete.
- **Increase the catch-up cap or defer more simulation:** this can preserve game speed by reducing dropped time, but can **reduce displayed image rate**. Evaluate separately as a correctness/smoothness policy, never count as an image-throughput optimization.

## 11. Common experimental protocol

### 11.1 Before the first change

- [ ] Snapshot both repository SHAs and ensure the application links the intended **installed** LibSaturn, not a stale local archive.
- [ ] Save release CMake cache, `variant.json`, ELF map, binary/SDK/BIOS hashes, actual input/source-data identities, CPU cache mode and startup diagnostics.
- [ ] Reproduce the post-stage 26.41/24.54 marked result with the same 280-second Kuma/Jin attract scenario. If it does not reproduce, stop and explain which build or input differs rather than claiming a new gain.
- [ ] Verify exact marked-segment boundaries and maintain the first-2,000-AI-update pad hash (historically `0xb99cc280` for this study), as well as full-tick reference equivalence.
- [ ] Check whether any option is forced by CMake or inherited from an old cache; generate each candidate in a **fresh** directory.

### 11.2 Reproducible commands

Use the project's existing harness; substitute **real, previously qualified local build directories** rather than invented asset paths:

```sh
# Create a clean variant from a known configured release build.
python3 scripts/build_saturn_variant.py \
  --baseline build/roster \
  --out build/perf-experiment-01 \
  --workload diagnostic \
  --cmake-option=-DT3_LOCAL_POSE_CACHE_ENTRIES=64

# Run the exact full-roster attract CD benchmark (Mednafen).
python3 scripts/mednafen_fight.py \
  --build build/perf-experiment-01 \
  --out assets/imported/perf-experiment-01-mednafen \
  --seconds 280

# Independently check Ymir with SH-2 cache emulation enabled by the harness.
python3 scripts/ymir_fight.py \
  --build build/perf-experiment-01 \
  --out assets/imported/perf-experiment-01-ymir \
  --fields 6000
```

**Important:** the first sample override is **illustrative only**. Currently the umbrella `T3_PARTICLES` CMake block can forcibly overwrite `T3_LOCAL_POSE_CACHE_ENTRIES`, so inspect the resulting `CMakeCache.txt` and actual compile definitions before running or interpreting it. `build_saturn_variant.py` validates its selected workload; use the release-specific validation profile where applicable rather than mislabeling a roster build as a two-Paul benchmark. Do not publish ROM-derived source data, disc images or private captures.

### 11.3 Benchmark matrix

| Workload | Primary purpose | Required observations |
| --- | --- | --- |
| Kuma/Jin, 280 s, neutral, two CPU | Primary full-roster gate | Marked mean, each marked segment, worst, images, AI pad hash, ticks/fields |
| One CPU fighter, neutral P1 (`T3_DEMO_CPU_MASK=2`) | Player-like scheduling | Marked cadence, worst, AI ticks, render joins |
| Paul/Law first-round comparison | Regression control for prior milestone | 60 logic ticks/s, image submissions, same-tick visuals |
| Strong-hit scripted fixture | Effect worst-case | Burst-specific cadence, particle count, max field gap, pixel parity |
| KO/replay, timeout, reset | Logical/presentation correctness | Full state traces, scene serial, audio, field/tick counters |
| 441 fighter pairs | Memory, topology, VDP1 edge coverage | All pass, no rejected faces, no draw timeout, safe stacks/maps |
| ADX on/off, CD-DA on/off | Audio contention and real-device behavior | No underruns; track `ADX UND`, `ADX MIN`, `ADX STEP`, `END MS` |
| Actual Saturn (Satiator/SAROO where available) | Hardware gate | `LOOP x10`, `TICK`, `DROP`, `IRQ`, `SLV`, `MCCR/SCCR`, swap timing |

Be explicit when a harness has not verified a field, a run did not reach the requested scene, or sound was disabled.

### 11.4 Evidence and promotion file

For every candidate, retain a small **non-ROM** Markdown/JSON report containing:

- ticket / work package, hypothesis and exact changed files;
- application and SDK SHAs; toolchain, emulator and BIOS versions;
- complete config diff and map: free WRAM-H/WRAM-L, stack headroom, VDP1 slots, VRAM and Sound RAM;
- exact control/candidate ELF SHA-256, input hashes and match seed;
- Mednafen repeat runs and segment-by-segment numbers, Ymir cache-on counters and physical-hardware results if obtained;
- whether simulation tick debts, discarded fields, swaps, timeouts, audio underruns or particles changed;
- original/host/Saturn logical equivalence and same-tick rendered-pixel/command comparison;
- decision **PROMOTE**, **REJECT**, or **INCONCLUSIVE**, with a concrete reason.

Record every failed path, but leave only promoted changes in production defaults. Keep experiments isolated from the SDK API unless a reusable capability is genuinely required; prefer a minimal LibSaturn API with ownership tests over game-specific SDK branches.

## 12. Dependency order and completion definition

```text
P0 Reproduce baseline and map memory
    |
    +--> P0a. Restore/redistribute hot cache + geometry -------------------+
    |                                                                     |
    +--> P1. Measure purges -> prove ownership -> selective invalidation --+
    |                                                                     |
    +--> P2. Isolate rare AI spikes -> conditional exact optimization -----+--> Re-run full release matrix
    |                                                                     |    and keep only verified wins
    +--> P3. Instrument strong hits -> specialize exact effect pipeline --+
    |
    +--> P4. Budget-aware stage scheduling (after new workload profile)
    |
    +--> P5. Immutable pose topology specialization (independent A/B)
    |
    +--> P6. Panorama key fitting (after P4/SDK floor profiling)
```

A milestone is **not complete** when an isolated microbenchmark improves or the `FRAME` HUD reaches 30 in a favorable segment. Completion requires all of the following on a reproducible release configuration:

- [ ] Original 60 Hz simulation with no concealed game-time loss, verified original combat behavior and AI/pad invariants.
- [ ] At least 30 newly **presented distinct** images/s in sustained full-roster measurements and a worst marked segment of at least 30, with capture methodology stated.
- [ ] No worse strong-hit burst behavior, no missing particles or original rendering differences.
- [ ] Safe stock hardware memory/stack, VDP1 frame-bank protocol and complete 441-pair regression.
- [ ] Functional HUD, fighter loading, CD/SFX/ADX, KO/replay, reset, and real-Saturn verification where available.
- [ ] Measured evidence, rollback path and concise update to `THIRTY_IMAGE_GATE.md` and `SATURN_PERFORMANCE_PLAN.md`.

**Execution decision:** Start with **P0 (exact configuration + memory matrix)**, not another renderer rewrite. If P0 cannot reproduce a legitimate candidate, begin **P2 (AI peak attribution)** in parallel with **P3 (strong-hit instrumentation)**. Implement P1 only if its purge measurement shows meaningful recoverable latency. No optimization in this document is represented as already tested or accepted.
