# VDP1 direct-VRAM command banks: validation

Status: validated within the scenarios and limits recorded below. The
historical panic cannot be attributed with certainty to the same internal
failure, although the independently reproduced failure was diagnosed and
corrected in the game.

## Continuation audit (2026-11-04)

The 441 individual JSON results and log are under
`/private/tmp/claude-501/-Users-celso-dev-tekken3-saturn/9c6e9176-f348-45a2-b1cf-7d1a1ccc4f7d/scratchpad/roster-441`.
The subsequent `long-cand` run completed 30,000 emulator frames, 2,437
simulation ticks, with no rejected faces or draw timeouts; it does not prove a
restart occurred.

Separate worktrees and SDK prefixes already existed under
`/Users/celso/dev/vdp1-audit`: `lib-control` is clean at `83eddbf2`, and
`lib-candidate` has the bank changes. The game CMake caches select their
respective SDKs and use the same fighter pair, lockstep grid and C flags. The
SDK archives have different SHA-256 hashes. An earlier control capture using
the plain ISO stopped in `load_fight_stage` with code -6 (`SAT_ERR_NOT_FOUND`,
not `SAT_ERR_BUSY`). The cause was not independently traced. With each build's
`disc/music.cue`, 3,000-frame injected Ymir runs both reached tick 187, with
186 submitted frames, 743 commands, zero rejected faces and zero draw timeouts.
Their screenshots have identical SHA-256
`5c365c19e6a6ddd06f407b65f9e71d6ed4ec401814fa17e6ae1b4c66bea76983`.

A repeat with the full-roster disc and Ymir `--dump-fb` compared the raw VDP1
display framebuffer: both 262,144-byte dumps had zero differing bytes and
SHA-256 `c3b5ade56a25c0d0c2f85141175dd54356147b3486e815cf5d7703d432889d24`.
Both executions stopped at tick 187 and 186 submitted frames, and their
composited screenshots matched the hash above. Raw dumps are
`/private/tmp/t3-vdp1-fb-{control,candidate}.raw`. This is one scene, which
does not contain Gouraud commands.

For the captured list, root selects candidate bank 1; both lists end at index
743. A comparison of active command fields, with command links, texture
addresses and LUT addresses normalised, found no differences. This scene had
no Gouraud commands, so it does not validate Gouraud tables. The existing
`vdp1-audit/compare_vdp1.py` reports false differences: it relocates unused
CMDLINK fields, uses the wrong Gouraud base for bank 1 and compares whole
texture regions of different sizes. It must be corrected before relying on it.
Raw owned-match bytes and 186 of 560 lockstep ring entries differ, while the
game struct and image/overrun counts match. The ring hashes `f->body` as raw
bytes. In the tick-187 dumps, two differing byte pairs lie in padding after
`body.spheres[5].radius` and `body.spheres[7].radius` of fighter 0 (and the
corresponding pairs of fighter 1); their offsets are established by the SH-2
layout table. Therefore this raw-struct hash is not a valid cross-build logical
state comparator. Other raw differences include addresses; the ring mismatch
alone is not proof of a logical mismatch.
The then-existing `scripts/lockstep/compare.py`, using the measured SH-2 layout,
reports no differing selected scalar leaf at the common tick 187. This is a
single sampled state, not a per-tick comparison.

A replacement audit-only hash reads scalar fields individually, excluding
padding and pointers. It covers the frontend combat counters, round/flow and
RNG fields plus selected fighter positions, facing, move clock and combat
counters (83 field selections, including array elements). In paired
8,000-field Ymir runs, the builds each reached tick 603, submitted 602 frames
and recorded zero grid overruns, rejected faces or draw timeouts. All 560
16-bit ring entries matched. Hash collisions and unselected fields limit this
evidence; it does not establish equality of the entire state. The test
implementation is `/private/tmp/t3-vdp1-leaf-hash.c`, with captures under
`/private/tmp/t3-vdp1-leaf2-{control,candidate}`. A first generated version
had an infinite loop and did not reach gameplay; it was fixed before this run.
The control and candidate captures are in `/private/tmp/t3-vdp1-verify-control`
and `/private/tmp/t3-vdp1-verify-candidate`; they are temporary evidence.

