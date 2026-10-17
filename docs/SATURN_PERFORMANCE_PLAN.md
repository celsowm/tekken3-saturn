# Harness-led Saturn performance work

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

The private USA V1.005 archive has its CUE and all 34 BIN tracks. Boot the CUE
through the BIOS, automate initial language/clock configuration, then reach an
actual fight. A game-code memory-capacity warning has been observed; that is
not yet a fight or a measured frame-rate result. Use a private formatted backup
image and retain the exact input script.

Compare native modes on the same pinned Ymir with explicit cache emulation and
normal clock factor. Record resolution, interlace, CPU work, command flow and
VDP2 use. Do not equate interlaced fields to completed full pictures, assume
how AM2 divides its work, or treat different games as equal geometry workloads.
If optical boot blocks, document that boundary and continue port profiling.

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
