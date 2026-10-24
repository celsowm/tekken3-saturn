# Continuous original / host / Saturn reference replay

This opt-in **diagnostic build** closes the aligned native-state comparison for
the supported USA Paul mirror VS encounters. It does not replace interactive
controller tests, CD-boot evidence or the 30 Hz render / 60 Hz logic budget.
The public build neither enables it nor bundles the required private data.

## State and evidence ownership

1. `audit_psx_match_replay.py --original-round-flow` validates the fingerprinted
   USA capture, instruction nesting and independent paused-RAM checkpoint. Its
   native evaluator initializes once from the first neutral entry and compares
   each later committed tick with the original. A failed or partial audit cannot
   produce a usable export manifest.
2. With `T3_REFERENCE_OUTPUT` set, the evaluator records which original RAM
   bytes initialization actually reads. The compact seed contains only those
   spans, with bounded gaps merged to fit 64 descriptors. It also exports the
   mapped gameplay pad samples, early physical/pressed button latch and original
   source-frame counter. Inputs are external IO; clear/script input remains
   native RoundFlow's responsibility. The optional `views.bin` carries the two
   pieces of visual state the original feeds back into gameplay: the previous
   CameraFrame view for the ticks whose RoundFlow draw consumes it, and the
   per-tick screen x that draw/CameraFrame leaves in `+0x1278` for
   `PairwiseDistances`. The view table is sparse (one record per round
   transition) because it is linked into High WRAM; the screen table is four
   bytes per tick.
3. The shared `tools/psx_match_reference.inc` retains the existing seed checks
   and observation fields. The Saturn initializer consumes the compact seed
   once before the first tick. Its ordinary match state then advances through
   `t3_psx_match_step_original`. No expected exit snapshot is supplied per tick.
4. After each committed tick, the shared visitor feeds actual field values,
   in schema order, as signed 64-bit big-endian integers into SHA-256. The guest
   records one 32-byte digest and a field count per tick in Low WRAM, then
   freezes at the capture endpoint. Expected digests stay on the host. Text
   labels and expected-value expressions are removed from the guest visitor by
   macros; they cannot read or apply original return RAM.
5. `saturn_reference_suite.py` resolves every dump address/size from the matching
   ELF. It checks the guest's fixture-manifest identity, committed count, error,
   every digest and every field count. Incomplete traces, changed exports,
   schema mismatches and the first divergent tick are reported explicitly;
   a matching prefix cannot pass the full gate.

The manifest binds all exported files, the original seed/trace, evaluator and
observation schema. The generated image contains initial seed bytes, compressed
external inputs/source-counter deltas, the sparse view/screen table and a
manifest fingerprint. It contains
**no per-tick expected state or expected digest table**. The SHA implementation
passes empty/abc/multiblock/million-a vectors and chunk-boundary tests; private
host replay also passes ASan/UBSan. This is a cryptographic comparison of the
specified values, not a byte-for-byte comparison of native C structs/padding.

## Declared comparison coverage

| Category | Observed values |
| --- | --- |
| Ground | Every `T3_GROUND_EVAL_FIELDS` member and X/Z step accumulators |
| Body | Eight sphere centers/radii per actor and separation push XYZ |
| Move | Slots, pose/root/move clocks, freeze/hold/frame step, ballistic/transition state, pending/queued move facts |
| Hit | Q16 HP, guarded/clean/done flags |
| Blend | Mode/activity, length/counter/weight, last frame, root terms/endpoints and all previous/delta local matrices |
| World pose | All 18 joint origins and rotation matrices per actor |
| Round | Every `T3_ROUND_FLOW_EVAL_FIELDS` member, actor scores/result/victory/activity/carried health/script input |
| Replay | Mode, record/play/KO mark/recorded/skip and outer round skip |

These captures check 1,349–1,357 scalar values per tick. The continuous state
comparison covers movement, high/mid/low, guard, damage/reactions, timeout, KO
playback, winner animation/frozen loser, later intro/reset and terminal VS.
Initial screen sides stay explicit. Team/arcade/survival, throws/juggles,
arbitrary camera-side remapping, every internal input-history byte, hand/wing
variants, audio/effect logs and exact view/projection rendering are not certified.
The guest fixture supplies the same native API IO as the host; physical Saturn
pad/SMPC wiring is exercised by the separate live behavioral suite.

## Reproduction

Keep all exports, BIOS and retail-derived builds under ignored paths. First
configure the existing private host evaluator and a separate Saturn build with
the same validated toolchain/model/bank/table inputs as `build/paul-memory`.
The commands below assume those build directories are already configured.

