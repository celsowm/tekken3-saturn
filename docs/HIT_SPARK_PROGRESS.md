# Hit sparks: original shared-RNG gap and phase port

Checkpoint: 2026-10-27, HEAD `a5aa52f-dirty`.

This records the pre-integration failure and phase proof. The subsequent
[EFFECT_POOL_INTEGRATION.md](EFFECT_POOL_INTEGRATION.md) closes the 3,000-tick
host fight gap and compares its semantic effect state continuously. Use that
report for current candidate identity and the remaining native/render gates.

A new 3,000-tick retail Paul–Law fixture with varied inputs revealed a missing
simulation system: the effect-object pool. The first difference is shared
`rand()` at source frame 222. The 300-entry sound recorder alone cannot fix
this; particle initialization consumes the same generator as voices, eyes
and some camera decisions.

## Continuous original-code result

The fresh fixture is `build/parity-replay-sounds-retail-fight`: characters 0/1,
costumes 0/0, stage 0, seed 15, human inputs generated with styles
`random/idle`, first-round setup, source frames 2–3001. Retail instructions
are restored before setup; no gameplay fixes, source filtering or state
reseeding are used. The original executable identity remains
`09e8b0b6575c6ff1c81f7f827d31daf2770fa4c0a586e946b6f10f7613dd0432`.

The fresh owned-camera evaluator matches the first **220 ticks**, then fails
at source **222**: C `rand=3197828885`, original `rand=3744430561`.
Advancing the former by exactly 52 libc draws reaches the latter. That is a
diagnostic observation, not a rule to insert a fixed draw count into the port.
The complete 3,000-row failing tail is retained in
`native-camera-before-rng.jsonl`, with the first-failure log at
`build/parity-replay-sounds-retail-fight-first-failure.log`.

The read-only `audit_psx_effect_rng.py` reproduces the original prefix and
observes actual RNG call sites and allocation-ordered live object state.
Evidence: `build/parity-effects-rng-audit.json`.

| Source frame | Original RNG callers | Live objects |
| --- | --- | --- |
| 220 | none | 8 type-3 power glows |
| 221 | 1 at `0x80041934` (sound decision) | 8 glows, 20 type-2 and 6 type-1 sparks, all sparks newly spawned |
| 222 | 20 each at `0x800706C8` / `0x800706D0`; 6 each at `0x800702DC` / `0x800702E4` | The same spark population initializes |
| 223–224 | none | Spark trajectories continue |

The 26 particles are allocated after the previous frame's EffectsUpdate and
initialize on the following pass. Later eye timers, voice cooldowns and RNG
then diverge. The complete fight is **not passing**. Existing fixed hit/miss
Saturn cases do not prove this shared-RNG path.

```sh
third_party/Tekken3Remake/work/venv/bin/python scripts/audit_psx_effect_rng.py \
  --case build/parity-replay-sounds-retail-fight --start 220 --end 224 \
  --out build/parity-effects-rng-audit.json
python3 scripts/fight_oracle_eval.py build/parity-replay-sounds-retail-fight \
  --evaluator build/host-law/t3_match_replay_camera_eval \
  --initialize-match --independent-camera
```

The second command currently exits nonzero. Do not treat the component proof
below or the release gate as a passing result for this fixture.

## Spark phase implementation

`src/game/psx_spark.c` adapts the normal/power spark phases from pinned
Tekken3Remake `remake/core/effects/effect_objects.gd` and `effects.md`, checked
against original functions `0x800700EC`, `0x800704D8`, `0x8007379C` and
`0x80073B94`. The file and `NOTICE.md` carry the upstream attribution.

Types 1/12 implement six decelerating directional sparks; types 2/13 implement
the constant-speed spray of twenty. The caller owns state and the actual
shared RNG. The routines preserve initialization's two draws, angle wrapping,
position and velocity arithmetic, first-frame position offset, age/lifetime,
floor expiry, normal/power model and both double-buffered CLUTs. Expiry sets
type/state to zero and flags to `0x80`; delayed unlinking belongs to the pool.
There is no synthetic 52-draw adjustment.

