# Real-hardware results

The first real Saturn runs of the demo came from players, and neither fault below
appears in Mednafen or Ymir. Emulator results cannot confirm a fix for them.

| Device | Video | Reported music | Reported speed |
|---|---|---|---|
| Satiator | <https://www.youtube.com/watch?v=Ddicuy3Pn1g&t=135s> | Plays | Very slow |
| SAROO | <https://www.youtube.com/watch?v=3zGGS0SJ9mY> | None; sound effects play | Close to expected |
| Probably Fenrir | <https://www.youtube.com/watch?v=kPrPbXBrUWY&t=7s> | Not recorded | Not recorded |

These are second-hand reports: the videos' picture and audio have not been reviewed
here. The cause of each fault is **not established**.

## The hardware report

V0.1.2 HW REPORT adds a report to the DEBUG overlay of roster builds, and a MUSIC
switch beside DEBUG in OPTION MODE. Turning music off skips the CD play command and
the SCSP mix writes, so both settings can be compared on one console and loader.
`scripts/package_itch_demo.py` puts the request for testers in the package README.

| Label | Meaning | Reading |
|---|---|---|
| `LOOP x10` | Images submitted per second, times 10 | Below 150 means game time is being lost |
| `TICK` | Simulation ticks per second, times 10 | 600 is full speed; well below that is slow motion |
| `DROP` | Total fields discarded by the catch-up cap | Should stop rising; rising means below 15 images/s |
| `ELAP` | Fields covered by the last image | Above 4 is past the cap (`T3_MAX_RECOVERY_FIELDS`) |
| `IRQ` | SCU VBlank interrupts live | 0 means fields come from FRT time (so speed holds) and images no longer overlap drawing |
| `VBI` | VBlank interrupt count | Should rise about 60 per second |
| `IFB` | Times VBlank interrupts stopped mid-run (`sat_irq_fallbacks`) | Above 0 is a hardware or loader clue |
| `HZ`, `PAL` | Refresh rate assumed, and TVSTAT's PAL bit | A disagreement is a video-timing bug |
| `MCCR`, `SCCR` | Master and Slave cache control as start-up found it | Anything but 1 means the BIOS or loader left it so and LibSaturn corrected it; `SCCR` 999 means not read |
| `SLV` | The Slave ran a verified task at boot | 0 means the work falls back to the Master |
| `FRAME MS` | Work in the last image, excluding `sat_end_frame` | Compare with the loop period |
| `END MS` | Average `sat_end_frame` time | Large means VDP1 is holding the loop |
| `MUSIC` | The switch: 1 on, 0 off | |
| `CD PLAY` | Magnitude of the start result, 0 is success | Non-zero: the fight continues without music |
| `CD CMD` | Magnitude of the last status command's result | Non-zero: `CD ST`, `CD TRK`, `CD FAD` are stale |
| `CD ST` | CD Block status: 1 pause, 2 standby, 3 play, 4 seek | 3 with music on |
| `CD TRK`, `CD FAD` | Current track and sector | Track 2 and a rising FAD mean audio is being played |

The CD status is read about once a second, only while DEBUG is on. A failed music
start no longer panics in the fight: it shows in `CD PLAY` instead. While DEBUG is on,
256 more VDP1 commands are kept for the HUD.

## How to act on the readings

| Reading | Meaning | Next step |
|---|---|---|
| `IRQ` 0 | VBlank interrupts did not start | Fix IRQ start-up in LibSaturn; the image rate drops, but game time stays right unless the FRT calibration also failed |
| `SLV` 0 | The Slave did not run | Find why it failed to start on that loader |
| `MCCR` or `SCCR` not 1 | A loader-dependent cache state | Already corrected at start-up; compare with builds before this fix |
| `MUSIC` 0 restores `TICK` | CD-DA contention is real | Trace which access stalls, then remove or move it |
| `LOOP x10` below 150 whatever the switch | Real draw or CPU cost | Reduce the load using `FRAME MS` and `END MS`; raise the catch-up cap only if simulation can afford it |
| SAROO `CD ST` 3, FAD rising, silent | Audio path, not the command | Compare SCSP set-up with a game whose CD audio works on SAROO |
| SAROO `CD ST` not 3, or FAD stuck | Play did not take effect | Try play by track number, and check the state left by earlier data reads |

