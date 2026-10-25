# Porting roadmap

This project is its own game/runtime repository, with LibSaturn as an external
installed dependency. Build strategy mirrors `ikemen-saturn` and `sa2-saturn`
but must address fundamentally different 3D fighting-game constraints.

Next execution scope: [Paul mirror-match playable milestone](NEXT_PLAYABLE_MILESTONE.md).
The current procedural slice boots visibly from CD in Mednafen and runs through
the headless Ymir harness; scripted input validation and original combat
integration are the next gates.

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
- [x] Build SH-2 application and confirm visible Saturn CD boot in Mednafen
- [x] Execute the application with the headless Ymir injection harness
- [ ] Measure render cadence and validate scripted pad inputs on Ymir/Mednafen
- [ ] Screenshot and telemetry capture on real hardware, SAROO, Satiator

## Milestone 1: asset pipeline

- [x] Pin the upstream Tekken3Remake research project as a separate licensed Git submodule.
- [x] Read SLUS-00402 Mode 2 Form 1 track and verify the US executable.
- [x] Inventory 303 BNS entries, selectively export original file records.
- [x] Export untextured stage TMD geometry as a research OBJ preview.
- [x] Parse original KMD 27-row model and PS1 seam exchange into the LibSaturn multi-anchor skin compiler (bind pose only).
- [ ] Calibrate KMD basis and proportions with the real Paul model, convert TIM materials and hand variants.
- [ ] Decode DIVMOT motion clips and verify their joint mapping.
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

Status (2026-10-23, details in `docs/FIGHT_ORACLE.md` and `docs/PAUL_STAGE_VDP2.md`):

- [x] Original fight camera, VDP2 floor and panorama, Paul and Law, owned combat.
- [x] CPU opponent bit-exact against the original AI; Arcade level steps.
- [ ] CPU AI cost on the Slave (probe counters `ai_calls/ai_ticks/ai_max_ticks`, FRT /128 = 3,728
  ticks per 60 Hz frame): 7.4 ms per update at first, 3.7 ms (max about 18 ms) after direct
  read windows over constant rows and the AI block (same AI pad hash over 1,000 updates).
  Control: the CPU's P2 input captured on the Slave (`T3_CAPTURE_INPUT_TAPE`, CPU tape in
  `move_probe.c`) and replayed as a pad with the AI off runs the same round-1 fight at
  1.00 ticks/vblank and 30.7 img/s; with the AI, 0.97 and 23.5 img/s. At that
  checkpoint, the gap came from the AI. A fixed Slave delay in that replay gave
  the budget: +1 ms per tick 29.5 img/s, +2 ms 27.3, +3 ms 24.2. Subsequent
  renderer work crossed 30 img/s without a 4-7x AI cut; AI cost still needs
  improvement to create margin.
- [x] 30 img/s for Paul vs Law in the measured 25–40 s round-1 Mednafen window.
  Ymir without cache emulation
  overstates it by about a third (`THIRTY_IMAGE_GATE.md`, "Paul vs Law"). Mednafen, round 1:
  - neutral input: 17.5 img/s at first, 22.3 after the pose cache and stage claim, 26.4 after
    the geometry change, 29.2 after skinning straight to screen, 29.9 before the latest face change
  - CPU P2: about 15, then 17.0, then 20.9, then 22.7, then 25.9, 26.0 (motion look-ahead peeks), 26.7 (knot cursor), 29.47 before one-pass screen culling, now 30.13 in the 25–40 s round-1 window (`THIRTY_IMAGE_GATE.md`, "One-pass screen culling")
  - two Pauls: 26.7 (before the geometry change)

  The latest measured gain comes from LibSaturn appending screen-culled faces in
  one pass. The preceding deferred-tick and Master-face changes stopped late
  images from repeatedly causing a third tick. There is little headroom:
  the silent Mednafen probe measured a two-tick Master image near 7,000 of the
  7,465 FRT/128 available in two vblanks (`THIRTY_IMAGE_GATE.md`). A longer
  CPU-Law CD run now measures 920 images / 30 s (30.67 img/s) in the active
  round-1 fight, from 25 to 55 s; the 25–65 s window including timeout is
  1222 / 40 s (30.55 img/s). The same ELF reached round 2 in a 4,200-field
  cache-on Ymir run with 3,799 scheduled/committed ticks and no discarded
  fields. Actual displayed image changes, real-hardware cadence and other
  matchups remain unmeasured.