At 30,000 emulator frames the same isolated builds each reached tick 2,437,
with 2,436 submitted frames, 29,242 observed fields, 740 commands, no rejected
faces, draw timeouts or late swaps. The selected scalar leaves agree at that
tick and the screenshots both hash to
`d1140282ee687e18faa909241e43a64b357d2405341e86b5158692d739065756`.
Captures: `/private/tmp/t3-vdp1-long-control` and
`/private/tmp/t3-vdp1-long-candidate`. This does not establish that a restart
occurred or compare every intervening logical tick.

The corresponding instrumented ELF maps place `__bss_end` at `0x060fb11c`
for the control and `0x060e7ca0` for the candidate: 78,972 bytes less WRAM-H
used, with respectively 3,812 and 82,784 bytes before the reserved stack at
`0x060fc000`. These are audit builds with lockstep instrumentation; the final
production map still needs verification.

The isolated runs extended to 60,000 emulator frames reached tick 4,937 in
both builds, without a native panic, rejected faces or draw timeouts. Both had
4,936 submitted frames and 29,242 plus 30,000 observed fields; the sampled
scalar leaves matched. The final screenshot hash was
`2d144bb0d6d057025d41fc109bf827b00463a37a4bd6c091661280651ad28c37`
for each. The control recorded five late swaps, the candidate three. Captures:
`/private/tmp/t3-vdp1-restart-control` and
`/private/tmp/t3-vdp1-restart-candidate`. A restart has not been established;
this run does not explain the earlier `SAT_ERR_BUSY` captures.

The full LibSaturn `gmake test` passed on 2026-11-04, with one explicitly
unavailable macOS ABI test skipped.

A subsequent review found that double-interlace bank acquisition ignored a
timeout. The next `begin_frame` could write a bank before the pending root was
published. The library now drops that frame without touching either bank; a
host regression stalls field publication, checks both complete banks and the
root, and verifies recovery after fields resume. It also holds COPR in the
bank across a field boundary and checks that it remains untouched. The pending
END is now stored before the interrupt-visible publication flag. These changes
affect the library's 448-line mode, not the 224-line fight scene used above.
Two separate host mutations, one ignoring the acquire timeout and one ignoring
COPR, both make the new regression fail.
An injected Ymir run of the rebuilt candidate menu (704x448 interlace) showed
the title screen after 600 fields, with 237 published lists, zero draw
timeouts, zero dropped frames and an empty panic record. The capture is
`/private/tmp/t3-vdp1-menu-short`; it covers ordinary title rendering, not
the fault-injected stall. A 3,000-field exploratory run transitioned into
another program, so its old menu-symbol dumps are not used as evidence.

With `T3_LOCKSTEP_FIELDS=0`, identical pair 0-1 and the same ADX ring C flag,
the isolated control and candidate ELF maps place `__bss_end` at
`0x060fa8ec` and `0x060e7470`: respectively 5,908 and 84,880 bytes before
the stack boundary `0x060fc000`, a reduction of 78,972 bytes. These are
non-lockstep audit builds, not yet the final consolidated build.

After removing the experimental lockstep code and correcting the interlace
protocol, the paired clean fight maps put `__bss_end` at `0x060fa8ec` for
the control and `0x060e75a0` for the candidate. This is a reduction of 78,668
bytes, leaving 84,576 bytes before the `0x060fc000` stack boundary. Both builds
use pair 0-1 and the same ADX ring flag. The total includes changes in code
size as well as removal of the staging buffers.

