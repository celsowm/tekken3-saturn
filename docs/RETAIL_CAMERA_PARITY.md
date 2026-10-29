# Retail camera oracle and InputSource ordering

Checkpoint: 2026-10-26. These results supersede the open frame-320
camera/side audit in [PSX_FIGHTER_SOUNDS.md](PSX_FIGHTER_SOUNDS.md), for the
three cases below. They do not close all camera, renderer or release gates.
The subsequent edge-decision implementation and latest candidate identity are
in [RETAIL_CAMERA_EDGE_CASES.md](RETAIL_CAMERA_EDGE_CASES.md).

## Correct the reference before changing the port

The pinned readable remake's `fight_harness.py` always applies `_fixes()`,
even with `Scenario.gameplay_fixes=False`. These presentation/latent fixes
are appropriate to that remake's policy but differ from the retail game:

| Bug | Patched original site(s) | Effect |
| --- | --- | --- |
| 1 | `0x800640F8` | CameraLookPoint uses target 1 Z for its second distance instead of reusing target 0 Z |
| 53 | `0x80063758`, `0x80063830` | Fixes the camera's stale third-target screen-X tie break |
| 54 | `0x800672A0` | Loads NormalClip values instead of passing stack addresses |
| 55 | `0x80067330` | Replaces the stream-to-preset path's inherited return value |
| 59 | `0x8002C708` | Clears the dead CPU fighter's otherwise uninitialized pads |
| 58 | `0x80075D60` | Changes the initial KO vibration pad index |

The initial frame-320 difference was bug 1: source 3 Z `-5114` versus the
patched trace's `-5113`. All inputs, targets and source records matched. A
standalone call to the original CameraBlend, on the captured inputs, produced
`-5114`, agreeing with C. Instrumenting the running harness confirmed the
same entry state; inspecting its documented `_fixes()` explained the output.
The port's blend arithmetic did not need changing.

`fight_oracle_export.py` now restores those seven code sites from the
original Japan Rev.1 executable **before setup/placement** by default.
It leaves drawing/audio stubs and the existing bounded panorama hook in
place. The export records `remake_presentation_fixes=false` and the original
executable SHA-256. Its restoration fails if the pinned patch inventory
changes. The harness class override is scoped to the capture generator and
is restored on close. A live check compared all seven sites with executable
bytes after the first frame: `build/parity-retail-camera-restoration.json`.

`--remake-presentation-fixes` explicitly reproduces the older oracle policy.
Older fixtures without the new provenance field remain historical **remake
presentation-patched** traces. Their finite combat comparisons are useful,
but their camera pixels/side choices cannot certify unpatched retail
presentation. The isolated MIPS blink/sound/reversal tests that use
`load_release()` directly were not affected by FightHarness's `_fixes()`.
No old captured observations or manifests were edited to pass this audit.

## Correct the human input phase

The restored escape and reversal traces removed the blend discrepancy, but
still exposed side flags first differing at sources 432 and 411. The readable
`fighter_input.gd` explains two distinct `FighterUpdateSide` calls:

1. FightFrame updates sides from the previous screen positions before the
   camera and move starts.
2. Human CpuOrPadInput updates the side again after FighterAnimate draws
   this frame's joint 11, then maps the physical controls.

The port previously kept the first result and mapped input before the step.
It now projects joint 11 at composition time, updates the human's side at
InputSource, and maps the frame's physical word there. CPU and scripted input
retain their separate paths. Live post-frame code no longer projects the
same fighters again; replay retains its separate final camera projection.

`t3_psx_match_frame_t.physical_inputs` explicitly enables physical input
mapping and requires the owned camera. The independent evaluator supplies
raw physical controls and zero pre-mapped inputs. The Saturn adapter recovers
the physical D-pad from the controller boundary's previous world-facing
mapping, then lets the same core perform the current screen-side mapping.
Existing optional port aliases are retained. This covers the default pad
directions/faces; the original STEP-key helper and remapped controls still
require separate coverage.

## Current proof

`ps1_original_retail_camera_continuous` passes **1,560 consecutive original
ticks**, fresh from configuration, with no original screen/view feedback,
mid-fight reseeding, discarded moves or filtered frames:

| Fixture | Ticks | Compared source frames |
| --- | --- | --- |
| `build/parity-retail-camera-escape` | 460 | 2–461 |
| `build/parity-retail-camera-reversal` | 600 | 2–601 |
| `build/parity-retail-camera-crossing` | 500 | 2–501 |

The comparator checks view rotation/translation/projection, both screen Xs,
director phase/state/weights, five camera sources, both current and previous
targets, all three RNGs, eye state, sound scripts/timers/holds, the existing
combat/round/HUD/tap fields, all **60 input history entries per fighter** and
current held/pressed buttons. The new test requires explicit retail
provenance, owned camera, physical mapping and the added comparisons on every
tick. It rejects an older trace that lacks that contract.

The crossing case holds physical right from source 430 through 459. At
430/431 the original direction is 6 (toward); at 432/433 it becomes 4 (away)
after the projected sides cross. C matches these transitions and the complete
fight through recovery. Evidence: the fixtures' `native-camera.jsonl` and
`build/parity-retail-camera-differential.log`.

Full host suites pass **72/72 Paul–Law**, **63/63 Paul**, logged in
`build/parity-retail-camera-host-tests.log` and
`build/parity-retail-camera-host-paul-tests.log`. Earlier camera air/timeout
and sound component fixtures still pass under their recorded oracle policy.
They are not relabeled retail captures.

Current production candidate `build/parity-reversal`, ELF SHA-256
`6db86f2e64cc7f5ea89632ad0636f5c8d90bdb0cec0c1117666ebc315cbe052b`,
has a **589,092-byte** initialized payload. The linker preserves both the
Low WRAM reserved area (`__wram_l_end=0x002FF794`) and **16 KiB** master stack
reservation (`__bss_end=0x060FB940`). See
`build/parity-retail-camera-saturn-build.log`.

Two injected Ymir smoke tests use that exact ELF:

| Run | Fields / simulation ticks | Result |
| --- | --- | --- |
| Neutral | 600 / 196 | FIGHT, HP 140/140, zero errors/timeouts/decode errors/rejected faces; 750/2048 commands |
| High hit | 920 / 516 | Paul hits Law once, HP 140/134, zero errors/timeouts/decode errors/rejected faces; 757/2048 commands |

Evidence: `assets/imported/parity-retail-camera-{neutral,high-hit}-saturn/`
(observations, screenshots and the high-hit physical schedule) and
`build/parity-retail-camera-summary.json`. The superseded intermediate
neutral capture was removed and replaced by the final ELF's capture. No
video was recorded. Disk budget remains 1.3 GB build / 4.4 GB imports.

## Remaining work

- Independent original camera/input comparison on SH-2 now has a separate
  `owned-camera-v1` contract and finite continuous evidence in
  [OWNED_CAMERA_SH2_PARITY.md](OWNED_CAMERA_SH2_PARITY.md). The legacy replay
  retains its external view/screen contract. Rerun the full release gate for
  the new playable ELF separately.
- Camera bugs 53/54/55 now have faithful C rules and direct phase proofs in
  [RETAIL_CAMERA_EDGE_CASES.md](RETAIL_CAMERA_EDGE_CASES.md). Extend these to
  natural multi-part throws and hit-camera ties; the three fixtures above
  do not certify those paths.
- Extend raw-code traces through KO/replay, longer matches, other throws,
  CPU KO input and additional seeds. Restore the original oracle policy for
  new air/timeout coverage rather than copying a legacy trace's result.
- Connect committed sound requests to replay/native playback; complete the
  original viewport/image comparison and actual display-cadence gates.

No complete port, pixel identity, CD boot or real-hardware performance claim
follows from these host comparisons or the injected Saturn smoke tests.

```sh
cmake --build build/host-law
ctest --test-dir build/host-law -V -R '^ps1_original_retail_camera_continuous$'
third_party/Tekken3Remake/work/venv/bin/python scripts/fight_oracle_export.py \
  --out build/<new-retail-case> --name paul_law --chars 0 1 --costumes 0 0 \
  --styles idle idle --seed-round-start \
  --pad-script tests/fixtures/psx_camera_crossing.pads --ticks 500
python3 scripts/fight_oracle_eval.py build/<new-retail-case> \
  --evaluator build/host-law/t3_match_replay_camera_eval \
  --initialize-match --independent-camera
```
