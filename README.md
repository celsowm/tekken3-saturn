# Tekken 3 Saturn

An **unofficial, independently built Sega Saturn fighting-game port experiment**, inspired by the original *Tekken 3*. It consumes [LibSaturn](https://github.com/celsowm/libsaturn) as an **installed package**, like [ikemen-saturn](https://github.com/celsowm/ikemen-saturn). The research reference [Tekken3Remake](https://github.com/n0madic/Tekken3Remake) is pinned as a **Git submodule**, never linked into the Saturn game.

## Upstream research submodule

The blue folder **[`third_party/Tekken3Remake`](third_party/Tekken3Remake)** is a pinned Git submodule, following the same layout idea as [wipeout-recomp](https://github.com/alexbeavs-ps1-ports/wipeout-recomp). It points to [n0madic/Tekken3Remake](https://github.com/n0madic/Tekken3Remake), rather than copying another project's Git history or source into this repository.

```sh
git clone --recurse-submodules https://github.com/celsowm/tekken3-saturn.git
# For an existing clone:
git submodule update --init --recursive
# Deliberately update to a new upstream revision, review, and commit the pin:
git submodule update --remote third_party/Tekken3Remake
```

**License boundary:** the submodule uses **PolyForm Noncommercial 1.0.0** with the upstream project's own required attribution, not this repository's MIT license. It is a separate research reference, not compiled/linked by the Saturn CMake build, and its source must not be silently copied or relicensed. Users must comply with the upstream license for uses of that source. No Tekken 3 retail data is part of the submodule checkout.

## Milestone 0: executable foundation

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

The gameplay engine in `src/game/` is a standalone porting foundation, **not** imported Godot or PS1 code. The [Tekken3Remake](https://github.com/n0madic/Tekken3Remake) repository uses **PolyForm Noncommercial 1.0.0**, with required attribution. Do not copy its source into this project without respecting its licence. Reference research and trace-based comparisons can be added separately.

No copyrighted game assets or proprietary disc images are stored here; future import tools must operate on a **user-supplied, legally obtained game copy**, with output excluded from Git. Namco/Bandai Namco own Tekken 3; this project is unaffiliated.

Animations: idle, walk, run, jump, punch, kick, guard, hit and knockdown. Playback follows the deterministic fight tick; bone deformation runs on the current rendering code path. **Saturn boot/real hardware performance are not yet validated.**

See [docs/ROADMAP.md](docs/ROADMAP.md) for stages, accuracy gates, and Saturn hardware targets.

## CI-generated Saturn ISO and validation

The [Saturn firmware and disc image](https://github.com/celsowm/tekken3-saturn/actions/workflows/saturn.yml) workflow uses the exact pinned LibSaturn source used for the original skeletal asset generator, GCC 13.2 targeting `sh2eb-elf`, an installed LibSaturn CMake package, and `libsaturn_add_disc(... IP_TEMPLATE_KIND native)`. On success it uploads `tekken3_saturn.iso`, `.bin`, `.cue`, `.elf`, `.app.bin`, resource/boot reports, and the asset manifest.

The native IP generator still uses Sega security/region payload bytes extracted from the LibSaturn-distributed Yaul *template*; no Yaul runtime is linked. Disc validation proves metadata, initial application load bytes, and Mode 1 sector data. It **does not** establish that the console BIOS, SAROO, Satiator, or an optical drive can boot the image.

The playable build has **no external ROM requirement** and must never ship copyrighted Tekken data. The generated humanoids are original LibSaturn content.

### Hardware/performance instrumentation

Press **Z** during gameplay for the lightweight diagnostic overlay (loop rate x10, scene faces, VDP1 command usage, frame work time). The ELF also exports a fixed-layout `g_t3_diagnostics` record defined in `src/saturn/diagnostics.h` for emulator memory inspection. It includes VBlank count, simulation ticks, VDP1 command count/capacity, culled/rejected faces, late swaps and draw timeouts.

These are instrumented counters only: **no emulator/real-console FPS measurement has been reported**. Rendering frame rate and presentation rate differ when the VDP1 spends longer than one display field drawing.
