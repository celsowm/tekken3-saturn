# Cold motion banks in Low WRAM, hot state in High WRAM

This study compares the port's performance history and harnesses with Virtua
Fighter 2. It tests one change: put the bytes that every frame touches in the
faster work RAM, and put the bytes that are only read occasionally in the slower
one. The goal remains 60 committed logic ticks and 30 newly completed images per
second.

## What Virtua Fighter 2 does differently

Retail VF2 captured in Ymir shows 59.9 distinct images/s at 704×448 interlaced. Its
VDP1 list is a sequential run of about 517 distorted-sprite quads plus 25
polygons, with no Gouraud and no command links. Both CPUs spend roughly half of
each field polling: Master about 50% idle, Slave about 57%. Across a field only
14 bytes of Low WRAM change. VF2 therefore keeps its per-frame working set
out of the slower DRAM.

The current Paul pipeline in the same emulator gives 25.5 distinct images/s,
with 709 quads plus 92 sprites, all 8×8 textures. Both CPUs are saturated: the
Master waits about 6% of a field and the Slave about 14%. The function call
counts are 3,399 / 3,949 per field, compared with VF2's 924 / 488. The hot
per-frame objects were placed in Low WRAM:
`saturn::core::g_state` (the SDK command buffer), the painter keys/order, the
world-pose cache entries, the owned match and the actor/geometry state. High
WRAM, meanwhile, held 544 KB of constant DIVMOT motion data
(`t3_local_bank` 303,808 B and `t3_local_common` 240,180 B).

The two emulators also disagree on the price of that placement. Mednafen charges
heavily for Low WRAM accesses, and Ymir barely does. In the SDK's
NATIVE_WRAM_STUDY, moving buffers to High WRAM cut geometry time by 23% in
Mednafen but by only 2.4% in Ymir. The HIGH_GEOMETRY_MEMORY test moved 35 KB and
gained 4.7% in Mednafen, 0% in Ymir. Ymir-only profiles therefore cannot show
this cost. Low WRAM is DRAM on real hardware as well, so the Mednafen number is
the one to take seriously. A physical-hardware check is still owed.

## Implementation (`T3_COLD_LOW_WRAM`, default OFF)

- `scripts/build_local_divmot_assets.py --cold-file` also writes `MOTION.BIN`:
  a 64-byte big-endian header (magic `T3MO`, version 1, sizes, offsets and the
  FNV-1a of the payload), the bank at offset 64 and the common block at a
  16-byte-aligned offset. In this mode, the generated tables become writable
  `.wram_l` storage, rather than initialized constants in the application.
- `src/saturn/cold_data.c` reads the file in 64 KB chunks directly into that
  storage at boot, then verifies the size, header and checksum before the
  first motion lookup. A mismatch panics instead of running with corrupt data.
  `src/saturn/disc.c` shares one CD mount between this loader and the VDP2
  stage loader.
- `src/saturn/placement.h` defines `T3_HOT_WRAM`. With the flag ON it expands to
  nothing, so the hot objects land in ordinary High WRAM `.bss`. With the flag
  OFF it still expands to `.wram_l`, which keeps the old layout byte-compatible.
  It is applied to the match/job/image state, painter keys/order, world/local
  pose caches, branch filter words, actor and geometry state.
- `src/saturn/hot_wram.ld` is a GNU ld `INSERT BEFORE .bss` fragment. It claims
  LibSaturn's `state.cpp.obj` `.wram_l` input section (the 64 KB SDK command
  state `g_state`) for High WRAM. LibSaturn's sources are unchanged. The section
  is NOLOAD, so `main()` zeroes it before any SDK call.

The candidate application shrinks from 876,316 to 332,924 bytes. High memory
holds `.text` 250,580 B, the SDK hot state 65,556 B and `.bss` 488,904 B, ending at
`0x060dca68`. Low WRAM ends at `0x002c748c`. The matched control is the same
source with the flag OFF.

## Results

Three alternating Mednafen CD-boot pairs were recorded with neutral input and
read from the HUD FRAME counter at movie seconds 30 and 90. Pair 1 ran
candidate then control, pair 2 control then candidate, pair 3 candidate then
control:

| Pair | Control FRAME | Candidate FRAME | Control delta | Candidate delta |
| --- | --- | --- | ---: | ---: |
| 1 | 206 → 1040 | 190 → 1413 | 834 | 1,223 |
| 2 | 206 → 1040 | 190 → 1413 | 834 | 1,223 |
| 3 | 206 → 1040 | 190 → 1413 | 834 | 1,223 |

That is **+46.6%** (13.90 → 20.38 submission increments/s). The control
reproduces the qualified world-composition pipeline's 833. The gain also
appears inside the first live round, before any round transition: 30 → 50 s
advances 281 versus 416 submissions (ratio 1.48). Pair 1 overlapped headless
Ymir suites; pairs 2 and 3 ran on an idle machine and match it exactly.

