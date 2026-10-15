# Tekken 3 Saturn

An **unofficial, independently built Sega Saturn fighting-game port experiment**, inspired by the original *Tekken 3*. It consumes [LibSaturn](https://github.com/celsowm/libsaturn) as an **installed package**, like [ikemen-saturn](https://github.com/celsowm/ikemen-saturn). The long-term reference is [Tekken3Remake](https://github.com/n0madic/Tekken3Remake), not a dependency.

## Milestone 0: executable foundation

- A portable, integer-only, deterministic 60 Hz fight simulation.
- Two fighters, four distinct limb attacks, hit/guard stun, spacing, sidesteps, collision, health, a timer, and best-of-three rounds.
- Controller 1 versus simple deterministic CPU; controller 2 can take over P2.
- A Sega Saturn frontend using LibSaturn's VDP1 scene-wide 3D painter, procedural block-out fighters, a floor, and a HUD.
- Native host tests with no emulator, ROM, Godot, or Saturn toolchain required.

**This is a gameplay/renderer prototype, not a claim of Tekken 3 accuracy.** The models are intentionally original placeholders. It has no original characters, animations, sound, Namco assets, or PS1 code yet.

## Build and test natively

```sh
cmake -S . -B build/host -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/host
ctest --test-dir build/host --output-on-failure
```

## Build for Sega Saturn

Install LibSaturn (using its CMake install/package instructions), the `sh2eb-elf` toolchain, and ISO utilities first. The firmware build uses only the **public, installed package**.

```sh
export LIBSATURN_PREFIX=/path/to/libsaturn-install
cmake --preset saturn
cmake --build --preset saturn
```

Disc artifacts are generated in `build/saturn/disc/`. Firmware startup and boot layout are provided by LibSaturn; no Yaul or JO Engine build-tree dependency is introduced.

### Saturn controls

D-pad LEFT/RIGHT: move away/toward the rival; UP/DOWN: sidestep.
X: left punch, Y: right punch, A: left kick, B: right kick.
L: guard; START: reset match. Connect a second pad to control P2.

## Scope, fidelity, and legal terms

The gameplay engine in `src/game/` is a standalone porting foundation, **not** imported Godot or PS1 code. The [Tekken3Remake](https://github.com/n0madic/Tekken3Remake) repository uses **PolyForm Noncommercial 1.0.0**, with required attribution. Do not copy its source into this project without respecting its licence. Reference research and trace-based comparisons can be added separately.

No copyrighted game assets or proprietary disc images are stored here; future import tools must operate on a **user-supplied, legally obtained game copy**, with output excluded from Git. Namco/Bandai Namco own Tekken 3; this project is unaffiliated.

See [docs/ROADMAP.md](docs/ROADMAP.md) for stages, accuracy gates, and Saturn hardware targets.
