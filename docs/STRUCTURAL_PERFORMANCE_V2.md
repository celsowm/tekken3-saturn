# Full-Paul structural performance recovery

This supersedes the optimization order in the older performance plans. The
acceptance target is 60 committed logic ticks/s and 30 new complete images/s
with two original Paul models, original combat and stock RAM. The user has
authorized rendering-coordinate differences of at most one pixel and at most
one presentation frame of additional pipeline latency. Simulation arithmetic
and authoritative ordering remain exact. Meshes, textures and resolution stay
unchanged.

## Corrected experiment identities

The affine-matrix, sparse projection lookahead and Q15 range-dispatch controls
recorded in the sibling SDK were **procedural Tekken frontend controls**:
143 vertices, 151 faces, 24 joints, 24 textures, and owned-match mode OFF.
Their approximately 30 submissions/s and function-cost reductions do not
qualify the original Paul encounter. The preserved CMake caches and generated
headers in `build/saturn-affine-*` and `build/saturn-projection-*` establish
this distinction. Matching before/after controls remain valid for that smaller
workload; they must not be called full-Paul improvements.

The current original Paul build contains 556 vertices (453 rigid and 103
blended), 773 faces, 22 joints and 724 textures per actor. The selected release
uses both original poses, owned-match combat, the close camera and complete-list
presentation. Its preceding 5,400-field parallel-skinning run completed 4,125
ticks and 1,031 submissions, with 149 late swaps and no draw timeouts/errors.
This is approximately 45.8 ticks/s and 11.5 submissions/s over the requested
nominal window, not a physical Saturn FPS result. A 15-second CD capture of
the final build establishes boot/appearance, not a long-run cadence result.

`saturn_probe.py --workload two-paul-release` now refuses procedural assets,
one-actor/no-draw modes, frozen simulation, disabled original combat and
intrusive timing/verification builds. It records generated shape, private input
hashes, archive identity and all project configuration options, then checks
both guest poses and original combat after execution. The ordinary diagnostic
profile remains explicitly labeled. `build_saturn_variant.py` accepts the same
profile and checks the completed build. Private assets remain ignored.

## Implementation sequence

1. Preserve the qualified full-Paul release baseline, identify all future
   controls, and complete per-CPU/VDP1/VF2 measurements with explicit clock,
   cache, field and interlace semantics.
2. Replace external-memory polling during Slave idle with FRT-local waiting,
   bounded heartbeat/sequence maintenance and tested shutdown/race recovery.
   Measure Master throughput as well as idle instruction reductions.
3. Build a shared camera-space character preparation path: compose joint,
   placement and camera once per joint, preserve blend before perspective,
   bound fixed-point errors, and share perspective work across screen axes.
   Retain precise fallback outside qualified bounds.
4. Replace the common 104-byte prepared face with a hot record of at most
   24 bytes and cold fallback storage. Reserve disjoint writer spans in one
   scene queue, preserve stable global ordering and eliminate worker merge
   copies. Qualify cubes, Dino and the original Paul pair.
5. Overlap visual preparation with authoritative simulation through two
   independently owned pose/camera snapshots. Retain input edges and original
   actor dependencies; expose pending ticks instead of silently deleting time.
6. Qualify neutral, active combat and KO/replay for at least 60 steady seconds
   each, with no growing tick debt, 60 ticks/s and 30 new complete images/s.

Previously rejected Q16 MAC.L skinning, full native branch tables, 88-byte
face unions, radix sorting, rounded-winding proofs and DMA-only changes are
not new candidates. The proposed compact queue and fused camera path remove
different work. A one-function gain is never evidence of the final milestone.

Each passing coherent increment is committed and pushed. Shared SDK changes
are installed and consumed by a fresh port build before comparison. Use
uninstrumented release runs for cadence, separate correctness/profile builds,
all applicable host/package checks, serial/worker comparisons, and actual
Mednafen CD execution. Authoritative changes additionally require the full
16,334-tick differential gate; final integration requires all 24 live scenarios.
The pinned Ymir divider has immediate results, so latency-sensitive conclusions
need an independent Mednafen control. Neither emulator certifies hardware FPS.

## Qualified first increment: local FRT idle wait

