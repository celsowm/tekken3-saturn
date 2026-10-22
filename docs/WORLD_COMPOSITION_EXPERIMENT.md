# World composition and shared-resource pipeline

The 60-tick/30-new-image goal remains unmet. These are opt-in experiments; the
previously qualified branch-filter binary remains preserved. No experimental
flag below defaults ON. The final topology pipeline has passed the live and
reference correctness campaigns and three paired native cadence runs, but
submission count alone does not prove distinct displayed images or image age.

## Implemented changes

`T3_WORLD_POSE_CACHE_ENTRIES=128` retains exact world composition after blend
and tracking. Four-way sets compare every valid local matrix, root rotation,
validity mask and immutable part-table identity. Fingerprints only reject
candidates. Root translation factors out of the recurrence exactly: rotations
and each `floor12(parent.rotation * offset)` are translation-independent.
Every valid ancestor/part origin is checked against the original bounds on a
hit; failures leave outputs untouched. Tables require explicit reinitialization
after edits at the same address. Cache storage is caller-owned and exclusively
accessed. The original uncached function handles misses.

The dedicated host test covers translated roots, intermediate bounds, partial
masks, root/local changes, forced fingerprint collisions, returned-buffer
mutation, eviction, table reload and 5,000 generated programs with changing root
translations and closed failures. All 49 host CTests pass.

The initial neutral guest observation has 1,383 hits and 779 misses. Its median
owned-step interpreter cost is 113,034 versus the preceding filter sample's
146,898, but this does not translate into a similar native improvement.

`T3_SHARE_ACTOR_RESOURCES=ON` shares immutable pixels, descriptors and material
programs between the two identical original actors. The actual imported asset
uses INDEX8 and one common palette, not an all-LUT4 asset. The second actor's
CRAM upload remains intact. Differing/overlapping palette configurations retain
independent fallback resources. Actual resident model texture bytes fall from
92,672 to 46,336. The presentation comparator reconciles exactly that two-actor
accounting with execution evidence and still requires all pixels to match.

`T3_SLAVE_SIMULATION=ON` reapplies the archived serial-simulation/painter
schedule to this changed useful-work and memory budget. Shared immutable
resources let its checked 16 KiB High-WRAM stack fit while retaining High-WRAM
texture descriptors. Cache ownership publication now includes the new world
cache and branch-filter metadata. Probe captures verify completed work, the
two-tick lag and the bottom 256-byte stack guard. Maximum observed stack use
across the 14 basic cases is 10,596 bytes. This is not a full physical
pose-to-display age proof.

`T3_ASSET_TOPOLOGY=ON` binds indices and face/material selectors directly to
the application's resident, decoded, native-endian archive before binding
validation/publication. That storage is mutable application-owned RAM, exposed
as a const model view by the decoder. Both actors only read it after boot;
vertices remain independently mutable. The previous duplicate Low-WRAM arrays
remain available. No additional High-WRAM topology allocation is needed.

Exact C32 deformation was also integrated with whole-actor preparation, with
placement applied once and verification moved after the worker join. The
verified frozen run performs 757 complete actor comparisons without fallback.
The subsequent two topology+C32 native arrangements both lose cadence against
the preserved topology pipeline, so C32 remains an opt-in experiment.

## Actual Mednafen cadence

All rows use neutral pads, actual CD boot and movie seconds 30/50/70/90. The
initial rows are one-run screens; the three final control/candidate pairs use
alternating C/A, A/C, C/A order, isolated profiles, the same binaries per
variant and exactly matching captured PNGs in all three repetitions.

| Variant | FRAME endpoints | 60-second increments |
| --- | --- | ---: |
| Same-source filter control | 181 / 426 / 671 / 917 | 736 |
| World cache | 184 / 433 / 681 / 926 | 742 |
| Cache + fused C32, Low descriptors | 181 / 428 / 673 / 914 | 733 |
| Cache + fused C32 + shared resources, High descriptors | 187 / 442 / 694 / 942 | 755 |
| Cache + shared resources + serial Slave simulation | 202 / 478 / 750 / 1021 | 819 |
| Same pipeline + resident topology | 206 / 487 / 764 / 1039 | 833 |
| Topology + C32, Low material programs | 201 / 475 / 747 / 1017 | 816 |
| Topology + C32, High material programs | 204 / 482 / 758 / 1031 | 827 |
| Topology + High-WRAM painter keys | 209 / 492 / 772 / 1050 | 841 |
| Sparse projection, contiguous runs | 145 / 342 / — / 735 | 590 |
| Sparse projection, compact one-call | 142 / 335 / — / 719 | 577 |
| Sparse compact + surviving-vertex safety proof | 180 / 426 / — / 911 | 731 |