## Emulator check of the report

`scripts/hw_report_check.py` exercises the report and the switch on Ymir. It shows they
work, and says nothing about real-hardware timing.

Results for ELF `e3a22c19db8db50d4e63ed310ef7102bdd59d3c0dcc558567e3d3d57a338ccb6`, 4,800
injected fields, cache emulation default (`assets/imported/hw-report-evidence/`):

| Scenario | `loop_rate_x10` | Discarded fields | First error | Fight audio RMS |
|---|---|---|---|---|
| Previous ELF `954ecc3d…`, attract fight | 364 | 43 | 0 | not measured |
| This ELF, attract fight, DEBUG off | 364 | 40 | 0 | 2,159–15,931 |
| DEBUG on, music on, 1,616 VBlanks | 354 | 0 | 0 | 1,259–2,495 |
| DEBUG on, music off, 1,617 VBlanks | 350 | 0 | 0 | 0 |

The attract runs cover a longer span than the two menu-driven fights, so their discard
counts are not comparable with those fights. The DEBUG overlay costs about 1 image/s here.
With music on, the fight screenshot reads `CD ST 3`, `CD TRK 2`, `CD FAD 12591`; with music
off it reads `CD ST 1`, `CD TRK 1`, `CD FAD 946`. In both, `IRQ 1`, `SLV 1`, `MCCR 1`,
`SCCR 1`, `END MS 0`.

Mednafen booted the mixed-mode CUE from CD into the attract fight, with sound disabled.
`scripts/mednafen_shots.py` key presses did not register in that session, so the options menu
and the switch were not exercised on Mednafen, and the build's CD audio was not heard there.
No real Saturn has run this build yet.

## Load estimates without hardware (2026-10-31)

These come from ELF `4609f6e7…`, attract fight (two CPU fighters), neutral pads, which is
the emulators' view only. Evidence is in `assets/imported/slowhit-study/`.

### VDP1 is probably not the cause of "very slow"

`scripts/vdp1_load.py` walks the real command list that Ymir holds in VRAM. In fight images:

| Measure | Per image |
|---|---|
| Commands | 714–749 |
| Lines | 4,722–5,195 |
| Pixels written | 102,000–105,000 |
| Of those, the clearing polygon | 71,680 (about 69%) |

`T3_FRAME_ON_SUBMIT` means the hardware erase never runs, so LibSaturn starts each list with
a full-screen polygon. Mednafen 1.32.1 charges 1.1875 clocks per pixel, 4 per line and 16 per
command (`src/ss/vdp1_common.h`, `src/ss/vdp1.cpp`). That is about 155,000 clocks, or 0.35 of
a field. To hold an image past the 4-field cap, VDP1 would need about 17 clocks per pixel. The
Mednafen figures are a model; the true hardware rate has not been measured here.

Direct test, Mednafen, 110 s, with the clearing polygon removed by a temporary
`sat_vdp1_set_erase_enabled(0)`:

| Run | Observed fields | Discarded | Ticks | Images | Images/s |
|---|---|---|---|---|---|
| Control | 3,411 | 241 (7.1%) | 3,170 | 1,012 | 17.8 |
| No clear | 3,415 | 238 (7.0%) | 3,177 | 1,017 | 17.9 |

Removing 69% of VDP1's pixels changed almost nothing, so VDP1 does not limit the loop on
Mednafen. The CPUs do. Images/s divides `frame_serial` by observed fields; the two counters
may start slightly apart. The no-clear picture is smeared, as expected
(`no-clear-frame.png`), and the test build was discarded.

### The margin above the catch-up cap is small

Mednafen runs this fight at about 18 images/s and already discards 7.1% of its fields. Ymir
with cache on runs about 33 images/s and discards 2.0% (89 of 4,439). Below 15 images/s, game
speed is `images/s × 4 / 60`, so 10 images/s would play at 67% speed. A console about 20%
slower than Mednafen would therefore be in visible slow motion. This is an inference and fits
the Satiator report, but it does not explain why SAROO was reported near normal speed.

### Where Ymir's discards come from

