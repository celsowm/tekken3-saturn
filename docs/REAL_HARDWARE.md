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
| `IRQ` | SCU VBlank interrupts live | 0 means fields are counted by `FRM`, so speed follows the image rate |
| `VBI`, `FRM` | Interrupt count and `sat_frame_count` | `VBI` should rise about 60 per second |
| `HZ`, `PAL` | Refresh rate assumed, and TVSTAT's PAL bit | A disagreement is a video-timing bug |
| `MCCR`, `SCCR` | Master and Slave cache control at boot | Bit 0 clear means that CPU's cache is off; `SCCR` 999 means not read |
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
| `IRQ` 0 | Field counting falls back | Fix IRQ start-up in LibSaturn, or count fields independently of `sat_wait_vblank` |
| `SLV` 0, or cache bit clear | Loader-dependent boot state | Set that state explicitly in LibSaturn start-up |
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

### One CPU fighter on Mednafen and Ymir (arcade)

Key presses do work with `scripts/mednafen_shots.py` (new `--mednafen-arg`, for
`-ss.input.port2 none`) and with `record_mednafen.py --input-script`, using Return at 25, 30, 36
and 41 s after launch. The earlier failure was a timing mistake: the title appears only about
22 s after launch. Arcade then reaches a fight against one CPU (`mednafen-arcade-frame.png`).

Mednafen, 90 s, ELF `6e2826e2…`, one CPU fighter, P1 neutral: 2,214 observed fields, 93 discarded
(4.2%), 2,121 scheduled ticks. This is about the same discard rate as the two-CPU attract fight
(3.9%), so on Mednafen the one-CPU fight is not clearly easier. There is no control build for this
scenario, so it says nothing about the per-fighter change.

Ymir, arcade, 12 fields from field 4000 (`arcade-profile-functions.txt`): six images in 12 fields,
each CPU about 900,000 cycles per image. The Slave is idle in `slave_wait_request` and its FRT
polling about 11% of the time. Its largest self cost is `project_q15` (18%, with no hot spot: its
cost is spread over the per-vertex loop), then `t3_psx_mul_rot` (5.6%), `sample_one` (5.2%),
`memcpy` (4.2%), pose and compose (about 8% together) and `native_collect` (2.2%). On the Master
`submit_instance` is 30% and `project_q15` 17%. No single routine offers a large gain; a real
saving would have to remove work (fewer vertices or poses per image), not tune a loop.

