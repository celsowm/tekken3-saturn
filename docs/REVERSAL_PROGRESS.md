# Original reversal integration

Checkpoint: 2026-10-26. Reversal selection and hit replacement now run in the
owned C match. A configuration-initialized original/host/SH-2 trace matches
600 ticks through the counter-throw's damage and Paul's return to stance.
The final release regression passes; this is not a full-game, independent
camera, native-CD or sustained-performance certification.

## What changed

`psx_reversal.c` implements ReversalCondition and BranchFindReversal separately
from ordinary input branches. It preserves signed 16-bit angles/slots, move
flags, descriptor joint matching, input/restriction gates and the original
single saved CALL/RETURN address. Ordinary branches still reject reversal
conditions. The bounded descriptor reader works independently of active-frame
point sampling and distinguishes missing data from a valid zero descriptor.

The USA fingerprint/layout-checked extractor writes `psx_reversal_lists.bin`:
52 positive s16 tag lists, normalized to bounded offsets, 850 bytes total.
The private asset builder embeds that file alongside reaction tables; CMake
tracks it as an input dependency. These bytes remain ignored game data.
Missing or malformed facts fail the match step when a reversal needs them,
instead of skipping a potentially matching row.

`psx_match_reversal.c` runs after HitTest and before events/HitApply in the
original frame parity. FighterOpponent uses the throw partner, persistent
last attacker/contact target or default opponent. The hit phase supplies its
current flags before that lookup. A matching row queues the remapped move,
keeps the branch kind, clears the original hit/contact fields and sets signed
attacker damage / 2 + 25. MoveStart consumes the override before the next
reversal phase clears it. A compact mask keeps incoming-slot cancellation
separate from clearing the selected opponent's contact flags; that opponent
can be the defender itself. HitApply consumes the mask.

Both original opponent indices are seeded once for neutral mid-fight fixtures
and compared on every host/SH-2 tick. No original state is copied into a fresh
match or reapplied after a tick. C adaptations carry pinned-upstream attribution
in their headers and NOTICE.md; no GDScript or game data was committed.

## Original-code differential checks

All pass with the private research environment:

| Check | Cases | Scope |
|---|---:|---|
| ReversalCondition, 0x8002EB24 | 9,056 | Types 0x45–0x4B, signed boundaries, flags, joints; list-query call and signed argument |
| MoveInSlotList, 0x8002D4B8 | 11,856 | Actual original lookup against all 52 USA-normalized lists |
| BranchFindReversal, 0x8002E95C | 1,280 | Authored rows through actual input/restriction/condition gates and common CALL/RETURN |

Tests are `test_psx_reversal.py`, `test_psx_reversal_lists.py` and
`test_psx_reversal_walk.py`; evaluator executables have the corresponding
`t3_psx_reversal_*_eval` names. They skip without the private EXE/dependencies;
a skip does not prove equality. The attack-record tests also cover inactive
moves, ordinary/built-in descriptors, missing tables and output preservation.
A transaction regression covers the self-selected opponent: canceling its
incoming slots preserves the physical attacker's contact and hit-done flags.

## Continuous fight evidence

Authored physical tapes live under `tests/fixtures/`. Current continuous test
`ps1_original_reversal_continuous` checks all declared combat, pose/blend,
body, round and opponent fields, plus host-only HUD/tap checks:

| Private original case | Ticks | Required original outcome |
|---|---:|---|
| `build/parity-reversal-full` | 600 | Counter-throw 3277/3304, 27 damage to Law, Paul returns to slot 3 |
| `build/parity-reversal-control` | 460 | Same two jabs without the reversal input |
| `build/parity-reversal-first-hit` | 360 | First-contact negative: ordinary guarded reaction, no counter-throw |

All 1,420 ticks pass, zero unsupported moves. The original positive first
queues slot 3277 at source frame 368, native tick 367, with transition 33,
entry 1, window 2 and damage override 27. Source 369 starts 3277/3304. At source
601 Paul is back in stance with 140 HP and Law has 113 HP. The initial shorter
positive case (`parity-reversal-candidate`, 460 ticks) remains historical
reproduction evidence. Before integration its matching prefix was 366 ticks:
C instead queued guard reaction 2247 and applied the incoming hit.

The seven existing Paul mirror cases still pass all 14,632 original-code
ticks, including KO, throws and later rounds. The existing Paul/Law case
`build/oracle-law-s1` still passes 3,000/3,000. Current logs use
`build/parity-reversal-final-oracle-paul-*-eval.json` and
`build/parity-reversal-old-oracle-eval.json`. The first-round, throw-escape
and fresh timeout-through-second-intro CTests also pass.

### Why the first-contact negative matters

The initial back+LP+LK/jab tape at source 280 enters slots 494/495 but does
not reverse in the original. A code hook observed `wasHitThisMove` set while
`lastAttacker` (+0x22) is still zero. FighterOpponent therefore supplies fighter
0 itself: its attack/joints do not satisfy Paul's rows. An earlier applied
contact initializes lastAttacker, allowing the later positive case.