`scripts/discard_map.py` lists every discard (89 in 6,000 fields, in clusters). Profiles of
8 fields, Slave, with CPU move selection counted as `rd`, `native_*`, `bank_*`, `bus_read`,
`row_address` and `t3_move_index_*`:

| Field window | Discards nearby | Move selection, cycles | Share of Slave |
|---|---|---|---|
| 1818 | Yes | 1,221,000 | 34% |
| 2588 | Yes | 1,263,000 | 35% |
| 3156 | Yes | 1,196,000 | 33% |
| 3196 | Yes | 1,363,000 | 38% |
| 5716 | Yes | 280,000 | 8% |
| 3500 | No | 171,000 | 5% |
| 3776 | No | 745,000 | 21% |

In the four move-selection windows the Master spends 2.0–2.5M of its 3.6M cycles waiting.
Move selection is the main cause there, but not the only one: the 5716 window is mostly
projection and pose sampling. On Mednafen, one AI update averages 9% of a field and peaks at
84% (3,130 of 3,728 FRT/128 ticks), with 1.66 updates per tick. A player against the CPU
needs fewer updates than this attract fight, which has two CPU fighters.

### Per-fighter move selection caches

The AI kept one table of picks and one last collection. With two CPU fighters each update
replaced the other's, so the demonstration fight repeated its candidate walks in full. Each
fighter now has its own (`src/game/psx_ai.c`); entries in the shared AI block are saved only
when the other fighter is about to overwrite them, so a fight with one CPU fighter copies nothing.

`scripts/ai_cost.py`, Ymir, cache on, 6,000 fields, ELF `6e2826e2a458c097…` against the same tree before
the change:

| Fight | Build | Pad hash | Images / fields | Discarded | AI mean, peak (of a field) |
|---|---|---|---|---|---|
| Attract, two CPU | Before | `0x640e32c0` | 1,956 / 4,437 | 87 | 0.078, 0.722 |
| Attract, two CPU | After | `0x640e32c0` | 2,097 / 4,436 | 11 | 0.059, 0.634 |
| Arcade, one CPU | Before | `0x4c460e70` | 1,669 / 3,027 | 0 | 0.050, 0.448 |
| Arcade, one CPU | After | `0x4c460e70` | 1,662 / 3,027 | 0 | 0.053, 0.448 |

Mednafen, 110 s, attract, against the control above:

| Build | Observed fields | Discarded | Ticks | Images | Images/s | AI mean, peak |
|---|---|---|---|---|---|---|
| Before | 3,411 | 241 (7.1%) | 3,170 | 1,012 | 17.8 | 0.09, 0.84 |
| After | 3,408 | 132 (3.9%) | 3,276 | 1,053 | 18.5 | 0.07, 0.73 |

The pad hash covers the first 2,000 updates and matches on both emulators, so the CPU's choices
did not change. `scripts/verify_psx_ai.py --cases 300` reports 0 mismatches against the original
code, without these caches. Discards on Ymir: 89 before, 11 now, clustered near fields 4010 and 5611.

**This helps the demonstration fight, not a fight against one CPU**, which is what a player
normally sees: arcade is about 0.4% slower here, cause not established. The
Mednafen margin above the cap barely moved. Arcade could not be run on Mednafen because its key
presses did not register.

The peak update is not explained. The FRT reports about 300,000 cycles, but a 12-field profile
near the field of the peak (field 4144 attract, 4135 arcade) shows at most 28,000, and a 200-field
trace makes the probe fail. Either the window missed it or the reading is not CPU work.

### One CPU fighter on Mednafen and Ymir

**Correction.** An earlier version of this section said key presses reach the game on Mednafen and
that a one-CPU arcade fight discards 4.2% there. Both were wrong. That run was the two-CPU attract
fight: its AI call count (about 1.86 per tick) and pad hash `0x640e32c0` are the attract fight's,
and a snapshot test showed Return does nothing at the title. The fight had only started because
the attract timeout expired. Its files are now named `mednafen-attract-*`.

Mednafen still cannot reach ARCADE MODE. To measure one CPU there, configure the build with
`-DT3_DEMO_CPU_MASK=2` (default 3): the attract fight then has P1 neutral against one CPU fighter.
`scripts/mednafen_fight.py` runs it and keeps only the numbers.

