# Harness-led Saturn performance work

Next execution order: [Paul presentation and runtime recovery](PAUL_PERFORMANCE_RECOVERY.md).
That plan prioritizes cost isolation, visual comparison and whole-path gains
after the paired increment below proved insufficient.
The first [stage-isolation results and camera correction](PAUL_STAGE_MEASUREMENTS.md)
now establish separate simulation and rendering costs.

This is the remaining performance execution plan for the
[playable milestone](NEXT_PLAYABLE_MILESTONE.md). Preserve Paul's geometry,
textures, authored poses and the verified original combat state. Target 60 Hz
logic and investigate 60 Hz presentation; 30/60 is the intermediate acceptance
gate, not a measured Saturn hardware ceiling.

## 1. Establish trustworthy headless diagnostics

The sibling harness increment `71e0230` adds explicit cache emulation, bounded
per-CPU execution-step histograms, call/return/interrupt/DIVU/DMAC events,
register and instruction-word checkpoints, and VDP lifecycle observations.
It uses Ymir core interfaces directly, without a GUI or separate debug frontend.
Tests execute a known SH-2 call/return program with delay slots on both CPUs
and distinguish NMI entry from instruction execution, with cache off/on.
The harness CI passed: run `39077900335`.

Readback is now synchronous. A traced/untraced hello-world pair has identical
RGB pixels and 64 KiB command-table dumps. Its entry snapshot matches the
application's first eight instruction words. Earlier asynchronous screenshots
could race the renderer and cannot establish exact frame correspondence.
The initially attempted hello-world **disc** `.bin` injection was invalid;
only the corrected `.app.bin` run is accepted.

The previous counter called `master_sh2_cycles` is elapsed scheduler time per
field, not a function-cost measurement. New costs sum interpreter execution
steps and separately report interrupt entry. They exclude sleep/standby and
separately advanced DMA, and inherit Ymir's timing-model limitations.
Automatic VDP1 drawing can repeat a list: draw completions, framebuffer swaps
and completed generations becoming visible are not distinct-pixel or application
submission counts. Never promote those counters into a presentation claim.

## 2. Bind profiling to the actual application

`saturn_probe.py --debug-profile --sh2-cache on` saves the matching ELF/map,
provenance and bounded trace. `--snapshot-symbol memcpy` resolves the entry PC
from that ELF and retains registers plus instruction words for disassembly.
The analyzer reconciles histogram instructions/cycles with both CPU totals,
rejects truncated evidence and recognizes weak C++ text symbols. Zero-sized
assembler boundaries inferred from the next text symbol are explicitly marked.
Costs are exclusive PC attribution, not reconstructed inclusive call times.

Current private cache-on evidence uses the pending rendering preparation
change, ELF `5df25be6908c835f2eaa180c8e4bf44bff50bd955612b2a1cd91693330d3b9f2`.
The neutral 600/1200 endpoints are:

| Observation | VBlanks | Logic ticks | Submissions |
| --- | ---: | ---: | ---: |
| 600 fields | 584 | 312 | 77 |
| 1200 fields | 1181 | 636 | 158 |

The interval gives **8.14 submissions/s and 32.56 logic ticks/s** at nominal
60 Hz. The traced 1200 endpoint agrees with the untraced endpoint in these
counters. Sampled work is 131 ms including 70 ms render/HUD and 10 ms skin;
skin is included in render. Cache-off observations remain separate baselines.

A complete 120-field trace attributes 9.52% of Master execution cost to
projection, 8.10% to geometric visibility, 6.90% to instance submission,
6.49% to skinning, 6.35% to face append, 4.91% to branch search and 4.69% to
`memcpy`. No Slave instructions execute. These are PC costs in this neutral
window, not independently additive stage medians or worst-case guarantees.
Private artifacts: `harness-port-profile-final/` and
`harness-port-untraced-{600,1200}/` under ignored imported assets.

## 3. Use Virtua Fighter 2 as an observed reference

