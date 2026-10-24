# Second character on the Saturn (Paul against Law)

P1 plays Paul and P2 plays Law, each with their own motion bank, render
mesh, textures and palette. The game logic is the same owned core that the
host oracle checks: `build/oracle-law-s1` matches the original for
3000 of 3000 ticks (see `FIGHT_ORACLE.md`).

## Build

Starting from the release configuration (`build/t30-cam`):

```
-DT3_LOCAL_SECOND_DIVMOT=.../divmot/082/bank_1.t3logic   # Law's bank
-DT3_LOCAL_ACTOR1_MESH_KMD=.../law-import/raw/079.bin    # Law's KMD
-DT3_LOCAL_ACTOR1_ARC=.../law-import/raw/081.bin         # Law's textures
-DT3_LOCAL_ACTOR1_KMD=                                   # skeleton from the mesh
-DT3_SHARE_ACTOR_RESOURCES=OFF                           # one mesh only
-DT3_WORLD_POSE_CACHE_ENTRIES=0                          # see below
```

`T3_SECOND_CHARACTER_ID` (default 1, Law) is P2's original character id.
These are all private inputs, and the outputs stay in `build/`.

## What changed

- `build_local_kmd_assets.py` compiles a second SKA1 (`t3_archive1`,
  `t3_kmd_double_sided1`). The count macros become the larger of the two
  meshes, and each actor takes its own counts from its decoded asset.
  The compiled renderer and shared actor resources still cover one mesh,
  and the build rejects them together with a second mesh.
- `characters.c` decodes one asset per mesh. P2's palettes go into the CRAM
  banks after P1's.
- `MOTION.BIN` version 2 appends the second bank after the common bank and
  includes it in the digest. `cold_data.c` loads it into Low WRAM.
  Version 1 is unchanged for one-bank builds.
- `move_probe.c` gives P2 the second bank (`actor_bank[1]`) and character id,
  and links both banks for the merged move index. The branch input filter
  gets a fourth table (1,680 of 3,072 words are used).

## Memory

LibSaturn's scene can now limit its full ("cold") face records
(`cold_capacity`). A face that would overflow it is dropped and counted in
`skipped_faces`, and a direct batch is granted only the records left.
The game keeps 640 records (`T3_COLD_FACES`) instead of 1,562. The measured
peak is 225, in KO close-ups. This saves 96 KB of Low WRAM: Low WRAM drops
from 829,004 to 733,116 bytes with Paul only, and is 1,026,572 of 1,048,576
bytes with Law's bank. With a second mesh, High WRAM holds both archives
(167 KB of `.data`) after dropping the 137 KB world-pose cache.

## Speed

Both builds were measured over the same 1,230-frame Ymir probe (neutral
standing, no inputs):

| Build | Images | Images/s | Frame work |
|---|---|---|---|
| Paul mirror, cache 128 (`t30-cam`) | 557 | 34.4 | 34 ms |
| Paul against Law, cache 0 | 283 | 18.7 | 51 ms |
| Paul against Law, cache 32 or 64 | 272 | 18.0 | 51 ms |

The mirror match's speed relied on the world-pose cache: 1,237 hits
against 709 misses, mostly one fighter reusing the other's identical idle
pose. Two different characters never share a pose, so the cache got no hits
at 32 or 64 entries, and it is off in the Law build.

A 10-image profile of the Law build showed each image cost about
1.35 M cycles (47 ms). The Master spent roughly 30% of that waiting for the
Slave. The Slave prepared P2's geometry (submit 225 K, projection 131 K and
skinning 110 K cycles per image) and then ran the 60 Hz simulation. Below
30 images/s an image carries three ticks, and the third ran before the scene
with both CPUs serialized, so a slow image made the next one slower.

### Changes

Each step was measured on the same probe; the oracles (`oracle-law-s1`
3000/3000, `oracle-paul-s2` 1383/1383, `oracle-paul-s3` 3000/3000) and the
host tests passed after every simulation change.