- [~] Drop shadows: a blob under each fighter, not the PSSDW mesh.
- [ ] Floor distance shading (VDP2 has no per-line brightness ramp without losing texture contrast).
- [~] Sound: swing, hand/foot guard and hit samples from VAB 0, plus Paul's and Law's shouts and hurt voices; no KO or result voices, music or most effects.
- [~] Effects: a hit/guard spark at the contact point (HUD cross); the original effect sprites are not ported.
- [~] HUD: private original system-texture bar caps/fills, round marks and timer
  digits upload to VDP1 with `T3_ORIGINAL_HUD=ON` and draw during gameplay.
  The runtime scales them to the Saturn's 320 × 224 display. The original
  four-field health-bar transition now advances at 60 Hz and matches 33,575
  original trace transitions; exact original 384 × 480 placement, name plates,
  font and round messages remain pending. See [ORIGINAL_HUD.md](ORIGINAL_HUD.md).
- [~] Menus: an optional title screen (`T3_TITLE_SCREEN`); no select or mode menus.
- [ ] More stages and characters (the private import covers Paul, Law and stage 0).

## Milestone 4: completeness and fidelity

Arcade, VS, Team Battle, Time Attack, Survival, Practice, character selection,
unlock progression, options, endings, and save support, gated on correctness and
measured performance. Additional modes only after the fight system is credible.

## Initial limitations

The current simulation is **not** Tekken 3's fighting engine and shares no
upstream assets; the blockout fighters deliberately do not resemble its cast.
There is an original-procedural SKA1 animation pipeline and private original
asset/move decoders, but original authored combat is not yet active in the
playable match. Combos, grabs, authenticated replay parity and save support
remain incomplete. The SH-2 ISO's CD boot is confirmed separately in Mednafen;
passing host tests alone is not boot evidence.

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

## Original combat-logic port (incremental and trace-verified)

- [x] Original DIVMOT bank layout, 56-byte move / 12-byte branches / 4,023 global slots.
- [x] Portable 60-frame input history, double tap, simple motion sequence matching.
- [x] Deterministic move VM for first-match branch and end-of-move continuation.
- [x] High/mid/low attacks, crouch/standing guard, jump and dash in fallback combat.
- [x] Discover and structurally validate all 73 USA disc DIVMOT records, preserving per-record variants and the shared bank in private compact output.
- [ ] Complete command/condition/restriction semantics and move-slot transitions.
- [ ] Replace fallback attacks with bank-defined full character moves and reactions.
- [x] Read original DIVMOT section 9 attack descriptors and per-frame local-space hit segments.
- [x] Preserve section 9 optionally in private compact banks; expose decoded segment diagnostics.
- [x] Implement independent PS1 world transforms and integer attack-segment ↔ 14-zone hurt-cylinder tests.
- [x] Parse PS1 DIVMOT animation section 6, decode 49 raw pose channels and stream limits.
- [x] Preserve tagged animation streams in locally compacted motion banks.
- [ ] Import private original EXE spline coefficients and validate the USA revision.
- [ ] Feed real 24-joint PS1 animation poses into hurt cylinders in the Saturn build.
- [ ] Validate exact PS1 collision arithmetic, body spheres and reaction logic against an oracle.
- [ ] Per-frame original movement, collision, hit segments, counters and juggles.
- [x] Throws: SituationClassify (0x800313B0), ThrowVictimStart pairing, the
      counter-throw victim marking (transition 0x22, +0xBD) and Paul's authored
      d/f+1+2 (follow-up 2848, victim slot 3143 through the merged index's
      fallback, authored 40 damage, throw-camera preset handoff) run in the
      live suite (`throw`, `throw-damage`).
- [ ] Reversals, multiple stances, knockdown/get-ups and posture/evades.
- [ ] Motion decode, model variants, effects, audio, original camera/round flow.
- [ ] CPU AI, Tekken Force/Ball, team, practice, replay, character selection and modes.
- [ ] Real-game oracle harness and frame-by-frame regression parity across characters.

