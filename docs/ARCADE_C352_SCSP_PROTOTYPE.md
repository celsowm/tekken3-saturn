# C352 to SCSP prototype: the first 10 s of the Paul Phoenix theme

A real prototype, run headless on Ymir: original samples converted offline, a register score executed by the
SCSP's 68000, the audio captured and compared with the libvgm C352 render. It is independent of the Tekken 3
Saturn build and of the CD-DA; nothing under `src/` and nothing in LibSaturn changed.

Everything private (ROM-derived bank, score, captures, WAVs) is under the git-ignored
`assets/imported/c352-proto/`. This file holds aggregate numbers only.

## Verdict against the acceptance criteria

| # | Criterion | Result |
|---|---|---|
| 1 | Recognisable music, several voices | **Not verified by ear.** 9 simultaneous voices, 5 samples; objective proxies below (envelope r = 0.977, spectrogram cosine 0.88). The aligned WAVs are the evidence to listen to. |
| 2 | Samples played natively by the SCSP | Met. 4 PCM8 samples copied as they are, 1 mu-law sample expanded offline to PCM16. 138,892 bytes read back from Sound RAM with 0 mismatching words. |
| 3 | Sequenced by the 68000 | Met. 488 events in the driver's score; `score_executed` 727 after 1 pass + part of the second, maximum lateness 0 ticks. |
| 4 | No continuous audio decoding on the SH-2 | Met by construction: after `sat_sound_driver_score_load` the SH-2 loop only draws text and never calls `sat_audio_update`; `active_voices` is 0 at frame 300, mid-playback. |
| 5 | No CD-DA | Met. The program never touches the CD. The probe wants a disc path, so an existing image is passed; the program is injected. |
| 6 | Sound RAM measured | 138,892 B of the 446,464 B pool (31.1 %), plus 12,288 B (0x3000) the driver reserves. |
| 7 | Documented comparison with the oracle | Below. |
| 8 | No change to the Tekken 3 Saturn logic | Met. |

**What the prototype does not show:** that the whole song fits. The 10 s excerpt is the *least* dense part of the
track, and it already uses half the driver's 1,024-event score. See "Limits".

## 1. The excerpt

`scripts/c352_scsp_convert.py --end 10`: the first 10.000 s of the VGM (0 to 10 s). Starting at 0 avoids
carrying notes that are already sounding, which would need a guessed sample phase. A start in the middle is
implemented (`--start`: entry at the sample position the C352 had reached) but is untested on the SCSP.

| | |
|---|---|
| C352 register writes in the window | 807 |
| Notes (key-ons) | 59, of which 27 never get a non-zero volume (dummy voices, skipped) and 32 are converted |
| Peak polyphony | 9 voices (at 8.07 s) |
| Samples | 5 unique: 34,780 + 42,192 + 41,309 + 7,532 PCM8 samples, and 6,539 mu-law samples |
| Memory | 125,814 B PCM8 + 13,078 B PCM16 = 138,892 B |
| Looped notes | 3 (a 256-sample loop of the bass voice pair, a 27,187-sample loop); both forward, seam step 0 |
| Volume changes | 281 (30 of them move the left/right balance); 1 pitch change; initial pitches are fixed per note |
| Pad, bass, hit | a 4-voice staggered pad from 0.12 s, a bass loop pair from 4.3 s, a mu-law hit at 9.05 s |
| Slowest C352 volume ramp | 1.9 ms (the chip slews 1 step per half sample), so it is neglected |

Why not a denser excerpt: for 10 s windows from 2 s onwards the same converter needs 579 SCSP events (2 s),
727 (4 s), 1,080 (6 s) and 1,943 to 3,975 (8 s to 48 s). Only windows starting in the first ~5 s fit the
driver's 1,024-event score; the track's intro at 0 to 10 s is the densest fitting choice that needs no state
carry. The first 4 s are quiet (RMS about 800 of 32,768) and the rhythm enters at 4.3 s.

## 2. The conversion (`scripts/c352_scsp_convert.py`)

