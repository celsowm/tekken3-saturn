# Completed CPU calls and private topology audit

The bounded harness profile can now report inclusive completed calls as well
as exclusive instruction costs. This supports the structural performance
study without changing gameplay, SDK code or release scheduling.

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
mapping as the renderer asset builder. It writes only counts, scope and the
input hash; it exports no mesh. Retail inputs and generated reports remain
private.

| Face class | Faces | Faces eligible for one-sided culling |
| --- | ---: | ---: |
| All vertices attached to one rigid joint | 410 | 406 |
| Rigid vertices attached to different joints | 86 | 84 |
| At least one blended vertex | 277 | 271 |

The source has 556 runtime vertices: 453 rigid and 103 blended. Single-joint
faces reference 440 distinct rigid vertices. This is **static eligibility**;
it does not prove a saved vertex count, visibility, transformed-normal
equivalence, rounding bounds, pixel parity or runtime benefit. A rigid-part
culling experiment would need an independent proof and matched captures before
changing renderer admission.

Synthetic checks cover blended seams, cross-joint and two-sided faces, invalid
indices, recursive/nested calls, independent CPU clocks, capture boundaries and
incomplete event files. The synthetic KMD integration also runs the real SDK
exporter when `LIBSATURN_SOURCE_DIR` is supplied.