With the later game-side stack correction and normal menu-driven boot (no
`T3_BOOT_FIGHTERS` override), the clean paired maps put `__bss_end` at
`0x060fbb6c` and `0x060e8820`. The candidate again saves 78,668 bytes and
has 79,840 bytes before the same stack boundary. The control has 1,172 bytes.
In a 3,000-field injected candidate run of that build, the combined
preparation/simulation stack had a measured canary high-water mark of 5,328
of 6,656 bytes, leaving 1,328 bytes in this run. It reached tick 769 with 745
commands, zero rejected faces, zero draw timeouts and no first error. Capture:
`/private/tmp/t3-vdp1-natural-stats`. This is not a worst-case stack bound.

The clean builds were also run for 8,000 fields with START pressed and
released. The control ended at tick 3,996 with 1,963 submitted images and 146
late swaps; the candidate ended at tick 3,993 with 2,037 images and 137 late
swaps. Both had zero rejected faces, draw timeouts and first errors. Different
logical ticks preclude a pixel comparison. These runs precede the final
interlace-only COPR guard; their reports are
`/private/tmp/t3-vdp1-final-{control,candidate}.log`.
After that guard, a final candidate run of 3,000 fields reached tick 2,241,
submitted 628 images, and reported zero rejected faces, draw timeouts and
first errors, with 56 late swaps. Report:
`/private/tmp/t3-vdp1-final3-candidate.log`.

A paired 8,000-field run used a device script pressing START at field 4,000
and releasing it at 4,100. Both builds submitted 602 frames and ended at tick
332; the corresponding run without the press ended at tick 603. Since
`t3_match_reset` clears `total_ticks`, this confirms a match reset. Both had
zero rejected faces, draw timeouts and late swaps, and the final PNGs shared
SHA-256 `a97be046c24b89c435d259fcbc33518c342cbc0894d261f25c25389a79c1aee3`.
These 8,000-field runs are independent from the 60,000-field runs above; they
do not establish a restart in the latter. The device script and captures are
`/private/tmp/t3-vdp1-restart.device` and `/private/tmp/t3-vdp1-start3-{control,candidate}`.

The same device script was also run with lockstep disabled, at the normal
presentation cadence and using the full-roster disc. At 8,000 fields the
control and candidate reached ticks 3,996 and 3,995 respectively, with no
rejected faces or draw timeouts. They submitted 1,963 and 2,037 frames and
recorded 146 and 140 late swaps respectively. The different final logical
ticks make the final images unsuitable for a pixel-equivalence claim. These
values are one run each, not a statistical performance measurement; captures
are `/private/tmp/t3-vdp1-prod-start-{control,candidate}`.

A separate host fixture compiled the same calls against each isolated library:
polygon, polyline and distorted sprite with four corners `8421 9c63 a8c4 bdef`,
and line with the same input (the line uses two corners). It compared all
command words and all four staged Gouraud tables after subtracting their respective table bases
from GRDA. The output was identical; see `/private/tmp/t3-vdp1-gouraud.cpp`
and `t3-vdp1-gouraud-{control,candidate}.txt`. This exercises candidate bank 0
in host memory; the permanent bank test checks GRDA for both banks. It does
not compare a hardware-rendered Gouraud framebuffer.

A separate Ymir scene injected one Gouraud polygon into each isolated build.
At tick 187, `scripts/compare_vdp1_vram.py` compared 744 commands and END,
including the table `2108 7fff 4210 6318` referenced by command 743. The
control GRDA was `0x2000`, candidate bank 1 GRDA `0x4804`; after normalisation,
commands, referenced textures, LUTs and Gouraud data matched. Both 262,144-byte
raw framebuffers shared SHA-256
`dcc40382ad1f92f48d35bf33a770efc52ff11b18c3221fbeb865c026513274ac`;
composited PNGs also matched. Changing the first table byte made the comparator
report command 743 and that byte. Captures are
`/private/tmp/t3-gouraud-{control,candidate}` and the temporary scene is
`/private/tmp/t3-gouraud-audit-main.c`. This is one synthetic Gouraud scene.