Ymir, arcade, 12 fields from field 4000 (`arcade-profile-functions.txt`): six images in 12 fields,
each CPU about 900,000 cycles per image. The Slave is idle in `slave_wait_request` and its FRT
polling about 11% of the time. Its largest self cost is `project_q15` (18%, with no hot spot: its
cost is spread over the per-vertex loop), then `t3_psx_mul_rot` (5.6%), `sample_one` (5.2%),
`memcpy` (4.2%), pose and compose (about 8% together) and `native_collect` (2.2%). On the Master
`submit_instance` is 30% and `project_q15` 17%. No single routine offers a large gain; a real
saving would have to remove work (fewer vertices or poses per image), not tune a loop.

### Simulation waste removed (2026-10-31)

Any cycle saved on the Slave's ticks lets it take the stage from the Master more often, so
simulation savings reach the image rate. Three kinds of waste were removed:

- `psx_ik.c`: the original's 32-bit wrapping arithmetic went through 64-bit helpers (`__ashrdi3`
  and three more libgcc calls), 14,592 calls in 60 fields. It now uses plain modulo-2^32
  arithmetic and inline constant shifts, and solves in place. `scripts/diff_ik_head.py` found
  2,000,000 random cases identical to the previous code, on the host only.
- `t3_psx_mul_rot`: no temporary, zero fill or copy unless the output aliases the first input.
- `psx_camera.c`: the table check ran four times per tick at about 1,441 cycles each (1.3% of the
  Slave). It now remembers the tables it has checked; the match start still does the full check.

Same pad hash on each row's two sides, so the CPU's choices did not change. Host tests: 95 of 95.

| Emulator, fight | Span | Images before | Images after | Discards | Pad hash |
|---|---|---|---|---|---|
| Ymir, attract (two CPU) | 6,000 fields | 2,097 | 2,306 (+10%) | 11 → 8 | `0x640e32c0` |
| Ymir, arcade (one CPU) | 6,000 fields | 1,662 | 1,706 (+2.6%) | 0 → 0 | `0x4c460e70` |
| Mednafen, mask 2 (one CPU) | 90 s | 835, 22.63/s | 946, 25.61/s (+13%) | 0 → 0 | `0x94e16040` |

Mednafen attract with this code: 760 images in 2,213 fields (20.61/s), 40 discards (1.81%), against
93 of 2,214 (4.2%) on ELF `6e2826e2…`, whose image count was not recorded. AI mean 0.075 field, peak
0.679.

So the one-CPU fight has no discards on Mednafen with or without this change. The gain is margin
above the 15 images/s limit. Final ELF `38a4213a…`. These are emulator numbers; nothing here has
run on a real Saturn, and the Satiator and SAROO faults remain undiagnosed.

Tried and rejected: `psx_blend.c` and `psx_blend_math.c` at `-O2` instead of `-Os` gave 947 images
against 946 on the same Mednafen run, for 336 more bytes of code.

After this, `project_q15` (about 17.5% per CPU) and the `submit_instance` loop (29% of the Master,
about 150 cycles per face) have no fat left to cut. A larger gain needs fewer vertices or faces.
The Slave is still idle about 20% of the time in this profile, which `T3_MASTER_WORKER_FACES` OFF
might use; that was not tried.

### Catch-up cap raised from 4 to 6 fields (2026-10-31)

Slow motion follows from the cap: one image may catch up only so many fields, and the rest is
discarded. `T3_CATCH_UP_FIELDS` is now 6 (was 4), so game speed holds down to 10 images/s instead
of 15. Earlier mentions of a 4-field cap in this file describe the builds measured then.

`T3_SLOW_IMAGE_FIELDS` (default 0) wastes whole fields on the Master after each image, to stand in
for a slower console. Mednafen, 120 s, one CPU (mask 2), 4 wasted fields, same pad hash `0x94e16040`:

| Cap | Images/s | Fields per image | Game speed | Discarded |
|---|---|---|---|---|
| 4 | 8.58 | 7.00 | 57% | 42.7% |
| 6 | 7.43 | 8.08 | 74% | 25.6% |
| 8 | 6.51 | 9.22 | 86% | 13.8% |