`tests/test_psx_spark.py` compares **1,384 cases / 18,048 original updates**:
all four types, fresh and continuing phases, full independent trajectories
up to 55 updates, both display buffers, lifetime/floor boundaries, angle and
coordinate wrapping, inactive phases and varied seeds. It executes unpatched
MIPS, including the actual original palette-ramp initializer, rand and motion
helper. It compares the canonical model/state/flags, both CLUTs, angles,
speed, all XYZ position/velocity fields, four counters and final RNG.
Every case matches. No original draw primitive is copied to C.

Both full host suites pass: **75/75 Paul–Law**, **66/66 Paul**. Logs:
`build/parity-effects-spark-differential.log`,
`build/parity-effects-spark-host-law-tests.log`,
`build/parity-effects-spark-host-tests.log`.
The new component compiles with the installed SH-2 toolchain. Rebuilding the
playable target leaves its ELF byte-identical because no match code calls or
allocates the component yet.

```sh
cmake --build build/host-law
ctest --test-dir build/host-law -V -R '^ps1_original_spark_phases$'
```

## Integration required

Preserve the original 80-object allocation limit and allocation-order update,
including same-pass appends, delayed type-0 removal, eight initial power glows,
replay reset and result clearing. Connect actual hit effects and their contact
point/direction after the effects pass, rather than drawing an inferred count
from a registered-hit counter. Power glows, emitted particles, voice echoes
and the replay effect log share capacity and cannot be omitted to certify a
complete pool. Projectile types additionally publish collision segments.

Storage must remain outside atomic per-tick match copies, with successful-step
commit. The current canonical spark record is 56 bytes, so even an 80-slot
spark-only array would take 4,480 bytes. The qualified native map leaves
1,644 bytes below the Low-WRAM reserved boundary and 1,616 bytes after the
High-WRAM 16 KiB master stack reservation. Packing/reclaiming storage and
measuring the resulting runtime cost are required before adding the pool;
a smaller particle cap would change the original RNG and is not acceptable.

After integration, rerun this entire failing fixture with the same raw pads,
then prove pool/state and timing on SH-2 before claiming the RNG gap closed.
Connect the sound ring and native playback as described in
[REPLAY_SOUND_RING.md](REPLAY_SOUND_RING.md). The whole 1:1 port remains open.

## Qualified native baseline

`assets/imported/parity-owned-camera-gate/summary.json` is complete and passed:
26 live combat cases (784.71 seconds) and all 3,000 independent-camera timeout
reference ticks (199.30 seconds); total 995.73 seconds. Its host census is
64/73, before the two standalone component tests added here and in the prior
checkpoint. Subsequent full 66/75 suites pass separately.

Playable ELF:
`79c0324fd4d85d25f9eefae00acbd38a63178bfcd6ebd5e04bfda3413eb54dbe`.
The post-component rebuild also preserves the exact gate's app and ISO hashes:
`2493bfdf61e26120605fac1530c1d832539726e66bcbd7ff3e5e2c6c25f9edc6`
and `a8cfbea287b53b18e7dbe4a1b600350d157ff27a0154e7f166d57f0839c631bf`.
Diagnostic ELF:
`6c23db7ee1934274efa68b60a9b05eff87939819054ff0be461ae9a347a63911`.
Reference manifest:
`3624ab066ccf03e114a9d88cbb44ae81463befec2214981aedb0aaa25f9ca2d6`.
Guest/expected digest file SHA-256:
`68410b3f222e5d33eded131025528309d7b064c79b0fa3e7d93d9c7b851a6304`.

The 26 live assertions are gameplay regressions, not continuous original-code
parity of each case. The exact 3,000-tick reference is neutral timeout/next
intro. Neither establishes CD boot, full image/audio identity, cadence or
all-move/roster coverage. The new varied-input fixture exposes their scope.
No recording, build variant or SDK copy was made. Disk remains 1.4 GB build /
4.4 GB imports. The temporary RNG probe and duplicate hand-built sound-ring
runner were removed after the committed auditor/CMake targets superseded them.