The older `lock-*-4500` and `lock-*-6000` captures do contain native panic
records with code -7 (`SAT_ERR_BUSY`) in `main.c`: line 1672 in the then
candidate and line 1687 in the then control. Those binaries both used the
candidate SDK, and the source line numbers refer to earlier versions of the
uncommitted file. The contemporaneous session transcript records that both
source lines then named `t3_characters_init`; the underlying returned BUSY and
whether it predates the bank change are still unknown. The old `diagnostics.bin`
is all zero after its magic and version: the panic occurred before the first
diagnostic snapshot in that program instance. A program restart was suspected,
but this dump cannot establish one. The isolated runs above reached the fight
and performed a match reset without reproducing the failure; that reset is not
a `sat_exec` program restart.

The retained `lock-*-3800` provenance identifies the contemporaneous injected
executables and mixed-disc files by SHA-256. The current `build/adx-fight`
executable and `disc/music.bin` no longer match those recorded hashes (for the
executable `21ef9340...` versus `7e2237d7...`, for the disc `0ea54826...`
versus `18fba57a...`). Thus the old build paths cannot reproduce that exact
run. The panic dump does not identify the inner failing operation; `parallel`
initialization and asset uploads are among the calls that can propagate BUSY.
The `capture.CMakeCache.txt` retained in both old 4500-run directories has
`T3_BOOT_FIGHTERS` empty and parallel geometry and skinning enabled. The
previous independent fight-to-fight test used explicit boot fighters, so it did
not exercise the menu-driven demonstration path indicated by these caches.

A fresh 24,000-field Ymir run reproduced BUSY in both truly independent builds
after the menu-led demonstration. The audit-only game changes reported the
inner error at `characters.c:284`: the call to `sat_parallel_init`. Both
screens captured the failure; at field 16,000 both had reached the match-end
animation, and at field 24,000 both reported that same call and error. The
control used the clean pre-bank LibSaturn commit `83eddbf2`; the candidate
used the bank implementation. This independently establishes that the
observed menu/demonstration failure is not introduced by the bank change. It
does not prove which internal busy condition applies or that it is identical
to the historical failure. Reports and screenshots are in
`/private/tmp/t3-vdp1-demo-{control,candidate}`. The 14 stage packs in each
disc were reused from `build/vram-banks`, checked by SHA-256, and the disc's
menu and fight binaries were checked against the corresponding audit build.