SDK `5d98b2f` replaces the executor's continuous shared-memory idle polling
with on-chip FRT waiting and bounded maintenance. Fresh matching SDK/port
controls reproduce 4,125 ticks/1,031 submissions in 5,400 Ymir fields for both
variants. There is no Ymir cadence gain. Actual Mednafen CD movies give FRAME
42 to 473 for the control and 45 to 494 for the candidate at movie seconds
18 and 78: 431 versus 449 submissions in 60 seconds, or **4.18% more**.
Both new SDKs use the same compiler/options. An earlier original-build movie
reproduces the control's endpoints. These are submission-counter observations,
not unique-picture/hardware FPS. All 24 original combat scenarios pass, as do
the supported SDK host/tools and five package checks. The port CI now pins
the qualified SDK commit. The new workload guard and cache-option preservation
increments also pass both port workflows.

## Rejected: shared bounded perspective

An opt-in projection experiment used one depth reciprocal for both axes,
overlapping its DIVU latency with coordinate work. It proved at most one pixel
of error for a bounded clip range and retained exact fallback elsewhere;
100,000 host cases and camera controls passed. However, matched complete-Paul
Ymir runs changed 1,031 to 1,024 submissions (4,125 to 4,097 ticks). The actual
Mednafen candidate retained FRAME 45 and 494 at movie seconds 18 and 78,
exactly the FRT control's endpoints. No complete-path improvement was shown,
so the runtime API and port option were removed. The preserved builds,
movies and reports remain private under `structural-v2-bounded*`. This is not
the planned fused camera-space skin/placement path, which remains open.


## Qualified second increment: compact projected queue

SDK `d31e6d7` adds an optional 24-byte projected record for RGB and compiled
material faces, retaining the general cold fallback and existing stable painter
ordering. The port enables this queue in High WRAM by default; Dino enables its
own queue in Low WRAM. This increment still retains full cold capacity and copies
worker results during merge. Disjoint writer spans and removal of those copies
remain open work.

Fresh matched full-Paul builds using the same SDK/compiler/options completed
4,041 ticks/1,010 submissions with the general queue and 4,153 ticks/1,038
submissions with the compact queue over 5,400 Ymir fields: **2.77% more
submissions**. Late swaps increased from 47 to 186, so this is not a presentation
cadence qualification. The new SDK's general path is also approximately 2% slower
than the preceding FRT-only SDK's 1,031 submissions. Frozen tick-120 model crops
have zero changed pixels, worker/serial verification completes 184 comparisons,
and all 24 original combat scenarios pass.

Isolated neutral Mednafen CD captures show FRAME 49 to 538 at seconds 18 and 78
for the compact queue, compared with 45 to 494 for the preceding FRT control:
489 versus 449 submissions in 60 seconds (**8.91% more**). These counters do not
establish unique complete pictures or hardware FPS. Private profiles disable
both gamepads during measurement without modifying the interactive emulator
configuration. The recorder now retains workload identity and profile metadata.

Matched Dino runs improve the animated submission counter from 20.6 to 23.9/s;
paused controls remain 24/s with identical model pixels, and scripted controls
change from 20.0 to 20.1/s while passing runtime gates. Supported SDK host/tools
checks and the five package checks pass. The full-Paul 30-picture/60-tick target
remains unmet.


## Rejected: compact camera-space skinning (C and MAC.W)

The experiment composed placement and camera with each referenced joint,
quantized rotation coefficients to Q12, retained Q16 translation and Q15 blend,
and projected camera-space vertices directly. Unsupported clipping retained an
atomic world-space fallback. An initial run packed all 22 storage joints although
only 18 were initialized; its zero fast-path executions invalidated the result.
The corrected build packs only referenced joints. The workload guard now rejects
selected release optimizations that complete no frames.

Corrected C runs completed 4,037 ticks/1,009 submissions with 1,010 camera-path
executions and no fallback over 5,400 Ymir fields. Specializing pure projection
changed this to 4,097/1,024. Its Mednafen movie shows FRAME 48 to 536 at seconds
18 and 78 (488 submissions), compared with the compact queue's 489.

The SH-2 MAC.W variant passed 11,316 exact target vertex comparisons on
reconstructed quantized matrices, including 1/2/3 influences, sums outside
signed 32-bit range, both incoming SR.S modes and SR.S/interrupt-mask restoration.
The reusable skinning benchmark includes packing each batch; that isolated path
costs **43.27% more** than the existing C API in Ymir. Full-Paul execution reaches
4,153 ticks/1,038 submissions with 1,039 camera-path executions and no fallback,
matching the ordinary compact queue. Mednafen again gives FRAME 48 to 536 (488).
Neither implementation shows an additional complete-path gain. The public runtime
API and port options were removed; private patches, builds and captures remain
under `structural-v2-camera-*`. Do not repeat this candidate as an untried MAC or
camera optimization. Snapshot overlap and direct worker queue spans remain open.
