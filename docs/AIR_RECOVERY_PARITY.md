# Airborne reactions and recovery evidence

Checkpoint: 2026-10-26. Six authored original-code cases match the existing
owned C simulation for 3,600 continuous host ticks. A separate rendered SH-2
reference matches the 600-tick juggle case. No gameplay correction was needed
for these cases; this slice adds regression coverage and records its limits.
It does not close all launch modes, the roster, camera or audiovisual parity.

## Reference and inputs

Before capturing, the pinned remake's `combat.md`, `moves.md`,
`remake/core/fighter/move_system.gd` and the existing ground implementation
were inspected. Original FightMain runs through the research Unicorn harness;
USA-layout snapshots are only comparisons. The C match starts from configuration
and receives input/camera-screen facts, without any fighter/round state seed or
per-frame replacement. Source frames 2–601 are all compared, including intro,
contact, air movement, landing and the subsequent recovery.

The input tapes under `tests/fixtures/` are authored physical PS1 pad changes,
not converted bank or model data. All traces are private under `build/`.
The `candidate` name below is the first launch-input scenario, not a claim
that it exercises the explicit launch transition 0x1E.

## Host outcomes

Every declared combat, body, pose/blend, round and opponent field matches;
the host also checks HUD and tap state. Every case has zero unsupported moves.

| Private case | Ticks | In-air ticks | Ballistic ticks | Final Law HP |
|---|---:|---:|---:|---:|
| `parity-launch-candidate` | 600 | 51 | 0 | 125 |
| `parity-launch-control` | 600 | 0 | 0 | 140 |
| `parity-launch-hopkick` | 600 | 52 | 0 | 125 |
| `parity-launch-juggle` | 600 | 93 | 36 | 122 |
| `parity-recovery-mash` | 600 | 48 | 0 | 125 |
| `parity-recovery-early-mash` | 600 | 51 | 0 | 125 |

Paul retains 140 HP throughout these tapes. In-air counts include later
recovery animations where applicable; they are not durations of one continuous
flight. The candidate and hopkick attacks use reactions 2538/2543 whose animation
moves the body root upward. The anchor Y and ballistic flag alone would miss
that flight: minimum original root Y is -2370/-2624 and inAir is set. Those
are actual airborne reactions even though ballistic remains zero.

The juggle adds a jab after Paul's first move recovers. The original starts
reaction 3414 (transition 0x24) at source 339, has 36 ballistic ticks and sets
landedB at source 375. Its animation continues after the ballistic landing;
the next recovery slot starts at source 389. The C root, velocity, timing,
branch and damage fields match throughout, including the airborne damage
reduction to 3 additional HP.

Early RP pulses occur while recoverMash is nonzero. At source 299 the
unpressed case has 11 and the RP case has 8; at source 303 they are 7 and 0.
Natural countdown reaches zero at 310. Later pulses, after the counter already
expired, select a different recovery branch: slot 3421 at source 346, then
3422 and 373. Therefore the early and late tapes verify different behavior;
neither is treated as a generic claim that all button mashing gets up sooner.

`tests/test_psx_air_recovery.py` requires these original outcomes and coverage,
all 600 consecutive source frames, all comparisons passing and no dropped
moves. It also requires the actual ballistic landing and the early counter
difference. A standing/guarded or shortened fixture cannot satisfy the test.
CTest name: `ps1_original_air_and_recovery_continuous`; it skips when private
fixtures are absent. A skip does not certify parity.

Latest Law host census is 65/65 passed in `build/parity-air-all-host-tests.log`.
The release gate collected before this additional CTest has its own 61 + 64
census; its JSON was not edited to inflate those counts.

## Native juggle evidence

`assets/imported/parity-launch-reference/reference-comparison.json` passes
600/600 ticks, error 0, no first difference. ELF:
`c68b66bfb459d944a88b04efd8f32ab28e53b2d7ef8ee461e23f07a00f802e1a`.
The matching observation reports first_error 0, draw_timeouts 0 and
faces_rejected 0. It also reports 2,511 discarded clock fields during the
8,000-field diagnostic run. This is equality evidence for every completed
tick, not a zero-discard, cadence, CD-boot or real-hardware measurement.
The completed `build/parity-launch-reference` directory was removed after
validation. Its private export, observed trace, JSON, screenshot and recipe
remain under the documented evidence paths.

The reference uses the costume-0 parts matching the original tape, as detailed
in [REVERSAL_PROGRESS.md](REVERSAL_PROGRESS.md). Per-tick screen positions
remain external camera facts. No independent camera claim follows from this
comparison. The gameplay source is the same qualified revision as playable
ELF `99ef5657620fc1032b999ccc94fa257a23991553f74a310a5392dfc7e19cb192`;
this diagnostic has its own model/configuration/trace identity.

The previous reversal candidate's full release gate is now complete:
`assets/imported/parity-reversal-gate` passes its 61 + 64 host tests, all 26 live
cases and all 3,000 reference ticks. The new air CTest is additional coverage;
no new gameplay binary was substituted into that saved verdict.

## Remaining air/transition scope

These traces cover animation-driven air reactions and the ballistic juggle
transition 0x24. They contain no explicit air-kind-5 transition 0x1E. The common
bank's special launch branches have other character restrictions, so Paul/Law
inputs do not establish their integration. The match does not yet advertise
T3_FACT_LAUNCH, and its ordinary input-queue policy still rejects transitions
30–32, 34 and 36, even though some are already supported through dedicated
reaction/throw paths. Those restrictions need original cases before their
removal; passing the six traces is not evidence that those paths work.
Recovery coverage is these exact early/late input windows, not every get-up,
tech roll, side/back knockdown or juggle orientation.

## Reproduction

Generate each case with the corresponding authored tape and a fresh output:

```sh
third_party/Tekken3Remake/work/venv/bin/python scripts/fight_oracle_export.py \
  --out build/parity-launch-juggle --name paul_law --chars 0 1 --costumes 0 0 \
  --styles idle idle --seed-round-start --ticks 600 \
  --pad-script tests/fixtures/psx_launch_juggle.pads
python3 scripts/fight_oracle_eval.py build/parity-launch-juggle \
  --evaluator build/host-law/t3_match_replay_eval --initialize-match \
  --export assets/imported/parity-launch-reference-export
python3 scripts/build_saturn_variant.py --baseline build/flush-trusted-reference \
  --out build/parity-launch-reference \
  --cmake-option=-DT3_LOCAL_REFERENCE_REPLAY="$PWD/assets/imported/parity-launch-reference-export"
build/asset-venv/bin/python scripts/saturn_reference_suite.py \
  --build build/parity-launch-reference --export assets/imported/parity-launch-reference-export \
  --workload diagnostic --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/parity-launch-reference --frames 8000 --timeout 180 --sh2-cache on
ctest --test-dir build/host-law -R ps1_original_air_and_recovery_continuous --output-on-failure
```

Other tape names are `psx_launch_{candidate,control,hopkick}.pads`,
`psx_recovery_mash.pads` and `psx_recovery_early_mash.pads`. Fixtures and hashes,
not only a narrative claim, remain in the ignored evidence directories. Keep
screenshots/JSON/logs; no recording was created for this slice.
