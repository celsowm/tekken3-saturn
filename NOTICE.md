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
  `src/game/psx_blend.c`, `src/game/psx_head.c` and `src/game/psx_blend_math.c` adapt
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
  `remake/core/fight/replay_state.gd`, to C.
  These files are under [PolyForm Noncommercial 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0/),
  not MIT. Required Notice: Copyright (c) 2026 n0madic.
  Its header identifies the changes. Builds linking this adaptation include
  that separately licensed component; the local MIT license does not replace
  its terms.

MIT terms on this project's independently written code do not relicense or
grant permissions for Tekken 3 data, LibSaturn, or Tekken3Remake code.
Future contributions must keep imported materials and their license records
separate and must not publish proprietary converted game assets.
