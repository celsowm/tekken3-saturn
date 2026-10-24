# Bounded local FK/IK reuse

The motion sample cache retains decoded channels, not solved local matrices.
The neutral profile had no sample misses but still evaluated both actors'
FK/IK on every tick. This experiment caches the raw local result before blend,
head tracking, world placement, body shapes and combat. Those dynamic stages
still run in original actor order on every committed tick.

## Ownership and exactness

`psx_local_pose.c` has no allocation or shared global state. A caller supplies
entries, capacity and immutable table lifetime. Since 2026-10-23 the key is a
stream frame, not the pose. That key is the address of the stream's first key
(which identifies the opened animation header in its bank), the clamped frame
and the frame count. The pointer/size identities of the FK trig, the IK trig,
the IK blob and the spline tables invalidate the cache when they change.

An entry also holds the sampled pose. A hit therefore replaces the spline
sample as well as FK and IK, and the separate sample cache is not built. A
power-of-two capacity is two-way set-associative, with consecutive frames of
a stream in consecutive sets. Two looping streams shorter than the set count
then never evict each other. With a full set, the way not used last is
replaced. The earlier pose-keyed FIFO of 128 entries held none of Paul's and
Law's 152 idle poses (`THIRTY_IMAGE_GATE.md`, "Paul vs Law"). The figures
below are for that earlier pose-keyed version.

The retained output includes all 18 local matrices, FK/full validity masks and
the pre-IK FK root-XX diagnostic. Consumers receive copies, so applying blend
or tracking cannot modify the cached result. Missing/singular limbs keep their
original masks and values. Failed evaluations are not inserted and do not
modify outputs. Table identity changes invalidate entries automatically;
same-address byte edits require explicit initialization/invalidation.

The cache is derived state outside the match transaction, like the existing
sample cache. Resetting the fight clears entries and statistics. It does not
retain world matrices, opponent facts, health, blend results or input decisions.
The normal uncached path remains available with capacity zero.

## Matching controls

Starting source: `cc4ad48`; SDK `10f995605e80a3903b345d1de029837168475a87`.
All private controls retain models, textures, camera, geometry scheduling and
stock RAM. Primary Ymir runs explicitly enable cache emulation. The pinned
divider returns results immediately; none of these results certifies hardware
timing. Profiling, stage sampling and uninstrumented release builds are separate.

Nested-FRT matrix: 1,200 fields per scenario, aggregate after field 300.
Values below are **instrumented tick-root ms/tick**, including hooks/interrupts.

| Workload | Disabled | 8 entries | 64 entries | 128 entries |
| --- | ---: | ---: | ---: | ---: |
| Neutral | 11.622 | 10.772 | 9.548 | 9.545 |
| Approach | 11.935 | 12.327 | 10.750 | 10.197 |
| Attack inputs | 12.168 | 12.552 | 12.223 | 11.700 |
| Simultaneous inputs | 11.411 | 10.480 | 9.878 | 9.495 |

The same field scripts reach different logic ticks when execution changes.
These workload comparisons identify opportunity/regression; they are not
aligned behavior or per-pose performance certificates. Their raw coherent
captures are revalidated by `report_local_pose_experiment.py`, including script
hash, BIOS/probe, cache/clock/CCR policy, divider model and profiler accounting.

Eight entries exploit same-tick actor sharing, with 50% neutral hits, but add
about 3% diagnostic tick cost to approach/attack scenarios. Sixty-four entries
retain the neutral cycle but still slightly regress this attack-input window.
The 128-entry control improves all four observed diagnostic roots by
17.87%, 14.56%, 3.85% and 16.80%, respectively. This is the selected candidate
for full qualification, rather than promoting the smaller neutral-only result.

Target storage is 440 bytes per entry plus a 48-byte cache header:
3,568 bytes for eight entries, 28,208 for 64 and **56,368 bytes for 128**.
This memory cost is explicit; no expansion cartridge is used. The 128-entry
release link has Low WRAM end `0x002a2b54`, High BSS end `0x060d94e8`, and the
existing 16 KiB Master stack reserve. Link bounds are not stack watermarks.

Separate whole-path stage builds, 1,200 cache-on fields:

| Work | Disabled median / p95 ms | 8 entries | 128 entries |
| --- | ---: | ---: | ---: |
| Simulation | 38 / 49 | 35 / 46 | 30 / 41 |
| Geometry, including join | 20 / 20 | 20 / 20 | 20 / 20 |
| Flush | 10 / 10 | 10 / 10 | 10 / 10 |
| Complete measured work | 87 / 98 | 84 / 95 | 79 / 90 |

The 128-entry complete median drops 9.20%. This ring excludes the outer
VBlank/begin-frame wait; stage medians are not additive or release FPS.

Uninstrumented neutral endpoints at fields 1,800 and 5,400:

| Control | Interval fields | Submissions | Ticks | Submissions/s | Ticks/s | Discarded time |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Disabled | 3,603 | 602 | 2,408 | 10.025 | 40.100 | 33.17% |
| 8 entries | 3,596 | 667 | 2,668 | 11.129 | 44.516 | 25.81% |
| 128 entries | 3,600 | 673 | 2,692 | 11.217 | 44.867 | 25.22% |

