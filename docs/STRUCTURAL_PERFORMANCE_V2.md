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