The final paired runs each give **833 vs 739 submission increments** from
second 30 to 90, or 13.88 vs 12.32/s (**+12.72%**). The paired control HUD
values are 181 / 427 / 674 / 920; the candidate values are 206 / 487 / 764 /
1039. On some captured fields the HUD's zero glyph resembles a six: adjacent
seconds 29/31 and 89/91 verify the candidate's 206 and 1039 values. The
earlier fresh control was 736, so compare 833 with the **paired** 739 for
qualification. These are submissions, not a proof of newly displayed images.
Root-cache reuse alone gave only 0.82%; its large memory cost is not justified
by that standalone result. Judge the complete pipeline by the paired result.
The C32 and High-key rows are one-run screens, not paired qualifications. The
Low-program C32 arrangement preserves the frozen tick-120 crop exactly; both
C32 arrangements lose native cadence against the 833 candidate. C32 with both High-WRAM descriptors
and programs does not fit the checked Master stack reserve; the two tested
arrangements move one of those tables to Low WRAM. High-WRAM keys gain only
8/833 submissions (0.96%) in one screening recording and remain opt-in.

The final topology/pipeline build passes all 14 basic live cases, and its
frozen tick 120 matches all 97 stable fields after the exact resident-byte
reconciliation and every crop pixel. RGB hash remains
`dbd1dcf895998266165aed2f9e3a3cfbe9626638b3a864f1f2a3678a800a2c2a`.
Its 1,200-field run commits 1,086 ticks and 454 submissions with no discarded
field, guest error, draw timeout or damaged stack guard. These Ymir results
do not override the slower native cadence.

All three rendered original-reference runs pass every tick and field count:
5,481 + 5,632 + 5,221 = **16,334 ticks**, with zero divergence. Their builds are
`world-pose-pipeline-reference-{p1,p2,draw}`. The reference builds place material
programs and descriptors in Low WRAM to preserve the stack reserve. Inspect
all 24 cases of the completed `paul-world-pose-topology-pipeline` live campaign:
its suite reports pass, the expected optimization guards and checked Slave stack.
Physical presentation age and the 30-new-image gate remain unqualified.

Private captures use `assets/imported/paul-world-pose-*`. Source and immutable
binaries are archived in `assets/imported/world-pose-pipeline-source`.

## Larger remaining boundary

The unchanged existing SDK DSP demo was built against the same installed SDK
and executed in both emulators. All five existing Ymir acceptance tests pass,
including exact DMA results and overlap. Ymir reports CPU-copy DSP / SH-2 /
DSP-DMA times of 511 / 177 / 264 FRT ticks. Actual Mednafen CD boot reports
550 / 164 / 298. These single boot samples concern 420 procedural vectors,
not original skinning or Paul FPS. The built-in 21-vector API is slower than
SH-2 here, so direct substitution is not a selected optimization.

### Tiled visibility experiment: exact, slower at release optimization

`src/saturn/dsp_visibility.dsp` and its generated program image implement the
primary original world-space normal predicate in 197 of the DSP's 256 program
words. A tile holds at most 48 unique vertices and 16 faces. The program reads
three shared coordinate arrays, a packed face list and the camera eye, then
returns visible and CPU-fallback masks. Zero normals preserve the original
collapsed-corner handling on the CPU. The input arrays are unchanged.

The owned placement yields world coordinates divisible by six. The caller must
check this, the tile span (at most 32767 in each coordinate), the camera/view
bound and the normal bound before accepting a DSP bit. Using exact
`q = world / 6`, the world normal is 36 times the q-normal, so its sign is
identical. The centroid is `trunc_toward_zero(6 * sum(q) / 4)`. A checked
normal component in `[-2^25, 2^25-1]` and `abs(view_component) < 2^20`
bound the three-product sum inside the DSP's signed 48-bit accumulator. The
host simulator compares all accepted bits with the independent full-width
world-space predicate for 120 full random tiles, degenerate corners and
out-of-range normals; it also checks the generated program image and unchanged
input vertices. The program is not linked into the game.

The original archive has 556 vertices and 773 faces per actor. Contiguous
16-face batches form 49 tiles per actor, with 17–45 unique vertices (median
29). The frozen tick-120 world arrays for both actors are divisible by six,
and every primary normal satisfies the program's bound. This is one pose, not
a whole-match admission proof.

