# Independent camera and physical-input comparison on SH-2

Checkpoint: 2026-10-27, HEAD `a5aa52f-dirty`.

The reference diagnostic can now initialize its own match and run the camera,
physical input mapping and gameplay without original screen positions or views.
The original-code harness restores retail presentation instructions before
setup, as described in [RETAIL_CAMERA_PARITY.md](RETAIL_CAMERA_PARITY.md).
The executable is Japan Rev.1 SHA-256
`09e8b0b6575c6ff1c81f7f827d31daf2770fa4c0a586e946b6f10f7613dd0432`.

## Observation contract

The `owned-camera-v1` manifest uses physical pad words and source frame
counters. Mapped inputs and external sides must be zero. `views.bin` is
forbidden, even if omitted from the file inventory. Startup uses configuration
and a 73-byte range file containing character/bank/voice IDs, maximum HP,
chip damage and collapse settings; fighter state is never loaded from the
original seed. There is no per-tick state reseeding.

`tools/psx_owned_reference.inc` shares the additional state visitor between
the host and SH-2. It hashes camera matrix/translation/projection, five camera
sources, weights/state/phase, current and previous targets, stale third-target
slot, screen positions, three RNG states, both complete 60-entry input
histories, held/pressed inputs, sound-script state/timers, blink state,
HP-bar state and tap latches. These accompany the existing combat observer.
The fixtures visit 1,628, 1,632 or 1,636 fields per tick, depending on pending
moves. Each value is signed-i64 big endian before SHA-256.

Expected RAM reads execute only in the host audit. The Saturn visitor hashes
actual state only; expected per-tick digests stay on the host. A runtime
`g_reference_owned_camera` flag confirms that the guest retained its camera
configuration. The comparator rejects a missing/zero flag. Owned replay also
has a compile-time camera requirement. The original legacy observation schema
and digest remain separate and continue to describe external view feedback.

Nineteen integrity tests cover both protocols, including rejection of missing
fresh initialization, patched-original provenance, mapped input, side/view
feedback, invalid executable identity and legacy-schema substitution.

## Continuous cases

The following injected Ymir runs compare every declared tick, starting from
configuration. No frames are filtered and unsupported moves are rejected.

| Case | Ticks | Guest trace SHA-256 | Diagnostic ELF SHA-256 |
| --- | ---: | --- | --- |
| Held physical right through side crossing | 500 | `47d0854b4cdc67b6ede7b0b56cfb04526b49f03642ce4b34c6f0ea8a2a3b695a` | `b4e50da58050d00c94ae24040016a58b1462530a8a3a69da6a439de9fb0618bb` |
| Reversal, Low WRAM filled with `0xA5` | 600 | `056ecb64c20a8498847f7d494dce9f07afce20dcbf54c5d406167200caa34774` | `dda68d7adec4fe75ab5394d46179f3ee7990d5f8e15cc5903e7e0105b5e16d4f` |
| Timeout and next intro, Low WRAM filled with `0xA5` | 3,000 | `68410b3f222e5d33eded131025528309d7b064c79b0fa3e7d93d9c7b851a6304` | `6c23db7ee1934274efa68b60a9b05eff87939819054ff0be461ae9a347a63911` |

All three runs completed with error counter zero and matching digests on every
tick: **4,100 independent SH-2 ticks** in total. The shorter runs preceded the
round-gap fix; the 3,000-tick run uses the final core. Evidence is
`assets/imported/parity-owned-reference-{crossing,reversal,timeout}-saturn/`;
exports use the same prefix and end in `-export`, `-reversal-export` and
`-timeout-export` respectively.
`build/parity-owned-reference-summary.json` records artifact identities.
The diagnostic tree is rebuilt in place, so earlier ELF hashes identify saved
results rather than the current executable in that directory.

## Timeout and the main-loop RNG

A fresh 3,000-tick retail trace exposed the first divergence at source frame
2720: only camera RNG differed. During the three FightMain frames between
rounds the port had stopped advancing it because the director was idle.
The remake's `fight_simulation.begin_frame` and `fight_harness.step` document
that this generator advances once per main-loop frame, before FightMain.

The C update now runs before round-gap/terminal early returns, separately
from additional draws inside camera rules. Removing the redundant director
and replay updates preserves the short fixtures. All 3,000 ticks then match
on the host, including timeout and the next intro. The retail continuous
CTest includes this fixture when its private capture is present. Both host
suites pass: 73/73 Paul–Law and 64/64 Paul.