A final pair on the rebuilt discs (1 MB read chunks, verify stub, cosmetic
edits) read candidate 239 → 1459 (+1,220) and control 206 → 1041 (+835),
ratio 1.46×. The three earlier pairs predate that rebuild. The reference
replays (5,481 + 5,632 + 5,221 ticks, no difference) and the frozen tick-120
RGB SHA `dbd1dcf8…a2c2a` (97 fields) were re-run on it and still match.

The 1.47× result narrowly misses the 1.5× continuation gate. It is still the
largest single gain in this project's history, at roughly 3.6 times the
+12.7% of the world-composition pipeline. At 20.38/s the warmed cadence implies
about 49 ms per submission against the 33.3 ms budget. The remaining factor to
30 images/s is about 1.47×, down from 2.16×.

Correctness:

- **Continuous reference replays:** all **5,481 + 5,632 + 5,221 = 16,334**
  original/host/Saturn ticks match every declared digest (p1, p2, draw), with
  no first difference.
- **Frozen tick-120 build:** all 97 fields are stable. The RGB SHA-256
  `dbd1dcf895998266165aed2f9e3a3cfbe9626638b3a864f1f2a3678a800a2c2a` matches the
  qualified pipeline.
- **Live combat:** the candidate and the matched control both pass all **24
  scenarios** with no offset flag, including KO, replay, victory, round, match
  reset and both timeout checkpoints. The suite now measures the first
  simulation tick of each build (candidate: field 310) and starts its input
  windows 50 fields later (`GAMEPLAY_LEAD_FIELDS`, an empirical margin: 16
  failed the high-hit case). The KO case samples back from the round end until
  the collapse move is visible, because a faster build runs past its start.

## Double-buffered VDP1 command tables: rejected before implementation

Double-buffered command tables only pay off when `sat_end_frame` waits for the
previous list's END or swap. A `T3_NATIVE_WAIT_PROBE` build of the final
candidate (`build/cold-low-wram-waitprobe`, Mednafen CD boot, neutral input)
reports at movie seconds 40 and 45:

| Second | SIM | PREP | FLUSH | HUD | END (avg) | Draw waits (total) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 40 | 2180 | 4897 | 2314 | 1315 | 667 | 2 |
| 45 | 1175 | 4893 | 2363 | 1053 | 667 | 2 |

The values are FRT ticks. Only two waits occur in the whole run, so a second
table has nothing to hide. END is now the plain CPU copy to VRAM, and it fell
from 1,408 to 667 ticks once the SDK command state moved to High WRAM. It is
about 6% of the frame, so removing it entirely would give at most about 1.06×.
The earlier synchronous SCU-DMA path made it slower. Preparation, at 43% of
the measured frame, is where the next gain has to come from.

## Costs and limits

- Boot reads 544,052 B from CD. With 1 MB read chunks the first tick is at
  field 250 versus 110 for the control (+140; 64 KB chunks gave +180). The rest
  is raw CD throughput. Streaming overlap and compression were rejected: the
  stage loader shares the CD, and the gain from compression is small. In Ymir
  the delay was roughly 185 fields (about 3 s) with the earlier chunk size. The 5,400-field Ymir comparison
  shows exactly this: 5,109 versus 5,286 committed ticks (177 fewer) and 2,214
  versus 2,219 submissions, with no errors, draw timeouts or extra discarded
  fields. Ymir shows no steady-state gain, as its memory model predicts.
- The emulator speed of Low WRAM is a model, not a measurement. Before promotion,
  the gain must be confirmed on physical hardware, or at least the cycle charges
  must be checked against the hardware manual.
- Motion data is now mutable RAM. The checksum guards the load, but nothing
  write-protects the data afterwards. The opt-in `T3_VERIFY_COLD` build
  re-hashes 4 KB per tick and panics on a mismatch; a clean run passed 4 passes
  over 1,213 ticks. It is detection only and no corruption test was done.
- Ymir gives Low and High WRAM the same 2-cycle access and a simplified cache
  model (`--sh2-cache` enables explicit cache emulation), which explains its
  zero gain.

## Hardware test protocol (owed)

Run `build/cold-low-wram/disc/tekken3_saturn.cue` and the control's disc on a
real Saturn with neutral input. Read the HUD FRAME counter at 30 s and 90 s
after power-on for each and report both values per disc; the Mednafen
reference is 834 (control) versus 1,223 (candidate) over those 60 s. Promote
the flag only if the ratio holds on hardware.
- Submission increments are not proof of 30 newly displayed images/s.

## Retained evidence (private, git-ignored)

Builds are `build/cold-low-wram` and `build/cold-low-wram-control`. The
reference-replay builds are `build/cold-low-wram-reference-{p1,p2,draw}`, and
the frozen build is `build/cold-low-wram-frozen`. Recordings are
`assets/imported/cold-low-wram{,-control}-cd-{1,2,3}`, the Ymir runs are
`cold-low-wram{,-control}-5400`, and the VF2 and Paul captures are
`vf2-cadence-vdp1-study` and `paul-vs-vf2-cadence-study`.
