# Post-VRAM Execution Tracker

Operational breakdown of [POST_VRAM_FRAME_RATE_PLAN.md](POST_VRAM_FRAME_RATE_PLAN.md).
The plan is the authority for the rules and acceptance gates; this file records
the order of work, the exact commands, and what each step must prove. Update it
after every experiment; a result is not a win until the listed gates pass.

**Started:** 2026-11-06
**App commit at start:** `ea0f248` (the plan commit; application code at `74e8a8f`)
**SDK commit at start:** `b52ec400` (`/Users/celso/dev/lib-saturn`, prefix
`/Users/celso/dev/saturn-prefix`, `libsaturn.a` SHA-256 `6926bea3…`)
**Reference build:** `build/` (Release, full roster, ELF `d9879130…`)
**Target:** 60 Hz simulation and at least 30 newly presented, distinct images/s
in full-roster fights including the worst sustained segment.

## 1. Baseline snapshot (P0 evidence)

Taken from `build/CMakeCache.txt` and `build/tekken3_saturn.elf.map` on
2026-11-06. These are the values every candidate is compared against.

| Item | Value |
| --- | --- |
| `T3_PARTICLES` | ON |
| Forced by `T3_PARTICLES` | `T3_LOCAL_POSE_CACHE_ENTRIES=8`, `T3_HIGH_GEOMETRY_SCRATCH=OFF`, `T3_COLD_FACES=320` |
| `T3_MASTER_SKIN_PERCENT` | 65 |
| `T3_DEMO_CPU_MASK` | 3 (two CPU fighters) |
| `T3_STAGE_CDDA` / ADX | ON / off |
| `__bss_end` | `0x060e64e0` |
| Free WRAM-H before the stack reserve | 105,248 B (`0x19b20`) |
| Master stack reserve | 16,384 B (`__stack_reserve = 0x4000`) |
| Usable WRAM-H for new data | **88,864 B** |
| Low WRAM end / linker limit | `0x002ffd60` / `0x002ffe00` (160 B free, plus the 512 B `sat_exec` reserve) |
| Slave simulation stack | 6,656 B (`simulation_stack.S`) |

The forced values are why
`--cmake-option=-DT3_LOCAL_POSE_CACHE_ENTRIES=64` in the plan's example does
nothing today: the `T3_PARTICLES` block overwrites it with `FORCE`. W0.2 fixes
that before any variant is built.

### Promoted base (P0 result, 2026-11-06)

`T3_LOCAL_POSE_CACHE_ENTRIES=128` is the new base after all W0.7 gates
passed: primary Kuma/Jin 280 s marked 26.41 → **26.72**, worst 24.50 →
**25.04**; second encounter (pair 7,4) 25.71 → 26.25 and 20.17 → 20.87; Ymir
cache-on and both frozen ticks identical. Its map keeps
`__bss_end 0x060f3330` (36,048 B above the stack reserve) and
`__wram_l_end 0x002ffd60`. Every later package (W1–W6) A/Bs against this base,
not against cache 8. The historical cache-8 numbers above remain the control
of record.

### Program status (2026-11-06, end of this session)

| Package | Outcome |
| --- | --- |
| W0 config/memory | **DONE**: cache-128 promoted; all W0.7 gates pass (primary +0.31/+0.54, second encounter +0.54/+0.70, Ymir equal hash, frozen pixels 2000/3000, 24 combat cases, 441-pair arena) |
| W1 purge | **CLOSED**: 2,502 purges = 3,264 FRT total (0.876 fields); no selective invalidation |
| W2 AI spikes | **MEASURED, NO CHANGE**: 39/8,684 updates over half a field, 0 over a field; spikes scan ~250 words vs a 4.5 mean; the trace hook disables the pick cache, so attribution needs a new trace; next step recorded |
| W3 strong hits | **PROMOTED, GATES PASS (2026-11-07)**: fixtures `chain` and `two-chains`; entry-list compaction (OT sort 43.24 → 3.67 ms) and cell-map lookup (worst image 32.62 → 32.17 ms) promoted; outcodes measured; face-mask hoist rejected; whole fight 26.84 → 26.89 marked, combat suite 24/24, frozen 2000/3000 equal, host tests pass (8 skip). **Defaults fixed (2026-11-07):** compaction and outcodes are now the CMake default with the ordered mask (compaction had been called promoted but was still OFF); whole fight 26.89 → 26.92 |
| W4 stage ownership | **CLOSED**: two policies rejected (mid −0.07/−0.18, aggressive −1.89/−1.48); keep the first-come race |
| W5 pose topology | **PROMOTED**: bit-exact digest; primary +0.13/+0.16 (6/7 spans), pair 7,4 +0.11/+0.23; default is ON |
| W6 panorama | **CLOSED**: profile captured (fit ≈70% of a 2.28 ms panorama); no exact rewrite with a measurable gain; keep the code |
| Base rate | 26.89 marked / 25.26 worst segment (2026-11-07 head; cache 8: 26.41/24.50) — the 30 img/s target is still open |

### Baseline reproduction (gate for everything else)

The plan's command is incomplete: the checked-in `build/` boots the menu's
demo rotation, which does not contain Kuma/Jin (the rotation is
`{1,8},{0,3},{7,4},{14,15},{5,2},{9,6}`). A 280 s run of it gives 24.09/21.13
and pad hash `0x640e32c0`, i.e. a different fight. The 2026-11-05 build used
the measurement option added in `cb4efc5`:

```sh
cmake -S . -B build/perf-kuma-jin -DT3_DEMO_PAIR='11,9'
cmake --build build/perf-kuma-jin --parallel 4
python3 scripts/mednafen_fight.py --build build/perf-kuma-jin \
  --out assets/imported/perf-kuma-jin-demo-280 --seconds 280
```

Reproduced on 2026-11-06:

| Metric | 2026-11-05 | 2026-11-06 |
| --- | ---: | ---: |
| Marked images/s | 26.41 | **26.41** |
| Worst segment | 24.54 | 24.50 |
| Images | 2,491 | 2,470 |
| AI pad hash | `0xb99cc280` | **`0xb99cc280`** |

Segments 24.90, 25.00, 28.05, 26.96, 24.50, 25.24, 30.25. The two 0.04
differences are within the documented run noise; the marked rate and pad hash
match exactly. `build/perf-kuma-jin` is the P0 **control** build. Keep
`-DT3_DEMO_PAIR='11,9'` in every P0 command; without it the run measures a
different attract fight.

If a future run does not reproduce this table, stop and identify the differing
build/input before running any candidate.

## 2. Work packages

### W0 — Configuration, memory layout, and the first candidate (P0)

| # | Task | Proof |
| --- | --- | --- |
| W0.1 | Baseline reproduction (command above); keep `fight.json`, delete `run.mov` | Numbers and pad hash match |
| W0.2 | CMake: stop `T3_PARTICLES` from `FORCE`-overwriting `T3_LOCAL_POSE_CACHE_ENTRIES`, `T3_HIGH_GEOMETRY_SCRATCH`, `T3_COLD_FACES` when the user set them; keep 8/OFF/320 as the fresh-build default | Fresh configure still 8/OFF/320; explicit `-D` survives in `CMakeCache.txt` |
| W0.3 | Build the layout matrix in fresh directories, check the map before booting | `__bss_end + 0x4000 <= 0x6100000`; Low WRAM under limit; note free bytes |
| W0.4 | Benchmark each candidate over the same 280 s attract fight | Marked rate, every segment, worst, images, pad hash |
| W0.5 | Cache accounting: hits/misses/evictions per workload (neutral, fast movement, KO, effect burst) | Work and cadence, not hit rate alone |
| W0.6 | Promote only a candidate that improves or holds the worst segment and survives the full gate | W0.7 |
| W0.7 | Winner gates: Ymir cache-on, 441 pairings, combat suite, frozen pixels at ticks 2000/3000, ADX/CD-DA, second encounter | All pass, no rejected faces/timeouts, pixels identical |

Layout matrix (one independent change per build; `build/` stays the control):

| Build | Pose cache | Purpose |
| --- | ---: | --- |
| control | 8 | 26.41/24.54 baseline |
| B | 64 | Doubles the cache, uses 28,160 B, leaves 60,704 B above the stack reserve |
| D | 128 | Largest validated option (56,320 B), leaves 32,544 B above the stack reserve |

