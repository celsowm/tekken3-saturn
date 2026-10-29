# Effect pool integration in the owned fight

Checkpoint: 2026-10-27, HEAD `a5aa52f-dirty`.

The varied-input retail fight that first diverged at source frame 222 now
matches all **3,000 host ticks**, including its camera, shared RNG, eyes,
sound decisions, input histories, combat, round flow and HUD. The cause and
previous failing tail remain in [HIT_SPARK_PROGRESS.md](HIT_SPARK_PROGRESS.md).
This integrates particle simulation; drawing its original sprites remains open.

## Original ordering and state

`psx_effect_pool.c` owns an 80-slot indexed free/live list with the original
allocation order, append/reuse order, retained object bytes, round-start glows,
phase reset and delayed type-0 removal. EffectsUpdate reads the next node
before dispatch, as the original does. Capacity stays 80; allocation failure
has the original partial-spawn behavior.

`psx_match_effects.c` runs the pool after animation and before body separation,
HitTest and HitApply. Hits allocate their sprays after that pass: twenty
constant-speed sparks, then six directional sparks, from the actual selected
hit segment's endpoint and the first attack segment's effect-space direction.
Their initialization consumes RNG on the following update. There is no fixed
52-draw correction. The shared original Atan2Units4096 is reused for direction.
The live hook also allocates the winner-announcer follow-up before the
camera/effects pass, and category-4 voice echoes outside throws.

Implemented pool dispatch covers placeholders, normal/power sparks, hidden
and active power glows, rising particles, announcer wait and live voice-echo
lifetimes. The new 440-byte private glow-joint/hand-offset import is
fingerprinted against the USA layout and byte-identical to Japan Rev.1:
SHA-256 `466e25b75e0c2050f52d7c4971092fd3ed778e066d81df61c3c3a4d803527810`.
Adaptations are attributed in their files and `NOTICE.md`.

Mutable storage stays outside the match value copy. The portable atomic step
stages that storage and commits it with a successful match; the native
in-place API retains its fatal-on-failure contract. A test injects an unknown
object type, confirms that the in-place path has advanced the pool counter
before failure, and verifies that the atomic API rolls back both objects and
match. Native slave/master cache synchronization includes the pool.

## Continuous state proof

`audit_psx_effect_rng.py --start 2 --end 3001` reexecutes the same original
retail scenario with read-only hooks and saves ordered object snapshots to
`build/parity-effects-pool-original.json`. The C evaluator independently
runs configuration plus physical pads. `tests/test_psx_effect_pool.py` compares
all 3,000 source frames, **22,939 object states / 190,576 semantic fields**.
All match, including original types **0, 1, 2, 3 and 16**. Core/RNG comparisons
must also pass on every tick; a missing, shifted or filtered frame fails.

The original free buffer contains unrelated previous RAM bytes. The test
checks fields initialized/read by each type and phase, always checking count,
order, type, flags and state. It does not copy or demand equality of undefined
hidden-glow coordinates, unused counters or pre-initialization primitives.
All defined spark positions, velocities, angles, lifetimes, counters, models
and double-buffered CLUTs compare after initialization. The policy is explicit
in the test, rather than inferred from C outputs.

The isolated phase test now also covers rising particles: **1,726 cases /
24,128 unpatched original updates**, types 1/2/4/5/12/13, with full canonical
state and independent trajectories. Full host suites pass **76/76 Paul–Law**
and **66/66 Paul**. The reference integrity suite passes **21 tests**, including
rejection of a missing/zero runtime-owned-pool marker and incompatible claims.
Logs use the shared `build/parity-effects-pool-*` prefix.

## Stock RAM and candidate

The native pool occupies 4,664 bytes. The derived local-pose cache changes
from 128 to 96 entries to make room. Even capacities remain two-way: a mask
and tail fold support 48 sets without SH-2 division. Existing power-of-two
mappings and odd fully-associative caches retain their behavior. A full
forward/reverse trajectory test compares the 96-entry cache with uncached
FK/IK and checks its storage boundary. No gameplay precision or object
capacity changes; the master stack reservation remains **16 KiB**.

Playable ELF: `8faa85fa307d59c4f8bee592d5182ded72ad3b481a4aacc5cc0d823722402e89`.
Initialized payload: **599,444 bytes**. High-WRAM BSS end `0x060FBCF0`, Low-WRAM
end `0x002FF794`. The qualified pre-pool control's ELF/app/ISO/cache identities
are retained in `assets/imported/parity-effects-control/identity.json`.

Two injected Ymir smokes use that exact new ELF and pass with zero runtime
errors, draw timeouts, attack decode errors and rejected faces:

| Case | Fields / ticks | HP | Submitted images | Cache hits / misses |
| --- | --- | --- | ---: | --- |
| Neutral | 600 / 195 | 140 / 140 | 134 | 7 / 391 |
| High hit | 920 / 516 | 140 / 134 | 359 | 367 / 717 |

Evidence: `assets/imported/parity-effects-pool-{neutral,high-hit}-saturn/` and
`build/parity-effects-pool-native-smoke-report.json`. These endpoints are not
a display-cadence or real-hardware certification. The older control's complete
26-case gate remains bound to its recorded ELF; the new candidate needs its
own complete live gate.

## Native reference diagnostic

The 3,000-tick varied-input export is
`assets/imported/parity-effects-pool-reference-export` (owned-camera-v1,
physical input, 73-byte startup settings, no view/side feedback). Its manifest
requires the owned effect pool; a runtime marker verifies that the guest kept
its pointer and private tables. This is in addition to the owned camera marker.
The native digest still covers the declared core/camera/RNG/eye/sound/HUD/input
fields; the semantic particle-field comparison above is a separate host proof.

The full `-O3` instrumented build exceeded the 16 KiB master-stack margin.
Compiling the diagnostic with `-Os` fits, but its first run timed out after
137 correct ticks. The bounded late profile shows the worker mostly in SHA
block processing, 64-bit shifts and field serialization, after the core had
completed phase 80. The worker's 60,000-FRC-tick deadline is unchanged.

The diagnostic now keeps SHA at `-O3` and serializes signed i64 values using
constant 32-bit shifts. It retains the exact big-endian wire format, complete
field census and expected digests. No tick, field or timeout assertion is
removed. The production candidate keeps its normal optimization settings.
The rejected run is summarized in
`build/parity-effects-pool-native-initial-rejection.json`; bounded profiling
evidence is `assets/imported/parity-effects-pool-reference-debug-late/`.
Two superseded failed capture directories were deleted after inspection.

Current diagnostic ELF:
`21ccb16a96582bd8e3a0350965aa6160111ac50df5788ed66779834c5110039a`;
payload **569,176 bytes**, High-WRAM BSS end `0x060F20B0`, Low-WRAM end
`0x002FF354`, the same 16 KiB master stack reservation. The final long run's
result belongs to `assets/imported/parity-effects-pool-reference-saturn/`.

The optimized diagnostic **passes 3,000/3,000 native ticks**, error counter 0,
with Low WRAM prefilled `0xA5`, both runtime-owned markers equal to 1 and no
runtime errors, draw timeouts, decode errors or rejected faces. All guest and
expected hashes agree; their complete digest-file SHA-256 is
`44749ea906d133117fd6a6a1cf27b6fad66eb30b6916d2899d8b9d7f2c92d3d3`.
Manifest identity:
`4fd0ef3a16ef04fe244021b79e928e9680cfd1129eb5ba32454e4e4ae0b02702`.
The final capture uses 799/2,048 draw commands. Results and artifact identities
are summarized in `build/parity-effects-pool-summary.json`.

The new candidate's full gate **passes**, with `complete: true`, under
`assets/imported/parity-effects-pool-gate/summary.json`: 66/66 and 76/76 host
tests, all 26 live combat cases (816.84 s), and all 3,000 independent reference
ticks (195.41 s). Total duration is 1,030.0 s; no stage failed. The playable
ELF is `8faa85fa307d59c4f8bee592d5182ded72ad3b481a4aacc5cc0d823722402e89`;
the diagnostic ELF is the `21ccb16a…` build above. This gate's reference run
uses the normal initial RAM state; the separate prefilled-`0xA5` result above
remains an additional check. The combat suite's broad original-round-flow and
PS1-input-replay flags remain false: passing its 26 cases does not establish
those universal properties. Image, cadence and complete-port qualification
remain open.

Original sprite/palette/model extraction and projected reference quads are
now recorded in [PARTICLE_RENDER_PROGRESS.md](PARTICLE_RENDER_PROGRESS.md).

## Remaining fidelity work

The existing sound recorder is still not connected to native sample playback.
Echo/announcer pool lifetimes currently follow the original harness's silent
SPU convention; real voice completion and audible requests need the audio
bridge. The new fixture does not exercise active power-glow transforms,
capacity exhaustion, voice echoes, projectiles, fire/gas, or replay log spawns.
Those require original-code cases and integration before claiming a complete
pool. Replay parity, sprite submission/lighting and full image/cadence proof
remain open. The complete 1:1 port is not achieved by these bounded results.

No recording, extra SDK or variant build tree was created. Disk usage remains
1.4 GB build / 4.5 GB imports.
