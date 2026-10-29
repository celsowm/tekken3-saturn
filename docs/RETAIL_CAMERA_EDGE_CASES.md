# Original camera edge decisions: bugs 53, 54 and 55

Checkpoint: 2026-10-26. The camera's production `faithful=1` path now
preserves three more retail behaviors. The readable remake deliberately
corrects them; sources are pinned `camera_director.gd`, `camera.md` and
`game-bugs.md`, with direct checks against Japan Rev.1 MIPS.
Attribution is in `psx_camera.c` and `NOTICE.md`.

| Original bug | Faithful C behavior | Original-code comparison |
| --- | --- | --- |
| 53, camera side tie | Equal target screen Xs compare the stale third-target screen slot with previous target 0, using signed 32-bit values | 216 tied-screen throw selections, both throwers, negative/positive/equal stale and previous values |
| 54, stream-to-preset switch | The original NormalClip receives nearby PS1 stack addresses; their packed upper halves coincide and its result is 0, selecting side 0 independently of the authored triangle | 500 varied root/midpoint/stream triangles through original `FUN_800670E4` and actual NormalClip/COP2 execution |
| 55, inherited stream return | The stream phase's reset path returns the caller's saved register: **1 in normal FightFrame** | 500 calls to the original phase with FightFrame's `$s3=1`; compare returned weight and reset state |

The C camera keeps an explicit `third_target_screen_x` rather than replacing
the stale slot with a previous-target comparison. Its reset preserves the
slot. Fresh normal VS starts with it zero; other modes and cross-match
global-state carry still need their own binding/coverage. The trace adapter
seeds it when recorded, and the owned evaluator compares it whenever captured.

The geometry correction and `-1` stream return remain available with
`faithful=0` for historical remake-style comparisons. Production already
sets `faithful=1`; it now selects all five documented camera bug behaviors
(1, 2, 53, 54, 55). The stream phase has a validated audit entry point,
`t3_psx_camera_stream_update`, calling the same routine used by the director.
The original caller-register rule is modeled for normal FightFrame, not
arbitrary MIPS callers or an SH-2 stack layout.

## Differential proof and its scope

`ps1_original_camera_edge_bugs` compares **1,216 retail cases** against
unpatched original code and repeats the same **1,216 cases** against the
remake's patched original code with C's corrected mode. The two policies
actually differ in **128** side ties, **212** clip-side choices and **500**
stream returns, so matching both cannot be explained by inputs that never
distinguish them. Both modes pass.

These are phase/decision tests: the side-selection test stops the original
at the choice/framing boundaries; the clip test executes NormalClip but
captures the following switch decision. They compare side/thrower or
weight/reset state, not entire cinematic trajectories. Original routines
and return registers execute in the harness; expected values are not
computed by a Python copy of the C conditions.

The existing **1,560-tick** fresh retail escape/reversal/crossing comparison
still passes every compared field, including camera sources/targets/view,
side flags, input histories, combat, HUD, sound/eye state and RNG. A separate
instruction hook over the 600-tick reversal fixture observed **zero** hits
at the four edge sites. That continuous fixture is a regression check, not
proof that it naturally exercises these rare paths. Full multi-part throw
trajectories and hit-camera ties remain to be covered.

Current host suites: **73/73 Paul–Law**, **64/64 Paul**. Evidence:

- `build/parity-camera-edges-differential.log` — both edge policies and the
  three complete retail fixtures.
- `build/parity-camera-edges-host-tests.log` and
  `build/parity-camera-edges-host-paul-tests.log` — full host suites.
- `build/parity-camera-edges-flow-coverage.json` and `-flow-probe.py` — the
  negative instruction coverage observation, from restored original code.
- `build/parity-camera-edges-summary.json` — identities and result inventory.

## Saturn candidate

`build/parity-reversal` was rebuilt in place. ELF SHA-256:
`3474e8a643681a65e54f9f4fbedd64512d3bb6485aeb6637347334125025a43a`;
initialized payload **589,196 bytes**. Low WRAM ends at `0x002FF794`, High
WRAM BSS at `0x060FB9A0`; the Low WRAM reserved area and **16 KiB** master
stack reservation both pass the linker checks.

| Injected Ymir check | Result |
| --- | --- |
| Neutral, 600 fields | 196 simulation ticks, FIGHT, HP 140/140; 750/2048 commands |
| High hit, 920 fields | 516 simulation ticks, one hit, HP 140/134; 757/2048 commands |

Both exact-ELF captures report zero runtime errors, draw timeouts, attack
decode errors and rejected faces. Evidence is under
`assets/imported/parity-camera-edges-{neutral,high-hit}-saturn/` with
observations, screenshots and the physical high-hit schedule. There was no
video recording or additional SDK. Disk usage remains 1.3 GB build / 4.4 GB
imports.

These native smoke tests do not execute the rare camera edges deliberately,
compare independent original camera state on SH-2, boot the CD or certify
cadence. The historical full release gate belongs to its recorded ELF.
Subsequent independent SH-2 camera/input evidence is recorded separately in
[OWNED_CAMERA_SH2_PARITY.md](OWNED_CAMERA_SH2_PARITY.md), including a main-loop
RNG correction during round gaps. Extend natural throw/KO/replay coverage and
connect committed sound requests to native audio. Full roster/modes and
image/performance gates remain open.

```sh
cmake --build build/host-law
ctest --test-dir build/host-law -V \
  -R '^ps1_original_(camera_edge_bugs|retail_camera_continuous)$'
```