| C352 | SCSP |
|---|---|
| Linear 8-bit ROM sample | Copied unchanged; the SCSP's PCM8 is also signed 8 bits shifted into the top byte |
| Mu-law 8-bit sample | libvgm's table expanded to 16-bit, written big-endian as PCM16 (no requantising) |
| `freq` (rate = 88200 x freq / 65536 Hz) | OCT/FNS word; the rate is rounded to the nearest 1/1024 of an octave step |
| Loop forward `LOOP` | LPCTL 1; LSA = loop - start; LEA = length (exclusive, as Ymir wraps before reading LEA) |
| `LOOP`+`REVERSE` | LPCTL 3 (alternate); not used in the window |
| Front + rear volumes (8 bits per side) | TL (0.375 dB) for the louder side, DIPAN (3 dB steps, bit 4 set attenuates the right) for the ratio, DISDL 7 |
| Key-on / key-off (`0x202`) | KYONB in slot register 0, KYONEX on the last key write of each tick |
| Voice number | A free slot, preferring one that already holds the same sample (saves four writes); a retrigger of a slot is preceded by a key-off in an *earlier* tick, because Ymir only keys on from the Release state |

Static slot registers (AR 31, RR 31, no LFO, no DSP send) are written once by the SH-2 before the score starts.
Not carried, and counted: phase invert, reverse play, link, noise, loop ends beyond 16 bits. All are zero in
this window. Interpolation, envelope shape and the chips' loop wrap are not assumed equal.

Level: the oracle WAV carries a playback gain of libvgm's (the VGM header volume byte is 0x23 and the player
normalises chip volumes; I did not separate the two), so the nominal conversion came out 5.76 dB louder. One global factor, 0.5156, was applied
(`--scale`); it was fitted on the whole-window RMS of that first run and is not per-note.

## 3. The program and how to reproduce

```
PATH=/Users/celso/dev/saturn-tools/bin:$PATH python3 scripts/run_c352_scsp_prototype.py \
  --vgm assets/imported/arcade-vgm/paul.vgz \
  --oracle assets/imported/arcade-render/paul_arcade_c352_88200.wav \
  --end 10 --scale 0.5156 --out assets/imported/c352-proto/final --keep-audio
```

`tests/c352_scsp_proto/` is a standalone LibSaturn program (CMake, own build dir `build/c352proto`, about 178 KB
image). It starts the audio system and the 68000 driver, programs the 32 slots, loads the bank with three
`sat_sound_create` calls (the first fit lands at 0x3000, verified by reading Sound RAM back), loads the score
with `sat_sound_driver_score_load` and then only draws. Tests: `python3 -m unittest tests.test_c352_scsp_convert`.

The score loops, so the capture includes the start of a second pass; only the first 10 s is compared.

## 4. Timing (`scripts/c352_scsp_timing_probe.py`)

Timing was measured with a separate score of 64 isolated notes at off-grid times, because the music mixes too
many voices to read one onset. The same program plays it.

| | |
|---|---|
| Requested time to the 5.805 ms tick | mean 1.47 ms, maximum 2.84 ms |
| Capture onset against the tick grid | jitter 0 (below one sample, 0.023 ms) |
| Clock ratio, capture against ticks | 1.0000000000000007 (drift about 0 ppm, 0 ms over 10 s) |
| Capture against the requested time | standard deviation 1.66 ms, maximum 2.89 ms |

So the 68000 runs every event exactly on its tick; the only timing error is the tick resolution. In the music,
notes start on average 1.56 ms (maximum 2.83 ms) from the oracle's time. There is no accumulated drift: the
driver tick is 256 SCSP samples, and the oracle's timeline is in 44.1 kHz samples.

The block-by-block lag between capture and oracle stays within +-3.9 ms, but its trend (+0.42 +- 0.10 ms/s) is
not a clock drift: the same analysis on a clockless model with tick and pitch rounding gives +0.17 +- 0.13 ms/s.

## 5. Comparison with the oracle (`scripts/compare_c352_scsp.py`)

Oracle 88,200 Hz brought to 44,100 Hz with a polyphase low-pass; the capture aligned to the first sound (a global
correlation can lock on a neighbouring peak, because the bass loop repeats every 32 ms) and then refined over the
window. `reference_44100.wav`, `scsp_aligned.wav` and `scsp_gain_matched.wav` are in
`assets/imported/c352-proto/final/compare/` for listening.

