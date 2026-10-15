# Tekken 3 Saturn

An **unofficial, independently built Sega Saturn fighting-game port experiment**, inspired by the original *Tekken 3*. It consumes [LibSaturn](https://github.com/celsowm/libsaturn) as an **installed package**, like [ikemen-saturn](https://github.com/celsowm/ikemen-saturn). The long-term reference is [Tekken3Remake](https://github.com/n0madic/Tekken3Remake), not a dependency.

## Milestone 0: executable foundation

- A portable, integer-only, deterministic 60 Hz fight simulation.
- Two fighters, four distinct limb attacks, hit/guard stun, spacing, sidesteps, collision, health, a timer, and best-of-three rounds.
- Controller 1 versus simple deterministic CPU; controller 2 can take over P2.
- Sega Saturn frontend using LibSaturn's VDP1 scene-wide 3D painter, two independently skinned original low-poly fighters, a floor, and a HUD.
- 24-joint SKA1 source archive, nine authored animation clips, red/blue costume palettes, and shared geometry.
- Native host tests with no emulator, ROM, Godot, or Saturn toolchain required.

**This is a gameplay/renderer prototype, not a claim of Tekken 3 accuracy.** The models are intentionally original placeholders. Its procedural fighters and animations are original LibSaturn example content, **not** Tekken 3 models or moves; no copyrighted Namco assets or PS1 code are included.

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