Each extra tick costs about 0.57 field here, so more catch-up also lowers the image rate. Cap 6
gains 30% in speed for 13% fewer images. Running all ticks deferred at cap 8 (6.54 images/s) did
not help and was reverted, though this stand-in cannot overlap ticks with its wasted fields.

Without the stand-in, cap 6: one CPU 25.71 images/s with 0 discards; attract 19.99 images/s with 9
discards of 4,011 (0.22%), hash `0x640e32c0`. Evidence: `capstudy-*.json`. The stand-in slows the
Master only, so a console whose bus is slower for both CPUs would do worse. This is not a hardware
result, and it does not help if the fault is a field count that stops advancing (`IRQ` in the report).

Each tick's cost limits how far catch-up can go; see "Where a tick goes" below.

### Boot state made explicit in LibSaturn (2026-10-31)

Nothing in the SDK ever enabled the SH-2 cache: it kept whatever the BIOS or a loader left. A loader
that left it off would make a console very slow, and no emulator run would show that. `sat_init` and
the Slave's start-up now set the full 4 KiB cache and record what they found
(`sat_sh2_cache_control_at_start`). They also clear the FRT's clear-on-compare-A bit, which a loader
could have left set and which would break every time measurement.

`T3_TEST_CACHE_OFF` (bit 0 Master before `sat_init`, bit 1 Slave once it runs) stands in for such a
loader. Mednafen, 120 s, attract, cap 6, same pad hash `0x640e32c0`:

| Build | Images/s | Discarded | Game speed |
|---|---|---|---|
| Fixed SDK, nothing forced | 20.28 | 0.10% | 99.9% |
| Fixed SDK, Master forced off before `sat_init` | 20.22 | 0.13% | 99.9% |
| Old SDK, Master off | 10.13 | 7.56% | 92% |
| Old SDK, both off | 4.66 | 53.34% | 47% |

So a cache left off is enough to give the slow motion players reported, but nothing shows that any
loader does so: this is a possible cause, not a diagnosis. `MCCR` and `SCCR` in the report will say.
The Slave's correction runs but could not be exercised: the test can only turn its cache off after
start-up. Evidence: `cachestudy-*`.

### Where a tick goes (2026-10-31)

Ymir, cache on, arcade (one CPU), 30 fields from field 4000 (`tickprofile/`), final SDK. Simulation
takes about 40% of the Slave, roughly 0.4 field per tick. `T3_SIM_PROFILE` could not give the same
split: it needs simulation on the Master, and the particle options that are on by default require it
on the Slave. As a share of the Slave:

| Part | Share | Note |
|---|---|---|
| Waiting (`frt`, `mailbox`, `signal`) | about 19% | Idle while the Master finishes |
| `project_q15` | 17.8% | Fighter vertices, not simulation |
| `t3_psx_mul_rot` | 5.5% | Already trimmed |
| `sample_one` | 4.9% | Motion sampling |
| `memcpy` | 3.9% | Already word copies; the large pose copies are already one call each; 36 call sites |
| `rd` (AI table reads) | 2.8% | |
| Compose, IK, FK | 2.1–2.6% each | |

The Master has no idle time (`submit_instance` 29.8%, `project_q15` 17.1%, `faces_flush` 9.4%), so it
is the limit, while the Slave waits. The simulation has no large waste left: no part both exceeds 10%
of a tick and has an obvious cut, so nothing was changed. `T3_MASTER_WORKER_FACES` OFF might use the
Slave's idle time, but the default particles require it ON, so it was not tried. Evenly sharing the
Master's face work with the Slave is the remaining idea here.

### A smaller VDP1 clear: estimated, not built (2026-10-31)

`scripts/vdp1_load.py --pairs`, Ymir, attract, ten images including the discard clusters: anything but
the clear touches 25,000–39,000 of 71,680 pixels. Using a polygon per 16 lines that covers the current
image and the one two swaps back, the clear could shrink to 35–62% of its area, saving about 25–55% of
all VDP1 pixels depending on the image.

It was not built. Finding those bounds means reading about 750 commands per image on the Master, about
0.1–0.2 field by estimate, and the Master is the limit. Removing the whole clear gained nothing on
Mednafen (above), so this would probably lower the image rate there, in exchange for a VDP1 saving that
only matters if real VDP1 is many times slower than the model. Reconsider it if `END MS` is large on a
real console. Row 208 and below was untouched in these images, but a fixed cut saves only 7% and has not
been shown safe for every camera, so that was rejected too.

