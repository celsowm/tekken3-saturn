# Building the Paul-against-Law disc

The repository holds no retail data. A disc is built locally from your own USA
Tekken 3 import (`scripts/psx_disc_import.py`, then the `build_local_*` and
`extract_psx_*` scripts, each run by CMake from the private paths below).

The 2026-10-27 gate in `assets/imported/parity-effects-pool-gate/summary.json`
passes 66/66 and 76/76 host tests, 26 live combat cases and a 3,000-tick
independent-camera/effect-pool reference.
Playable ELF: `8faa85fa307d59c4f8bee592d5182ded72ad3b481a4aacc5cc0d823722402e89`.
This is finite gameplay/reference coverage, not a completed 1:1 port;
[EFFECT_POOL_INTEGRATION.md](EFFECT_POOL_INTEGRATION.md) records its passing
varied-input native reference, standalone prefilled-RAM result and coverage
limits. Native audio, particle drawing, image and cadence qualification remain
separate. The preceding `parity-owned-camera-gate` result belongs to its
recorded pre-pool ELF `79c0324f…`.

1. Toolchain and SDK: `PATH=$HOME/dev/saturn-tools/bin:$PATH`, one LibSaturn
   prefix (`~/dev/saturn-prefix`, rebuilt in place from `~/dev/lib-saturn`).
2. Configure. `scripts/print_release_configure.py <existing build> <new build>`
   prints the full command from a working build directory; the options that
   matter are `T3_LOCAL_*` (private inputs: both fighters' meshes, banks and
   cameras, the stage, the physics, reaction and AI tables, `T3_LOCAL_SFX`),
   the renderer set (`T3_SLAVE_SIMULATION`, `T3_FUSED_ACTOR_PREPARATION`,
   `T3_VDP2_FLOOR`, `T3_PSX_CAMERA`, ...) and optionally `T3_TITLE_SCREEN=ON`.
3. `cmake --build <build>` writes `<build>/disc/tekken3_saturn.cue` (+ `.bin`).
4. Gate before sharing a disc image with anyone (it contains retail data and
   must not be published):
   - `ctest --test-dir build/host` and the oracle cases of
     `docs/FIGHT_ORACLE.md` (`build/host`, `build/host-law`);
   - `python3 scripts/saturn_probe.py --workload paul-law-release ...`
     (`first_error` 0, `loop_rate_x10` near 300);
   - `python3 scripts/saturn_combat_suite.py --workload paul-law-release
     --with-ko --with-timeout ...` (26 cases, including throw and throw damage).

   `scripts/saturn_regression.py` runs the CTest builds and the 26-case suite
   in one command and writes a single JSON verdict; see
   [SATURN_VALIDATION_SCRIPTS.md](SATURN_VALIDATION_SCRIPTS.md#run-the-release-gate).
   The standalone commands remain the reference for what each gate asserts.

Controls: D-pad move/guard, X/Y punch, A/B kick, START resets the match (the
title screen, when built in, waits for START first). With port 2 empty the CPU
plays Law and gets stronger after each match you win; with a pad in port 2 it
is a 2 player match.

Set `T3_ORIGINAL_HUD=ON` and `T3_LOCAL_HUD_EXE` to the private USA
`SLUS_004.02` to build the original fight HUD bar, mark and timer sprites into
the disc. CMake runs `scripts/extract_psx_hud.py` and packs the private
`HUD.BIN`; the ignored `assets/imported/tekken3-usa/hud/manifest.json` records
the source hash, dimensions and palettes when the script is run directly.
The name plates, normal round/result messages and REPLAY use the original
sprites/fonts (schema-4 HUD.BIN). Production matches now include the original
first-round intro; input becomes active at its frame 151. The combat suite
measures its input offset from the first controllable fight state. This option needs
`T3_LOCAL_POSE_CACHE_ENTRIES=128` in the measured release configuration to
fit High Work RAM. See [ORIGINAL_HUD.md](ORIGINAL_HUD.md) for the build and
runtime evidence.
See [FIRST_ROUND_START.md](FIRST_ROUND_START.md) for initialization, text and
HUD-phase comparisons and the remaining qualification gates.
See [REVERSAL_PROGRESS.md](REVERSAL_PROGRESS.md) for the original reversal
phase, its private tag-list input and the expanded reference schema. Current
host fixture data must embed bank bytes (`cold=False`) and use the model
parts for the original trace's costumes.

## Original hit sparks (optional)

`T3_PARTICLES` is ON by default in a configuration that already has the original
HUD, camera, VDP2 floor, owned match and private assets (and OFF otherwise);
pass `-DT3_PARTICLES=OFF` to opt out or `=ON` to force it. It forces the particle chain, the original
vertical viewport for stage and fighters, and the memory settings that link on
stock RAM (see `docs/PARTICLE_INTEGRATION_PLAN.md`). The gate for it is
`saturn_regression.py --particle-build <fixture build> --particle-real-build
<the T3_PARTICLES build>`; the fixture build adds
`T3_PARTICLE_SPRAY_SELFTEST`, `T3_PARTICLE_CELL_CHECK` and
`T3_PARTICLE_DROP_SELFTEST`. Pass `-DT3_HIGH_GEOMETRY_SCRATCH:BOOL=OFF`
explicitly when cloning with `print_release_configure.py`, which omits OFF
options. Particle pixels have not been compared with a PS1 capture.