**Correction to the plan's matrix:** `T3_HIGH_GEOMETRY_SCRATCH` cannot change
this build. `src/saturn/characters.c` keeps `g_actor_geometry` in High WRAM
whenever `T3_FIGHTER_PACKS` is defined, and the full-roster release defines it,
so the option's Low-WRAM branch is unreachable here. `T3_COLD_FACES` cannot
grow either: the Low-WRAM table is 160 B from its limit and only allows
320…1562, so every increase fails the link. P0 therefore varies the local pose
cache only; the plan's A/C/E rows are the same build as control/B/D. Record
this in the P0 report instead of spending runs on them.

Build recipe (a fresh `build_saturn_variant.py` directory regenerates every
private stage, about 13 minutes; a clone of the configured control reuses the
baked assets and is much cheaper):

```sh
cp -cR build/perf-p0-cache64 build/perf-p0-cache128
cmake -S . -B build/perf-p0-cache128 -DT3_LOCAL_POSE_CACHE_ENTRIES=128
cmake --build build/perf-p0-cache128 --parallel 4

python3 scripts/mednafen_fight.py --build build/perf-p0-cache128 \
  --out assets/imported/perf-p0-cache128-280 --seconds 280

sh2eb-elf-nm build/perf-p0-cache128/tekken3_saturn.elf | grep -E '__bss_end|__wram_l_end'
python3 scripts/wram_usage.py build/perf-p0-cache128/tekken3_saturn.elf.map --top 15
```

The first `cmake` on a cloned directory can fail once with "current
CMakeCache.txt directory ... is different"; run it again and it fixes the
cache path. A rebase or checkout updates script mtimes, so a fresh build may
regenerate the baked stages even when their inputs did not change; budget for
that once per build directory.

Map checks on the cache-64 build:

| Item | 8 entries (control) | 64 entries |
| --- | ---: | ---: |
| `__bss_end` | `0x060e64e0` | `0x060ec520` |
| Free above stack reserve | 88,864 B | 64,224 B |
| Low WRAM end | `0x002ffd60` | `0x002ffd60` |

Prior evidence for the same option lives in
[LOCAL_POSE_REUSE_PERFORMANCE.md](LOCAL_POSE_REUSE_PERFORMANCE.md) (2026-10 or
earlier tree, `T3_PARTICLES` off, 128 entries the private default). There the
uninstrumented neutral cadence moved only from 11.129 to 11.217 submissions/s
(+0.8%) from 8 to 128 entries, while instrumented approach/attack tick roots
improved 3–18%. Its High BSS end (`0x060d94e8`) and Low WRAM end
(`0x002a2b54`) belong to that older layout, not this baseline, so P0 still has
to be measured on the current ELF. The prior result predicts a small neutral
attract gain and a larger one under active inputs; if cache-64/128 land inside
±0.04 img/s here, P0 is INCONCLUSIVE rather than a win, and the program moves
to W2/W3.

Cache accounting: read `_g_local_pose_cache` with
`scripts/mednafen_state.py --symbol _g_local_pose_cache --words`
(`hits` = word 5, `misses` = word 6, `evictions` = word 7, zero-based after
the address/size header the script prints).

Acceptance: mean **and** worst segment nonregressive, gain beyond the ~0.04
img/s noise and meaningful for the program; identical frozen pixels; pad hash
unchanged. Reject and delete a variant the moment it loses the worst segment.

### W0.7 gates for the cache-128 candidate
| Gate | Command | Result |
| --- | --- | --- |
| 441-pair arena | `python3 scripts/report_fight_arena.py --iso-root build/perf-p0-cache128/disc/iso_root` | 21 packs, arena 734,672 B for the worst pairing; **0 of 441** leave less than 0 B. The pose cache lives in High WRAM, not the arena. |
| High WRAM | map | `__bss_end 0x060f3330`; 36,048 B free above the 16,384 B stack reserve |
| Low WRAM | map | `__wram_l_end 0x002ffd60` (same as control); 160 B to the linker limit |
| Ymir cache-on | `scripts/ai_cost.py --scenario arcade --frames 6000` on control and candidate | control: pad hash `0xb99cc280`, 1,769 frames / 3,162 fields, max AI 0.71 field; candidate: same hash, 1,778 / 3,160, max AI 0.708, `first_error 0`, 1 discarded field each || Frozen pixels 2000 | `T3_RENDER_BENCH_TICK` capture pair | **PASS**: `sim_ticks 2000`, 104 stable fields + model pixels equal (`4b1e8b36…`); shots frozen |
| Frozen pixels 3000 | `T3_RENDER_BENCH_TICK` capture pair | **PASS**: `sim_ticks 3000`, 104 stable fields + model pixels equal (`838c0a15…`); shots frozen |
| Combat suite | `saturn_combat_suite.py --with-ko` on a `T3_BOOT_FIGHTERS='0;1'` variant of the candidate | **PASS**: 24 live cases, `native_round_replay_victory_validated` true; timeout cases not run (`--with-timeout` skipped) |
| Second encounter | `T3_DEMO_PAIR='7,4'`, 240 s, control vs candidate | **PASS**: +0.70/+0.26/0.00/+1.20 per shared span, marked 25.71 → 26.25, worst 20.17 → 20.87, both hash `0x335d1300` |
| Combat suite | `python3 scripts/saturn_combat_suite.py --build <b> --workload diagnostic --bios <bios> --out <out> --with-ko` | pending |

Commands written down so the gates are reproducible: the combat gate is the
`combat_stage` of `scripts/saturn_regression.py` without `--with-timeout` (the
two 12,000-field timeout cases cost hours on Ymir); the second encounter
rebuilds both directories with `-DT3_DEMO_PAIR='7,4'` (the rotation's third
pair) and reruns the 280 s Mednafen scenario.

**The combat suite needs a boot-to-fight build on this tree.** Its
`first_gameplay_field` searches only 1,200 probe fields for
`sim_ticks > 0 && round_state == 1`; the full-roster release spends that window
in the opening movie and title (both programs publish no diagnostics), so the
first two attempts failed with `controllable fight never started within 1200
fields` even with `--title-start-field 400`. The same case scripts were
qualified on `paul-law-release`/`two-paul-release` builds, which the suite's
assertions (Paul's throw 2848, Law's reactions 2250/2328/2352) assume. The gate
therefore runs on a variant of the candidate built with
`-DT3_BOOT_FIGHTERS='0;1' -DT3_LOCAL_POSE_CACHE_ENTRIES=128` in
`build/perf-p0-cache64`, which starts Paul vs Law directly; the combat logic
under test is unchanged by the pose-cache size.

**ELF hashes move when source lines move.** After the W1/W2/W4 probes were
added (all `#if`-off in release), rebuilding the candidate changed 91 bytes of
`.text`: every `__LINE__` constant in `main.c`/`move_probe.c` shifted (for
example `mov.w … ! 707` → `! 73a` in `_main`). Symbol tables are byte-equal, so
the code is identical apart from those constants; the A/B numbers were measured
on the pre-edit ELF (`4bd0f025…`), the gates run the post-edit one
(`877970b4…`).

**Frozen captures go through the Ymir arcade entry, not the Mednafen CD.**
A `-DT3_RENDER_BENCH_TICK=2000` build only clamps the *frontend* program, and a
cold Mednafen CD run spends its first ~180 s in the opening movie and menu
(the screenshots at 100/130/145 s are the movie and a transition), so the
harness's save-state read fails with "Work RAM does not hold this ELF". The
Ymir probe with the `scripts/ai_cost.py` arcade script reaches the frontend
fight deterministically, freezes at the bench tick, and writes
`observation.json` in the format `compare_saturn_presentation.py` checks
(state fields plus exact model pixels). Command:

```sh
python3 scripts/saturn_probe.py --build <b> --disc <b>/disc/music.cue \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/<b>-frozen-<tick>-ymir --frames 5000 --timeout 900 \
  --device-script <tofight.devices> --sh2-cache on --port2 none \
  --screenshot-frame 4900
```

Tick 3000 needs `--frames 6200 --screenshot-frame 6100`: 5,000 frames end at
`sim_ticks` ≈2,163, before the freeze, and the comparison then fails.

