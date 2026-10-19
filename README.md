# Tekken 3 Saturn

An **unofficial Sega Saturn port project for Tekken 3**, using the readable implementation and original-game research in [n0madic/Tekken3Remake](https://github.com/n0madic/Tekken3Remake) as its implementation reference. Its documented combat, motion, model and game-flow rules guide the portable C implementation, with differential checks against its research tools. The Saturn runtime uses [LibSaturn](https://github.com/celsowm/libsaturn) as an **installed package**.

**Source reference: [Tekken3Remake @ e59c4da](https://github.com/n0madic/Tekken3Remake/tree/e59c4da179f39b4e915ccdc547567c8479a9f611)** — tracked at [`third_party/Tekken3Remake`](third_party/Tekken3Remake) as a Git submodule. GitHub displays that directory as a linked repository with its pinned commit. The Godot runtime is separate from the Saturn executable; the submodule supplies the source reference and host-side comparison tools.

## Upstream research submodule

The blue folder **[`third_party/Tekken3Remake`](third_party/Tekken3Remake)** is a pinned Git submodule, following the same layout idea as [wipeout-recomp](https://github.com/alexbeavs-ps1-ports/wipeout-recomp). It points to [n0madic/Tekken3Remake](https://github.com/n0madic/Tekken3Remake), rather than copying another project's Git history or source into this repository.

```sh
git clone --recurse-submodules https://github.com/celsowm/tekken3-saturn.git
# For an existing clone:
git submodule update --init --recursive
# Deliberately update to a new upstream revision, review, and commit the pin:
git submodule update --remote third_party/Tekken3Remake
```

**License boundary:** the submodule uses **PolyForm Noncommercial 1.0.0** with the upstream project's required attribution. Its Godot application is not compiled by the Saturn build. The C adaptations in `src/game/move_clock.c`, `move_lifecycle.c`, `psx_ground.c`, `psx_match.c`, `psx_body.c`, `psx_exact_contact.c`, `psx_hit_transaction.c` and `psx_round.c` retain that license; see [NOTICE.md](NOTICE.md). The local MIT license does not replace those terms. No Tekken 3 retail data is part of the submodule checkout.

## Milestone 0: executable foundation

The [current private milestone](docs/NEXT_PLAYABLE_PROGRESS.md) renders two
original Paul models with converted textures and independently controlled
DIVMOT poses, confirmed through Mednafen CD boot and headless Ymir tests.
Original USA Paul mirror-match input/health/defense observations have also been
captured in PCSX-Redux and cross-checked against paused RAM. Live Saturn combat
still uses the provisional match below; original combat parity is pending.

- A portable, integer-only, deterministic 60 Hz fight simulation.
- Two fighters, four distinct limb attacks, hit/guard stun, spacing, sidesteps, collision, health, a timer, and best-of-three rounds.
- Controller 1 versus simple deterministic CPU; controller 2 can take over P2.
- Sega Saturn frontend using LibSaturn's VDP1 scene-wide 3D painter, two independently skinned original low-poly fighters, a floor, and a HUD.
- 24-joint SKA1 source archive, nine authored animation clips, red/blue costume palettes, and shared geometry.
- Native host tests with no emulator, ROM, Godot, or Saturn toolchain required.

**This is a gameplay/renderer prototype, not a claim of Tekken 3 accuracy.** The models are intentionally original placeholders. Its procedural fighters and animations are original LibSaturn example content, **not** Tekken 3 models or moves; no copyrighted Namco assets or PS1 code are included.

## 104 original PlayStation input sequences

An offline importer can identify the two **PS1 special command tables**
(63 + 41 entries) from the private USA SLUS executable. It checks the known
executable fingerprint and validates every pointer, window and command word
before a local opt-in build embeds their command patterns into the Saturn
move debugger. See [DIVMOT and original input extraction](docs/DIVMOT_LOGIC.md).
The retail input data is never included in public CI or releases, and the
combat itself is not yet 1:1.

## Local PS1 IK table exporter

The new `scripts/extract_psx_ik_tables.py` tool converts five
explicitly verified offsets from the **user's own USA executable**
into private `psx_trig.bin` and `psx_ik.bin` for the Saturn IK
diagnostic. It validates bounds, source SHA-256 and basic fixed-point
coefficients without publishing original data. See the
[private lookup export instructions](docs/DIVMOT_LOGIC.md).

## Private PS1 animation oracle comparison

A new endian-independent, portable `t3_psx_trace_encode` serializes
the 18 body joint transforms in PS1-native coordinates (564 bytes per
fighter/frame). The local `scripts/compare_psx_pose_traces.py` tool
compares captured Saturn frames against a separately captured original
PS1 emulator oracle in JSONL, detecting frame, move, per-axis origin
and 3×3 Q12 rotation differences. Strict matching is the default;
optional tolerances are for diagnosing numerical divergences. The
binary format and synthetic tests are documented in
[the pose-oracle workflow](docs/DIVMOT_LOGIC.md).
A private Saturn ELF now exposes debugger symbols
`g_t3_pose_trace` and `g_t3_pose_trace_epoch` for recording both
fighter traces at every simulation tick (with an even/odd write guard).
Private original combat traces are now captured and independently RAM-checked;
complete paired Saturn/PS1 joint-trace parity remains pending.

## Authored PS1 root movement and heading

The portable `src/game/psx_root.[ch]` now evaluates original
49-channel root displacement with signed 16-bit wrap, KMD scale,
fighter facing and heading. Private pose previews use this scaled
root motion with a clearly neutral *model-local* arena anchor;
production combat still requires calibrated PS1 fighter world
coordinates. [Geometry and root details](docs/DIVMOT_LOGIC.md).

## Authored PS1 hit sweeps and animated hurt zones

`src/game/psx_contact.[ch]` constructs the **14 original defensive
cylinders** directly from complete KMD joint poses (the first 18
PS1 joint blocks), and preserves exact previous **world-space hit
segment endpoints** across consecutive DIVMOT attack frames. It
provides a bounded geometric segment/zone contact query with host
regressions. The optional private Saturn debug build now reports
model-local hurt-zone readiness (diagnostics v9). A real damage
decision remains disabled until both fighters' original world
roots/headings are calibrated. See
[PS1 contact implementation](docs/DIVMOT_LOGIC.md).

## Experimental PRIVATE original-pose rendering

With KMD geometry, original DIVMOT animation data and user-supplied
PS1 sine/IK tables, `-DT3_ENABLE_PSX_SKIN_PREVIEW=ON` now allows
the original 18-slot pose to drive the **LibSaturn CPU skinning mesh**
instead of merely being reported in diagnostics. Only complete and
numerically safe poses are applied; all regular/procedural builds
keep their existing animation. Real PS1 joint parity and the
fighting-engine integration are still pending; see
[PS1 KMD skin preview instructions](docs/DIVMOT_LOGIC.md).

## Original PS1 four-limb inverse kinematics

`src/game/psx_ik.[ch]` now calculates the **eight missing arm/leg
rotation matrices** from DIVMOT targets and swivel angles, completing
the 18-slot pose solver for valid inputs. It uses original fixed-point
rules and private normalized PS1 lookup coefficients. The private
Saturn move probe reports IK slot validity; missing or singular limbs
are marked unavailable, not guessed. **Real 1:1 PS1 oracle parity,
model skin animation and live hit resolution remain pending**.
See [IK algorithm and private tables](docs/DIVMOT_LOGIC.md).

## PS1 Euler FK rotation and model hierarchy

The portable C core now calculates the **ten genuine PS1 Euler matrix
slots** using GTE-style Q12 operations and a private sine lookup window.
A second routine composes known KMD parent/offset chains and marks limbs
requiring IK as unavailable. The optional private Saturn build exposes
the Euler FK validity mask through emulator telemetry version 5.
This is a tested foundation for the remaining inverse-kinematic solver,
**not yet full animated joints**. See [FK scope](docs/DIVMOT_LOGIC.md).

## Original DIVMOT animation stream reader

`src/game/psx_motion.[ch]` now parses section-6 tagged PS1 animation
streams with **49 pose channels**, including reverse-time channels,
linear/per-frame values and **lookup-table-based keyed splines**.
The real spline coefficient tables are not redistributed. Local
`scripts/divmot_to_logic.py --with-motion --with-hitboxes` preserves
the original streams in a private analysis bank. This gives us
the scalar poses, **not yet the 18-slot FK/IK solver or 24 real PS1
world joints**. See [motion import scope](docs/DIVMOT_LOGIC.md).

## PS1 world-space collision primitives

New `src/game/psx_collision.[ch]` supports integer Q12 world transforms
of baked attack segments, explicit previous-frame roots for swept punches
and kicks, and **14 original-layout hurt cylinders** from PS1 joint centres.
The vertical cylinder test uses bounded 64-bit arithmetic without
floating point or allocation. Deterministic synthetic tests run in host CI.
It is **not yet wired into damage or a bit-exact MIPS oracle** because the
character animation/joint poses still need to be ported. Details:
[original move logic and collision geometry](docs/DIVMOT_LOGIC.md).

## PS1 attack geometry: experimental importer

A new `src/game/attack_records.[ch]` decoder reads **authored baked
strike segments from DIVMOT section 9**, with packed 40-bit XYZ points,
per-frame windows, first/second hit segments, and prior-frame sweep
markers. Use the optional `--with-hitboxes` switch in
`scripts/divmot_to_logic.py` to retain section 9 for a **private**
Saturn move-VM diagnostic. It reports active attack segment counts and
decode errors. It **does not yet make the game use original hitboxes**;
joint transforms, body hurt regions, PS1-only builtin records and
reactions are upcoming. Full instructions:
[DIVMOT logic and hitbox research](docs/DIVMOT_LOGIC.md).

## Original Tekken 3 combat-data foundations

The portable C layer now has a validated [DIVMOT interpreter](docs/DIVMOT_LOGIC.md)
for the original 56-byte move rows, 12-byte branch records, 4,023-slot
indices, basic input command/history matching and a deterministic move VM.
These are tested independently of Saturn hardware. An **optional private local-bank build** can run the move VM as a live Saturn diagnostic without replacing gameplay. The playable fallback is
still a **prototype**, not a one-to-one reimplementation. Raw imported
move banks never enter Git; the local compactor keeps only move logic.

## Import data from a local Tekken 3 USA disc

The source can be a BIN/ISO/ZIP stored locally or the explicit user-specified archive.org ZIP address. The importer locates the PS1 `SLUS_004.02` executable and the 303-entry `TEKKEN3.BNS` archive; it verifies the executable fingerprint and extracts only explicitly selected records, without modifying the disc.

```sh
python3 scripts/psx_disc_import.py \
  --source '/path/to/Tekken 3 (USA).zip' \
  --scratch-dir /mnt/large-disk/tmp \
  --extract-ids 36,71,73 \
  --stage-ids 56
```

The `--stage-ids 56` option creates a first **untextured stage panorama OBJ** as a geometry research preview. It does not yet convert TIM materials, fighter skins, or motion into playable Saturn assets. Outputs live under the gitignored `assets/imported/` directory. Run the importer **locally**, not in CI. See [PSX disc importer instructions](docs/PSX_DISC_IMPORT.md).

## Testing a local KMD inside the Saturn game (experimental)

After extracting your own `raw/071.bin`, the separate Saturn build can display
the original KMD geometry instead of the procedural stand-ins. **Only a static
bind pose and generated neutral/red debug textures are present**. The gameplay
remains the independently written prototype, with no original Tekken 3 moves.

```sh
export LIBSATURN_PREFIX=/path/to/libsaturn-install
cmake --preset saturn \
  -DLIBSATURN_SOURCE_DIR=/path/to/libsaturn \
  -DT3_LOCAL_KMD="$PWD/assets/imported/tekken3-usa/raw/071.bin"
cmake --build --preset saturn
```

Use a **separate build directory or clean CMake cache** when switching between
the original procedural asset and a local KMD. The generated C file and BIN/ISO
contain copyrighted Namco model geometry, so keep them private; **never upload
those artifacts to GitHub, CI, releases or public file-sharing services**.
Default automated builds do not set `T3_LOCAL_KMD`.

## Extracting a real KMD into LibSaturn's SKA1 format

```sh
python3 scripts/kmd_to_ska.py \
  --kmd assets/imported/tekken3-usa/raw/071.bin \
  --libsaturn-source /path/to/libsaturn \
  --output assets/imported/tekken3-usa/paul
```

This emits a **static-bind, neutral-texture SKA1 and untextured OBJ preview**.
It carries the KMD 22-part skeleton and original seam blending as separate
joint-local skin anchors, but **not** Tekken textures, motions, costume
variants, or a verified visual pose. See [the KMD conversion limitations](docs/KMD_IMPORT.md).
The normal Saturn game still uses its freely redistributable procedural
humanoids; original commercial content never enters CI.

## Build and test natively

The portable core uses LibSaturn's standalone `saturn/int_math.h`; no Saturn
runtime, emulator or cross compiler is needed. Keep the pinned LibSaturn checkout
at `../lib-saturn`, or pass `-DT3_SATURN_MATH_INCLUDE_DIR=/path/to/libsaturn/include`
to configure. CI checks out the pinned SDK explicitly.

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

## Build for Sega Saturn

Install LibSaturn (using its CMake install/package instructions), the `sh2eb-elf` toolchain, and ISO utilities first. The firmware links only the **public installed package**. Asset preparation separately uses the original rig generator in a LibSaturn **source checkout**.

```sh
export LIBSATURN_PREFIX=/path/to/libsaturn-install
cmake --preset saturn -DLIBSATURN_SOURCE_DIR=/path/to/libsaturn-source
cmake --build --preset saturn
```

The build compiles an original low-LOD articulated 24-joint SKA1 asset into the executable, and writes an asset manifest under `build/saturn/generated/characters/`. Requires Python dependencies of the LibSaturn generator; nothing is fetched from a ROM or proprietary disc.

Disc artifacts are generated in `build/saturn/disc/`. Firmware startup and boot layout are provided by LibSaturn; no Yaul or JO Engine build-tree dependency is introduced.

### Saturn controls

D-pad LEFT/RIGHT: move away/toward the rival; UP/DOWN: sidestep.
X: left punch, Y: right punch, A: left kick, B: right kick.
L: guard; START: reset match. Connect a second pad to control P2.

## Scope, fidelity, and legal terms

The gameplay engine in `src/game/` is a standalone C porting foundation. The original move-clock advance path is adapted from the pinned [Tekken3Remake](https://github.com/n0madic/Tekken3Remake) research under **PolyForm Noncommercial 1.0.0**, with its required attribution and license URL in the file and [NOTICE.md](NOTICE.md). The Godot application and proprietary PS1 executable are not compiled into the Saturn application.

No copyrighted game assets or proprietary disc images are stored here; future import tools must operate on a **user-supplied, legally obtained game copy**, with output excluded from Git. Namco/Bandai Namco own Tekken 3; this project is unaffiliated.

The procedural asset includes idle, walk, run, jump, punch, kick, guard, hit and knockdown clips. The private Paul build uses original DIVMOT poses. CD boot is verified in Mednafen; original combat integration and real-hardware performance remain unverified. See the [current milestone evidence](docs/NEXT_PLAYABLE_PROGRESS.md).

See [docs/ROADMAP.md](docs/ROADMAP.md) for stages, accuracy gates, and Saturn hardware targets.

## CI-generated Saturn ISO and validation

The [Saturn firmware and disc image](https://github.com/celsowm/tekken3-saturn/actions/workflows/saturn.yml) workflow uses the exact pinned LibSaturn source used for the original skeletal asset generator, GCC 13.2 targeting `sh2eb-elf`, an installed LibSaturn CMake package, and `libsaturn_add_disc(... IP_TEMPLATE_KIND native)`. On success it uploads `tekken3_saturn.iso`, `.bin`, `.cue`, `.elf`, `.app.bin`, resource/boot reports, and the asset manifest.

The native IP generator still uses Sega security/region payload bytes extracted from the LibSaturn-distributed Yaul *template*; no Yaul runtime is linked. Disc validation proves metadata, initial application load bytes, and Mode 1 sector data. It **does not** establish that the console BIOS, SAROO, Satiator, or an optical drive can boot the image.

The playable build has **no external ROM requirement** and must never ship copyrighted Tekken data. The generated humanoids are original LibSaturn content.

### Hardware/performance instrumentation

Press **Z** during gameplay for the lightweight diagnostic overlay (loop rate x10, scene faces, VDP1 command usage, frame work time). The ELF also exports a fixed-layout `g_t3_diagnostics` record defined in `src/saturn/diagnostics.h` for emulator memory inspection. It includes VBlank count, simulation ticks, VDP1 command count/capacity, culled/rejected faces, late swaps and draw timeouts.

The [harness-led measurements](docs/SATURN_PERFORMANCE_PLAN.md) now distinguish
actual application submissions, logic ticks and VDP lifecycle events. The
earlier wide-camera paired experiment measured 8.43 submissions/s and 33.72 logic
ticks/s. With the current renderer package, the closer camera measures 9.47
submissions/s and 37.87 logic ticks/s in its neutral interval; the 30/60
performance gate remains open. These
are emulator observations. Automatic VDP1 redraws and framebuffer swaps do not
establish distinct-picture cadence.

With LibSaturn `f11b277bed2ddda963fa3dba0da08656baf08322` installed, configure
`-DT3_PARALLEL_GEOMETRY=ON` to prepare the second actor on Slave SH-2. Add
`-DT3_VERIFY_PARALLEL_GEOMETRY=ON` to compare every worker result with serial
preparation; this adds diagnostic overhead and must stay off for timing.
Both modes preserve the original model and global painter order. The generic
serial path remains available with the first option off.

The default camera now moves with the fighter midpoint and increases its
distance with their separation. `-DT3_CLOSE_CAMERA=OFF` retains the old wide
view for comparisons. This changes presentation only; gameplay scale is fixed.

The HUD always shows `FRAME <number>` between the win counters. This is the
one-based render submission serial (the diagnostic `frame_serial`), independent
of VBlanks and simulation ticks; START does not reset it.
See [reusable validation commands](docs/SATURN_VALIDATION_SCRIPTS.md) to inspect
saved runs, compare cadence and wait for CI on the exact commit.

For cost isolation, `-DT3_PERF_MODE=1` keeps simulation but omits scene/HUD
drawing; mode `2` renders one actor and mode `0` is the normal two-actor game.
Combine mode 0 or 2 with `-DT3_RENDER_BENCH_TICK=120` for frozen-pose rendering.
`-DT3_PERF_SAMPLES=ON` adds a bounded stage timing ring (diagnostic overhead).
Capture it with `saturn_probe.py --dump-symbol g_t3_perf`, then decode the
result with `python3 scripts/analyze_saturn_stages.py <capture>/symbol-0.bin`.
These diagnostic modes remove work and cannot qualify playable performance.
`scripts/run_saturn_stage_matrix.py --baseline <optimized-build> --bios <bios> --out <fresh-private-directory>`
builds and captures all four diagnostic cases, preserving compiler optimization
settings. See the [stage measurements](docs/PAUL_STAGE_MEASUREMENTS.md) for
results and limits. The [subsequent runtime experiments](docs/PAUL_RUNTIME_EXPERIMENTS.md)
retain a 20% skin-stage reduction and the accumulated renderer update reduces
frozen two-actor work from 64 to 49 ms. Full-game cadence improves from 8.18 to
9.76 submissions/s and from 32.73 to 39.03 logic ticks/s in the neutral interval.
The [nested simulation profiler](docs/SIMULATION_PROFILING.md) now separates
pose, movement, contact and branch costs by tick, with reusable capture scripts
and explicit instrumentation limits.

## FK/IK differential oracle

`tools/psx_pose_eval.c` and
`scripts/diff_psx_pose_solver.py` compare all valid PS1 Q12 rotation
matrix components of a batch of 49-channel DIVMOT poses with the
**pinned upstream Tekken3Remake research implementation**. Host CI
uses synthetic data only and reports precise per-slot mismatches.
See [the offline solver harness](docs/DIVMOT_LOGIC.md).
A match establishes parity against upstream research—not real-hardware
MIPS parity or a fully rendered 1:1 game.

### Private DIVMOT-to-pose differential pipeline

`tools/psx_motion_export.c` now lets a host build sample an
original **local** DIVMOT move slot into the 49-channel binary
format consumed by the independent C/Python solver differential.
It supports shared `divmot99` animations and optional private
spline coefficients, with strict bounds and no bundled game data.
See [end-to-end private workflow](docs/DIVMOT_LOGIC.md).

### Automated motion coverage

`scripts/batch_psx_pose_oracle.py` selects distinct DIVMOT animations
across the local character bank, decodes representative frames via the
C exporter, and tests the portable 18-slot FK/IK solver against the
pinned upstream research oracle. It reports skipped moves and
per-slot numerical differences without committing retail data.
See [batch FK/IK audit](docs/DIVMOT_LOGIC.md).


### Original combat collision observer

The portable `psx_shadow_combat` module now checks **both fighters'
original attack segments and KMD body cylinders** against the same
frame's world transforms. It tracks swept endpoints and contact edges
without writing to health or the match state; genuine PS1 fighter world
placement must be supplied explicitly. See [the shadow observer
scope](docs/DIVMOT_LOGIC.md).


For **private emulator testing**, `-DT3_ENABLE_PSX_SHADOW_CONTACT=ON`
exports debugger-controlled world roots and read-only contact
telemetry (version 10). Missing or unsynchronized roots yield
an unavailable status, not a guessed hit. Prototype HP/blocks remain
unchanged. Details: [PS1 shadow debugger inputs](docs/DIVMOT_LOGIC.md).

### PS1 hit classification and damage preview

`src/game/psx_hit_damage.[ch]` now reconstructs original-style
post-contact hit categories and integer damage calculations for all
14 defensive body zones, including guard, chip, counter hit,
close reactions and airborne juggles. It requires genuine runtime
classification facts and private close-reaction lookup metadata;
it **does not change gameplay HP** or invent missing PlayStation state.
See [hit candidate research scope](docs/DIVMOT_LOGIC.md).

### Original reaction record selection

The new `psx_reaction` decoder identifies original hit-response
move slots for directional hits, guarded hits, counter hits, and
knockdowns using the PS1 42-byte reaction table format. It also
recognizes throw-victim slot references and respects original
attack-class gating. Missing air/fixed special tables remain
**unresolved**, and no prototype gameplay state is altered.

### Experimental PS1 HitApply state preview

The portable `psx_hit_apply` module now calculates **read-only**
original normal-mode HitApply outcomes from verified PS1 hit slots:
move-event extra damage (including throw victims), two-slot arbitration,
Q16 health, KO/chip conversion and reaction selection when original
tables are present. The provisional playable match is untouched.
See [HitApply scope](docs/DIVMOT_LOGIC.md).

### Original DIVMOT move events

`psx_move_events` decodes frame commands 11–13 (throw and
damage-exchange events) from **private** DIVMOT section 4 while
enforcing the original previous/current pose-frame window.
`scripts/divmot_to_logic.py --with-events` preserves that section
without requiring motion or hitbox data. No original data is
distributed and the prototype's health is not altered.
See [move-event format and tests](docs/DIVMOT_LOGIC.md).

## Two-fighter PS1 post-contact combat pipeline

`src/game/psx_combat_pipeline.[ch]` now connects the independent
geometric collision observer, original HitTest eligibility, HitClassify,
HitDamage, signed 16.16 HitApply health projection, and optional
reaction selection. Both fighters are processed from the same immutable
pre-hit snapshot, so simultaneous trades are not dependent on player
iteration order. Original defender hit-slot occupancy must agree with
the gate's free-slot mask; missing guard, live damage, pose, or original
reaction-table facts are never guessed.

This remains **a read-only research pipeline**; normal Saturn combat
HP, block, KO and animation state are unchanged. Host regression checks
trades, posture gating, guarded hits, unavailable geometry and atomic
failure. Details are in `docs/DIVMOT_LOGIC.md`.

## Read-only PS1 hitstun, knockdown and recovery preview

The portable [post-hit transition harness](docs/PSX_STATE_TRANSITIONS.md)
now consumes the simultaneous HitTest → HitApply preview and an explicitly
supplied per-defender original-game timing policy. It previews hitstun,
blockstun, knockdown, recovery, neutral return, combos and KO without changing
MoveVM or live Saturn combat. Missing original reaction records, phase timing,
recovery destinations or combo rules fail closed; **this is not PS1 frame parity**.
Synthetic host tests exercise trades, reaction expiry and absent data.

## Original MoveVM projection and PS1 combat trace comparison

The [MoveVM post-hit bridge](docs/PSX_STATE_TRANSITIONS.md)
resolves reaction move slots against original local and shared DIVMOT
banks. Each fighter's next animation pose frame must come from an
external observation; a stun timer is not an animation timer.
Two-player projections are atomic and do not alter gameplay.

A [strict JSONL comparator](scripts/compare_psx_combat_traces.py)
is available for frame-by-frame PS1-versus-Saturn source oracle
validation, with a synthetic regression suite in host CI. Real
Paired Saturn/PlayStation timing and combat parity remain pending.


## Original-USA PlayStation combat oracle capture

A **read-only** [PCSX-Redux VSync recorder](scripts/pcsx_redux_capture_usa.lua)
and an independent [2 MiB RAM dump decoder](scripts/psx_ram_oracle.py)
now export true original-game *observations* (when run locally on a
user-owned USA SLUS_004.02 disc) into the strict paired combat JSONL
format. Both use the explicit USA guest-memory point mapping from the
pinned upstream research. Invalid fighter data, mixed fights, and
nonconsecutive frame counters fail closed. LuaJIT syntax and synthetic
RAM decoding are tested in host CI.

The [event analyzer](scripts/analyze_psx_combat_oracle.py) reports
observed move-slot/health/KO/pose-stall transitions without inventing
stun durations or other original PS1 rules. Private original gameplay traces
now include measured movement, attack, health and defense transitions.
Read-only CPU phase hooks now establish where these observations fall within
the original frame. Eight real contacts pass the portable C damage/health and
reaction-slot audit. **Live Saturn combat integration/parity remains pending.**
Follow the complete [private capture guide](docs/PS1_COMBAT_ORACLE_CAPTURE.md).