### Input extraction milestone
- [x] Independently implement safe 104-pointer PS1 sequence table scanner.
- [x] Accept validated command sequence tables in C move interpreter.
- [x] Wire private local input sequence manifest into the Saturn debug build.
- [x] Extract and structurally validate all 104 USA commands against the fingerprinted private original SLUS_004.02 image.
- [ ] Verify original command acceptance and timing against matched PS1 gameplay input traces.
- [ ] Implement original conditional input buffering / stance look-ahead.

### FK pose milestone
- [x] Translate 10 PS1 Euler triples to their original local matrix slots with Q12/GTE-style arithmetic.
- [x] Compose only valid model-joint chains, propagating missing IK/attachment status.
- [ ] Reconstruct remaining 8 matrices with original PS1 two-bone limb IK.
- [ ] Import real KMD bind offsets into diagnostic composition and validate against PS1 traces.

### Original four-limb IK
- [x] Implement signed-integer PS1 two-bone IK with 32-bit wrap, Q12 bend and SquareRoot0 table.
- [x] Complete eight IK matrix slots with a fail-closed limb validity mask.
- [x] Wire optional normalized private IK constants into Saturn move diagnostics.
- [ ] Verify USA sine/sqrt/basis/limb constant offsets and compare joint-by-joint against the PS1 oracle.
- [x] Opt-in private KMD+DIVMOT original-pose preview into LibSaturn CPU skinning, with all-18-part safety gate.
- [ ] Verify actual USA authored pose renders in emulator against PS1 oracle and resolve per-frame contacts.

- [x] Add a strict private USA PS-X EXE FK/IK table extractor with five verified offsets and SHA-256 manifest.
- [ ] Determine and independently validate the five actual USA PS1 lookup offsets.

### PS1 defensive collision
- [x] 14 KMD original body-part hurt cylinders with strict pose validity.
- [x] Original swept attack history, per-move/per-frame continuity.
- [x] Optional geometric contact probe separated from fallback combat.
- [ ] Calibrated PS1 root placement and live damage/classification semantics.

### Authored PS1 root motion
- [x] Implement original KMD-scale root displacement with signed s16 wrap, facing rotation and heading basis.
- [x] Include KMD scale in private generated C, and propagate model-local motion to composed joints.
- [ ] Calibrate PS1 fighter roots/headings and layer animated world geometry over genuine duel positions.

### Deterministic PS1 oracle harness
- [x] Define endian-independent 18-joint 564-byte pose-trace serialization.
- [x] Compare joint translations and rotations against externally captured PS1 oracle frames.
- [ ] Capture actual original PS1 and Saturn frame traces, align control inputs, and close numerical deviations.

### Independent solver parity
- [x] Add native batch evaluator for 49-channel input and all 18 Q12 matrices.
- [x] Add numerical Python-vs-C differential harness using pinned research submodule.
- [x] Exercise FK/IK across synthetic bending, singular poses and mixed rotations.
- [ ] Compare actual USA animation streams with PlayStation emulator traces.

- [x] Export selected private DIVMOT move slots as original signed 49-channel pose samples.
- [x] Test local/shared banks and strict frame windows before feeding the C/Python differential harness.

### Batch motion-to-FK/IK audits
- [x] Auto-enumerate sparse DIVMOT move slots and deduplicate identical section-6 streams.
- [x] Sample representative streams across the bank and compare C vs pinned Python per frame.
- [x] Record incomplete frames, coverage, invalid slots and unavailable coefficient reasons, fail closed on mismatches.
- [ ] Run real private USA character banks with verified coefficients and investigate differences.

### Shadow original combat observation
- [x] Simultaneously evaluate original attack segments against 14 KMD hurt cylinders, using explicit world roots.
- [x] Preserve per-frame swept endpoint history and distinguish contact intervals from health-changing hits.
- [ ] Integrate calibrated original PS1 world roots into Saturn's live private probe and validate against PS1 collision traces.
- [ ] Replace provisional hit/apply rules only after PS1 damage, guard, reaction and throw oracle parity.

- [x] Wire optional read-only two-player contact observer into Saturn private DIVMOT tick with external synchronized root injection.
- [ ] Capture correctly aligned USA PS1 movement/root samples for non-synthetic shadow contact validation.