The final SH-2 diagnostic also matches all 3,000 ticks with Low WRAM filled
with `0xA5`, no runtime/draw/decode errors or rejected faces, and 797/2,048
commands at capture. It runs 48,000 emulator fields, with a 591,168-byte
payload, High-WRAM BSS end `0x060F6440`, Low-WRAM end `0x002FF354` and the
16 KiB master stack reservation. Manifest identity is
`3624ab066ccf03e114a9d88cbb44ae81463befec2214981aedb0aaa25f9ca2d6`;
observation schema is
`e2c675bb404fd6124e6e16c2caec040e9ef7a623d28d386720b8ad9a2750aaeb`.

## Reproduce

The private diagnostic uses Paul model 071 and Law's matching costume, no
pose cache, a High-WRAM trace/count array, a 16 KiB stack, camera enabled and
no title screen. Reuse the installed SDK and existing diagnostic tree.

```sh
python3 scripts/fight_oracle_eval.py build/parity-owned-timeout \
  --evaluator build/host-law/t3_match_replay_camera_eval \
  --initialize-match --independent-camera \
  --export assets/imported/parity-owned-reference-timeout-export
PATH=/Users/celso/dev/saturn-tools/bin:$PATH cmake -S . -B build/flush-trusted-reference \
  -DT3_PSX_CAMERA=ON -DT3_LOCAL_POSE_CACHE_ENTRIES=0 \
  -DT3_REFERENCE_TRACE_HIGH=ON \
  -DT3_LOCAL_REFERENCE_REPLAY="$PWD/assets/imported/parity-owned-reference-timeout-export"
PATH=/Users/celso/dev/saturn-tools/bin:$PATH cmake --build build/flush-trusted-reference
python3 scripts/saturn_reference_suite.py \
  --build build/flush-trusted-reference \
  --export assets/imported/parity-owned-reference-timeout-export \
  --bios assets/imported/ymir/profile/roms/ipl/saturn_101a_us.bin \
  --out assets/imported/parity-owned-reference-timeout-saturn \
  --frames 48000 --timeout 600 --port2 pad --wram-low-fill 165
```

Use a new empty capture directory when repeating a run. The saved original
capture is generated by `fight_oracle_export.py --seed-round-start --ticks
3000 --styles idle idle`, with default retail restoration, characters 0/1,
costumes 0/0 and `--name paul_law`.

## Limits

The playable `build/parity-reversal` was rebuilt with the round-gap RNG fix:
ELF SHA-256 `79c0324fd4d85d25f9eefae00acbd38a63178bfcd6ebd5e04bfda3413eb54dbe`,
589,204 initialized bytes, High-WRAM BSS end `0x060FB9B0`, with the 16 KiB
master stack reservation retained. Injected smoke captures at
`assets/imported/parity-owned-reference-{neutral,high-hit}-saturn/` pass with
zero errors, draw timeouts, attack decode errors or rejected faces. Neutral
runs 196 ticks in 600 fields (HP 140/140); high-hit runs 516 ticks in 920
fields (HP 140/134). Their command counts are 741 and 756 of 2,048.

These are rendered, instrumented native diagnostics. They do not certify CD
boot, display cadence, pixel identity, native sound playback or real hardware
speed. Sound decisions/state are compared, but the new logical requests still
need a committed replay/native playback bridge. Natural rare camera edges,
KO/replay with the retail policy, other fighters and full modes remain open.
The subsequent complete release gate at
`assets/imported/parity-owned-camera-gate/summary.json` passed all stages:
64/73 host tests, 26 live combat cases (including KO/replay, terminal result
and timeout), and 3,000 independent-camera reference ticks. It binds the
playable and diagnostic ELFs above and took 995.73 seconds. The reference
trace digest is identical to the earlier Low-WRAM-fill run; this gate uses
normal startup memory. It does not extend exact parity to all 26 live cases.
The subsequent varied-input retail fight still fails because effect objects
are missing; see [HIT_SPARK_PROGRESS.md](HIT_SPARK_PROGRESS.md).

No recording or SDK copy was created. Disk usage is 1.4 GB build / 4.4 GB
imports, below the mandatory 5 GB / 20 GB limits. The rejected camera-disabled
capture was deleted; its rejection counters remain in
`build/parity-owned-reference-initial-rejection.json`.
