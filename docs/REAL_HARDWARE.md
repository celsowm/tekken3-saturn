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