The private USA V1.005 archive has its CUE and all 34 BIN tracks. Actual
BIOS boot now reaches an Akira/Lau fight: screenshots at fields 4800 and
5400 establish gameplay, beyond the earlier memory-capacity warning.
`capture_saturn_reference.py` creates a fresh private backup image, hashes
all CUE tracks, BIOS, probe and input script, and records the exact command.
It neither injects an executable nor edits fighter state. The versioned
`inputs/vf2_usa_reference.pad` schedule handles BIOS language/clock setup
and arcade entry. `--pad-script` supplies P1 itself; overriding its callback
with a separate P1 device specification loses those inputs in this harness.

A cache-on, clock-factor-1/1, synchronous 120-field fight window observed
704×448 video, TVMD 33219, BGON 23 and CCR 1 on both CPUs. Master executed
32,792,873 instructions / 57,329,999 interpreter execution cycles; Slave
executed 32,500,489 / 57,330,001. These are model costs, not hardware timing.
There were 600 Master DMAC-start events and none on Slave. The 119 endpoint
intervals had 119 draw starts, ends, swaps and visible completed generations.
Automatic redraw and interlace prevent interpreting that as 60 unique full
pictures per second.

A separate complete one-field checkpoint window retains actual register
values and eight instruction words per hit. Disassembly identifies a Master
memory-polling loop, a Slave decrement delay loop, and a shared `MAC.L`
geometry kernel. In the longer window the two waiting loops account for
about 50% of Master and 57.53% of Slave execution cost respectively.
This demonstrates why busy CPU cycle totals alone do not identify productive
work. Retail bytes and disassembly stay private; no game code is copied into
this repository. Artifacts: `harness-vf2-fight/` and
`harness-vf2-registers/` under ignored imported assets.

Reproduce with the sibling probe built at the pinned Ymir revision:

```sh
python3 scripts/capture_saturn_reference.py \
  --disc "$PRIVATE_VF2_CUE" --bios "$PRIVATE_US_BIOS" \
  --out assets/imported/vf2-new-reference
```

Compare resolution, interlace, useful CPU work, command flow and VDP2 use on
the same emulator configuration. Do not assume how AM2 divides its work or
treat different games as equal geometry workloads.

## 4. Optimize the demonstrated critical path

First isolate configuration/synchronization defects and duplicate work. Then
reduce immutable-material/face copies in LibSaturn, measure the existing paired
geometry executor, and specialize skin/projection kernels while preserving
rounding, clipping, visibility and global painter ordering. Reuse immutable
FK/IK results without caching blend, head tracking, world roots or contact.
Budget caches and worker buffers together in stock RAM with the stack reserve.

Use identical configurations and short attributable experiments. Retain only
whole-path gains confirmed without tracing. Correct pacing to expose logic
debt instead of silently dropping elapsed ticks; increasing recovery limits
alone does not satisfy acceptance.

## 5. Acceptance and publication

Preserve the 16,334-tick continuous reference gate after core changes, run live
controls/combat/KO/replay/next-round scenarios, and validate memory, commands and
timeouts. Demonstrate sustained 60 Hz logic and at least 30 Hz presentation
without growing debt. Confirm the complete CD encounter in Mednafen. Keep
60/60 open until demonstrated. Each coherent passing increment gets a commit,
push and applicable CI review in both repositories; private assets stay outside
Git. Interrupted checks from the previous pause remain pending until completed.


## Executed paired-geometry increment

LibSaturn `e2fbf76781227862da991b960735bd5830ad1775` enables function/data
section collection in both build systems and explicitly retains both BIOS
entry points. Its installed-package gates pass. The private serial initialized
image is 824,124 bytes versus 960,252 before collection; adding the existing
Slave executor and geometry buffers gives 835,892 bytes. These are initialized
image sizes, not total RAM consumption. Linker checks include the stock RAM
regions and stack reserves; no expansion cartridge or CD asset streaming is
required by this increment.

