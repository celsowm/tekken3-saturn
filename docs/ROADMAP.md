# Porting roadmap

This project is its own game/runtime repository, with LibSaturn as an external
installed dependency. Build strategy mirrors `ikemen-saturn` and `sa2-saturn`
but must address fundamentally different 3D fighting-game constraints.

## Milestone 0 (started): vertical slice foundation

- [x] Initialize independent repository, native/SH-2 CMake split
- [x] Implement deterministic fixed-tick simulation with host tests
- [x] Four separate attack specifications and controls, guard/hit stun
- [x] Movement, sidestep, physical pushboxes, timer, best-of-three logic
- [x] Initial procedural 3D blockout with camera, stage and HUD
- [x] Original skinned low-poly 24-joint fighters via LibSaturn SKA1 tooling
- [x] Nine animation clips, independent P1/P2 poses and palettes
- [ ] Measure renderer command budget and performance on Saturn hardware
- [x] External LibSaturn package and native IP boot contract
- [ ] Validate SH-2 build, Saturn disc boot, frame rate and pad inputs on Ymir/Mednafen
- [ ] Screenshot and telemetry capture on real hardware, SAROO, Satiator

## Milestone 1: asset pipeline

- Identify documented game file formats and verify legally owned input.
- Build an **offline** asset converter to an explicit versioned Saturn format:
  quantized vertices/bone weights, limited joint counts, LOD, frame sampling,
  materials indexed for VDP1, VDP2 backgrounds, and disc streaming.
- No models, audio, animations, ROMs, copyrighted textures, or derived
  distributable binaries in Git. Hash/check conversions against input data.
- Adopt existing public LibSaturn import/model/skinning APIs; request any
  missing generic capabilities upstream instead of copying library internals.
- Establish actual VRAM, WORK RAM and optional 4 MiB RAM-cart budgets.
  Do not require a RAM cart before measurements justify it.

## Milestone 2: Tekken-specific simulation

- Record versioned original-game reference traces (character, move, input,
  position/rotation, collision, frames, health, rounds and replay outcome).
- Incrementally port verified per-character moves and states from research,
  **not** a fabricated universal moveset.
- Frame-accurate move properties, command buffering, juggling, bounds,
  sidesteps, throws, recovery and input-latency policy.
- Deterministic CPU decisions with controlled seeds, an oracle comparator,
  and regressions for round rules, reversals and collision corner cases.
- Keep renderer/animation separate from fight simulation.

## Milestone 3: Saturn presentation and performance

- Replace original procedural skeletal fighters with legally imported models and stages.
- Quantize/skinning work distribution across Master/Slave SH-2 when helpful.
- Scene-wide depth sorting with geometry intersection splitting where necessary.
- Camera authored per fight, shadows, VDP2 floors/backgrounds and lighting.
- SCSP SFX/voice/music budget and controlled CD reads, with fallbacks.
- Profile 30/60 fps render modes independently from the fixed 60 Hz logic.

## Milestone 4: completeness and fidelity

Arcade, VS, Team Battle, Time Attack, Survival, Practice, character selection,
unlock progression, options, endings, and save support, gated on correctness and
measured performance. Additional modes only after the fight system is credible.

## Initial limitations

The current simulation is **not** Tekken 3's fighting engine and shares no
upstream assets; the blockout fighters deliberately do not resemble its cast.
There is an original-procedural SKA1 animation pipeline, but no Tekken 3
asset import, move data, combos, grabs, replay oracle or save data yet. Passing host tests is NOT evidence the SH-2 ISO boots.

## Sources and licensing boundaries

- LibSaturn: https://github.com/celsowm/libsaturn (installed package).
- Tekken3Remake: https://github.com/n0madic/Tekken3Remake
- Its research docs: https://github.com/n0madic/Tekken3Remake/tree/master/docs/research

Tekken3Remake's current source is PolyForm Noncommercial 1.0.0 and contains
required attribution. This project has **not** copied that implementation.
Any future code reuse must preserve that license and notice in the affected
files and distributions; independent source is kept separate. Bandai Namco
retains rights in the original game and data. A derived ISO may contain
copyrighted assets and must not be distributed without permission.