The selected control improves this roughly 60-second neutral cadence by
11.89%. It remains far below 60 logic ticks/s and 30 distinct images/s.
The earlier 600–1,800-field windows gave identical 11.163 submissions/s for
eight and 128 entries; the longer interval and active-workload matrix are
needed to assess the choice. Neither window establishes displayed-image FPS.

## Display and remaining budgets

A complete 12-field cache-on release debugger trace with eight entries has no
dropped records, observed CCR=1 on both CPUs and median sampled VDP1 list
drawing of 0.09193 fields. Its PTMR=2 mode repeatedly draws a list: 11 draw
starts/ends, swaps and `visible_completed_draws` increments over the 11-field
endpoint interval do **not** mean 11 new CPU submissions or distinct images.
The next harness qualification must distinguish repeated lists from new image
content. This narrow trace does not bound other coverage or scenes.

The private model manifest reports 556 compiled vertices, including 103 blend
vertices. Thus 453 vertices are rigid; an all-rigid optimization cannot stand
in for the complete Paul path. Presentation still converts joints, skins,
places vertices, projects/prepares faces and emits commands. Fusing matrices
across those stages must preserve intermediate rounding, particularly at seams.
The existing rejected Q12, MAC skinning, High-WRAM and bundled actor scheduling
experiments remain relevant and are not silently repeated by this cache.

## Correctness and evidence

Synthetic tests compare cache misses/hits against the independent existing
uncached solver for 200 changing poses plus incomplete masks, eviction,
fingerprint collision, returned-buffer mutation, table-bounds invalidation,
reset and atomic output failure. At frozen tick 120, eight- and 128-entry
builds match all 97 stable snapshot fields and exact model-region RGB pixels:
`48cdd00660bfecf7c9164266d052121b2986bc9777a9d164f18f93b3aa2fb896`.
These single checkpoints complement, and do not replace, the continuous gate.

Both candidate CDs boot in Mednafen. Reviewed second 34 has both actors and
FRAME 146; the frame counter is not a frame-rate measurement.
The completed continuous/live gates are recorded below.

All three preserved baseline sequences and both cached controls (eight and
128 entries) pass every committed original/host/Saturn digest and field count:
5,481 + 5,632 + 5,221 = **16,334 ticks per control**, with no divergence.
The existing original-audited exports remain unchanged; expected per-tick
digests are never provided to the running match.
Fresh host/original audits of the same three captures also pass all 16,334
ticks with the current evaluator (`local-pose-host-{p1,p2,draw}`).

The disabled public firmware build and all 44 host CTests pass. The host
solver test includes the additional cache cases above; all 31 Saturn tool
tests pass. The eight-entry control passes all 24 adaptive combat cases;
the selected 128-entry control also passes all 24 cases, including both neutral
timeouts, KO/replay/victory, later rounds, terminal match and reset. These live
cases use cache emulation OFF on both controls for correctness; they are not
the cache-ON performance measurements above.

Fresh private owned-match builds now default to 128 entries. Fresh public
builds default to zero; an explicitly enabled cache requires owned-match mode.
Existing CMake caches are not silently overwritten. Use an explicit capacity
when comparing old configurations.

Release ELF SHA-256 (disabled / selected):

- `058ab654d4bc81b2fadf47d540304b589a0183d1175b5f7f6efdca16465911b2`
- `e97bd34c16b64795ac94042e0f17e501cb3cddd6490787ba31a8ee16ee0ad231`

Clearing the selected build's cached capacity and configuring with the new
private default reproduces the exact qualified ELF hash. The public default
is separately checked as zero and its firmware rebuild passes.

Private evidence (ignored):

- `assets/imported/local-pose-cache-{0,8,32,64,128}-*`
- `local-pose-cache-{0,8,64}-profile/` and `local-pose-cache-128-profile-matrix/`
- `local-pose-cache-{0,8,128}-stages/`
- `local-pose-cache-{8,64,128}-matrix-comparison.json`
- `structural-reference-baseline-{p1,p2,draw}/`
- `local-pose-cache-{8,128}-reference-{p1,p2,draw}/`
- `local-pose-cache-{8,128}-combat-suite/` and `*-mednafen/`

Reproduce from the retained release configuration using the existing variant
builder and `-DT3_LOCAL_POSE_CACHE_ENTRIES=0|8|32|64|128`. Use fresh outputs and
preserve the baseline before editing source. `run_simulation_matrix.py` builds
the diagnostic matrix; its measurements must never substitute for release
endpoints. For example:

```sh
python3 scripts/report_local_pose_experiment.py \
  --baseline assets/imported/local-pose-cache-0-profile \
  --candidate assets/imported/local-pose-cache-128-profile-matrix \
  --baseline-cadence assets/imported/local-pose-cache-0-1800 \
                     assets/imported/local-pose-cache-0-5400 \
  --candidate-cadence assets/imported/local-pose-cache-128-1800 \
                      assets/imported/local-pose-cache-128-5400 \
  --out assets/imported/local-pose-cache-128-matrix-comparison.json
```