`T3_PARALLEL_GEOMETRY` prepares P2 with the existing runtime worker while Master
prepares P1. Both actors' skin/world inputs are stable until the join. Only
Master merges into the scene and emits VDP commands, preserving P1/P2 painter
ordering. `T3_VERIFY_PARALLEL_GEOMETRY` additionally recomputes P2 serially and
checks consumed face/material/coordinate fields, keys and cull/clip counts.
It excludes padding and unspecified world coordinates. The diagnostic build
verified 213 paired frames with movement, attacks, reactions and reset, with
zero serial fallbacks or mismatches. Its overhead is excluded from performance.

A fixed original pose at tick 120, cache on and tracing off, gives:

| Path | Render/HUD | Commands | RGB SHA-256 |
| --- | ---: | ---: | --- |
| GC SDK, serial | 70 ms | 793 | `3ae675c90aecd46785bbef131800b959f21939fe052f38834edf6be088529f34` |
| GC SDK, paired | 59 ms | 793 | same |

The neutral 600/1200 endpoints with that same SDK are serial
(584/1181 VBlanks, 312/636 logic ticks, 77/158 submissions) and paired
(582/1187, 324/664, 80/165). At nominal 60 Hz the intervals are **8.14 → 8.43
submissions/s** and **32.56 → 33.72 logic ticks/s**. This is a small whole-path
gain, not the 30/60 gate. The scene is preserved; no mesh, texture or pose
reduction is used. Fixed-pose and live endpoint artifacts are
`final-paul-{gc,paired}-bench-600/` and
`final-paul-{gc,paired}-live-{600,1200}/`.

The original-pose presentation also skips the generic base skeleton when the
PS1 bridge supplies every referenced skin joint. Accessory/invalid-pose paths
retain full generic evaluation. The authoritative gameplay core is unchanged.
The continuous reference gate remains the published 16,334-tick checkpoint;
new changes to that core must rerun it, rather than reusing this render-only
qualification.


The fixed-pose 64 KiB VDP1 command tables are byte-identical as well
(SHA-256 `fbd7804ff945dabf7ae5c8bdd1ff1e0aab8e04c271880f82fa6b9e188013fa21`).
A complete ten-field paired trace at fields 900–909 reconciles both CPU totals
without drops. Most Slave cost in that short window is mailbox polling; the
worker is not yet used for skinning or authoritative logic. Avoid claiming
balanced useful work from equal elapsed cycle totals.

A fresh 160-second **actual Mednafen CD boot** of the paired recorded-input
build passed: the 80-second capture shows KO replay, 92 seconds shows the
winner's authored victory with the loser down, and 100 seconds shows round 2
with reset health and score 1–0. This is a recorded-input fixture, not physical
pad coverage. Movie/ELF/disc hashes are retained in
`assets/imported/final-paired-demo-cd/run.json`; the ELF SHA-256 is
`775320fd142cb1848f1d1e2adee4d4a23e04561306d18465abd72fb598aadb38`.
The paired live build has a different input configuration and is qualified by
the headless controls/combat suite separately. The generic SDK textured-model
example also reaches its model from a real Mednafen CD boot at 30 seconds.

The paired live build passed 22 headless control/combat/KO/replay/victory and
match-reset cases, plus two independently run neutral timeout checkpoints
(next round and drawn match). The reports remain separate in
`final-paired-combat-suite/suite.json` and `final-paired-timeout-suite/suite.json`.
A real BIOS boot of that live disc also recorded 11 parallel preparations and
zero serial fallbacks. Host CTest passed all 42 checks.

The pinned SDK package, native-boot and host gates passed on GitHub. Its wider
Phase 3 workflow failed while packing the unrelated `bullet_hell_stress`
example: the checkout lacks `assets/palette_bgr555_be.bin`. This is an open
SDK asset-packaging issue; do not describe the complete SDK CI as green.