An accelerated audit-only demonstration restart reproduced the error in both
builds by 6,000 fields. The diagnostic distinguishes an already initialized
parallel runtime from the dual-SH2 lifecycle and reported the latter in
`SAT_DUAL_SH2_FAULT` (4) in both cases. In the candidate's stopped image,
the local lifecycle state was OFFLINE (0), but the shared control block at
`0x06002000` retained magic `DSH2`, generation 2 and status FAULT (4);
the parallel runtime was uninitialized. These captures are in
`/private/tmp/t3-vdp1-busy-{control,candidate}` and
`/private/tmp/t3-vdp1-busy-registers`. A separate audit build stopped on the
first failure inside the dual-SH2 start path, before AUTO could fall back to
the master. While waiting for Slave READY the shared status was still
STARTING (1); the wait then timed out. Captures:
`/private/tmp/t3-vdp1-startfault` and `/private/tmp/t3-vdp1-startstatus`.
An audit-only experiment that entered the Slave through the cache-through
alias and purged its cache before its initialization still timed out in
STARTING; it was discarded. Capture: `/private/tmp/t3-vdp1-slave-cache`.
The emulator's misaligned-access report for the accelerated failure records
two Slave writes, first at PC `0x0600071c` to `0x011e014d` (32 bits), and no
Master misaligned accesses. Report:
`/private/tmp/t3-vdp1-slave-trace/misaligned.txt`.
Comparing the initial title with the stopped image showed that 770 bytes of
the reserved Slave vector area `0x06000400..0x060007ff` changed. A watch
starting at field 600 recorded zero Master writes and 164,955 Slave writes
there; the first writes at field 1,925 were in
`t3_psx_camera_joint_matrix`, reached during fused actor preparation. That
work ran on the default Slave stack, below the separate simulation stack.
Capture: `/private/tmp/t3-vdp1-slave-watch`. An audit-only wrapper that runs
actor preparation on the existing simulation stack eliminated all such
watched writes during 10,000 fields with accelerated demonstration restarts;
the execution had advanced to a later introduction at the end, instead of
stopping at BUSY. Capture: `/private/tmp/t3-vdp1-stackfix`. This test also
instrumented Slave startup to stop on timeout; it did not fire.
The same wrapper then passed a paired 10,000-field accelerated-restart run
with both isolated libraries restored to their normal startup code. Both
executions reached a later introduction, neither CPU wrote the watched vector
area after field 600, and an audit-only check of the shared stack's first
canary byte did not fire. Captures:
`/private/tmp/t3-vdp1-stackguard-{control,candidate}`. The check is not a
complete measurement of stack headroom.
Finally, the uninstrumented game and libraries completed 24,000 Ymir fields
through the natural menu/demonstration flow in both builds. The 16,000-field
candidate capture shows the final round; at 24,000 both show a later fight,
not the former BUSY panic. Neither CPU wrote the watched vector area after
field 600. The scenes are sampled at different logical instants and are not
used as a pixel-equivalence comparison. Captures:
`/private/tmp/t3-vdp1-natural-{control,candidate}`.
An audit-only attempt to call `sat_parallel_shutdown(60000)` before the
program transition still produced the same FAULT/BUSY by 6,000 fields, so
that change was discarded. Its screenshot is
`/private/tmp/t3-vdp1-shutdown-candidate/6000.png`.

A subsequent paired audit did exercise `sat_exec`: at tick 300 a test-only
trigger restarted the fight program from each build's own mixed-mode disc.
On the new instance it consumed the handoff argument and set a marker after
another 300 ticks. In 12,000 Ymir fields both control and candidate set the
marker, reached tick 565, submitted 564 frames and had no rejected faces,
draw timeouts or late swaps. Their screenshots shared SHA-256
`e17349c17cd780ca94853c9681c87c383c2765ea59dcb3ccecd6c137583b9da9`.
The trigger and reports are `/private/tmp/t3-vdp1-exec-audit-main.c` and
`/private/tmp/t3-vdp1-exec-{control,candidate}`. This tests one forced fight
restart; it does not establish the cause of the old BUSY or cover every menu
transition.

## Isolated candidate roster sample (2026-11-04)

The candidate SDK and executable were rebuilt for pairs 05-14, 15-09, 17-17,
19-19 and 20-20 and run for 1,500 injected Ymir fields each. The retained
`build/vram-banks/disc/music.cue` supplied the complete roster/stage assets;
program code was injected from the isolated candidate build. All five reached
the fight without rejected faces or draw timeouts. Commands used were 736,
615, 662, 715 and 702 of 2048 respectively; the middle three each recorded one
late swap, as in the earlier 441-pair run. Reports and screenshots are under
`/private/tmp/t3-vdp1-sample`. This is a candidate-only smoke sample, not a
paired control comparison.

The first attempt used the audit disc, which contained only stages 0 and 1;
pairs 05-14 and 15-09 failed in `load_fight_stage` with `SAT_ERR_NOT_FOUND`.
Those failures are not included in the sample results.

## Design under test

LibSaturn writes each frame's VDP1 command list straight into the VDP1 bank
the root does not name (bank 0 at VRAM 32, bank 1 at 32 + 80 KiB; each bank
holds 64 KiB of commands and 16 KiB of Gouraud tables). The root is 32 bytes at
VRAM 0. Publishing flips the root, and the old bank is reclaimed once COPR is
outside its range (`reclaim_settled`). The SCU IST register is not written.