```sh
mkdir -p assets/imported/local-reference-export
T3_REFERENCE_OUTPUT="$PWD/assets/imported/local-reference-export" \
  python3 scripts/audit_psx_match_replay.py \
    --capture assets/imported/ps1-paul-ko-replay-continuous \
    --evaluator build/host/t3_match_replay_eval --original-round-flow \
    --output assets/imported/local-reference-host-audit

cmake -S . -B build/paul-reference \
  -DT3_LOCAL_REFERENCE_REPLAY="$PWD/assets/imported/local-reference-export" \
  -DT3_CAPTURE_INPUT_TAPE=OFF -DT3_LOCAL_INPUT_TAPE= -DT3_RENDER_BENCH_TICK=0
PATH="$HOME/dev/saturn-tools/bin:$PATH" cmake --build build/paul-reference -j4
python3 scripts/saturn_reference_suite.py \
  --build build/paul-reference --export assets/imported/local-reference-export \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/local-reference-saturn
```

The default probe runs 48,000 emulator frames, allowing the diagnostic to finish
its capture. Its hash overhead deliberately affects execution speed; do not use
this variant to claim live cadence or latency. It visibly labels itself
`PS1 REFERENCE REPLAY`. Ymir still uses injection: no CD-boot claim follows
from its passing state comparison. Native image, stock High/Low WRAM bounds
and the 16 KiB Master stack reserve remain enforced.

## Verified checkpoint (2026-10-17)

| Capture / report directory | Consecutive ticks | Compared scalar values | Result |
| --- | ---: | ---: | --- |
| P1 timeout, perfect KO / `paul-reference-p1-final` | 5,481 | 7,396,953 | All match |
| P2 timeout, perfect KO / `paul-reference-p2-final` | 5,632 | 7,601,124 | All match |
| Two draws, terminal 2–2 / `paul-reference-draw-final` | 5,221 | 7,043,897 | All match |

The three independently seeded sequences total **16,334 ticks / 22,041,974
scalar comparisons**. Every declared host/original value first passes its
original RAM comparison; every corresponding Saturn tick then matches the
host digest and field count, using the same shared visitor schema. Reports
include exact ELF, probe, BIOS, SDK, research, fixture and trace hashes.

The P1 export uses 6,144 compact seed bytes, 247 external-input changes (4,446
bytes) and 1,370 source-counter bytes. Its 175,392-byte digest buffer and
10,962-byte field-count buffer fit in Low WRAM. Initialized image sizes are
981,112 (P1), 980,408 (P2), and 976,564 (draw), below the 983,040-byte loader
limit. Reference code uses size optimization; the live core remains unchanged.

This closes the **declared continuous native-state gate**, not the whole
[playable milestone](NEXT_PLAYABLE_MILESTONE.md). The remaining live performance
shortfall and presentation-camera calibration stay in the continuation plan.

## Verified checkpoint (2026-10-24, Paul vs Law)

| Capture / report directory | Consecutive ticks | Compared scalar values | Result |
| --- | ---: | ---: | --- |
| Paul mirror VS Law, USA, one round / `paul-law-reference-final` | 3,000 | 4,168,960 | All match |

This export closes the last divergent tick of the continuous replay. At source
frame 2,340 the original's RoundFlow fighter draw leaves the opponent at screen
x >= 0x191, so `PairwiseDistances` takes its off-screen stand-in (target 0x4000,
distance 480) and `FighterRelativeAngles` skips the `aim_dir` update. Feeding
only the view was not sufficient: the draw also has to establish the per-tick
screen x, otherwise the stand-in decision freezes. The fixture therefore holds
one 38-byte view record (tick 2,183, the `T3_ROUND_WIN_POSES` transition draw)
and 3,000 four-byte screen pairs; host runs reproduce the failure with
`T3_REPLAY_NO_VIEW=1` and `T3_REPLAY_NO_SCREEN=1`.

The fixture uses 6,240 seed bytes, 1,259 external-input events (27,698 bytes),
750 source-counter bytes, 38 view bytes and 12,000 screen bytes. The 96,000-byte
digest trace and 6,000-byte field-count trace sit in Low WRAM; the initialized
image is 616,100 bytes, below the 983,040-byte loader limit. Expected state and
digests remain host-only.

The 2026-10-17 P1/P2/draw exports above are bound to the observation schema of
that revision. Their `observation_schema_sha256` differs from the current
`tools/psx_match_reference.inc` hash, so today's fixture generator refuses them;
that drift predates this checkpoint and would need a fresh original audit to
regenerate. The loader still accepts view-less exports (covered by
`tests/test_reference_replay.py`).
