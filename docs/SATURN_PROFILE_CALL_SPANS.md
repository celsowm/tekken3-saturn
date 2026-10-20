# Full-Paul call spans, stage budget and topology audit

The bounded harness profile can now report inclusive completed calls as well
as exclusive instruction costs. This supports the structural performance
study without changing gameplay, SDK code or release scheduling.

## Completed stage budget

Diagnostic stage-ring version 2 adds `camera`, `stage` (floor/rails) and
`diagnostics`. They respectively cover camera update/scene begin, stage face
preparation, and snapshot publication. All three are inside complete `work`;
they are separate from actor geometry and HUD submission. The 1,024-slot ring
uses 65,548 Low-WRAM bytes, 12,288 more than version 1. The decoder retains
version-1 support and omits its unmeasured new stages rather than reporting
zeros. No new timers execute with `T3_PERF_SAMPLES=OFF`.

The private `structural-v2-critical-detail/matrix.json` retains matched
cache-on original-Paul controls over fields 300–1199. Values below are
instrumented **median milliseconds per submission**, except the final row.
The full window contains 170 completed samples/680 committed ticks; the
frozen window contains 297 samples and no further committed ticks.

| Stage | Full game | Frozen two-actor renderer |
| --- | ---: | ---: |
| Simulation | 30 | 0 |
| Skeleton | 1 | 1 |
| Skinning | 4 | 4 |
| Placement including pose publication | 2 | 2 |
| Actor geometry including worker join | 20 | 20 |
| Flush | 10 | 10 |
| Floor and rails | 4 | 4 |
| Camera/scene begin | 0 | 0 |
| Diagnostic publication | 0 | 0 |
| HUD | 1 | 1 |
| Presentation | 1 | 1 |
| Complete work | 75 | 45 |
| Simulation aggregate divided by committed ticks | 8.326 | n/a |

Zero-millisecond medians reflect timer resolution. Stage medians need not add
to the complete-work median. Outer VBlank/begin-frame waits are excluded.
The independent simulation-only version-1 control measures 8.336 ms/tick
over 894 committed ticks. Removing rendering is a diagnostic control; it does
not establish full-game cadence. These captures have zero guest errors and
draw timeouts. Full-game samples still discard 214 elapsed fields beyond the
four-field clamp, while the simulation-only measured window discards none.

Matching detailed full/frozen ELF hashes are respectively
`48d8b3f8fb4c275e10b170eb31034ab33baa5843525aff3fc0cf7225f81cb189` and
`8a5182ea349a57f9d91ea31f8e0591dc19451e7ab1a709777b29749ed335f5b3`.

Independent Mednafen CD controls remain necessary. The earlier full-game
35-second wall-time run stored only 5.833 seconds of video and cannot supply
gameplay samples. Its replacement `structural-v2-critical-native-full-resume`
contains 64.871 recorded seconds and visibly reaches gameplay. At movie
seconds 18/20/22/24 the overlay shows complete work of 102/103/111/109 ms,
simulation of 39/40/48/46 ms and geometry of 21/22/21/21 ms. This build uses
the version-1 stage HUD. The detailed version-2 frozen CD control
`structural-v2-critical-native-detail` contains 64.887 recorded seconds;
seconds 18/22/24 show 64/65/64 ms complete work, 21/22/22 ms geometry,
15/14/14 ms flush, 4/4/4 ms floor and 0/1/0 ms diagnostic publication.
These are inspected snapshots with profiler/HUD overhead, not distributions,
release cadence, unique-image counts or physical-hardware results.

The rebuilt release application remains 854,112 bytes. A complete binary
comparison with the qualified direct-span release finds only 38 differing
bytes in 35 panic source-line literals. Each maps to the same unchanged source
assertion; all other application bytes match. The private comparison is
`structural-v2-critical-study/release-code-control.json`. Consequently this
increment demonstrates instrumentation coverage, not a release speed gain.

## Reproduce the call report

Keep the ELF copied beside a capture. A later rebuild's symbol addresses are
not a valid replacement, even if its configuration has the same name.

```sh
python3 scripts/analyze_saturn_profile.py \
  --prefix assets/imported/structural-v2-critical-profile-36/debug \
  --elf assets/imported/structural-v2-critical-profile-36/profile.elf \
  --nm /path/to/sh2eb-elf-nm \
  --out assets/imported/structural-v2-critical-profile-36/inclusive-qualified.json \
  --span-symbol t3_move_probe_owned_step \
  --span-symbol t3_characters_render \
  --span-symbol sat_parallel_wait \
  --span-symbol sat_scene3d_prepare_batch_execute
```

All completed functions receive count, total, minimum, median, nearest-rank
p95 and maximum. Repeat `--span-symbol` to retain individual invocation
intervals for selected exact demangled ELF names. The analyzer matches an
observed SH-2 call with its `PC + 4` return address, including cache mirrors.
Nested and recursive calls retain their own spans. Unmatched returns do not
discard a caller; skipped calls and calls still open at the capture boundary
are counted separately. A tail-called function without an observed call is
included in its observed caller rather than invented as a separate invocation.

