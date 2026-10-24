# Legal notices and provenance

This repository contains newly authored source code under the local MIT
license, except files explicitly identified below. It is **not** a redistribution of the Tekken 3 game or the Godot
implementation of Tekken3Remake.

- **Tekken 3**, characters, artwork, models, audio, animations and original
  game data: rights belong to Bandai Namco and relevant rightsholders.
  No such assets are checked into this repository. This is not official.
- **LibSaturn**: external dependency,
  https://github.com/celsowm/libsaturn . Its own license applies separately.
- **Tekken3Remake**, by n0madic:
  https://github.com/n0madic/Tekken3Remake . Its current project source is
  licensed under PolyForm Noncommercial 1.0.0 and carries the required notice
  "Copyright (c) 2026 n0madic". It is pinned under `third_party/Tekken3Remake` as a separate Git submodule. The upstream license and notices still apply to its checkout and adapted code.
- **Original engine adaptations**: `src/game/move_clock.c`,
  `src/game/move_lifecycle.c`, `src/game/psx_ground.c`,
  `src/game/psx_match.c`, `src/game/psx_body.c`,
  `src/game/psx_exact_contact.c`, `src/game/psx_hit_transaction.c` and
  `src/game/psx_round.c`, `src/game/psx_round_flow.c`, `src/game/psx_replay.c`,
  `src/game/psx_blend.c`, `src/game/psx_head.c`, `src/game/psx_camera.c`,
  `src/game/psx_match_camera.c`, `src/game/psx_motion.c`,
  `src/game/stage_backdrop.c`,
  `src/game/input_history.c`, `src/game/psx_reaction.c`,
  `src/game/move_index.c`, `src/game/attack_records.c`,
  `src/game/psx_ai.c`, `src/game/psx_ai_game.c`, `tools/psx_ai_flat.c`,
  `scripts/extract_psx_sfx.py` (sound codes, playback rate and the PS-ADPCM rule from
  `docs/research/code/sound.md` and `formats/vab-banks.md`), and
  `src/game/psx_blend_math.c` adapt
  grounded physics, original hit transaction/reaction queuing and KO collapse,
  root/attack placement, exact integer collision, normal arena bounds, body
  spheres/separation, blend matrix normalization and scaling,
  clock advancement, queued move starts and
  branch scheduling from the pinned research's `tools/research/fight_sim.py`
  and `fight_math.py`, plus body profiles from
  `remake/core/combat/combat.gd`, procedural head tracking from
  `remake/core/fighter/fighter_animation.gd` and result classification from
  `remake/core/fight/round_flow.gd`, plus selective round placement from
  `remake/core/fight/fight_simulation.gd` and replay state/pose hooks from
  `remake/core/fight/replay_state.gd`, plus the fight camera director, view
  matrix, backdrop turn, camera streams, round-end phases and shake from
  `remake/core/camera/camera_director.gd`, `camera_math.gd`,
  `view_matrix.gd`, `backdrop_turn.gd`, `camera_reel.gd`, `camera_phase.gd`
  and `camera_shake.gd`, the GTE projection from `remake/core/math/gte.gd`,
  and the camera's place in the fight frame (its fighter inputs, the screen
  joint and the landing and move-event shakes) from
  `remake/core/fight/fight_simulation.gd` and
  `remake/core/fighter/fighter_animation.gd`, plus the stage panorama's
  placement (scale, height and turn) from `docs/research/code/stages.md` and
  `remake/presentation/stage/stage_view.gd`, plus PairwiseDistances and its
  off-screen stand-in, the throw link and arena partner correction from
  `remake/core/combat/combat.gd`, the throw situation code, MoveStartAll's
  throw order, ThrowVictimStart, the thrower and victim starts and throw
  pairing, MoveBranchStep's branch window and the attack segment count
  from `remake/core/fighter/move_system.gd`, the built-in attack
  descriptors from `remake/core/combat/attack_records.gd`, the merged move
  index from `remake/core/fight/fight_simulation.gd`, MoveTotalDamage from
  `remake/content/move_row.gd`, RoundFlow's win-pose draw from
  `remake/core/fight/round_flow.gd`, the double tap from
  `tools/research/fight_sim.py`, and the round gap and placement
  resets from `remake/core/fight/fight_simulation.gd` and
  `docs/research/code/fight-frame.md`, plus the CPU opponent (AiUpdate,
  AiInitRound, AiReset and their helpers) from `tools/research/ai_sim.py` and
  `ai_update.py`, and the bank linking it reads (DivmotLinkBank's relocated
  pointers) from `docs/research/formats/divmot-banks.md`, to C.
  These files are under [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/),
  not MIT. Required Notice: Copyright (c) 2026 n0madic.
  Its header identifies the changes. Builds linking this adaptation include
  that separately licensed component; the local MIT license does not replace
  its terms.

MIT terms on this project's independently written code do not relicense or
grant permissions for Tekken 3 data, LibSaturn, or Tekken3Remake code.
Future contributions must keep imported materials and their license records
separate and must not publish proprietary converted game assets.