| Change | Images | Images/s |
|---|---|---|
| Starting point (cache 0) | 283 | 18.7 |
| Stage tables built after the actors, while the Slave ticks | 350 | 23.2 |
| Pose and motion math without 64-bit multiplies or variable shifts | 358 | 24.0 |
| Rotation copies as one block, not one `memcpy` per matrix | 381 | 25.6 |
| The Master skins half of P2's vertices alongside the Slave | 404–411 | 27.5 |

- **Stage after the actors** (`main.c`). The backdrop lines, VDP2 floor and
  line-scroll encoding (about 90 K cycles) ran on the Master before the Slave
  task started. Their move-probe inputs are now read first and the tables are
  built after the actors, while the Slave runs its ticks.
- **Exact 32-bit pose math** (`psx_pose.c`, `psx_motion.c`, `psx_ik.c`).
  `t3_psx_dot12` splits a vector into `high*4096 + low` so every product of a
  4.12 rotation fits 32 bits, and returns the same floor as the 64-bit form
  within the unit limit. `t3_psx_mul_rot` sums three products in 32 bits when
  every entry is within ±16384, and otherwise adds each product's floor and
  the carry of their low bits. The spline keeps only the low 32 bits of its
  weighted sum, so it multiplies modulo 2^32. Shifts by a variable count and
  signed shifts by a constant call libgcc on the SH-2; they became constant
  logical shifts with sign extension.
- **Block copies.** Struct assignment of the 18-byte rotation calls `memcpy`;
  the per-matrix copy loops in FK, IK and part composition are now one copy.
- **Shared skinning** (`characters.c`). Right after starting the worker, the
  Master skins `T3_MASTER_SKIN_PERCENT` (50) of P2's vertices and raises
  `lead_ready`. The worker skins the rest, waits for the flag, drops its cached
  lines for those vertices and projects. Skinning them before starting the
  worker gained nothing: it delayed the Slave by the same amount. 40 to 70%
  measure the same; 100% and 25% are slower.

`DEFERRED_TICKS` was kept at 2 here: deferring 3 measured the same once the
stage moved. It is now 3; see "Late images and the third tick" in
`THIRTY_IMAGE_GATE.md`.
`saturn_pc_profile.py callers` lists the executed call sites of a helper such
as `memcpy` or a libgcc shift per CPU, which found the copies and shifts.

### Where it stands

In the 30-field profile window each image now costs about 0.90 to 0.96 M
cycles: the Master is busy about 880 K (actor 0 geometry 490 K with its share
of P2's skinning, flush and painter order 150 K, stage 90 K) and so is the
Slave (P2 geometry 400 K, simulation about 400 K for two ticks). The two-field
budget is 953 K, so an image that slips to three fields gives the next one a
third tick. Measured dead ends: a register-blocked `mul_rot` (no change), and
the earlier reciprocal projections (see `ACTOR_PREPARATION_EXPERIMENT.md`
and `STRUCTURAL_PERFORMANCE_V2.md`). Skinning already uses `mac.l`.

The Slave's idle time (about 80 K cycles per image, its `slave_wait_request`
loop) comes before its task starts, not after it, so it cannot take over
Master work:

| Moved to the end or start of the Slave task | Images |
|---|---|
| None (control) | 408 |
| All stage tables (about 100 K cycles) at the end | 336 |
| Only the backdrop lines (about 45 K) at the start, Master waits on a flag | 380 |

With the whole stage on the Slave, the 30-field window held 12 images instead
of 15: a late Slave gives the next image a third tick, which makes it later
still. Both CPUs are within about 5% of the two-field budget, so the
remaining gap needs less work per image on both, chiefly in the SDK's face
submission (about 230 K cycles per actor, roughly 300 per face, with no
single hot spot) and flush.

The combat suite's cases report about 31 images/s for this build, but the
suite runs Ymir without cache emulation; only `--sh2-cache on` probes are
compared here. Windows of 300 ticks along the idle probe all measure 27 to
28 images/s.

The release check for this build is the `paul-law-release` workload
(`saturn_workload.py`): the Law shape (593 vertices, 773 faces, 754
textures) and hashed Law bank, mesh and textures, plus every two-Paul
release requirement.