The separate Saturn probe measures 16 faces and 30 shared vertices with the
same installed SDK. Its input is world coordinates, so packing includes exact
division by six. At **Release `-O3`**, Mednafen CD boot reports 16-run totals
of **720 FRT ticks on the SH-2** and **148 packing + 1661 transfer/execute/read
ticks on the DSP path**: about **45 vs 113 ticks per tile**, zero mismatches.
Ymir agrees: 708 vs 152 + 1650. The earlier unoptimized probe had suggested a
small DSP win; it was an invalid comparison against the optimized game. The
optimized test rejects this DSP visibility path as an integration candidate.
Captures: `assets/imported/dsp-visibility-probe-30-release-{native,ymir}`.

### Current critical path and next experiment

An exact-ELF Ymir CPU profile over 12 fields of the final candidate attributes
about 11% of each CPU's execution cycles to projection, 9% to world-space
visibility, and 12% to the enclosing instance submission. The separate
instrumented stage capture with Slave simulation disabled reports median 16 ms
geometry, 9 ms painter flush and 15 ms simulation; it is an isolation run, not
the native release cadence. A separate native CD diagnostic HUD, built from the
same pipeline with `T3_NATIVE_WAIT_PROBE=ON`, reports at movie second 40:
**SIM 3481, PREP 5188, FLUSH 3652, HUD 1196 and END 1389 FRT ticks**; the
previous frame reports 65 ms total work. Its mean `sat_end_frame` copy is 1408
ticks. The diagnostic adds HUD work and therefore is not a candidate cadence
measurement, but it locates costs on the actual Mednafen path. At seconds 30,
40 and 45 it reports only two VDP1 draw/swap waits cumulatively; the many late
swaps count fields where a submitted list missed the next VBlank. They do not
explain away the roughly 24 ms preparation and 17 ms flush costs.

A second native diagnostic linked an isolated `LIBSATURN_ENABLE_PROFILE_METRICS`
SDK and showed the internals of that flush. At movie second 40 the previous
frame spent **3663 FRT ticks in flush: 804 in grouped painter ordering and
2055 in the entire ordered-emission function**, leaving about 804 outside
those two spans. At second 45 the split is 3522 / 781 / 2047. The emission
span includes queue traversal, dispatch, fallback work and command writes;
it is **not** a measured 2055-tick cost of API validation or command stores
alone. The unassigned span includes scene admission, residency checks and
other wrapper work; it is not a separately measured single cause. This
profiled build also draws extra HUD text and is for cost attribution, not
cadence qualification. Capture: `world-pose-flush-profile-cd`.

`T3_DMA_COMMAND_LIST=ON` tested the SDK's existing SCU-DMA path on the same
diagnostic build. At movie second 40, mean END cost rose from 1339 to 2000
FRT ticks and FRAME fell from 335 to 320; at second 45 the respective values
were 1408 to 2061 and 402 to 384. DMA contention with the busy Slave is the
likely cause, consistent with the SDK's documented dual-CPU caveat. Keep it
OFF. `T3_HIGH_SORT_KEYS=ON` moves only the 6.2 KiB painter-key array into the
remaining checked High-WRAM space. Its single native screen improves 833 to
841 increments, far short of the goal; it needs paired correctness/cadence
qualification before any promotion.

The DSP predicate removes too little of the measured path and adds per-tile
transport. The next candidate needs a substantial reduction across scene
preparation, ordering and emission; the diagnostic roughly 24/17 ms
preparation/flush split gives a direction, not an additive release budget. At the
frozen tick-120 pose, a read-only analysis of the original SKA1 face indices,
the generated two-sided flags, both resident vertex arrays and the actual
camera eye finds **401/773 and 392/773 faces rejected** by the exact primary
world-normal test. Only **399/556 and 415/556 vertices** are referenced by the
survivors; each actor has about 81 contiguous used-vertex runs. This is one
pose and an upper bound on saved projection work, not a measured speedup. It
motivated an early-visibility prototype, but the run count warned that naive
one-call-per-run projection could erase the saving. At the
current ~14 submissions/s an iteration often accumulates about four logic
ticks: the first two must finish before geometry, while at most two can overlap
flush. Near 30 submissions/s, two ticks per image would allow both to defer
after the first image. That potential scheduling benefit depends on retaining
tick debt and proving bounded image age; it is not a measured speedup or a
substitute for eliminating renderer work.