Harness fix found while running the Ymir gate: `sat_panic_info_t` grew from 44
to 80 bytes (it now carries the last `SAT_REFUSE` origin), so
`scripts/saturn_probe.py:decode_panic` rejected every non-empty dump as
"truncated" even when `frames_run` was complete. It now accepts ≥44 bytes and
parses the unchanged prefix; a real panic still identifies by magic/code/line.
`decode()` also rejected diagnostics version 25/26 although it already parsed
their fields; the accepted range is now 12…26.

**Ymir injection does not reach the frontend's attract demo on this tree.**
`--watch-write` on the frontend-only symbols shows the injected
`tekken3_saturn.elf` writes `_g_t3_diagnostics.magic` at frame 96, then the CD
loader copies `MENU.BIN` over the same Low WRAM and the menu program
(`_main 0x0600519c`, first Low WRAM symbol `_g_text_blob` at `0x00200000`)
runs from frame 130 on. `_g_t3_ai_pad_hash` and `_g_tick_clock` receive no
writes during 3,000 frames. The Mednafen attract numbers are therefore not
reproducible on Ymir through plain injection; the Ymir gate uses the
input-driven arcade path of `scripts/ai_cost.py` (the same entry the combat
suite uses), which executes the frontend fight program and publishes
diagnostics.

### W1 — Cache purge and selective invalidation (P1)