| Metric | Oracle | SCSP |
|---|---|---|
| RMS, left / right | 2,313 / 2,346 | 2,315 / 2,261 (ratio -0.16 dB) |
| Peak | 10,895 | 10,939 (+0.04 dB) |
| Octave band levels, 62 Hz to 8 kHz | | within -0.30 to +0.35 dB |
| 8 to 16 kHz / 16 to 22 kHz | | +1.34 dB / +4.57 dB (interpolation, images) |
| Left/right ratio | -0.12 dB | +0.21 dB; per second within 0.41 dB (mean 0.26 dB) |
| Side/mid | 0.237 | 0.245 |
| 10 ms RMS envelope | | correlation 0.977; error mean -0.05 dB, sd 1.12 dB, 95th percentile 2.26 dB |
| Spectrogram (log-magnitude cosine) | | 0.88 overall; 0.92 in the first 4 s, 0.83 to 0.87 afterwards |
| Waveform correlation, per second | | -0.07, 0.00, 0.16, 0.24 \| 0.95, 0.93, 0.94, 0.87, 0.87, 0.79 |
| Pitch, strongest partials | | median +0.2 cents; mean |e| 10 cents, 95th percentile 44 cents (1 s blocks); 5.3 and 24 cents (2 s) |

**Why the waveform correlation is near zero in the pad:** the pad is four long samples of one ensemble, and a
2.9 ms start error plus a pitch rounding of up to 0.74 cents moves a 1 kHz partial by several cycles over a
2 s note. `scripts/model_c352_scsp_render.py` renders the converted notes in Python and checks this:

| Model | Mean correlation with the capture |
|---|---|
| Exact start times, exact pitch (what the C352 does) | 0.57 |
| Exact start times, rounded pitch | 0.60 |
| **Ticks and rounded pitch** (start offset -2.70 ms) | **0.978** (1.00 in each of the first four seconds) |

The capture is the conversion's intended output to within the model's accuracy (pitch of strong partials
against the model: mean 0.14 cents), and the exact-time, exact-pitch model matches the oracle at r = 0.98 in every
0.25 s block of the first 4 s once a one-source-sample delay (-2 samples) is allowed. What separates SCSP from C352 here is therefore the
quantisation, not a conversion error. The pitch outliers (mean |e| 10 cents) come from the pad ensemble, whose
voices are 3 cents apart and beat inside one analysis block; the median is 0.2 cents.

Loops and envelopes: the two loop seams in the window are continuous in the data (step 0), both chips wrap the
same samples, and the attack ramp of the C352 is at most 1.9 ms against an instant SCSP attack. How a key-off
(RR 31) sounds against the C352's immediate cut, and whether the loops are inaudible, were **not** measured:
that needs listening.

## 6. Limits found

1. **Score length.** The driver's score holds 1,024 events. This excerpt uses 488; 10 s of the denser passages
   need 1,100 to 4,000. A sequencer that runs the whole song needs a longer or refillable score in the 68000
   driver. No 68000 assembler is installed on this machine (`m68k-elf-as`, `m68k-apple-macos-as`, `vasmm68k_mot`
   not found), so the driver was not touched.
2. **Tick resolution** (5.805 ms) gives a timing error of up to 2.9 ms per note, uniformly. A finer clock needs a
   driver change as well.
3. **Pitch resolution** (10-bit FNS) gives up to 0.74 cents and de-phases long notes against the oracle. Exact
   rates would need per-voice resampled data, which was deliberately not done.
4. **Pan** has 3 dB steps and one attenuated side per slot; the louder side sets TL, so a voice with different
   front and rear gains is approximated. Gains above 1 cannot be played; the global factor keeps all below.
5. Frequencies above 8 kHz differ by 1.3 dB, and above 16 kHz by 4.6 dB.
6. The excerpt is one window of one track, on Ymir only (not hardware, not Mednafen).

## 7. Next steps

- Decide if a full sequencer is worth it, knowing that dense passages exceed the score by 2 to 4 times: either a
  driver with a larger or double-buffered score (an assembler is needed) or a compact bytecode that expands
  per tick on the 68000.
- Keep the converter and the model: they price any window in events, slots and bytes before it is built, and
  the model is the quickest check that a capture follows the score.
- Try a mid-song window with `--start` once the score can hold it, to hear the full band.
- Listen to the three WAVs; this report cannot judge them.