This is evidence about this JP Rev.1 harness setup, not a claim that retail
reversals universally fail on first hits. Preserve the stateful lookup instead
of forcing the other fighter. The original call trace and compact earlier
results remain in `build/parity-reversal-first-hit-evidence`; the regenerated
negative fixture now verifies all 360 ticks with the integrated implementation.

## Native artifacts and limits

Final fresh reference passes **600/600 ticks**, error 0, no first difference:
`assets/imported/parity-reversal-reference-final/reference-comparison.json`.
Its ELF is `03cc23d389b097e605ce2d702869b1dd4997fccc7a9cd75f8f967904438d9d0d`.
The matched observation reports first_error 0, draw_timeouts 0, faces_rejected
0 and 2,521 discarded clock fields during the 8,000-field diagnostic run.
This is per-tick equality evidence, not a cadence/zero-discard result.

The shared digest now includes both opponent indices. Tap/HUD audits remain
additional host checks. Screen X and sparse previous-camera views are external
facts, so this does not certify independent camera equality. The reference
uses the original costume-0 model parts (Paul KMD 071) matching the original
tape. The playable configuration's actor-0 parts use KMD 075. Feeding those
into the costume-0 host fixture exposed one-unit joint differences; the host
fixture was corrected to its matching original parts, not its expected states.
Host bank arrays are embedded with `cold=False`; cold Saturn arrays are boot
storage populated from MOTION.BIN and cannot be used uninitialized by a host
executable. Recipes are under `build/host-reversal-{paul-,}assets/recipe.json`.

The long diagnostic initially exceeded its High WRAM stack boundary by 4,496
bytes. Reference builds now keep renderer descriptor-validation ranges in spare
Low WRAM. Production placement is unchanged; both stack reserves remain intact.
Its final BSS ends at 0x060FBAB0 and Low WRAM at 0x002FCC3C, preserving the
16 KiB Master stack reserve. No SDK copy or alternate prefix was created.

Final identities:

- Playable candidate `build/parity-reversal`: ELF `99ef5657620fc1032b999ccc94fa257a23991553f74a310a5392dfc7e19cb192`.
- Long diagnostic `build/flush-trusted-reference`: ELF `ffbb240456a3c9dbc4865feecc64480a776816c5276bc5f7b70bd1868a9ad506`.
- Previous playable control `build/hud-bar-tick`: ELF `7a92db1c0c9deb316bcc6c8faa2421745d71afa3027022fd7ed41a940e795a10`.

The final gate is `assets/imported/parity-reversal-gate`: 61 + 64 host CTests,
all 26 live behavior cases and all 3,000 reference ticks pass, no first
difference. [AIR_RECOVERY_PARITY.md](AIR_RECOVERY_PARITY.md) records subsequent
air/recovery coverage on the same gameplay implementation. An earlier partial
gate was explicitly stopped after the final
hit-slot/contact correction; its summary is labeled superseded in
`build/parity-reversal-pre-mask-gate-summary.json`. It is not a pass.
The completed 600-tick diagnostic build and superseded 460-tick captures were
deleted after validation. The final trace/export and reproduction recipe remain;
only the older comparison JSON is kept as `pre-mask-460-comparison.json`.

## Reproduction

Use fresh output directories or remove only explicitly superseded outputs:

```sh
third_party/Tekken3Remake/work/venv/bin/python scripts/fight_oracle_export.py \
  --out build/parity-reversal-full --name paul_law --chars 0 1 --costumes 0 0 \
  --styles idle idle --seed-round-start --ticks 600 \
  --pad-script tests/fixtures/psx_reversal_candidate.pads
python3 scripts/fight_oracle_eval.py build/parity-reversal-full \
  --evaluator build/host-law/t3_match_replay_eval --initialize-match \
  --export assets/imported/parity-reversal-reference-export
python3 scripts/build_saturn_variant.py --baseline build/flush-trusted-reference \
  --out build/parity-reversal-reference \
  --cmake-option=-DT3_LOCAL_REFERENCE_REPLAY="$PWD/assets/imported/parity-reversal-reference-export"
build/asset-venv/bin/python scripts/saturn_reference_suite.py \
  --build build/parity-reversal-reference --export assets/imported/parity-reversal-reference-export \
  --workload diagnostic --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/parity-reversal-reference-final --frames 8000 --timeout 180 --sh2-cache on
```

The old 3,000-tick fixture was re-exported under the expanded observation
schema as `assets/imported/parity-reversal-reference-control-export`. Older
exports remain historical evidence; their manifest schema hash is intentionally
incompatible with the new field visitor. Do not overwrite their manifests to
make them load. See [THROW_ESCAPE.md](THROW_ESCAPE.md) for the prior candidate's
completed 26-case/3,000-tick gate and [ONE_TO_ONE_CONTINUATION.md](ONE_TO_ONE_CONTINUATION.md)
for the remaining full-port scope.