## 441 fight pairs (roster-441)

Build: candidate `build/vram-banks`, attract configuration (`T3_FRAME_ON_SUBMIT`
ON), 1500 frames per pair, each pair reconfigured with
`-DT3_BOOT_FIGHTERS=a;b`. Results: `scratchpad/roster-441/*.json`.

| Metric | Result |
| --- | --- |
| Pairs completed | 441 of 441, zero run errors |
| `faces_rejected` | 0 in every pair |
| `draw_timeouts` | 0 in every pair |
| `event_overflow` | 0 in every pair |
| Commands used, max | 809 of 2048 |
| Faces submitted, max | 780 |
| Slave stack used, max | 5328 B |
| `draw_waits` | 1 in every pair |
| `late_swaps` | 1 in pairs 15-09, 17-17, 19-19; 0 elsewhere |

Note: 809 commands is about 40% of the 2048 capacity. The capacity boundary
is covered by the host tests (see below), not by these fights.

## Late swaps

A late swap is a VBlank-IN at which a pending ON_SUBMIT swap still waits for
the list to end (`on_vblank_in`, `g_late_swaps`). The counter is in the HEAD of
LibSaturn, so it predates the bank change.

Pair 15-09 was reproduced (deterministic: 615 commands, `late_swaps` 1) with
per-field video observations (`video.csv`, one row per emulated field):

- Picture changes after boot: 75. `frame_serial` at the end: 75. Each submitted
  frame changed the picture once. No frame is missing from the sequence.
- Steady cadence: one picture change every 12 fields.
- The only steady-state anomaly is one hold of 23 fields (fields 608 to 631)
  between two normal changes. The picture at 608 stays on screen until 631,
  then the sequence continues at the 12-field cadence.

Reading: the late swap delays presentation. It does not drop a logical frame,
and it does not show a different picture twice. The next swap waits for the
held list, which moves the sequence by about one cadence period.

Baseline comparison (6000 frames, port 2 none, `late-probe.sh`):

| Build | Late swaps | Submitted frames | Late per 100 frames |
| --- | --- | --- | --- |
| Control `build/adx-fight` | 27 | 317 | 8.5 |
| Candidate `build/vram-banks` | 28 | 318 | 8.8 |

Both measurements used the candidate SDK, as established below. They describe
the observed ON_SUBMIT pacing in those two game configurations but cannot rule
out a regression against the old library. In this sample a late swap delayed
presentation by roughly one 12-field cadence period, about once in 12 frames.

## Lockstep (candidate and control, same inputs)

Results: `scratchpad/lock-*-{1061,4144}` (summary via `lock-summary.py`).

Re-check with a per-tick state hash (`g_t3_lockstep_ring`, 16-bit, pointer-free
match struct, same grid in both builds). Both builds stopped at the same logical
tick: 4145 sim ticks, 50522 VBlanks, 128 late swaps each. Over the last 560 ticks
(3585 to 4144, one hash per tick) all 560 entries are equal. A coarser run
(one hash per 8 ticks, 125000 frames: 4645 ticks, 56522 VBlanks, 75 late swaps
each) also matched in all 560 entries.

What this does and does not show: the two game builds agree tick for tick. It
does not show that the candidate matches the pre-change library. Both
`build/adx-fight` and `build/vram-banks` link the same installed
`saturn-prefix/lib/libsaturn.a`, which contains the bank protocol (the
`g_reclaim_pending` and `reclaim_settled` symbols are present in both ELFs). HEAD
has none of these symbols. So the "control" is not a pre-change control, and no
result here compares the bank change against HEAD. The two configurations also
differ (`build/adx-fight` has a stale `T3_DMA_COMMAND_LIST=ON` cache entry that no
source file uses, and `T3_BOOT_FIGHTERS` differs), so the lockstep result is
between two game configurations on one library.

