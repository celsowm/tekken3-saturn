# AGENTS

## Disk budget (mandatory)

Experiments once grew `assets/imported` to 251 GB and left hundreds of variant
builds and several SDK copies behind. Do not repeat that.

- **Recordings are the cost.** One Mednafen `--seconds 60` run in a `*-cd`
  directory is about 2 GB. Record only when the result is needed, with the
  shortest window that answers the question. Read the numbers and screenshots,
  then delete the video (`run.mov`) at once. Keep screenshots, JSON and logs.
- **One candidate plus one control.** Keep at most the current candidate, its
  control, and the final evidence for a documented result. Delete every
  intermediate variant (`build/<name>`, `-frozen`, `-ref-*`, probe builds and
  their `assets/imported/<name>-*` outputs) as soon as it is rejected or
  superseded.
- **Never copy the SDK.** Do not clone, copy or install extra LibSaturn trees
  or prefixes (`*-src`, `*-sdk`, `*-prefix`, `*-runtime`). Use one SDK
  prefix, rebuilt in place. Do not keep a second private clone.
- **Budget check.** Before a batch of runs, check
  `du -sh build assets/imported`. Stop and clean up if `assets/imported`
  exceeds 20 GB or `build` exceeds 5 GB. Say what you removed.
- **Always keep:** `assets/imported/tekken3-usa`, `ymir`, the
  `paul-reference-*-export` and `paul-reference-sanitized-export` data, the
  frozen-pixel baseline, and `build/asset-*`, `build/paul-blend`,
  `build/saturn`, `build/host*`.
- **Name for deletion.** Give experiment outputs a shared prefix so one
  pattern removes them. Delete by an explicit list, never a bare wildcard.
- Everything under `build/` and `assets/imported/` is git-ignored. Anything
  worth keeping belongs in a report under `docs/`, not in a recording.

## Look in Tekken3Remake first (mandatory)

`third_party/Tekken3Remake` (pinned submodule) is a readable Godot remake of
Tekken 3. It comes with reverse-engineering docs and Python ports that are
verified bit-exact against the original. Before you reverse-engineer
`SLUS_004.02`, capture PS1 RAM, or guess any original rule, search it:

- `CLAUDE.md`: its layout, commands and fidelity rules.
- `docs/research/code/`: game logic with addresses. Key files:
  - `camera.md`: director, framing, presets, streams, round intro/end, shake.
  - `stages.md`: panorama box, backdrop turn and parallax, floor grid and
    shading, stage lighting, shadows.
  - Also `fight-frame.md`, `moves.md`, `combat.md`, `fighter.md`, `hud.md`,
    `effects.md`, `memory-map.md`, `function-index.md`, `game-bugs.md`, and
    `usa-version.md` for USA differences.
- `docs/research/formats/`: file formats (`stage-tmd.md`, `arc-archives.md`,
  `divmot-banks.md`, `3dmk-models.md`, `pssdw-member.md`, ...).
- `tools/research/`: Python ports with `verify_*.py` harnesses (for example
  `camera_sim.py`, `projection_sim.py`, `pose.py`). `fight_harness.py` and
  `flow_trace.py` export per-frame golden traces from the original code,
  including the camera, camera sources, camera RNG and panorama turn.
- `remake/core/` (simulation, camera) and `remake/presentation/` (stage,
  fighters): typed GDScript implementations of those rules.

### Disc images and golden traces

- The USA disc is at `roms/Tekken 3 USA/Tekken 3 (USA).cue`. Its private
  import lives in `assets/imported/tekken3-usa`.
- The Japan Rev.1 disc (EXE SHA-256 `09e8b0b6...`) is at
  `roms/Tekken 3 (Japan) (Rev 1)/`. Its EXE and BNS records are already
  extracted to `third_party/Tekken3Remake/work/jp_rev1/` (git-ignored, 38 MB,
  placeholder record names). Do not use `extract_work.py`: it also expects the
  original SLPS-01300 ECM disc, which we do not have.
- The original-code harnesses work. Ghidra 12.1.4 (`brew install ghidra`,
  with its own `openjdk@21`) and `ghidra_psx_ldr` (built in
  `~/dev/ghidra_psx_ldr` and installed in Ghidra's user Extensions) produced
  `work/decomp/SLPS_013.00.{functions.tsv,cop2.txt,c}`; the Ghidra project
  is `work/ghidra/proj`. Run the harnesses with `work/venv/bin/python`
  (unicorn, numpy) from `tools/research`. For example, `verify_camera_sim.py`
  matches the original on every case, and
  `export_traces.py --banks --enbu --gon-eyes 0 --attachments 0
  --no-back-colour --no-vibration --fight paul_law --jobs 1` writes a
  per-frame golden fight trace (camera, camera sources, panorama turn) to
  `work/traces/`. Game names from `symbols.txt` are not applied in this
  project; use `docs/research/code/function-index.md` for names.
- For USA-only checks, record per-frame RAM from the USA disc with PCSX-Redux
  (`scripts/pcsx_redux_*.lua`). Map Rev.1 addresses through
  `tools/remake_import/layouts/usa.json`.

Addresses are Japan Rev.1. Map them to USA with
`tools/remake_import/layouts/usa.json`. The submodule is
PolyForm Noncommercial: adapt rules into C with attribution in the file and
`NOTICE.md` (as in `src/game/psx_head.c`). Never copy GDScript verbatim, and
never commit converted game data from it.