### Read-only HitApply preparation
- [x] Classify verified geometric contacts into guarded/chip/clean/counter/airborne/close hits using explicitly supplied PS1 facts.
- [x] Compute 14-zone original integer damage values, select raw reaction references, and rank the best hit slot.
- [ ] Decode private 42-byte reaction tables and move events 11–13, then validate against real PS1 HitApply traces.
- [ ] Apply to the playable match only after original HP, hit-freeze, guard and reaction parity is demonstrated.

- [x] Decode 42-byte original reaction record alternatives and private throw-victim references (guard/front/side/back/counter/down).
- [x] Preserve original transition codes and push-table indices, including attack-class suppression.
- [ ] Import verified original USA reaction records, AirReactionKind and push tables; run full source-oracle comparisons.

### Read-only original HitApply accumulator
- [x] Port HitExtraDamage for move events 11–13, linked throw victim and signed 16-bit wrapping.
- [x] Compose verified HitTest slots, HitClassify, HitDamage, best-slot ranking, KO/chip flag conversion and optional reaction lookup.
- [x] Compute original Q16 health with explicitly wrapped 32-bit MIPS arithmetic; never mutate playable fallback HP.
- [ ] Validate all inputs/state transitions against original USA PS1 real-game HitApply oracle captures.
- [ ] Replace temporary prototype health/guard/throw logic only when original cooldown, events, state transitions and round flow are reproducible.

### Original per-frame MoveEvents
- [x] Import original section 4 into private compact DIVMOT banks with explicit opt-in.
- [x] Parse u16 frame/command event lists and evaluate original crossed-frame windows.
- [x] Decode move event 11/12/13 extraKind and extraDamage for read-only HitExtraDamage.
- [ ] Wire verified event state and hit cooldown/reactions into an original-combat replay oracle.
- [ ] Port hand, audio, camera, sparks and animation event side effects after source visual parity.

### Original post-contact logic integration
- [x] Evaluate independent HitTest eligibility after KMD/DIVMOT geometric contact, including jump, posture, cooldown and hit-slot rules.
- [x] Compose original HitTest -> HitClassify -> HitDamage -> read-only 16.16 HitApply and optional reaction selection for simultaneous two-fighter trades.
- [x] Fail closed when original HitSlot occupancy or attacker move state disagrees with the supplied PS1 facts.
- [ ] Verify original replay hit slots, damage, KO and reaction transitions against matched PS1 emulator frames before replacing fallback Saturn damage.

### MoveVM original reaction projection and differential harness
- [x] Project simultaneous original reaction states onto independent, copied MoveVM snapshots.
- [x] Resolve reaction moves from character DIVMOT and shared divmot99 bank; reject missing target slots.
- [x] Require next pose frame as an explicit external PS1 observation, separate from stun duration.
- [x] Add paired-tick PS1/Saturn combat JSONL comparator for move slots, frames, 16.16 HP and optional reaction flags.
- [x] Run local/common synthetic reaction bridge tests and comparator regressions in host CI.
- [ ] Capture authentic synchronized PlayStation combat traces and verify real reaction timing and pose frames.
- [ ] Reconstruct airborne reaction, KO victim move, combo reset, get-up options and move branches from measured PS1 facts.
- [ ] Activate in-game MoveVM transitions only after replay/HP/move/pose parity; no speculative direct state writes.

### USA original-emulator combat oracle instrumentation
- [x] Derive USA SLUS_004.02 P1/P2, game-state and global-counter RAM addresses from upstream canonical-to-source point mapping.
- [x] Read-only PCSX-Redux Lua capture of two fighter states per advancing FightMain frame with source counter and raw diagnostic fields.
- [x] Independent Python decoder of two-fighter RAM snapshot frames with strict guest-frame continuity and atomic output.
- [x] Compare only directly observed move-slot, pose-frame, health and KO; preserve raw PS1 state without claiming inferred stun/combos.
- [x] Produce measured raw move/HP/KO/pose-stall event timeline; synthetic regression tests and CI syntax checks.
- [x] Run original USA Paul mirror encounters in PCSX-Redux, record consecutive input/move/health/guard observations and cross-check paused Lua/RAM.
- [ ] Align VSync sampling with PS1 FightFrame, HitApply and MoveStepAll boundaries; verify source-frame skew explicitly.
- [ ] Inject original measured reaction durations and valid transition destinations into the read-only MoveVM bridge.
- [ ] Capture equivalent Saturn simulation observations and close exact frame-by-frame parity before touching live match state.