The 4144 divergence in the table above was a stop-alignment artefact: the two
stops were at different logical ticks and VBlank counts, so fighter X was
compared at different times. Limits: 16-bit hashes, one attract scenario.

| Tick | Build | Screen PNG hash | Commands | Fighter X | Health | VBlanks |
| --- | --- | --- | --- | --- | --- | --- |
| 1061 | control | 2fcb5509447c5554 | 752 | 304, -305 | 1000, 428 | 13514 |
| 1061 | candidate | 2fcb5509447c5554 | 752 | 304, -305 | 1000, 428 | 13514 |
| 4144 | control | 94d9d5134cd17edc | 725 | 126, -126 | 557, 492 | 50510 |
| 4144 | candidate | 5445296cc30a6fd1 | 732 | 133, -133 | 557, 492 | 50522 |

The screen at the sampled tick 1061 is byte-identical. The tick-4144 rows were
later found not to be aligned at the same tick and field, so they do not show a
logical divergence.

## Host tests (lib-saturn, fresh builds with gmake)

- `test_vdp1_banks`: bank protocol, COPR, 1-cycle race, timeouts, 2046-command
  near-capacity load with Gouraud, exact 2,048-entry limit (including END and
  setup commands), 1-cycle random pacing, stuck-draw recovery.
- `test_vdp1_checkpoint`, `test_vdp1_clip`, `test_sprite_batch`,
  `test_scene_native_commands`, `test_dma_api`: pass.
- Full `gmake test`: rc 0.

## Open items

1. The selected-scalar 560-tick hash comparison passed; broader state remains
   outside its scope.
2. Structural and synthetic Gouraud scene comparisons passed. The tool compares
   contiguous command entries and referenced data; it does not independently
   emulate arbitrary VDP1 control flow.
3. The captured historical BUSY lacks the inner error and cannot be proven
   identical to the independently reproduced stack corruption. The latter
   was fixed and tested across natural and accelerated restarts in both builds.
4. Exact 2048-entry admission and overflow were tested on the host; an in-game
   synthetic load at that limit was not run. Not needed for this game so far:
   the roster smoke (11 pairings, 2026-11-04) peaks at 771 commands, and 937
   with the DEBUG report drawn.
5. Ymir fault injection: no emulator hook exists; host fault tests cover the
   modeled cases and do not substitute for a hardware fault-injection test.
6. The production lockstep hooks and configuration were removed, with a
   temporary archive at `/private/tmp/t3-vdp1-lockstep-harness.tar` and the
   audit worktree sources at `/private/tmp/t3-vdp1-audit-lockstep-sources.tar`.
   The clean maps and changelog are updated.
7. Separate task: the SCU IST writes. The DMA and `sat_exec` writes are gone;
   only `clear_smpc_ist` (`src/hal/smpc/smpc.cpp`) is left, before an INTBACK
   CONTINUE. This game reads both ports in 15-byte mode, so a pad, 3D pad or
   mouse report fits one chunk and never continues. Ymir, 800 fight fields
   with 3D pads on both ports: 0 IST writes on either CPU; the same run with
   the probe's test multitap on port 1: 1 write, from `finish_peripheral_read`.
   So the write only matters with a multitap, and a replacement could only be
   shown safe with one on a console (`lib-saturn/docs/SCU_IST_AUDIT.md`).
8. Fixed 2026-11-04 (lib-saturn 485effc8): the first `ON_SUBMIT` list after
   1-cycle mode was started with its framebuffer change already asked for.
   On Mednafen the next field's change stopped that list before END, COPR
   never reached END, and `submit` waited for ever (the fight hung after 13
   ticks). That list now enters manual mode with FCM alone and asks for the
   change at the VBlank-IN after END, like every later list. Ymir is
   unchanged (same pad hash, 30.68 images/s in the ADX fight).