The units are **per-CPU inclusive interpreter cost from pre-call to pre-RTS**.
Child calls and interrupt work overlap enclosing spans. The final RTS and its
delay slot are excluded. These are neither exclusive costs nor hardware elapsed
cycles. Do not add nested totals, align the two CPU cost clocks, or convert a
call total into an assumed recoverable frame-time saving. Field labels locate
an invocation within the capture; they do not synchronize the CPU cost clocks.

The analyzer rejects truncated or dropped profiles, incomplete requested field
counts, non-monotonic event clocks/fields, unknown events and event costs beyond
the recorded CPU total. It independently reconciles call, return, interrupt,
division and DMA event counts with metadata. This detects a partially copied
event CSV even when its metadata still reports a complete capture. Exclusive
PC histogram conservation remains checked separately.

## Qualified full-Paul sample

The private 36-field capture covers fields 1160–1195 with cache emulation ON,
SDK `6302bec`, and the pinned Ymir immediate-result divider. All requested
fields and events are present. Matching ELF SHA-256:
`d3a64d016976da473396d0f73cac4dfb3b8b53b1b03e820235b89552842b1b21`.

| CPU / observed function | Completed calls | Median inclusive CPU cost |
| --- | ---: | ---: |
| Master: `t3_move_probe_owned_step` | 29 | 200,715 |
| Master: `t3_characters_render` | 7 | 716,342 |
| Master: `sat_scene_submit_instance` | 7 | 409,243 |
| Master: `sat_parallel_wait` | 7 | 94,437 |
| Master: `sat_scene_flush` | 7 | 262,477 |
| Slave: `sat_scene3d_prepare_batch_execute` | 7 | 416,740 |

The first five rows include overlapping work and must not be summed. There
are four open Master calls and one open Slave call at the end; 7,510 Master
and eight Slave returns have no observed matching call. No calls are skipped
by a nonlocal return. In particular, uncalled assembler helper entry jumps
must not be mistaken for missing event records.

These results support investigating overlap at a defined visual snapshot
boundary. They do not show that all geometry or all Slave idle work can be
recovered. The authoritative fighter order and mutable motion caches still
prevent running the two fighters' simulation independently. The full 60-tick /
30-picture performance gate remains open.

## Reproduce the private topology audit

```sh
python3 scripts/audit_kmd_topology.py \
  --kmd assets/imported/tekken3-usa/raw/071.bin \
  --libsaturn ../lib-saturn \
  --out assets/imported/structural-v2-critical-study/topology-qualified.json
```

This uses the same canonical skin exporter and source-to-runtime vertex
mapping as the renderer asset builder. It writes counts, arithmetic bounds,
scope and the input hash; it exports no mesh. Retail inputs and generated
reports remain private.

| Face class | Faces | Faces eligible for one-sided culling |
| --- | ---: | ---: |
| All vertices attached to one rigid joint | 410 | 406 |
| Rigid vertices attached to different joints | 86 | 84 |
| At least one blended vertex | 277 | 271 |

The source has 556 runtime vertices: 453 rigid and 103 blended. Single-joint
faces reference 440 distinct rigid vertices; only 235 are used exclusively by
cullable single-joint faces. All 103 blended vertices have two equal half
weights. This is **static eligibility**;
it does not prove a saved vertex count, visibility, transformed-normal
equivalence, rounding bounds, pixel parity or runtime benefit. A rigid-part
culling experiment would need an independent proof and matched captures before
changing renderer admission.

The audit also reports the exported anchor shift and a hypothetical signed-Q12
rotation dot-product bound. Paul uses shift 15 and maximum anchor L1 norm
5,488: the full signed-16-bit coefficient bound is 179,830,784 before rounding,
with a rounding bias of 1,024. The bias follows the exported shift; the synthetic
shift-14 fixture uses 512. This bounds only that hypothetical dot product,
excluding translation and blending. It proves neither equivalence to current
Q16 skinning nor a one-pixel final projection bound. No Q12 runtime path is
introduced.

## Snapshot ownership constraints

An overlap experiment must snapshot the final two-actor joint matrices, world
placement center, camera and all HUD/round/replay facts together before any
later authoritative tick. `draw_hud` currently reads both `g_game` and live
round accessors; copying fighter transforms alone would leave mixed-time HUD
state. `prepare_actors` also reads published world poses and generic clip/fade
state, while geometry reads mutable actor vertices, instances and scene state.
Those buffers require independent ownership until worker completion/release.
The authoritative core retains shared mutable sample/pose caches and ordered
opponent reads. Keep that simulation serial. Input edges, reset, pending tick
debt and the authorized presentation-age bound require explicit handling;
the current four-field clamp still deletes time. No overlap, debt repair or
60-tick/30-picture qualification is claimed by this diagnostic increment.

Synthetic checks cover blended seams, cross-joint and two-sided faces, invalid
indices, recursive/nested calls, independent CPU clocks, capture boundaries and
incomplete event files. The synthetic KMD integration also runs the real SDK
exporter when `LIBSATURN_SOURCE_DIR` is supplied.