**CLOSED 2026-11-06 — no recoverable latency.** The probe build measured
`sat_parallel_purge_cache()` across 2,502 calls (one per prepare entry over the
frontend's ~100 s of the 280 s run): 3,264 FRT total (**0.876 fields ≈ 15 ms**),
worst call 3 FRT, about 1.3 FRT (~6 µs) per call. The full purge is already a
no-op when CCR bit 0 is clear and a hardware purge otherwise; at <0.03% of the
tick budget no selective-invalidation path can pay for its own complexity, so
W1.2–W1.5 are not started. Evidence:
`assets/imported/perf-probe-w1w2w4-280/fight.json` (`_g_t3_slave_frt[5..7]`).

1. W1.1 Measure `sat_parallel_purge_cache()` and the misses immediately after
   it on the current chain (`prepare_simulation_entry`, `src/saturn/main.c`).
2. W1.2 Producer/consumer map of every cross-CPU range (pads, snapshots,
   camera/actor state, mutable skin, worker descriptors, stage inputs, result
   flags, task slots), with writer CPU and timing.
3. W1.3 Prototype consumer-side invalidation of independently owned,
   16-byte-aligned spans; keep the full purge as fallback.
4. W1.4 LibSaturn host tests: alignment, overlap, publication, restart, abort,
   stale lines, cached/uncached aliases.
5. W1.5 Full A/B on Mednafen and Ymir.

Files: `src/saturn/main.c`, `libsaturn/src/core/parallel/executor.cpp`,
`libsaturn/src/hal/sh2/cache.cpp`. Never disable the cache or remove barriers.

**W1.1 instrumentation (applied to a probe build only).** `g_t3_slave_frt`
currently has five slots (0 ticks, 1 actor preparation, 2 stage tables, 3/4
stage builds by Slave/Master). Grow it to eight with `[5]` purge FRT total,
`[6]` purge calls, `[7]` worst purge FRT, and time the call in
`prepare_simulation_entry`:

```c
#if defined(T3_SLAVE_FRT_PROBE)
    SLAVE_FRT_START(purge_started);
#endif
    sat_parallel_purge_cache();
#if defined(T3_SLAVE_FRT_PROBE)
    SLAVE_FRT_ADD(5,purge_started);
    SLAVE_FRT_COUNT(6);
    { const uint32_t d=(uint16_t)(sat_time_frc()-purge_started);
      if(d>g_t3_slave_frt[7])g_t3_slave_frt[7]=d; }
#endif
```

Also read `g_t3_boot_cache` from the same save state: `sat_parallel_purge_cache()`
calls `purge_keeping_mode()`, which returns immediately when CCR bit 0 is clear,
so if the Slave boots with the cache off the whole purge is already a no-op and
W1 ends without a code change. Field budget: an FRT tick is 1/28.636 MHz; one
field is 3,728 ticks. A purge total below ~0.5% of field time is not
recoverable latency. Note the probe cannot be built while another variant is
compiling, and its numbers must not be compared with release timings.


### W2 — Rare AI spikes (P2)

**W2.1 measured 2026-11-06.** 8,684 AiUpdates in the 280 s probe run: 39 over
half a field (0.45%), **0 over a full field**, worst 3,172 FRT = 0.85 field.
The eight worst updates (tick, fighter, slot, reads, branch conditions) are
spread over ticks 894–5,316 and both fighters:

| FRT | tick | fighter | slot | window | reads | conditions | misses |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3,172 | 1739 | 1 | 1 | 7 | 303 | 1 | 0 |
| 2,890 | 3432 | 1 | 1 | 7 | 258 | 5 | 0 |
| 2,865 | 1782 | 0 | 0 | 7 | 293 | 4 | 0 |
| 2,838 | 1153 | 0 | 0 | 7 | 271 | 2 | 0 |
| 2,776 | 5316 | 0 | 0 | 7 | 261 | 2 | 0 |
| 2,742 | 1890 | 1 | 1 | 7 | 266 | 2 | 0 |
| 2,727 | 894 | 1 | 1 | 7 | 218 | 2 | 0 |

**W2 outcome 2026-11-06: target not named, nothing ships.** The existing
per-read `trace` hook cannot attribute a normal-path spike: `psx_ai_game.c`
builds its bus with `ai->trace ? 0 : ai->pick[...]`, i.e. enabling the trace
**disables the pick cache**, so the traced build reads ≥1,024 words per update
and the run collapses (18.64 marked vs the 26.72 base, worst AI 6,869 FRT vs
3,172). The un-traced ring numbers stand: a spike scans ~250 bus words against
a 4.5 mean, so it is a rare fallback scan, but which table needs either a
non-cache-disabling trace or an instruction-level profile over the exact worst
tick. Recorded as the next step for W2; no runtime change was made.

Every spike reads 218–303 bus words, while the run's mean is **4.5 reads per
update** (`_g_t3_ai_reads / _g_t3_ai_calls` = 39,060 / 8,690), with zero
out-of-field misses and the normal seven windows. So the rare update really
does ~50× the memory work — a full table/branch scan when the pick/memo path
is left — and the extra ~11 ms is not explained by the 250 reads themselves
(~0.2 ms at bus speed). The first fix attempt must therefore instrument the
scan (the existing per-read `trace` hook, a bounded ring over the worst
update); until that names the table, W2.3 has no proven target. The probe is
off in release, so nothing ships from W2 yet.

1. W2.1 Low-overhead ring per AI update: fighter, tick, FRT start/end, move
   slot, window, branches, candidates, cache hits/misses, bank reads, fallback.
2. W2.2 Reconcile the reported ~84%-of-a-field peak with instruction profiles;
   decide whether it is instructions, interrupts, wait, or a wrap.
3. W2.3 If real, specialize only the measured branch/table access; no new
   pick cache (one already exists).
4. W2.4 Verify pad hash `0xb99cc280` on the first 2,000 updates and the AI
   differential suite; show the worst tick and worst segment move.

Harness: `scripts/ai_cost.py` (attract and arcade), `T3_DEMO_CPU_MASK=2`.

**W2.1 design.** The existing `ai_clock()` in `src/saturn/move_probe.c` already
brackets every `AiUpdate` with FRT and keeps `calls`, total ticks, max and the
first-2,000 pad hash. A ring that only keeps the last N updates would show the
end of the fight, not the peak, and 280 s of attract is ~8,700 updates, so keep
a **top-8 table plus a slow histogram** behind `T3_AI_RING` (off by default):

- per update, after `spent` is known: `tick = ai->match->ticks`, `fighter`,
  `spent`, `ai->slot[fighter]`, `ai->window_count`, `ai->pick_index[fighter]`,
  and deltas of `reads`, `conditions`, `misses` against the previous update of
  the same fighter;
- insert into eight slots only when `spent` exceeds the current minimum
  (eight comparisons per update, no allocation, no ordering assumptions);
- count updates over 1,864 FRT (half a field) and over 3,728 (one field).

The save state then shows the worst eight updates with their tick, fighter,
slot, window and bus counters, which is exactly the "instructions vs wait vs
wrap" evidence W2.2 needs. The ring changes no decision: the pad hash must stay
`0xb99cc280` and the AI differential suite must pass unchanged.


### W3 — Strong-hit / particle critical path (P3)

1. W3.1 Build a deterministic strong-hit fixture: high damage, simultaneous
   contact, block, miss, hit→KO; capture matching ticks/fields.
2. W3.2 Per-effect-image costs and counts: sprites, surviving faces, masks,
   original-projection calls, sort keys, VDP1 commands, VDP2 tile updates,
   Master/Slave waits, simulation debt.
3. W3.3 Check for duplicate reprojection between the mixed compositor and the
   original-projection stage; prototype one shared bounded representation.
4. W3.4 Compare the full burst distribution: minimum rate, max field gap, p95
   image time, dropped fields, particle cells, exact pixels.

**W3.1 status 2026-11-07: fixture not yet valid.** `build/perf-w3-boot`
(`T3_BOOT_FIGHTERS='11;9'`, `T3_EFFECT_RING=ON`, pose cache 128) runs the
`high-hit` and `simultaneous` scenarios of `scripts/saturn_effect_burst.py`
cleanly (first gameplay field 890, real hits landed: P2 9175040→7995392 and
P1 9175040→8650752). Neither run recorded an effect image: the effect ring
count, `_g_t3_effect_ring_images` and `_g_t3_mixed_ot_particles` are all 0,
so no hit particle reached the mixed path. The arcade scenario (earlier
`perf-w3-liveentries`, now removed as superseded) never reached a fight.

**W3.1 PASS (2026-11-07, `chain` with P2 holding `LEFT`):** the `chain` scenario
lands six hits (P2 140 → 44 HP, 96 damage in total) and records **23 effect
images** (`live-entries check: 21 images, 0 mismatches`). Output:
`assets/imported/perf-w3-burst-chain2`. Neutral P2 blocks the strikes (the
earlier `chain` run had 0 hits and 6 blocks), so the defender must hold a
non-guard direction for them to connect.

**W3.1 correction: the `arcade` scenario does reach a fight.** The earlier note
that it "never reached a fight" is wrong; it is the only fixture that produces
the *sustained* effect images this package needs. On
`build/perf-w3-liveentries` (LIVE_ENTRIES ON) with the arcade script
(`400/700/2100/2160/2400` START presses) the run reaches the fight at field
2,400 and records **55 effect images over 40 mixed-OT frames**, 426 particles,
peak 30 live, 3 dropped, with hits `[5, 3]`, blocks `[1, 0]`, health
`[4849664, 6946816]` and `first_error 0`. The `chain` fixture's 23 images are
the single-burst case; the arcade run holds two bursts (ticks 1,867–1,942 and
2,773–2,817) and is the fixture the W3.4 distribution is measured on. Evidence:
`assets/imported/perf-w3-liveentries-control` and
`assets/imported/perf-w3-liveentries-candidate`.

**W3.2 costs, corrected (2026-11-07):** the first numbers below came from the
`T3_MIXED_OT_LIVE_ENTRIES_CHECK=ON` build, which runs the coverage loop twice
per image (a reference pass plus the compacted pass). Both builds on the same
`chain` input (`build/perf-w3-boot`, check ON; `build/perf-w3-nocheck`, check
OFF):

| Build | Kept images | Mixed OT total | Coverage | Compose | Mixed per image | Worst image (mixed+prep+flush) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| check ON | 23 | 580 ms | 457 ms | 107 ms | 25.2 ms | 57.2 ms |
| check OFF | 31 | 351 ms | 190 ms | 150 ms | 11.3 ms | 30.3 ms |

The check build is about 2.2× the production cost, so the production path is
the check-OFF row. The image counts differ because which 32 images the ring keeps
depends on timing; per-image figures are the fair comparison. Coverage is the
larger share in the totals (190 of 351 ms), but compose is close (150 ms), and
on the worst row compose (3,004 FRT) is about twice coverage (1,558 FRT).
**Phase split measured (2026-11-07, `build/perf-w3-boot`, check OFF, `chain`):**
the coverage phase is 190.3 ms over 31 kept images, of which the particle
bounds loop is **3.1 ms** (1.6%). The rest, about 187 ms, is the face loop over
the 1,603 examined entries (about 6 ms per image). So the coverage cost is the
face loop itself, including the dead entries that `apply_original_fighter_ot`
culls. Compose stays at 144 ms in the same run.

**Correction (2026-11-07): the live-list candidate does not save work.** The
mixed pass runs once per field, and the list would be built by one scan of the
same range per field, so the scan just moves. A valid candidate has to make
each of the ~1,600 entries cheaper to read or stop producing dead entries in the
fused pass (where the fighter faces are allocated and culled in
`apply_original_fighter_ot`). Neither has a design or a measurement yet, so
W3.4 is open with no candidate.

**W3.4 candidate: per-vertex outcodes (2026-11-07, `T3_MIXED_OT_OUTCODES`).**
Before the face loop, each projected vertex of both fighters gets a 4-bit
outcode against the rectangle of the covered cells. A face whose four outcodes
share a bit lies wholly on one side of that rectangle, so the full per-face
bounds test would reject it too; only the other faces take the full test.
`T3_MIXED_OT_OUTCODES_CHECK` runs the full test on every face and counts the
outcode rejects that reach a covered cell: on `chain`, **19,681 faces tested,
15,558 rejected by outcodes (79 %), 0 mismatches**, so the candidate keeps
exactly the faces the control keeps. Output
`assets/imported/perf-w3-burst-occheck`.

A/B on `chain`, `build/perf-w3-boot` rebuilt in place (control = flag OFF).
Both builds repeat to the tick (two runs each, identical rings):

| Build | Kept images | Mixed | Coverage (of which vertex/particle pass) | Compose | Worst image | Frames / sim ticks |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| control | 31 | 345.3 ms | 190.3 ms (3.1) | 144.4 ms | 30.52 ms (tick 414) | 411 / 752 |
| outcodes | 30 | 315.2 ms | 133.7 ms (38.3) | 168.8 ms | 31.24 ms (tick 421) | 412 / 753 |

On the images both runs draw from identical state (same tick, particles, faces
and depth), coverage drops **6.87 → 5.19 ms** (tick 291) and **6.97 → 5.34 ms**
(tick 295), and the image total drops 1.6 ms. Coverage is lower on every
candidate image (1,194–1,394 FRT against 1,404–1,558). But the faster images
shift which ticks get drawn, and the candidate's heaviest image lands on a tick
with more live faces (331 against 280), whose compose is higher. The fixture
worst case is therefore **0.72 ms worse**, and the frame count is unchanged
(412 against 411). Under the rule that a change ships only when img/s and the
worst case are no worse, **the candidate is not promoted** on this evidence. It
stays behind the OFF-by-default flag. The 30 img/s target is still unverified.
Evidence: `assets/imported/perf-w3-burst-control` and
`assets/imported/perf-w3-burst-outcodes`.

**Second fixture: `two-chains` (2026-11-07).** The `arcade` script gives no
hits on the boot-to-fight build (its menu presses need the full roster), so
`scripts/saturn_effect_burst.py` gained `two-chains`: the `chain`, then the
same chain from P2 while P1 holds `RIGHT` (hits `[5, 6]`, health 43/44 HP).
The ring is full (32 images) in both runs:

| Build | Mixed | Coverage (of which vertex/particle pass) | Compose | Worst image | Frames / sim ticks |
| --- | ---: | ---: | ---: | ---: | ---: |
| control | 515.4 ms | 206.1 ms (6.3) | 290.6 ms | 32.11 ms | 662 / 1253 |
| outcodes | 506.8 ms | 168.8 ms (45.2) | 317.6 ms | 31.24 ms | 662 / 1253 |

Here the worst case is **0.87 ms better**, with the frame count equal. Over the
two fixtures the worst case moves by −0.87 and +0.72 ms, which is the drift
from which ticks get drawn, not the change itself. The like-for-like saving
(same tick, same state) is 1.6 ms per image. The ring keeps only the 32
costliest images, so its compose totals are biased toward heavier ticks and
are not a per-image cost. The strict rule is not met on `chain`, so the flag
stays OFF until the owner decides; the evidence for both fixtures is in
`assets/imported/perf-w3-burst-two-control` and `-two-outcodes`.

**Compose split (2026-11-07).** Ring field 19 now holds the FRT of the face
masks inside compose (`t3_stage_particle_multi_face`); the rest of compose is
the particle cells (`t3_particle_prepare_add`) and the HUD occlusion. On
`two-chains` (control, with the extra timer): compose 332.3 ms, of which face
masks **118.4 ms** and particle cells plus HUD **213.9 ms**.

**Promoted: cell lookup through the cell map (2026-11-07).** For every cell a
sprite reaches (up to 25), `t3_particle_tile_accumulate_status` called
`find_tile`, a scan of the whole cell list (up to 52). The stage layer already
keeps `g_particle_cell[28][40]`, exact for the list on entry to each add.
`t3_particle_tile_accumulate_mapped` reads it instead. After a compaction
(which renumbers the list) the lookups still scan. Proof of identity:
`check_mapped_lookup` in `tests/test_psx_particle_tile.c` runs 200 random
trials of sprites and full-cell occlusions, with compactions, and the two
canvases (cells, order, pixels, status) are identical after every step. A/B on
`build/perf-w3-boot` (both with the compose timer):

| Fixture | Build | Worst image | Frames | Same-state images: particle part of compose |
| --- | --- | ---: | ---: | ---: |
| `two-chains` | control | 32.62 ms | 661 | 64.23 ms (9 images) |
| `two-chains` | cell map | 32.17 ms | 661 | 60.50 ms |
| `chain` | control | 32.62 ms | 410 | 69.97 ms (18 images) |
| `chain` | cell map | 32.17 ms | 411 | 66.22 ms |

Every same-state image is cheaper or equal (heavy images −0.3 to −0.9 ms), the
worst case drops 0.45 ms on both fixtures, and the frame count is no lower, so
the change meets the rule and is the normal path whenever the ordered mask is
built (no flag). Evidence: `assets/imported/perf-w3-burst-compose-split`,
`-two-cellmap`, `-chain-control3`, `-chain-cellmap`. The promoted build
(flag removed) repeats the `two-chains` cell-map run exactly.

`scripts/saturn_combat_suite.py` cannot check this build: it pins Law's
reaction slots (`high-hit` expects 2250), and `perf-w3-boot` boots the pair
11;9, whose defender reacts with slot 2321. The hit itself is as expected (one
hit, 18 damage, `first_error 0`). The suite still has to run on a build of
its own pair.

**W3 gates for the promoted head (2026-11-07):**

| Gate | Result |
| --- | --- |
| Whole fight (Mednafen, 280 s) | marked 26.84 → 26.89 img/s, worst segment 25.14 → 25.26, images 2,520 → 2,525, pad hash `0xb99cc280` on both |
| Combat suite (`--with-ko`, `T3_BOOT_FIGHTERS='0;1'` build) | **PASS**: 24 live cases |
| Frozen pixels 2000 (cache 128, 5,000 frames) | **PASS**: 104 fields, model pixels `4b1e8b36…` |
| Frozen pixels 3000 (cache 128, 6,200 frames) | **PASS**: 104 fields, model pixels `838c0a15…` |
| Host tests | 103/103 pass, 8 of them skipped (private trace data); after the W5 topology reset fix |

The first tick-3000 attempt looked like a stall: every build (head, the
previous commit, topology off, live) stopped at `sim_ticks` 2,161–2,163. The
cause was the capture length. The baseline was captured with `--frames 6200`
(screenshot 6,100), and 5,000 frames end before the replay reaches tick 3000.
The command above now notes this. The gains are real per image but stay
within whole-fight noise; the base rate stays ≈26.9, and 30 img/s is still open.

**Defaults corrected (2026-11-07).** `T3_MIXED_OT_LIVE_ENTRIES` was still
`OFF` in `CMakeLists.txt`, so a default build never had the compaction; only
the `perf-w3-cand` directory set it by hand. Both it and
`T3_MIXED_OT_OUTCODES` now default to `T3_PARTICLE_ORDERED_MASK_PROBE` (ON
whenever `T3_PARTICLES` is). The owner chose to ship the outcodes on the
same-state evidence (1.6 ms cheaper per effect image, 0 mismatches over 19,681
faces); the fixture worst-case drift (−0.87 / +0.72 ms) is which ticks get
drawn. The `T3_MIXED_OT_OUTCODES_CHECK` line lacked a space before its doc
string, which wrote a malformed cache entry; fixed. Gates on the new defaults
(`build/perf-w3-cand`, cache 128, effect ring on):

| Gate | Result |
| --- | --- |
| Whole fight (Mednafen, 280 s, vs `perf-w3-med-final-280`) | marked 26.89 → **26.92**, segments +0.04/+0.11/−0.05/0.00/−0.02/+0.06/+0.05, worst 25.26 → 25.24 (inside ±0.04 noise), 2,525 images, pad hash `0xb99cc280`, 8/8 marks |
| Frozen pixels 2000 / 3000 | **PASS**: model pixels `4b1e8b36…` / `838c0a15…`, equal to the cache-128 baseline |
| Host tests | 103/103 pass (8 skip without the private trace data) |

Evidence: `assets/imported/perf-w3-med-dflt-280`, `perf-w3-dflt-frozen-*-ymir`.

**Where the 30 img/s gap is.** 2,525 images in 5,836 fields is 2.31 fields
per image; 30 img/s needs 2.0, so about a third of the images take a third
field. Only 71 of them are effect images, so the gap is the ordinary fight
image. The same run counts 1,806 late swaps (`sat_vdp1_frame_stats`: VBlanks
at which a submitted VDP1 list had not ended, so the old picture stayed up)
against only 15 CPU waits on the VDP1. Late swaps alone do not say whether the
VDP1 draw or a late submit is the cause; the next package splits that per
image (CPU build end vs. VDP1 draw end) before choosing a target.

**Rejected: shape check once per face (2026-11-07).** `t3_face_quad_tile_coverage`
runs `t3_face_quad_supported` (a walk of every scanline) for each cell a face
reaches. Checking it once per face changed nothing measurable: face masks
112.97 → 113.87 ms on `two-chains`, worst image 32.17 → 32.22 ms, the 31
same-state images equal to 0.01 ms. Most faces reach one cell. The code was
removed.

**W5 fix: the topology check and tables rewritten in place (2026-11-07).**
With `T3_POSE_TOPOLOGY_FAST=ON` two host tests failed
(`world_pose_cache_exact_and_bounded`, `ps1_euler_fk_partial_joints`): the
check is kept per table address, and both tests rewrite a table in place and
expect the new, malformed table to fail. `t3_psx_pose_topology_reset()` forgets
the checked table. The world pose cache reset calls it (that reset is already
the documented way to announce reloaded tables), the match setup in
`move_probe.c` calls it on every match and round (a new fighter can be loaded
into the same buffer), and the test calls it after its edit. All 103 host
tests pass (95 run, 8 skip without the private trace data).

**W3.3 finding (2026-11-07): no duplicate reprojection to remove.** The ring's
counters per image are: original projection calls 2–6 (`proj`), faces
examined 1,603 (`exam`, constant), faces in front of the farthest particle
0–728 (`depth`). The cost is the coverage test over the examined faces (the
loop in `src/saturn/characters.c` around line 930–1018), not a second
projection of the same faces. The shared-representation prototype of W3.3
would save projections that are already rare, so it is not justified by these
numbers. The candidate that the counters do support is an earlier bound on the
1,603 examined entries (for example skipping faces that the coverage map
rejects before the per-face test), to be checked by W3.4 on the same fixture.

**W3.3 candidate: compact the entry list (2026-11-07).** The second cost the
counters do support is the *entry list itself*: the mixed OT inserts ~1,631
entries per effect image (the 822 roster faces plus the particles) and sorts
them into a 1024-bucket chain, but only 0–195 faces survive the coverage
test. The OT census counts about twice as many culled as live faces, so the
sort spends ~99 % of its time on entries that can never reach a particle
pixel. `T3_MIXED_OT_LIVE_ENTRIES` inserts only the live faces and keeps an
exact entry→face slot map (`g_mixed_ot_face_slots[]`), so the chain the sort
builds is the chain the full list would build: `t3_psx_scene_ot_order` chains
only *visible* entries, and the live faces keep their relative insertion
order, so the LIFO order inside a bucket and the equal-depth order are
unchanged. The map costs 3,456 B of High WRAM (`__bss_end` 0x060f41a0 →
0x060f4f10); Low WRAM is unchanged at 0x002ffd60.

**The cross-run tick↔image key is not an oracle.** The ring records the
end-of-frame tick (`g_game.total_ticks`) while the image content is captured
mid-frame, so the same image lands on tick 1881 in one run and 1882 in the
next. Comparing two emulator runs by tick therefore mixes the two runs'
frames: `scripts/compare_effect_rings.py` reports 13 mismatched fields and
two disjoint tick sets for two builds that are in fact equivalent. The
decisive proof has to be **in-build, same-tick, same-field**.

**W3.4 proof: the in-build double compose.** The new
`T3_MIXED_OT_LIVE_ENTRIES_CHECK` option (off by default) composes every
effect image twice in the same field — once with the full entry list, once
with the compacted one — and compares the published cells: the checksum
covers every cell's position and every 16-bit pixel of it, in list order, so
equal checksums mean the published overlay is identical, not merely similar.
The compacted image is the one that stays on screen, so the check does not
change what is displayed.

| Fixture | Effect images checked | Mismatches | Full list (last image) | Compacted list (last image) |
| --- | ---: | ---: | --- | --- |
| `arcade` (`T3_MIXED_OT_LIVE_ENTRIES_CHECK=ON`) | 28 | **0** | 1,607 entries / 4 faces / 2 cells / sum `b9249f69` | 8 entries / 4 faces / 2 cells / sum `b9249f69` |
| `chain` (same option) | 21 | **0** | — | — |

**W3.4 A/B (arcade fixture, same day, `build/perf-p0-cache64` vs
`build/perf-w3-liveentries`, differing only by `T3_MIXED_OT_LIVE_ENTRIES=1`):**

| Metric (ms over the worst 32 images) | Control | Candidate | Delta |
| --- | ---: | ---: | ---: |
| mixed OT total | 370.25 | 305.39 | −64.86 |
| — coverage test | 168.76 | 147.15 | −21.61 |
| — **OT sort** | **43.24** | **3.67** | **−39.57** |
| — compose | 155.85 | 152.24 | −3.61 |
| particle prepare | 109.12 | 108.88 | −0.24 |
| scene flush | 149.28 | 149.80 | +0.52 |
| **worst single image** | **31.35** | **27.63** | **−3.72 (−11.9 %)** |
| p95 image | 28.48 | 25.08 | −3.40 |
| entries per image | ~1,629 | 9–195 | −98 % |

`scripts/compare_burst_distribution.py` (added for W3.4) over the same two
captures: sort 9,669 → 821 FRT over the 32 kept images, worst image 7,010 →
6,179 FRT, p95 5,927 → 5,522 FRT; `particles` 663 → 661, `visible` 395 → 388,
`commands` 25,454 → 25,418, `examined` 51,296 → 51,296 (the same faces are
examined), `depth` 15,258 → 15,211, `submitted` 51,424 → 51,424 (the same
number of images is submitted). Gameplay counters match: hits `[5, 3]`,
blocks `[1, 0]`, health `[4849664, 6946816]`, `first_error 0`, one discarded
field on each side.

**W3.4 on the Mednafen CD path (2026-11-07, 280 s, pair 11,9).** The control
is `build/perf-w3-ctrl` and the candidate `build/perf-w3-cand`; the two build
directories differ by exactly one define (`-DT3_MIXED_OT_LIVE_ENTRIES=1`),
both rebuilt from the same source on the same day, and both with the effect
ring in the save state.

Whole-fight cadence (`scripts/compare_fight_reports.py`, shared marks only):
marked 26.47 → 26.45 (−0.02), segments −0.04 / −0.06 / +0.05, pad hash
`0xb99cc280` on both sides, `first_error 0`. **The whole-fight rate cannot
resolve this change**: the sort saves ≈39.6 ms over 32 effect images, and the
280 s run holds 71 effect images, so the whole fight gains ≈88 ms ≈ 4 images
≈ **+0.014 img/s**, a third of the ±0.04 img/s run noise. The verdict on the
whole fight is therefore "inside noise, no regression", and the gate has to
be judged on the burst, which is what P3 asks for.

Per-effect-image costs measured on the Mednafen CD path (from the ring in the
same two save states):

| Metric (ms over the worst 32 effect images) | Control | Candidate |
| --- | ---: | ---: |
| worst single image | 48.02 | 34.95 |
| p95 image | 36.32 | 29.73 |
| mixed OT total | 629.13 | 204.85 |
| — coverage test | 239.86 | 87.12 |
| — **OT sort** | **55.23** | **1.99** |
| — compose | 326.54 | 113.16 |
| particle prepare | 123.21 | 113.24 |
| scene flush | 218.44 | 213.24 |
| entries per image | 1,625 | 25 |

(The two rings are not yet the same set of images: the candidate's run was
truncated by host contention and covers the first burst only, so the second
burst's expensive images are in the control's ring but not the candidate's.
The reruns below settle it.)



**Cause found (2026-11-07):** the spray (`t3_psx_match_effects_hit` →
`t3_psx_effect_spray`, `src/game/psx_match_effects.c`) fires only for damage
≥ 21 (or the fire-breath strike joints). Health is 16.16 with max 140, so the
measured damage was: high-hit 18 (P2), simultaneous 8 (P1), mid-hit
`RIGHT+DOWN+X` 9 (P2). The set-3 hit burst spawns regardless, but it is not
a spark. A six-attack chain (`X`, `Y`, `A`, `B`, `RIGHT+X`, `RIGHT+Y`,
`DOWN+X`) registered 0 hits and 6 blocks, so no spray either.

Scenarios added to `scripts/saturn_effect_burst.py`: `mid-hit`, `low-hit`,
`chain`, `throw` (`X+Y` from close range: 0 hits, 1 block, so it did not
connect). None of them is a 21+ hit yet. The next step is a move with damage
≥ 21 (from the Kuma/Jin move table, or a power move: `powerTimer` raises the
spark variant), or a fixture that trades hits until one lands at 21+.
W3.2 costs are not measured until then.

Existing tools: `scripts/burst_cadence.py`, `scripts/compare_ps1_saturn_burst.py`,
`assets/imported/slowhit-study`. Fidelity rules: identical effect pixels, no
fewer spark cells, no missing fighters, keep the exact fallback.

### W4 — Stage ownership policy (P4)

1. W4.1 Record dispatch time, claimant, Slave deferred ticks, Master remaining
   work, stage duration, join, presentation cadence.
2. W4.2 Test a conservative static rule (Master claims only when the Master
   has proved idle capacity and the Slave has ≥3 ticks); then a small
   deterministic table; adaptive last.
3. W4.3 Keep table serial ownership, VBlank publication, coefficients and
   reset behavior; compare checksums and pixels.

Files: `src/saturn/main.c` (`g_stage_claim`), `src/saturn/stage_vdp2.c`.

**W4 verdict 2026-11-06: keep the first-come race.** Both prototypes lose
against the promoted base. `T3_STAGE_MASTER_MIN_DEFERRED=3`: 26.65 marked,
24.86 worst, five spans below base (−0.07/−0.18). `T3_STAGE_SLAVE_SKIP_DEFERRED=2`
(making the short chain leave the stage to the Master) is much worse: 24.83
marked, 23.56 worst, every span down. The measured outcome — Slave builds the
two-tick stages, Master the three/four-tick ones — is already the right
balance, so W4 ships no runtime change and the policy switches stay off.

**W4.1 measured 2026-11-06** (same probe run; one field = 3,728 FRT):

| deferred ticks | Master claims | Slave claims | Master ms/call | Slave ms/call |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 1 | 128 | 0.02 | 4.46 |
| 2 | 130 | **1,509** | 4.25 | 4.67 |
| 3 | **559** | 18 | 4.48 | 4.65 |
| 4 | 131 | 25 | 4.49 | 4.72 |

The race is not symmetric: on the common two-tick image the slave chain takes
the ~4.6 ms build (1,509 of 1,639), while at three or four ticks the Master
does it. Worst calls: Master 1,114 FRT, Slave 1,171 FRT (0.30–0.31 field).
Two prototype policies are being A/B'd against the promoted base:
`T3_STAGE_MASTER_MIN_DEFERRED=3` (Master stays out of one/two-tick images) and
`T3_STAGE_SLAVE_SKIP_DEFERRED=2` (the short slave chain leaves the stage to the
Master). Both keep table serial ownership, the TAS.B claim and `stage_sync()`
as the fallback; only which CPU can win the race changes.

**W4.1/W4.2 probe implemented (off by default).** `T3_STAGE_OWNER_PROBE`
keeps `g_stage_owner_claims[2][7]` and `g_stage_owner_ftr[2][7]` — row 0 the
Master, row 1 the Slave, column the deferred-tick count (capped at 6) — plus
the worst call per CPU. The deferred count is set in the main loop and read by
both claim sites, so a bucket always describes the image that raced.
`T3_STAGE_MASTER_MIN_DEFERRED=3` is the first static policy to test: the Master
leaves the stage to the Slave's chain below three deferred ticks, and the
existing `stage_sync()` join stays the fallback. `0` or `1` keeps today's
first-come rule. Cadence A/Bs must use the release value; the probe columns
only attribute the change.

### W5 — Immutable pose topology (P5)

**Enumeration (W5.1).** `t3_psx_compose_parts` checks three immutable part
properties per tick — `parent < -1 || parent >= i`, `slot >= DRAW_PARTS`,
`valid_vec(offset)` — plus the per-tick pose checks (`valid_slots` bit,
parent-already-placed mask, `t3_psx_mul_rot`/`place` success). The part table is
imported, immutable model data (`f->parts`, `ACTOR_PARTS(i)`), so only the first
group is hoistable.

**W5.2/W5.3 fast path (behind `T3_POSE_TOPOLOGY_FAST`, default OFF).** The
three immutable checks run once per part-table address (a function-local
cache) and the per-tick loop keeps only the pose checks. Failure still returns
0 before any output (the function commits `out` only after the whole loop).

**W5.4 bit-exact digest:** `tests/test_psx_pose_digest.c` sweeps five part
tables (chain, tree, invalid slot, invalid offset, parent loop), three
`valid_slots` masks, valid and invalid roots, and calls every combination
twice (the second call exercises the cached verdict). Both builds print
`ok 12 fail 48 digest 9b5ea1db3b2c6213`:

```sh
cc -O2 -I src/game -I $PREFIX/include [-DT3_POSE_TOPOLOGY_FAST=1] \
  tests/test_psx_pose_digest.c src/game/psx_pose.c src/game/psx_ik.c -o /tmp/pd && /tmp/pd
```

**W5 A/B 2026-11-06 (primary Kuma/Jin, 280 s, cache-128 base):** candidate
ahead in six of seven spans — +0.16/+0.16/+0.15/+0.24/+0.16/0.00/+0.05, marked
26.72 → **26.85**, worst 25.04 → **25.20**, pad hash unchanged. Second pair
(`T3_DEMO_PAIR='7,4'`, 240 s): +0.23/+0.08/0.00/+0.15, marked 26.25 → 26.36,
worst 20.90 → 21.13, same hash. **PROMOTED**: the CMake default is ON from
2026-11-06; the new base rate is 26.85 marked / 25.20 worst.

The Mednafen A/B decides promotion; without a whole-fight gain beyond noise the
switch stays off (W5.5).


1. W5.1 Enumerate every check in `t3_psx_compose_parts`; split immutable
   topology/offset validity from per-tick pose validity.
2. W5.2 Validate each roster character once at load; bounded execution plan
   with full fallback for invalid data.
3. W5.3 Remove only provably redundant checks; preserve Q12/Q15 rounding,
   multiply order, invalid-vector handling, output atomicity.
4. W5.4 Bit-exact per-pose comparisons across all recorded animations,
   topology classes, blends, IK edges, KO/replay, both actors.

Files: `src/game/psx_pose.c`, KMD/part import.

### W6 — Panorama key fitting (P6)

**W6.1 profile on the promoted base (2026-11-06, 2,478 prepared stages in the
280 s run, FRT /128):**

| Component | FRT per prepare | ms per prepare |
| --- | ---: | ---: |
| panorama (`t3_stage_backdrop_lines_offset`, fit + 224-line interpolation) | 1,262,105 / 2,478 = 509 | **2.28** |
| line-table encode loop | 289,651 / 2,478 = 117 | 0.52 |
| floor coefficients (`sat_vdp2_floor_from_view`) | 724,226 / 2,478 = 292 | 1.31 |
| rotation parameters | 30,188 / 2,478 = 12 | 0.05 |
| **total** | 930 | **4.16** |

This reproduces the earlier 628/394/12 split within measurement drift and
shows the panorama is the largest single component, ahead of the SDK floor.
The finer probe with the same build separates the panorama itself (1,711
prepared stages in the instrumented run): **fit keys 546,294 FRT (≈70%), the
per-line interpolation 230,062 FRT (≈30%)**.

**W6.2 outcome 2026-11-06: no exact rewrite with a measurable gain found, so
nothing ships.** The fit is per-key, per-sample work — five wall-ray samples,
each a rotate, a wall intersection with up to two 64/32 divides and an
`atan2_turn` with one divide and two polynomial chains — and the samples depend
on the key's screen line, so nothing is hoistable across keys. The only exact
common subexpressions (the `across[k]*right[i]` products, 15 of 27 multiplies
per key) are a few hundred cycles against ~10,000 per prepare. Merging the two
divides per key would remove an intermediate rounding step, i.e. change
pixels, which the plan forbids. Interpolation is already a pointer-free
incremental loop. The SDK floor (1.31 ms) was optimized in the last milestone;
the 32-line key interval stays untouched.

The candidate's base run for this profile measured 26.78 marked / 25.04 worst
(pad hash unchanged), i.e. the split probe itself is timing-neutral.


1. W6.1 Profile only the fitting-key computation after the SDK floor change.
2. W6.2 Precompute only camera-independent terms; integer CSE and fewer
   divisions.
3. W6.3 Compare every line-scroll word and floor depth value over yaw, pitch,
   zoom, boundaries, vertical offsets, near/far clipping, stage wraps.
4. W6.4 Full-match cadence including who claims the stage.

Files: `src/game/stage_backdrop.c`, `src/saturn/stage_vdp2.c`,
`libsaturn/src/graphics/vdp2/floor.cpp`. Do not widen the 32-line key interval.

## 3. Order and parallelism

```text
W0.1 baseline
  └─ W0.2 selector ─ W0.3 map ─ W0.4 matrix ─ W0.5 cache stats ─ W0.6/W0.7 gate
W1.1 purge measurement (parallel, cheap)
W2.1 AI ring (parallel)
W3.1 strong-hit fixture (parallel)
W4 stage policy  ─ after W0 baseline profile
W5 pose topology ─ independent A/B
W6 panorama      ─ after W4/SDK floor profile
```

W0.2–W0.7 and W2.1/W3.1 can run in parallel after the baseline. W1.2–W1.5 only
if W1.1 is meaningful. W4–W6 each require a fresh baseline run on the promoted
W0 winner so their gain is attributed correctly.

## 4. Rules carried from the plan

- No fidelity reduction counts as a speedup (geometry, textures, animation,
  depth order, clipping, color, resolution, particle cells).
- Compare logical state at the same tick and rendering at the same tick/field;
  structs byte-for-byte are not an oracle.
- Keep stock memory, cache coherence, VDP1 bank safety and Slave sync.
- Distinguish submitted lists, completed draws, swaps, changed pixels,
  committed ticks, discarded fields, late swaps and sound underruns.
- A win must survive Mednafen A/B, Ymir cache-on, the visual/gameplay gates and
  a second encounter; a worse worst segment rejects it.
- Record failed experiments and delete rejected runtime paths.

## 5. Disk and cleanup

Budget at start: `build` 2.7 GB, `assets/imported` 4.3 GB. Keep the control
`build/`, the plan-required assets, one candidate and its evidence. Give each
experiment the `perf-p0-*` / `perf-p1-*` prefix; delete rejected builds and
their `assets/imported/*-280` recordings (the `run.mov` is deleted by the
harness; delete the whole folder when the candidate is rejected).

## 6. Decision log

| Date | Package | Candidate | Result | Decision |
| --- | --- | --- | --- | --- |
| 2026-11-06 | W0.1 | `build` without a fixed demo pair | 24.09 marked, 21.13 worst, pad hash `0x640e32c0` | Not the reference fight; not a baseline |
| 2026-11-06 | W0.1 | `build/perf-kuma-jin`, `T3_DEMO_PAIR='11,9'` | 26.41 marked, 24.50 worst, pad hash `0xb99cc280` | **Control accepted**; use for all P0 A/Bs |
| 2026-11-06 | W0.2 | explicit `-D` survives `T3_PARTICLES` | cache 8 confirmed in a fresh build | Change kept (uncommitted at log time) |
| 2026-11-06 | W0.4 | `perf-p0-cache64`, first run (host busy with the cache-128 build) | 26.13 marked, 24.84 worst, **only 5 of 7 marks reached**; pad hash `0xb99cc280` | **Confounded**; rerun on an idle host before judging |
| 2026-11-06 | W0.4 | `perf-p0-cache64`, clean rerun (idle host, ELF `f6a2cf82…`) | **26.61 marked, 24.84 worst**, 2,490 images, 7/7 segments, pad hash `0xb99cc280` | Lead confirmed; gate after the cache-128 run and a same-day control |
| 2026-11-06 | W1.1 | boot cache sample from the same run (`_g_t3_boot_cache`) | both CPUs report CCR `1` | Cache **on**: the purge is not a no-op, so W1.1 needs the probe build |
| 2026-11-06 | W0.4 | same-day control (`perf-p0-cache64` reconfigured to 8, ELF map `__bss_end 0x060e64e0`) | 26.41 marked, 24.50 worst, segments identical to the historical control | Control is reproducible same-day; A/B is valid |
| 2026-11-06 | W0.4 | `perf-p0-cache128` clean (ELF `4bd0f025…`) | **26.72 marked, 25.04 worst**, 2,503 images, pad hash matched | Lead confirmed |
| 2026-11-06 | W0.4 | control vs cache-128, all 7 spans (`compare_fight_reports.py`) | segments +0.74/+0.40/−0.05/−0.13/+0.54/+0.22/+0.40; marked +0.31; worst +0.54 | **PROMOTE candidate**, pending W0.7 gates |
| 2026-11-06 | W0.4 | cache-64 vs cache-128 | marked 26.61 → 26.72, worst 24.84 → 25.04 | 128 is the better layout; 64 is superseded |
| 2026-11-06 | W0.7 | Ymir cache-on, arcade path, both builds | identical pad hash `0xb99cc280`; cadence 1,769 → 1,778 frames / 3,162 → 3,160 fields; max AI 0.710 → 0.708 | Gate passes |
| 2026-11-06 | W0.7 | frozen pixels, ticks 2000 and 3000, control vs candidate | 104 stable state fields and model-region RGB identical at both ticks (`4b1e8b36…`, `838c0a15…`) | Gate passes |
| 2026-11-06 | W0.7 | combat suite on the full-roster build | fails to find a fight in 1,200 fields (intro + title publish no diagnostics) | Gate re-run on a `T3_BOOT_FIGHTERS='0;1'` variant of the candidate |
| 2026-11-06 | W0.7 | combat suite on the boot-to-fight variant | 24/24 live cases pass (points/guards/throws/KO/replay/victory/next round/match reset); timeout cases skipped by choice | Gate passes |
| 2026-11-06 | W0.7 | second encounter `T3_DEMO_PAIR='7,4'` | candidate ahead on all 4 shared spans: marked 25.71 → 26.25 (+0.54), worst 20.17 → 20.87 (+0.70), same pad hash | Gate passes |
| 2026-11-06 | W0 | **P0 decision** | primary A/B +0.31 marked/+0.54 worst, Ymir cache-on equal-hash, frozen pixels equal at 2000/3000, 24-case combat suite pass, 441-pair arena pass, second encounter +0.54/+0.70 | **PROMOTE `T3_LOCAL_POSE_CACHE_ENTRIES=128` as the new base**; cache-8 stays the historical control; `T3_PARTICLES` no longer forces the trio |
| 2026-11-06 | W1.1 | purge FRT over 2,502 calls | 3,264 FRT total (0.876 fields ≈ 15 ms), worst 3 FRT | **W1 closed**; no selective invalidation |
| 2026-11-06 | W2.1 | AI top-8 ring | 39/8,684 over half a field, 0 over a full field; spikes read 218–303 words vs a mean of 4.5 | A real rare scan, but the ~11 ms is not the reads themselves; needs the per-read trace before W2.3 can name a table |
| 2026-11-06 | W4.2 | `T3_STAGE_MASTER_MIN_DEFERRED=3` | 26.65 marked, 24.86 worst; 5 spans below base, worst −0.18 | **REJECT**; variant deleted |
| 2026-11-06 | W4.2 | `T3_STAGE_SLAVE_SKIP_DEFERRED=2` | 24.83 marked, 23.56 worst; every span down | **REJECT**; keep the first-come race, both switches off |
| 2026-11-06 | W2.2 | traced run (`T3_AI_TRACE`) | trace disables the pick cache by design; run collapses to 18.64 marked, worst AI 6,869 FRT, reads capped at 1,024 | Cannot attribute the normal spike; record as next step, ship nothing |
| 2026-11-06 | W6.1 | split probe on the promoted base | panorama 2.28 ms (fit ≈70%, interp ≈30%), encode 0.52, floor 1.31, params 0.05 per prepare | Fit is the target, but see below |
| 2026-11-06 | W6.2 | exact rewrites reviewed | per-key/per-sample divides and rotations; the only CSE is <1% of the fit; merging divides changes rounding | **REJECT**: no exact measurable win, keep the code unchanged |
| 2026-11-06 | W5.5 | `T3_POSE_TOPOLOGY_FAST=ON`, primary fight | 6/7 spans up, marked 26.72 → 26.85, worst 25.04 → 25.20, digest bit-exact | Lead; second pair running before the default flips |
| 2026-11-06 | W5.5 | second pair 7,4, fast OFF vs ON | +0.23/+0.08/0.00/+0.15; marked 26.25 → 26.36, worst 20.90 → 21.13; same pad hash | **PROMOTE**: default ON, new base 26.85/25.20 |
| 2026-11-06 | W4.1 | stage claims by deferred bucket | Slave takes 1,509 of 1,639 two-tick stages (~4.6 ms each); Master takes the three/four-tick ones | Two policies queued for A/B |
| 2026-11-06 | tooling | `saturn_probe.py`: panic record 44→80 bytes, diagnostics version range →26 | Ymir runs complete and decode instead of failing on the prefix | Fix kept |

### Measurement caveat (found with the cache-64 run)

Mednafen is throttled to wall time, and the host could not emulate the game at
60 fields/s: the control reached 5,836 game-loop fields in 280 s and the
cache-64 run only 3,898 while the other build regenerated stage packs. The
per-segment rate is a ratio of images to game-loop fields and therefore stays
comparable, but a slower host reaches fewer of the eight tick marks, so the
later segments are missing and the *aggregate* marked rate covers a shorter
span. Never benchmark while another build is running, and compare a candidate
only over the marks both runs reached.

First five segments of the confounded run (same deterministic fight, pad hash
matches), then all seven of the clean rerun:

| Segment | Control (8) | cache-64 (busy) | cache-64 (clean) |
| --- | ---: | ---: | ---: |
| 1 | 24.90 | 25.46 | 25.46 |
| 2 | 25.00 | 25.30 | 25.30 |
| 3 | 28.05 | 28.05 | 28.05 |
| 4 | 26.96 | 27.01 | 27.01 |
| 5 | **24.50** | **24.84** | **24.84** |
| 6 | 25.24 | — | 25.40 |
| 7 | 30.25 | — | 30.20 |
| marked | 26.41 | 26.13 (5 spans) | **26.61** |
| images | 2,470 | 1,672 | 2,490 |

The clean rerun is the first candidate datum: six of seven segments are at or
above the control, the worst segment moves 24.50 → 24.84, and the marked rate
moves 26.41 → 26.61. The remaining comparison is the same-day control and the
128-entry run; the historical 26.41 was measured on a different day, so the
final verdict waits for the same-day control (`build/perf-p0-cache64`
reconfigured to 8 entries, which avoids the 13-minute stage regeneration of a
fresh clone).