The bounded opt-in LibSaturn prototype kept the exact world predicate,
projected-winding test, clipping fallback, material identities and stable
painter. Three layouts were screened as separate actual CD boots. Projecting
the surviving contiguous runs gave **590** submissions; compacting them to one
projection call per actor gave **577**; restoring the all-safe fast-path proof
over surviving vertices recovered **731**. All lose against **833**. Each
variant passed the frozen 97-field pixel comparison with the same RGB hash.
The final compact variant loses **12.2%** despite projecting fewer vertices:
the pre-pass, packing/scattering and displaced face fast paths cost more than
they save. Its source change was removed from LibSaturn; only the ignored
experiment binaries/captures remain. Do not promote selective projection on
the strength of a vertex count alone.

## Reviewed budget and execution order

The paired candidate makes **833 submissions in 60 seconds**, or 13.88/s and
about **72.0 ms of wall time per submission**. The first 1.5x continuation
gate is at least 1250 submissions/60 s, requiring at most 48 ms per
submission. Thirty new images/s requires at most 33.3 ms per completed image,
about a 2.16x cadence increase from the current submission count, while also
committing 60 logic ticks/s. These are throughput requirements, not a claim
that this diagnostic build has a stable 72 ms render cost. Its one-frame
roughly 24 ms preparation and 17 ms flush estimates are instrumented, include
different work than the release, and cannot simply be subtracted from the
72 ms average. Even removing all 17 ms of flush from its roughly 65 ms frame
would leave about 48 ms before scheduling feedback; optimizing a subset of
flush has no credible standalone route to 30 images/s.

The existing ordinary compiled-program path already calls
`push_projected_program`, which writes the final Work-RAM VDP1 command
directly. The ordered-emission function adds queue traversal and a possible
cold path; its 2055 ticks cannot be attributed to repeated validation.
The previous [dense-command and simulation-pipeline experiment](COMMAND_PREPARATION_EXPERIMENT.md)
wrote the final list directly and gained
only 3.20% against its matched native control, while its serial writer
regressed. The separate compiled-native-command prototype in
[the renderer plan](COMPILED_RENDERER_PLAN.md#prototype-result--2026-10-20)
also regressed. Repeating either design is justified only by a new measured
bottleneck and a change that removes materially more work.

The SDK profile's `projection_ticks` and `preparation_ticks` are overwritten
by each instance submission. Both actors can submit in parallel, so these
fields are neither a sum of actor work nor a race-free frame breakdown.
`painter_ticks` and `emission_ticks` are measured in the later serial flush,
but still come from a HUD-instrumented alternate SDK. The native `SIM` HUD
measures only the leading simulation block; the deferred simulation join is
inside the later HUD span. The `END` span covers more than a bare copy. Do not
add these labels as if they were disjoint release-critical-path costs.

Next implementation sequence:

1. Instrument one representative native frame with **per-CPU, per-phase** FRT
   spans and counts: actor world transform, projection, visibility/queue
   preparation, geometry join, painter ordering, ordered emission including
   cold fallback count, deferred simulation join and VDP1 submission. Aggregate
   each actor only after its worker joins; report Master and Slave work and
   wall-clock dependencies separately. Record a distribution over the warmed
   interval, not one HUD snapshot, and keep this profiling build separate
   from cadence qualification.
2. Use that breakdown to select one opt-in **whole-path removal**: eliminate
   a full face or command pass, reduce an actual actor critical dependency, or
   move reusable static stage work to VDP2 without making the unfinished
   parallax/shading plan worse. Define the specific removed work and its
   maximum measured budget before coding. Preserve exact world visibility,
   cold clipping, stable painter, HUD/END capacity and full-frame rollback.
   Do not retry selective projection packing, native command reservation or
   a direct writer alone without new evidence that changes their earlier
   negative result.
3. Screen that integrated candidate with matched neutral native CD runs.
   The continuation gate is at least 1250 submissions in the same warmed
   60-second window, with no loss of committed ticks, image correctness or
   memory safety. If it misses, remove the prototype and identify the next
   measured whole-path cost; small isolated wins do not accumulate into an
   unmeasured claim of 30 FPS.
4. For a candidate passing the screen, require exact frozen pixels, continuous
   original-reference ticks, all 24 live cases, checked memory/stack and
   alternating paired native recordings. Then retain elapsed tick debt and
   correlate completed VDP1 images, changed pose epochs and display swaps
   over neutral, combat and round transitions. Final acceptance remains
   60 committed ticks and at least 30 newly displayed images/s with bounded
   image age, not submission counters alone.

This renderer work does not close the separate VDP2 stage plan: source parallax
and distance shading are still open. Preserve the current original-art floor
and panorama while measuring. Do not reuse the rejected isolated DSP predicate
as a speed claim.