### VDP2 is the only fight floor (2026-11-01)

The fight floor is RBG0 on VDP2, like other Saturn 3D fighters, and was already the default. The
experimental VDP1 floor options (`T3_VDP1_FLOOR_PROBE`, `_SHADE_PROBE`, `_SUBDIVIDE_PROBE`,
`_CAPACITY_SELFTEST`, `_FORCE_BUDGET`, `_SPLIT_MIN_AREA`) were removed, so a stage build can no longer
draw the floor with VDP1. Their results remain in `docs/PARTICLE_RENDER_PROGRESS.md` and in git history.

### Skipping work for rejected faces: measured, not worth building (2026-11-01)

`submit_instance` and `project_q15` are the largest Master costs on Ymir's profile. Question: how much of
their work serves only faces that are rejected anyway? Temporary counters in the live draw path, removed
afterwards, compared every face and vertex in every image of two 120 s Mednafen fights. Counts do not
depend on emulator timing, and the counters left both pad hashes unchanged.

| Measure | Two CPU (`0x640e32c0`) | One CPU (`0x94e16040`) |
|---|---|---|
| Images | 1,166 | 1,409 |
| Faces rejected by winding | 53.4% | 53.2% |
| Surviving faces fully off screen | 0.53% | 1.91% |
| Vertices used only by rejected faces | 29.2% | 28.8% |
| The same, lowest to highest image | 22.4% to 37.2% | 21.4% to 34.9% |
| Vertices also skipped if off-screen faces count as rejected | 0.34% | 1.22% |
| Blended vertices used only by rejected faces | 28.6% | 30.4% |
| Rigid vertices in joints with no vertex in use | 0.02% | 0.04% |

The two face rows cover both draw paths; the vertex rows cover only the SDK path. The original projection,
used only with live particles, ran for 4.6% and 0.7% of actor images and used about 65% of its vertices.

Result: no change. The 29% needs a perfect check, and the gate was about 25% after the check's cost:

- A vertex can only be skipped if its faces are known to be rejected before it is projected. A per-joint
  check would skip 0.02% to 0.04%, because every joint nearly always has some face toward the camera.
- A finer check would group each joint's faces by normal direction. This was not built; by geometry, a
  group is safely rejected less often than its faces are, and each vertex needs all its groups rejected,
  so it should land well under 25%, before paying for its tests and a per-vertex branch.
- Even a free, perfect check would save about 29% of `project_q15`, or about 5% of the Master.
- An off-screen test would remove too little to matter.

Next is the Slave's idle time, because this waste is real but cannot be reached cheaply. The counters also
show a smaller point for later: images on the original projection append all their faces and reject them
afterwards, unlike the SDK path. They are too rare in these fights to change the average.

### Moving fighter work between the CPUs: measured, not worth building (2026-11-01)

The idea was to spend the Slave's idle 19% on the Master's face work. Two temporary builds moved work each
way, on the same fights (pad hashes unchanged). Ymir is 6,000 fields from boot (`scripts/ymir_fight.py`);
Mednafen is 120 s.

| Fight | Current | Slave also prepares actor 1's faces | Master skins both actors |
|---|---|---|---|
| Ymir, one CPU | 34.65 | 33.04 (-4.6%) | 31.98 (-7.7%) |
| Ymir, two CPU | 31.17 | 27.30 (-12.4%) | 29.87 (-4.2%) |
| Mednafen, one CPU | 25.73 | 22.63 (-12.0%) | 25.83 (+0.4%) |
| Mednafen, two CPU | 20.03 | 16.78 (-16.2%) | 22.50 (+12.3%) |

Images per second. The Slave's faces were correct: a Master recomputation matched them byte for byte on all
1,007 images compared in a 120 s run (23 more were skipped by the check), so the loss is timing, not a fault.

Result: no change. The faces build loses everywhere. The skinning build wins only in the two-CPU attract fight
on Mednafen and loses in arcade's one-CPU setup on Ymir, so the current split stays.

Temporary FRT counters on the current build explain it, as a share of all fields:

| Fight | Slave ticks | Slave skinning | Slave stage | Slave unaccounted | Master waiting at the join |
|---|---|---|---|---|---|
| Ymir, one CPU | 47.6% | 19.4% | 7.3% | 25.7% | 4.9% |
| Ymir, two CPU | 54.9% | 18.0% | 4.5% | 22.5% | 7.7% |
| Mednafen, one CPU | 56.7% | 15.2% | 4.4% | 23.7% | 10.2% |
| Mednafen, two CPU | 65.7% | 12.3% | 2.5% | 19.4% | 19.7% |

- This corrects the note above that the Master has no idle time: it also waits for the Slave, by 5% to 20%.
- The ticks scale with image time, so work added before them is multiplied: on Mednafen's two-CPU fight, 66%
  of each field goes to ticks, so one more field of chain costs about three (1 / (1 - 0.66)). The faces build
  added 0.27 field per image and the image took about 0.6 field longer.
- The Slave's unaccounted time is therefore not spare time inside the parallel part, which is why it cannot
  absorb faces. It must fall mostly outside the Slave's task, while the Master works before dispatching it. That
  is 0.4 to 0.6 field per image, and it was not split further. The counters leave out the executor's own
  overhead, so this is an upper bound.

Which CPU limits a real console is unknown, because the emulators disagree on the balance.

### A cheaper `faces_flush`: built (2026-11-01)

Ymir's profile of the attract fight showed about 709 faces per image costing about 107 Master cycles
each to emit and 77 to sort. Three LibSaturn changes, none of which changes a command word:

- The hot face record stores its corners as the XY words of a VDP1 command (`xy[4]`), so emitting one
  is one load and one store, and `submit_instance` copies one word per corner instead of two halves.
- Emitting no longer rechecks each face's texture. The flush has already validated the queue, or the
  caller has said it is valid (`sat_scene_flush_trusted`). The two command prefix pairs are read as
  whole words.
- The painter clears and sums only the counters up to the top bucket in use, not all 1,024.

Master cycles per image on Ymir, two-CPU fight, 600 fields:

| Function | Before | After |
|---|---|---|
| `faces_flush` | 75,846 | 50,488 (-33%) |
| `paint_order_grouped_buckets` | 54,750 | 43,485 (-21%) |
| `submit_instance` | 230,119 | 219,056 (-5%) |
| All Master work per image | 831,837 | 790,367 (-5%) |

Images per second, with the same pad hashes (`0x94e16040` one CPU, `0x640e32c0` two CPU):

| Fight | Before | After | Change |
|---|---|---|---|
| Ymir, one CPU | 34.65 | 37.13 | +7.2% |
| Ymir, two CPU | 31.17 | 32.62 | +4.7% |
| Mednafen, one CPU, 120 s | 25.73 | 27.32 | +6.2% |
| Mednafen, two CPU, 120 s | 20.03 | 20.70 | +3.3% |

The gain is larger with one CPU, probably because the Master waits less for the Slave there (about
5–10% of fields against 8–20%, measured on the old build), but this was not tested separately. Discards did not change: none with one CPU, none on Ymir with two
CPUs, and 0.22% on Mednafen with two CPUs, as before.

The picture is unchanged. `T3_RENDER_BENCH_TICK` captures from the old and new SDK at ticks 1500 and 3000
passed `compare_saturn_presentation.py` (102 stable state fields and the model region), and the whole
screenshots have the same hash. This covers those two frozen images, not every image of a fight.
`test_scene_native_commands` now checks every word of an emitted program command.

These are emulator timings, not hardware timings. A real console has not run this build.

### Regression check after these changes (2026-10-31)

Mednafen, 120 s, final SDK, same pad hashes as before:

| Fight | Images/s | Discarded | Before |
|---|---|---|---|
| One CPU, 4 wasted fields per image | 7.43 | 25.62% | 7.43, 25.6% |
| One CPU | 25.70 | 0 | 25.71, 0 |
| Attract, two CPU | 20.28 | 0.10% | 19.99, 0.22% |

So the boot-state fixes cost nothing and, as expected, gain nothing when the emulator already had the
cache on. The next hardware result to look for is `MCCR` and `SCCR` in the report. Package:
`build/tekken3-saturn-v0.1.2-hwreport-c.zip`.
