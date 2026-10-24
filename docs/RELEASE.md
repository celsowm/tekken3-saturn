# Building the Paul-against-Law disc

The repository holds no retail data. A disc is built locally from your own USA
Tekken 3 import (`scripts/psx_disc_import.py`, then the `build_local_*` and
`extract_psx_*` scripts, each run by CMake from the private paths below).

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
     --with-ko --with-timeout ...` (24 cases).

   `scripts/saturn_regression.py` runs the CTest builds and the 24-case suite
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
The name plates and original text font are still pending. This option needs
`T3_LOCAL_POSE_CACHE_ENTRIES=128` in the measured release configuration to
fit High Work RAM. See [ORIGINAL_HUD.md](ORIGINAL_HUD.md) for the build and
runtime evidence.
