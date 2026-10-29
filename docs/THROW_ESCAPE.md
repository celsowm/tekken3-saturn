# Original throw-escape tap latch

Checkpoint: 2026-10-26. The original input latch now feeds branch conditions
0x3A/0x3B/0x3C in the owned match. This closes a demonstrated missing input
state for Paul's d/f+1+2 throw against Law; it does not certify all throws,
reversals, characters or independent camera behavior.

## Failure and correction

The original accepted LP+RP at source frame 316 while Law was in victim slot
3143. The native core previously never supplied T3_FACT_BUTTON_TAPS, so its
first difference was native tick 315 (source 316): branch window -1 versus
13 and pending slot -1 versus 2899. No frames after that difference were
filtered out.

The pinned remake already documents the rule in
`remake/core/fighter/fighter_input.gd`, `move_system.gd` and
`docs/research/code/game-bugs.md` (bug #52). The C adaptation keeps:

- LP, RP and LP+RP flags, previous entry and the condition-used marker per fighter.
- A newest-history byte containing **pressed** buttons and direction.
- Both InputSource calls before the fixed P0/P1 latch calls, followed by the
  parity-selected MoveStep order.
- The original global frame/seen guard: a repeated round frame does not latch.
- Sticky flags: setting one does not clear the others. A frame without a fresh
  punch clears them only when the latch is not frozen.
- The original frozen-latch stale-register behavior (#52). InputUpdate leaves
  pressed bits in the shared scratch register, InputClear leaves 5, and a
  non-frozen P0 latch can replace it before P1 sees it.
- A condition-used write even when a tested tap condition returns false.
  Move starts clear that marker, including immediate USA KO collapse.
  InputClear resets the previous entry and does not clear the tap flags.

`psx_tap_latch.c` carries upstream attribution in its header and NOTICE.md.
No converted bank rows or original RAM are public fixtures.

## Original-code and host evidence

`tests/test_psx_tap_latch.py` executes the original JP Rev.1 routine at
0x80040D68 in Unicorn and compares the C evaluator's complete latch output,
including the scratch register. All 3,960 cases pass: the valid flag/button
combinations, same-frame calls, frozen and unfrozen states, condition markers
0/1/2, plus deterministic randomized byte/register states.

Two authored physical-pad tapes are tracked under `tests/fixtures/`:
`psx_throw_escape_control.pads` and `psx_throw_escape_candidate.pads`.
The private original traces are `build/parity-escape-control` and
`build/parity-escape-candidate`. Both run 460 ticks from configuration, without
copying the original fighter or round state. Both pass every existing combat,
pose/blend, body, round and HUD comparison. They additionally compare all three
tap flags, the condition-used marker, previous entries and frame-seen on every
tick: 920 complete tap-state ticks, zero unsupported moves.

At source 317 the control remains in slots 2848/3143; the escape tape starts
3064/2899. The held attacking tape later produces a second throw, which does
40 damage. Therefore final health alone does not prove this escape: the
intermediate branch and the entire following timeline are checked.
`tests/test_psx_throw_escape.py` requires those original branch outcomes and
full tap-state coverage, preventing a neutral or shortened fixture from
passing. The existing 840 first-round host ticks continue to pass.

An additional fresh-configuration 3,000-tick neutral trace now passes across
the first timeout, result, replay-done path, win wait, outer round gap and
second-round placement/intro. It checks all tap and HUD fields on every tick,
with zero unsupported moves. The original trace contains round states
0/1/2/4/6/9 and round numbers 1/2. Evidence is
`build/parity-round-timeout-oracle` and `build/parity-round-timeout-eval.json`.
`ps1_original_fresh_timeout_continuous` requires those transitions; it passed
alongside the throw-escape test in `build/parity-round-timeout-tests.log`.
This adds 3,000 host ticks, not a new Saturn or camera result. The Law host
configuration now has 60 tests; the main gate's earlier host stage had 59.

The completed host runs before the final immediate-KO reset review passed
58 tests in `build/host` and 59 in `build/host-law`. Final-tree results are
recorded separately by `assets/imported/parity-escape-gate`.

## Saturn evidence and memory

An initial rendered SH-2 reference run passed all 460 ticks, with no original
fighter-state seed: ELF `f8eded48f82fa8a970350d2b65eebf44d5a45f666fbaad1bfd2fc791c2384ada`.
Its comparison JSON is retained as
`assets/imported/parity-escape-reference-final/pre-ko-reference-comparison.json`;
the superseded emulator output directory was deleted.
The host-only extra input/HUD checks are explicitly outside its shared combat
digest. Per-tick screen X remains external input, so this is not independent
camera certification.

The tap integration initially exceeded the long diagnostic's High WRAM stack
boundary by 448 bytes. Reference builds now place the two auxiliary frontend
match snapshots (872 bytes total) in spare Low WRAM. The owned PS1 match stays
in its normal placement; production placement and the 16 KiB Master stack
reserve are unchanged. The rebuilt diagnostic's BSS ends at 0x060FBE70 and
Low WRAM ends at 0x002FB48C at this checkpoint.

An 8,000-field run of the old 3,000-tick fixture was **incomplete**, not a parity
failure: all 1,490 observed ticks matched, error 0. Retained evidence is
`assets/imported/parity-escape-reference-control`. Use the full regression
window rather than counting that short run as a pass.

Final revision identities, after the immediate-KO marker reset:

- Playable ELF: `7a92db1c0c9deb316bcc6c8faa2421745d71afa3027022fd7ed41a940e795a10`.
- Long reference ELF: `a3a75b2e845101e6ae812421b4057ed2543a71687f4d73ef0f600ad94b20ad1e`.
- Fresh escape reference ELF: `e010461915255ac034c22619d2cdb87ec9d10076cd8ba84266bf6594bf092f8b`.

The final fresh reference passes 460/460 ticks, error 0 and no first difference:
`assets/imported/parity-escape-reference-final/reference-comparison.json`.
The complete host/live/long reference gate passes under
`assets/imported/parity-escape-gate`: 58 + 59 host tests, all 26 live behavior
cases, and 3,000/3,000 reference ticks with no first difference. This evidence
belongs to the ELF identities above; subsequent reversal work has its own
checks. No new native-CD or sustained-performance result is claimed by this
change.
The completed `build/parity-escape-reference` diagnostic directory was removed
after validation; its private export, final comparison and reproduction recipe
remain. The playable candidate and long reference control remain available.

## Reproduction

Use a fresh export directory (or delete only its explicitly superseded trace):

```sh
third_party/Tekken3Remake/work/venv/bin/python scripts/fight_oracle_export.py \
  --out build/parity-escape-candidate --name paul_law --chars 0 1 --costumes 0 0 \
  --styles idle idle --seed-round-start --ticks 460 \
  --pad-script tests/fixtures/psx_throw_escape_candidate.pads
python3 scripts/fight_oracle_eval.py build/parity-escape-candidate \
  --evaluator build/host-law/t3_match_replay_eval --initialize-match \
  --export assets/imported/parity-escape-reference-export
python3 scripts/build_saturn_variant.py --baseline build/flush-trusted-reference \
  --out build/parity-escape-reference \
  --cmake-option=-DT3_LOCAL_REFERENCE_REPLAY="$PWD/assets/imported/parity-escape-reference-export"
build/asset-venv/bin/python scripts/saturn_reference_suite.py \
  --build build/parity-escape-reference --export assets/imported/parity-escape-reference-export \
  --workload diagnostic --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/parity-escape-reference-final --frames 8000 --timeout 180 --sh2-cache on
```

Use the control tape with the corresponding control export path to reproduce
its 460 ticks. `ctest --test-dir build/host-law --output-on-failure` includes
the continuous comparison when both private traces exist and skips it when
absent. The original-routine differential also skips without the private EXE
or Unicorn environment. Neither skip proves original parity.

For the long timeout fixture use the same original export command with
`--out build/parity-round-timeout-oracle --ticks 3000` and omit `--pad-script`.
Then run `tests/test_psx_throw_escape.py --runner build/host-law/t3_match_replay_eval --timeout`.
